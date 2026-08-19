/*
 * Copyright (C) 2026 Giorgos Gasparis <gasp.giorgos@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "extractor.hpp"
#include "state_manager.hpp"
#include <iomanip>
#include <algorithm>

ArchiveValidationResult Extractor::ValidateArchive(const fs::path& tarPath) {
    ArchiveValidationResult res{};
    res.archiveName = tarPath.filename().string();

    if (!fs::exists(tarPath)) {
        res.valid = false;
        res.errorReason = "Archive file does not exist at specified path.";
        return res;
    }

    std::error_code ec;
    res.fileSize = fs::file_size(tarPath, ec);
    if (ec || res.fileSize == 0) {
        res.valid = false;
        res.errorReason = "Archive file is 0 bytes or unreadable.";
        return res;
    }

    // 1. Check for known VM archive naming patterns
    std::string lowerName = res.archiveName;
    for (auto& c : lowerName) c = (char)tolower(c);

    if (lowerName.find("vm") != std::string::npos ||
        lowerName.find("ova") != std::string::npos ||
        lowerName.find("virtualbox") != std::string::npos ||
        lowerName.find("win10_14.7") != std::string::npos) {
        res.valid = false;
        res.isVmArchive = true;
        res.errorReason = "Detected Xilinx Windows 10 VirtualBox/OVA VM archive. The Native Deployer requires the Bare-Metal Windows archive (Xilinx_ISE_DS_Win_14.7_1015_1.tar).";
        return res;
    }

    // 2. Minimum size threshold check (Bare-metal archive is ~6.18 GB / 6.63 billion bytes)
    if (res.fileSize < 1024ULL * 1024ULL * 1024ULL) {
        res.valid = false;
        res.errorReason = "Archive size is too small (" + std::to_string(res.fileSize / (1024 * 1024)) + " MB). The full Bare-Metal archive is ~6.18 GB (6,632,273,920 bytes). File is likely corrupted or incomplete.";
        return res;
    }

    // 3. Inspect first 1024 bytes of tar header for internal filenames
    std::ifstream tarFile(tarPath, std::ios::binary);
    if (tarFile.is_open()) {
        char header[1024] = {0};
        tarFile.read(header, sizeof(header));
        tarFile.close();

        std::string headerStr(header, sizeof(header));
        if (headerStr.find(".ova") != std::string::npos || 
            headerStr.find(".vmdk") != std::string::npos ||
            headerStr.find("VirtualBox") != std::string::npos) {
            res.valid = false;
            res.isVmArchive = true;
            res.errorReason = "Archive internal header contains VirtualBox OVA/VMDK VM payloads. Native Windows 11 deployment requires the native Bare-Metal archive.";
            return res;
        }
    }

    res.valid = true;
    return res;
}

fs::path Extractor::FindInstallerArchive(const fs::path& installerDir) {
    if (fs::exists(installerDir)) {
        for (const auto& entry : fs::directory_iterator(installerDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".tar") {
                return entry.path();
            }
        }
    }

    // Dynamic resolution of user's Downloads directory on ANY machine
    wchar_t profilePath[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, profilePath) == S_OK) {
        fs::path dlDir = fs::path(profilePath) / "Downloads";
        if (fs::exists(dlDir)) {
            for (const auto& entry : fs::directory_iterator(dlDir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".tar" &&
                    entry.path().filename().string().find("Xilinx") != std::string::npos) {
                    fs::path dest = installerDir / entry.path().filename();
                    fs::create_directories(installerDir);
                    try {
                        fs::rename(entry.path(), dest);
                        return dest;
                    } catch (...) {
                        return entry.path();
                    }
                }
            }
        }
    }
    return "";
}

fs::path Extractor::FindSetupExecutable(const fs::path& extractedDir) {
    if (!fs::exists(extractedDir)) return "";
    
    // 1. Check direct top-level children
    for (const auto& entry : fs::directory_iterator(extractedDir)) {
        if (entry.is_directory()) {
            fs::path topSetup = entry.path() / "xsetup.exe";
            if (fs::exists(topSetup)) {
                return topSetup;
            }
        }
        if (entry.is_regular_file() && entry.path().filename() == "xsetup.exe") {
            return entry.path();
        }
    }

    // 2. Check 64-bit nt64 setup
    for (const auto& entry : fs::recursive_directory_iterator(extractedDir)) {
        if (entry.is_regular_file() && entry.path().filename() == "xsetup.exe" &&
            entry.path().string().find("nt64") != std::string::npos) {
            return entry.path();
        }
    }

    // 3. Fallback to any xsetup.exe
    for (const auto& entry : fs::recursive_directory_iterator(extractedDir)) {
        if (entry.is_regular_file() && entry.path().filename() == "xsetup.exe") {
            return entry.path();
        }
    }

    return "";
}

void Extractor::ConfigurePermissions(const fs::path& targetDir) {
    std::cout << " " << Colors::DIM << "Configuring Windows execution permissions and ACLs..." << Colors::RESET << std::flush;
    std::wstring cmd = L"icacls \"" + targetDir.wstring() + L"\" /grant Everyone:(OI)(CI)F /T /C /Q >nul 2>&1";
    _wsystem(cmd.c_str());

    std::wstring psCmd = L"powershell -ExecutionPolicy Bypass -Command \"Get-ChildItem -Path '" +
                         targetDir.wstring() + L"' -Recurse | Unblock-File\" >nul 2>&1";
    _wsystem(psCmd.c_str());
    std::cout << Colors::GREEN << " Done." << Colors::RESET << std::endl;
}

void Extractor::DisableWebTalkTelemetry(const fs::path& extractedDir) {
    std::cout << " " << Colors::DIM << "Applying WebTalk telemetry auto-bypass configuration..." << Colors::RESET << std::flush;

    // 1. Write global user opt-out XML
    wchar_t profilePath[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, profilePath) == S_OK) {
        fs::path xilinxUserDir = fs::path(profilePath) / ".Xilinx";
        fs::create_directories(xilinxUserDir);
        fs::path userXml = xilinxUserDir / "webtalk.xml";
        std::ofstream xmlFile(userXml);
        if (xmlFile.is_open()) {
            xmlFile << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                    << "<WebTalkConfiguration>\n"
                    << "    <property name=\"WebTalkEnable\" value=\"0\"/>\n"
                    << "    <property name=\"WebTalkPrompt\" value=\"0\"/>\n"
                    << "</WebTalkConfiguration>\n";
            xmlFile.close();
            StateManager::Instance().RecordAction(ActionType::CREATE_FILE, userXml.string());
        }
    }

    wchar_t appDataPath[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appDataPath) == S_OK) {
        fs::path xilinxAppDir = fs::path(appDataPath) / "Xilinx" / "Common";
        fs::create_directories(xilinxAppDir);
        fs::path appXml = xilinxAppDir / "WebTalk.xml";
        std::ofstream xmlFile(appXml);
        if (xmlFile.is_open()) {
            xmlFile << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                    << "<WebTalkConfiguration>\n"
                    << "    <property name=\"WebTalkEnable\" value=\"0\"/>\n"
                    << "    <property name=\"WebTalkPrompt\" value=\"0\"/>\n"
                    << "</WebTalkConfiguration>\n";
            xmlFile.close();
            StateManager::Instance().RecordAction(ActionType::CREATE_FILE, appXml.string());
        }
    }

    std::cout << Colors::GREEN << " Done." << Colors::RESET << std::endl;
}

static uint64_t FastComputeDirectorySize(const fs::path& dir) {
    uint64_t total = 0;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return 0;
    for (const auto& entry : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) continue;
        if (entry.is_regular_file(ec)) {
            total += entry.file_size(ec);
        }
    }
    return total;
}

bool Extractor::ExtractPackage(const fs::path& tarPath, const fs::path& destDir) {
    // 1. Payload Pre-flight Validation
    auto val = ValidateArchive(tarPath);
    if (!val.valid) {
        std::cerr << "\n " << Colors::RED << "[PAYLOAD ERROR] Invalid Archive Selected:\n   " 
                  << val.errorReason << Colors::RESET << "\n";
        return false;
    }

    // 2. Pre-flight Disk Space Validation (Require at least 25 GB free space)
    double freeGB = GetDriveFreeSpaceGB(destDir);
    if (freeGB >= 0.0 && freeGB < 25.0) {
        std::cerr << "\n " << Colors::RED << "[DISK SPACE ERROR] Insufficient storage on target drive " 
                  << destDir.root_name().string() << ".\n"
                  << "   Required: 25.0 GB minimum free space\n"
                  << "   Available: " << freeGB << " GB free\n"
                  << " Aborting extraction to prevent partial corrupt state." << Colors::RESET << "\n";
        return false;
    }

    fs::create_directories(destDir);
    StateManager::Instance().RecordAction(ActionType::CREATE_DIR, destDir.string());

    uint64_t expectedTotalBytes = (uint64_t)(val.fileSize * 1.02);
    if (expectedTotalBytes == 0) expectedTotalBytes = 6800000000ULL;

    double expectedTotalGB = (double)expectedTotalBytes / (1024.0 * 1024.0 * 1024.0);

    std::cout << "\n " << Colors::BOLD << Colors::WHITE << "Decompressing Package: " << Colors::RESET << tarPath.filename().string() << "\n";
    std::cout << " " << Colors::DIM << "Archive Size : " << (val.fileSize / (1024 * 1024)) << " MB (Verified Bare-Metal x64 Installer)" << Colors::RESET << "\n";
    std::cout << " " << Colors::DIM << "Free Storage : " << freeGB << " GB available" << Colors::RESET << "\n";
    std::cout << " " << Colors::DIM << "Destination  : " << destDir.string() << Colors::RESET << "\n\n";

    std::wstring cmd = L"tar.exe -xf \"" + tarPath.wstring() + L"\" -C \"" + destDir.wstring() + L"\"";

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    auto start_time = std::chrono::steady_clock::now();

    if (!CreateProcessW(NULL, (LPWSTR)cmd.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        std::cerr << " " << Colors::RED << "[ERROR] Failed to invoke tar.exe" << Colors::RESET << std::endl;
        return false;
    }

    // Background directory size sampler
    std::atomic<uint64_t> currentExtractedBytes(0);
    std::atomic<bool> stopSizeWorker(false);
    std::thread sizeWorker([&]() {
        while (!stopSizeWorker.load()) {
            uint64_t sz = FastComputeDirectorySize(destDir);
            currentExtractedBytes.store(sz);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
    });

    const int barWidth = 24;

    while (WaitForSingleObject(pi.hProcess, 150) == WAIT_TIMEOUT) {
        auto now = std::chrono::steady_clock::now();
        double elapsedSec = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count() / 1000.0;
        if (elapsedSec < 0.1) elapsedSec = 0.1;

        uint64_t curBytes = currentExtractedBytes.load();
        double curGB = (double)curBytes / (1024.0 * 1024.0 * 1024.0);
        double speedMBps = (curBytes > 0 && elapsedSec > 0.5) ? ((double)curBytes / (1024.0 * 1024.0)) / elapsedSec : 0.0;

        int percent = (int)((curBytes * 100) / expectedTotalBytes);
        if (percent > 99) percent = 99;
        if (percent < 0) percent = 0;

        int etaSec = (speedMBps > 1.0 && curBytes < expectedTotalBytes) 
                     ? (int)(((double)(expectedTotalBytes - curBytes) / (1024.0 * 1024.0)) / speedMBps) 
                     : 0;

        int filled = (percent * barWidth) / 100;
        std::string bar = "";
        for (int i = 0; i < barWidth; ++i) {
            if (i < filled) bar += "=";
            else if (i == filled) bar += ">";
            else bar += " ";
        }

        std::cout << "\r\033[K " << Colors::CYAN << "[Decompressing] " << Colors::RESET
                  << "[" << Colors::GREEN << bar << Colors::RESET << "] "
                  << Colors::BOLD << std::setw(3) << percent << "%" << Colors::RESET << " | "
                  << std::fixed << std::setprecision(2) << curGB << " / " << expectedTotalGB << " GB | "
                  << Colors::YELLOW << std::setw(3) << (int)speedMBps << " MB/s" << Colors::RESET << " | "
                  << Colors::DIM << "Elapsed: " << (int)elapsedSec << "s"
                  << (etaSec > 0 ? (" | ETA: " + std::to_string(etaSec) + "s") : "") << Colors::RESET
                  << std::flush;
    }

    stopSizeWorker.store(true);
    if (sizeWorker.joinable()) {
        sizeWorker.join();
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    auto end_time = std::chrono::steady_clock::now();
    double totalSec = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count() / 1000.0;
    if (totalSec < 0.1) totalSec = 0.1;

    uint64_t finalBytes = FastComputeDirectorySize(destDir);
    double finalGB = (double)finalBytes / (1024.0 * 1024.0 * 1024.0);
    double finalAvgSpeed = ((double)finalBytes / (1024.0 * 1024.0)) / totalSec;

    std::string fullBar(barWidth, '=');

    if (exitCode == 0) {
        std::cout << "\r\033[K " << Colors::GREEN << "[SUCCESS] " << Colors::RESET
                  << "[" << Colors::GREEN << fullBar << Colors::RESET << "] "
                  << Colors::BOLD << "100%" << Colors::RESET << " | "
                  << std::fixed << std::setprecision(2) << finalGB << " GB extracted in "
                  << (int)totalSec << "s (" << (int)finalAvgSpeed << " MB/s)."
                  << Colors::RESET << "\n\n";

        ConfigurePermissions(destDir);
        DisableWebTalkTelemetry(destDir);
        return true;
    } else {
        std::cerr << "\n\r\033[K " << Colors::RED << "[ERROR] tar.exe exited with code: " << exitCode << Colors::RESET << "\n";
        return false;
    }
}

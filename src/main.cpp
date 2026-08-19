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

#include "common.hpp"
#include "extractor.hpp"
#include "patcher.hpp"
#include "drivers.hpp"
#include "state_manager.hpp"
#include <tlhelp32.h>
#include <atomic>
#include <thread>
#include <iomanip>

// Background Telemetry Guard: Tracks installer process tree and terminates child telemetry tasks
void WebTalkGuardThread(std::atomic<bool>& stopFlag, DWORD installerPid) {
    while (!stopFlag.load()) {
        if (installerPid != 0) {
            TerminateDescendantProcesses(installerPid, L"xwebtalk.exe");
            TerminateDescendantProcesses(installerPid, L"webtalk.exe");
            TerminateDescendantProcesses(installerPid, L"_xwebtalk.exe");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void PrintBanner(const fs::path& projectRoot) {
    ClearScreen();
    std::cout << Colors::CYAN
              << " +-----------------------------------------------------------------------------+\n"
              << " |                                                                             |\n"
              << " |   XILINX ISE DESIGN SUITE 14.7 - NATIVE WINDOWS 11 DEPLOYMENT SUITE         |\n"
              << " |                                                                             |\n"
              << " |            Bare-Metal x64 Native Architecture & State Management            |\n"
              << " +-----------------------------------------------------------------------------+\n"
              << Colors::RESET;
    std::cout << " " << Colors::DIM << "Target  : Windows 11 x64 (Build 26100 / 24H2 / 25H2+)" << Colors::RESET << "\n";
    std::cout << " " << Colors::DIM << "Root    : " << projectRoot.string() << Colors::RESET << "\n";
    std::cout << " -------------------------------------------------------------------------------\n";
}

void ExecuteOption1(const fs::path& projectRoot) {
    PrintBanner(projectRoot);
    std::cout << "\n " << Colors::BOLD << Colors::WHITE << "[STEP 1] XILINX ISE 14.7 DEPLOYMENT PIPELINE" << Colors::RESET << "\n\n";

    fs::path installerDir = projectRoot / "installer";
    fs::path extractedDir = installerDir / "extracted";

    fs::path xsetupExe = Extractor::FindSetupExecutable(extractedDir);
    bool doExtract = false;

    if (!xsetupExe.empty()) {
        std::cout << " " << Colors::GREEN << "[FOUND] Existing extracted installer at:" << Colors::RESET << "\n";
        std::cout << "   " << Colors::DIM << xsetupExe.string() << Colors::RESET << "\n\n";
        std::cout << " " << Colors::YELLOW << "Re-extract from .tar archive? (y/N): " << Colors::RESET;
        std::string ans;
        std::getline(std::cin, ans);
        if (ans == "y" || ans == "Y") {
            doExtract = true;
        }
    } else {
        doExtract = true;
    }

    if (doExtract) {
        fs::path tarPath = Extractor::FindInstallerArchive(installerDir);
        if (tarPath.empty()) {
            std::cout << " " << Colors::RED << "[ERROR] No .tar installer package found in:\n   "
                      << installerDir.string() << Colors::RESET << "\n\n";
            std::cout << " Please place Xilinx_ISE_DS_Win_14.7_1015_1.tar in the installer folder.\n";
            std::cout << "\n Press Enter to return to main menu...";
            std::cin.get();
            return;
        }

        // Payload Validation
        auto val = Extractor::ValidateArchive(tarPath);
        if (!val.valid) {
            std::cout << "\n " << Colors::RED << Colors::BOLD << "[PAYLOAD REJECTED] Archive validation failed:" << Colors::RESET << "\n";
            std::cout << "   " << Colors::YELLOW << val.errorReason << Colors::RESET << "\n\n";
            if (val.isVmArchive) {
                std::cout << " " << Colors::WHITE << "Remediation: Download the Multi-OS Bare-Metal installer (6.18 GB)\n"
                          << "              Filename: Xilinx_ISE_DS_Win_14.7_1015_1.tar\n"
                          << "              Do NOT download the Win10 VirtualBox VM edition.\n" << Colors::RESET;
            }
            std::cout << "\n Press Enter to return...";
            std::cin.get();
            return;
        }

        if (!Extractor::ExtractPackage(tarPath, extractedDir)) {
            std::cout << "\n Press Enter to return...";
            std::cin.get();
            return;
        }

        xsetupExe = Extractor::FindSetupExecutable(extractedDir);
        if (xsetupExe.empty()) {
            std::cout << " " << Colors::RED << "[ERROR] xsetup.exe not found after extraction." << Colors::RESET << "\n";
            std::cout << "\n Press Enter to return...";
            std::cin.get();
            return;
        }
    }

    // Pre-flight bypasses
    Patcher::SetEnvironmentVariablePermanent(L"XILINX_VC_CHECK_NOOP", L"1");
    SetEnvironmentVariableW(L"XILINX_VC_CHECK_NOOP", L"1");
    Extractor::DisableWebTalkTelemetry(extractedDir);

    std::string suggestedDrive = projectRoot.root_name().string();
    if (suggestedDrive.empty()) suggestedDrive = "C:";

    std::cout << "\n " << Colors::BOLD << Colors::WHITE
              << "+-------------------------------------------------------------------------+\n"
              << " |                 SETUP WIZARD CONFIGURATION DIRECTIVES                   |\n"
              << " +-------------------------------------------------------------------------+\n"
              << " | 1. Edition:       " << Colors::CYAN << "Choose your desired Edition" << Colors::WHITE << "                          |\n"
              << " | 2. Path:          " << Colors::CYAN << (suggestedDrive + "\\Xilinx") << Colors::WHITE << " (or " << Colors::CYAN << "C:\\Xilinx" << Colors::WHITE << ") - " << Colors::YELLOW << "No spaces in path!" << Colors::WHITE << "        |\n"
              << " | 3. Cable Drivers: " << Colors::RED << Colors::BOLD << "UNCHECK 'Install Cable Drivers'" << Colors::WHITE << "                      |\n"
              << " |                   " << Colors::DIM << "(Prevents 91% freeze; Step 2 configures drivers)" << Colors::WHITE << "     |\n"
              << " | 4. WinPcap:       " << Colors::RED << Colors::BOLD << "UNCHECK 'Install WinPcap'" << Colors::WHITE << "                            |\n"
              << " |                   " << Colors::DIM << "(Obsolete on Win11; prevents installation errors)" << Colors::WHITE << "    |\n"
              << " | 5. Guard Thread:  " << Colors::GREEN << "ACTIVE (Process Tree Tracking Enabled)" << Colors::WHITE << "               |\n"
              << " +-------------------------------------------------------------------------+" << Colors::RESET << "\n\n";

    std::cout << " " << Colors::GREEN << "Launching Xilinx installer wizard..." << Colors::RESET << "\n";

    fs::path workDir = xsetupExe.parent_path();
    std::wstring exeStr = xsetupExe.wstring();
    std::wstring dirStr = workDir.wstring();

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = exeStr.c_str();
    sei.lpDirectory = dirStr.c_str();
    sei.nShow = SW_SHOWNORMAL;

    DWORD installerPid = 0;
    if (ShellExecuteExW(&sei) && sei.hProcess != NULL) {
        installerPid = GetProcessId(sei.hProcess);
    } else {
        sei.lpVerb = L"open";
        if (ShellExecuteExW(&sei) && sei.hProcess != NULL) {
            installerPid = GetProcessId(sei.hProcess);
        }
    }

    std::atomic<bool> stopGuard(false);
    std::thread guardThread(WebTalkGuardThread, std::ref(stopGuard), installerPid);

    if (installerPid != 0) {
        std::cout << " " << Colors::WHITE << "Installer wizard active (PID: " << Colors::CYAN << installerPid << Colors::WHITE << ").\n"
                  << " Guard is monitoring the installer's child processes to prevent telemetry deadlocks.\n"
                  << " Complete the installation wizard, then return here and execute "
                  << Colors::CYAN << Colors::BOLD << "Option [2]" << Colors::RESET << ".\n";
    } else {
        std::cout << " " << Colors::YELLOW << "[NOTICE] Installer launched. Monitoring process tree...\n" << Colors::RESET;
    }

    std::cout << "\n Press Enter when installation wizard is finished to stop guard...";
    std::cin.get();

    stopGuard.store(true);
    if (guardThread.joinable()) {
        guardThread.join();
    }
    if (sei.hProcess) {
        CloseHandle(sei.hProcess);
    }
}

void ExecuteOption2(const fs::path& projectRoot) {
    PrintBanner(projectRoot);
    std::cout << "\n " << Colors::BOLD << Colors::WHITE << "[STEP 2] APPLY IN-PLACE PATCHES AND SHORTCUTS" << Colors::RESET << "\n\n";

    fs::path root = Patcher::DetectXilinxRoot();
    if (root.empty()) {
        std::cout << " " << Colors::YELLOW << "Default Xilinx path not found. Enter path (e.g. C:\\Xilinx or " << projectRoot.root_name().string() << "\\Xilinx): " << Colors::RESET;
        std::string inputPath;
        std::getline(std::cin, inputPath);
        root = inputPath;
    }

    if (!fs::exists(root / "14.7" / "ISE_DS")) {
        std::cout << "\n " << Colors::RED << "[ERROR] Invalid directory: " << root.string() << "\\14.7\\ISE_DS does not exist.\n"
                  << " Complete Step 1 (Installation) before applying patches." << Colors::RESET << "\n";
        std::cout << "\n Press Enter to return...";
        std::cin.get();
        return;
    }

    std::cout << " " << Colors::CYAN << "Target Installation: " << Colors::BOLD << (root / "14.7" / "ISE_DS").string() << Colors::RESET << "\n\n";
    std::cout << " Applying in-place patches and system configurations...\n\n";

    auto results = Patcher::ApplyAllPatches(root);
    for (const auto& r : results) {
        std::cout << "  " << (r.success ? (Colors::GREEN + "[OK]   ") : (Colors::RED + "[FAIL] "))
                  << Colors::BOLD << r.component << Colors::RESET << " - "
                  << Colors::DIM << r.description << Colors::RESET << "\n";
    }

    std::cout << "\n " << Colors::CYAN << "Configuring USB Cable Drivers..." << Colors::RESET << "\n";
    auto driverResults = DriverManager::InstallXilinxDrivers(root);
    for (const auto& dr : driverResults) {
        std::cout << "  " << (dr.second ? (Colors::GREEN + "[OK]   ") : (Colors::YELLOW + "[NOTE] "))
                  << Colors::BOLD << dr.first << Colors::RESET << "\n";
    }

    bool hvci = DriverManager::CheckHVCIStatus();
    if (hvci) {
        std::cout << "\n  " << Colors::YELLOW << "[NOTICE] Windows 11 Core Isolation (HVCI) is active.\n"
                  << "    - Digilent Adept USB JTAG programming works natively.\n"
                  << "    - For Xilinx Platform Cable USB (DLC9G/10), toggle Memory Integrity in Windows Security if required."
                  << Colors::RESET << "\n";
    } else {
        std::cout << "\n  " << Colors::GREEN << "[OK] Windows 11 Core Isolation is not blocking legacy drivers." << Colors::RESET << "\n";
    }

    std::cout << "\n " << Colors::GREEN << Colors::BOLD
              << "===============================================================================\n"
              << " [SUCCESS] In-Place Patching and Windows 11 Integration Complete!\n"
              << " Launchers created on your Desktop:\n"
              << "   - Xilinx ISE Project Navigator (64-bit)\n"
              << "   - Xilinx PlanAhead (64-bit)\n"
              << "   - Xilinx iMPACT (64-bit)\n"
              << "   - Xilinx License Manager\n"
              << "==============================================================================="
              << Colors::RESET << "\n";

    std::cout << "\n Press Enter to return to main menu...";
    std::cin.get();
}

void ExecuteOption3_Diagnostics(const fs::path& projectRoot) {
    PrintBanner(projectRoot);
    std::cout << "\n " << Colors::BOLD << Colors::WHITE << "[STEP 3] DIAGNOSTIC AUDIT & AUTO-REPAIR ENGINE" << Colors::RESET << "\n\n";

    fs::path root = Patcher::DetectXilinxRoot();
    if (root.empty()) {
        std::cout << " " << Colors::YELLOW << "Enter Xilinx root directory to audit (e.g. C:\\Xilinx or " << projectRoot.root_name().string() << "\\Xilinx): " << Colors::RESET;
        std::string inputPath;
        std::getline(std::cin, inputPath);
        root = inputPath;
    }

    if (!fs::exists(root / "14.7" / "ISE_DS")) {
        std::cout << "\n " << Colors::RED << "[ERROR] Could not find Xilinx ISE installation at: " << root.string() << Colors::RESET << "\n";
        std::cout << "\n Press Enter to return...";
        std::cin.get();
        return;
    }

    std::cout << " " << Colors::CYAN << "Auditing Installation at: " << Colors::BOLD << (root / "14.7" / "ISE_DS").string() << Colors::RESET << "\n\n";
    std::cout << " " << Colors::DIM << "Running integrity scan..." << Colors::RESET << "\n\n";

    auto auditResults = Patcher::RunDiagnosticsAndRepair(root);

    std::cout << " " << Colors::BOLD << "+------------------------------+------------+--------------------------------------+\n"
              << " | COMPONENT                    | STATUS     | ACTION TAKEN                         |\n"
              << " +------------------------------+------------+--------------------------------------+\n" << Colors::RESET;

    int healthyCount = 0;
    for (const auto& item : auditResults) {
        std::string statColor = Colors::GREEN;
        if (item.status == "REPAIRED") statColor = Colors::YELLOW;
        if (item.status == "MISSING" || item.status == "CORRUPTED" || item.status == "WARNING") statColor = Colors::RED;

        if (item.healthy) healthyCount++;

        std::cout << " | " << std::left << std::setw(28) << item.component.substr(0, 28)
                  << " | " << statColor << std::setw(10) << item.status << Colors::RESET
                  << " | " << std::left << std::setw(36) << item.actionTaken.substr(0, 36) << " |\n";
    }

    std::cout << " " << Colors::BOLD << "+------------------------------+------------+--------------------------------------+\n" << Colors::RESET;

    bool hvci = DriverManager::CheckHVCIStatus();
    std::cout << "\n " << Colors::CYAN << "Security & Hardware Subsystem:" << Colors::RESET << "\n";
    std::cout << "  - Windows 11 Memory Integrity (HVCI): " << (hvci ? (Colors::YELLOW + "ENABLED (Jungo legacy driver blocked, Digilent native active)") : (Colors::GREEN + "DISABLED (All drivers active)")) << Colors::RESET << "\n";

    std::cout << "\n " << Colors::GREEN << Colors::BOLD << "[SUMMARY] " << Colors::WHITE << healthyCount << " / " << auditResults.size() << " subsystem checks verified & healthy." << Colors::RESET << "\n";
    std::cout << "\n Press Enter to return to main menu...";
    std::cin.get();
}

void ExecuteOption4_Rollback(const fs::path& projectRoot) {
    PrintBanner(projectRoot);
    std::cout << "\n " << Colors::BOLD << Colors::WHITE << "[STEP 4] TRANSACTIONAL ROLLBACK & CLEAN UNINSTALL" << Colors::RESET << "\n\n";

    std::cout << " " << Colors::YELLOW << "This will undo all applied patches, restore backed-up original DLLs,\n"
              << " revert network provider orders, remove created shortcuts, and clear environment variables." << Colors::RESET << "\n\n";

    std::cout << " Are you sure you want to proceed with full rollback? (y/N): ";
    std::string ans;
    std::getline(std::cin, ans);
    if (ans != "y" && ans != "Y") {
        std::cout << "\n Rollback cancelled.\n Press Enter to return...";
        std::cin.get();
        return;
    }

    fs::path statePath = StateManager::GetDefaultStatePath();
    StateManager::Instance().RollbackAll(statePath);

    // Also restore network provider
    Patcher::RestoreNetworkProviders();

    std::cout << "\n " << Colors::GREEN << Colors::BOLD
              << "===============================================================================\n"
              << " [SUCCESS] System Rollback Complete.\n"
              << " All modifications have been reversed.\n"
              << "==============================================================================="
              << Colors::RESET << "\n";

    std::cout << "\n Press Enter to return to main menu...";
    std::cin.get();
}

void ExecuteOption5_Launchers(const fs::path& projectRoot) {
    PrintBanner(projectRoot);
    fs::path root = Patcher::DetectXilinxRoot();
    if (root.empty()) {
        std::string d = projectRoot.root_name().string();
        root = (d.empty() ? "C:" : d) + "\\Xilinx";
    }

    fs::path iseExe = root / "14.7" / "ISE_DS" / "ISE" / "bin" / "nt64" / "ise.exe";
    fs::path paBat = root / "14.7" / "ISE_DS" / "PlanAhead" / "bin" / "planAhead.bat";
    fs::path impactExe = root / "14.7" / "ISE_DS" / "ISE" / "bin" / "nt64" / "impact.exe";

    std::cout << "\n " << Colors::BOLD << Colors::WHITE << "[DIRECT APPLICATION LAUNCHERS]" << Colors::RESET << "\n\n";
    std::cout << "  " << Colors::CYAN << "[1]" << Colors::RESET << " Launch 64-bit ISE Project Navigator\n";
    std::cout << "  " << Colors::CYAN << "[2]" << Colors::RESET << " Launch 64-bit PlanAhead\n";
    std::cout << "  " << Colors::CYAN << "[3]" << Colors::RESET << " Launch 64-bit iMPACT Device Programmer\n";
    std::cout << "  " << Colors::CYAN << "[4]" << Colors::RESET << " Return to Main Menu\n\n";
    std::cout << " Select application [1-4]: ";

    std::string choice;
    std::getline(std::cin, choice);

    if (choice == "1" && fs::exists(iseExe)) {
        std::wstring wExe = iseExe.wstring();
        std::wstring wDir = iseExe.parent_path().wstring();
        ShellExecuteW(NULL, L"open", wExe.c_str(), NULL, wDir.c_str(), SW_SHOWNORMAL);
    } else if (choice == "2" && fs::exists(paBat)) {
        std::wstring wExe = paBat.wstring();
        std::wstring wDir = paBat.parent_path().wstring();
        ShellExecuteW(NULL, L"open", wExe.c_str(), NULL, wDir.c_str(), SW_SHOWNORMAL);
    } else if (choice == "3" && fs::exists(impactExe)) {
        std::wstring wExe = impactExe.wstring();
        std::wstring wDir = impactExe.parent_path().wstring();
        ShellExecuteW(NULL, L"open", wExe.c_str(), NULL, wDir.c_str(), SW_SHOWNORMAL);
    }
}

int main() {
    EnableVTMode();

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    fs::path projectRoot = fs::path(exePath).parent_path();

    while (true) {
        PrintBanner(projectRoot);
        std::cout << "\n " << Colors::BOLD << Colors::WHITE << "CONTROL MENU:" << Colors::RESET << "\n\n"
                  << "  " << Colors::CYAN << Colors::BOLD << "[1]" << Colors::RESET << " " << Colors::BOLD << "Step 1: Extract & Launch Xilinx 14.7 Installer" << Colors::RESET << "\n"
                  << "      " << Colors::DIM << "Validates payload, checks disk space, decompresses, and runs setup with telemetry guard." << Colors::RESET << "\n\n"
                  << "  " << Colors::CYAN << Colors::BOLD << "[2]" << Colors::RESET << " " << Colors::BOLD << "Step 2: Apply All Windows 11 Patches & Create Shortcuts" << Colors::RESET << "\n"
                  << "      " << Colors::DIM << "Applies 32/64-bit NOSH allocators, non-destructive network order, and logs transaction state." << Colors::RESET << "\n\n"
                  << "  " << Colors::CYAN << Colors::BOLD << "[3]" << Colors::RESET << " " << Colors::BOLD << "Step 3: Diagnostic Audit & Auto-Repair Existing Installation" << Colors::RESET << "\n"
                  << "      " << Colors::DIM << "Audits existing Xilinx files, detects broken/missing patches, and repairs them on the fly." << Colors::RESET << "\n\n"
                  << "  " << Colors::CYAN << Colors::BOLD << "[4]" << Colors::RESET << " " << Colors::BOLD << "Step 4: Transactional Rollback & Clean System Uninstall" << Colors::RESET << "\n"
                  << "      " << Colors::DIM << "Unspools changes from ledger: restores original DLLs, clears shortcuts, and reverts registry." << Colors::RESET << "\n\n"
                  << "  " << Colors::CYAN << Colors::BOLD << "[5]" << Colors::RESET << " Direct Application Launchers (ISE, PlanAhead, iMPACT)\n"
                  << "  " << Colors::CYAN << Colors::BOLD << "[6]" << Colors::RESET << " Exit Suite\n\n";

        std::cout << " Select an option [1-6]: ";
        std::string choice;
        std::getline(std::cin, choice);

        if (choice == "1") {
            ExecuteOption1(projectRoot);
        } else if (choice == "2") {
            ExecuteOption2(projectRoot);
        } else if (choice == "3") {
            ExecuteOption3_Diagnostics(projectRoot);
        } else if (choice == "4") {
            ExecuteOption4_Rollback(projectRoot);
        } else if (choice == "5") {
            ExecuteOption5_Launchers(projectRoot);
        } else if (choice == "6") {
            ClearScreen();
            std::cout << "\n " << Colors::GREEN << "Exiting Xilinx ISE 14.7 Deployment Suite." << Colors::RESET << "\n\n";
            break;
        }
    }
    return 0;
}

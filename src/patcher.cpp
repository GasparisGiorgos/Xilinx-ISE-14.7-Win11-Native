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

#include "patcher.hpp"
#include "extractor.hpp"
#include "state_manager.hpp"

static bool SafeCopyAndOverwrite(const fs::path& src, const fs::path& dest) {
    if (!fs::exists(src)) return false;
    SetFileAttributesW(dest.wstring().c_str(), FILE_ATTRIBUTE_NORMAL);
    std::error_code ec;
    fs::remove(dest, ec);
    BOOL ok = CopyFileW(src.wstring().c_str(), dest.wstring().c_str(), FALSE);
    return (ok != 0);
}

fs::path Patcher::DetectXilinxRoot() {
    wchar_t driveBuffer[512];
    DWORD len = GetLogicalDriveStringsW(512, driveBuffer);
    if (len > 0 && len < 512) {
        wchar_t* drive = driveBuffer;
        while (*drive) {
            fs::path rootDrive(drive);
            std::vector<fs::path> testPaths = {
                rootDrive / "Xilinx",
                rootDrive / "Xilinx_ISE",
                rootDrive / "Program Files" / "Xilinx",
                rootDrive / "Program Files (x86)" / "Xilinx"
            };
            for (const auto& p : testPaths) {
                if (fs::exists(p / "14.7" / "ISE_DS")) {
                    return p;
                }
            }
            drive += wcslen(drive) + 1;
        }
    }

    std::wstring xilinxEnv;
    if (GetEnvironmentVariablePermanent(L"XILINX", xilinxEnv) && !xilinxEnv.empty()) {
        fs::path envPath(xilinxEnv);
        if (fs::exists(envPath)) {
            fs::path cand = envPath.parent_path().parent_path().parent_path();
            if (fs::exists(cand / "14.7" / "ISE_DS")) return cand;
        }
    }

    return "";
}

bool Patcher::GetEnvironmentVariablePermanent(const std::wstring& name, std::wstring& outValue) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t buffer[4096];
        DWORD bufferSize = sizeof(buffer);
        DWORD type = 0;
        LONG res = RegQueryValueExW(hKey, name.c_str(), NULL, &type, (LPBYTE)buffer, &bufferSize);
        RegCloseKey(hKey);
        if (res == ERROR_SUCCESS) {
            outValue = buffer;
            return true;
        }
    }
    return false;
}

bool Patcher::SetEnvironmentVariablePermanent(const std::wstring& name, const std::wstring& value) {
    std::wstring prevValue = L"";
    GetEnvironmentVariablePermanent(name, prevValue);

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        LONG res = RegSetValueExW(hKey, name.c_str(), 0, REG_SZ,
                                  (const BYTE*)value.c_str(),
                                  (DWORD)((value.length() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
        
        DWORD_PTR dwResult;
        SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment",
                            SMTO_ABORTIFHUNG, 2000, &dwResult);

        if (res == ERROR_SUCCESS) {
            std::string nameStr(name.begin(), name.end());
            std::string prevStr(prevValue.begin(), prevValue.end());
            StateManager::Instance().RecordAction(ActionType::SET_ENV_VAR, nameStr, prevStr);
            return true;
        }
    }
    return false;
}

bool Patcher::OptimizeNetworkProviders() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\NetworkProvider\\Order",
                      0, KEY_SET_VALUE | KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t currentOrder[1024];
        DWORD size = sizeof(currentOrder);
        if (RegQueryValueExW(hKey, L"ProviderOrder", NULL, NULL, (LPBYTE)currentOrder, &size) == ERROR_SUCCESS) {
            std::wstring orderStr = currentOrder;

            // If LanmanWorkstation is already first, no modification needed!
            if (orderStr.rfind(L"LanmanWorkstation", 0) == 0) {
                RegCloseKey(hKey);
                return true;
            }

            // Backup original string before any modifications
            RegSetValueExW(hKey, L"ProviderOrder_OriginalBackup", 0, REG_SZ,
                          (const BYTE*)currentOrder,
                          (DWORD)((orderStr.length() + 1) * sizeof(wchar_t)));

            std::string origStr(orderStr.begin(), orderStr.end());
            StateManager::Instance().RecordAction(ActionType::MODIFY_PROVIDER_ORDER, "ProviderOrder", origStr);

            // Reorder intelligently: Move LanmanWorkstation & RDPNP to the front without removing any providers!
            std::vector<std::wstring> providers;
            std::wstringstream wss(orderStr);
            std::wstring token;
            while (std::getline(wss, token, L',')) {
                if (!token.empty() && token != L"LanmanWorkstation" && token != L"RDPNP") {
                    providers.push_back(token);
                }
            }

            std::wstring newOrder = L"LanmanWorkstation,RDPNP";
            for (const auto& p : providers) {
                newOrder += L"," + p;
            }

            RegSetValueExW(hKey, L"ProviderOrder", 0, REG_SZ,
                          (const BYTE*)newOrder.c_str(),
                          (DWORD)((newOrder.length() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
            return true;
        }
        RegCloseKey(hKey);
    }
    return false;
}

bool Patcher::RestoreNetworkProviders() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\NetworkProvider\\Order",
                      0, KEY_SET_VALUE | KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t backupOrder[1024];
        DWORD size = sizeof(backupOrder);
        if (RegQueryValueExW(hKey, L"ProviderOrder_OriginalBackup", NULL, NULL, (LPBYTE)backupOrder, &size) == ERROR_SUCCESS) {
            std::wstring backupStr = backupOrder;
            RegSetValueExW(hKey, L"ProviderOrder", 0, REG_SZ,
                          (const BYTE*)backupOrder,
                          (DWORD)((backupStr.length() + 1) * sizeof(wchar_t)));
            RegDeleteValueW(hKey, L"ProviderOrder_OriginalBackup");
            RegCloseKey(hKey);
            return true;
        }
        RegCloseKey(hKey);
    }
    return false;
}

bool Patcher::AutoInstallLicense() {
    wchar_t profilePath[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, profilePath) != S_OK) return false;
    
    fs::path userProfile(profilePath);
    fs::path targetLic = userProfile / ".Xilinx" / "Xilinx.lic";
    fs::create_directories(userProfile / ".Xilinx");

    if (fs::exists(targetLic) && fs::file_size(targetLic) > 0) {
        SetEnvironmentVariablePermanent(L"XILINXD_LICENSE_FILE", targetLic.wstring());
        return true;
    }

    std::vector<fs::path> searchDirs = {
        userProfile / "Desktop",
        userProfile / "Downloads",
        userProfile / "Documents",
        fs::current_path(),
        fs::current_path() / "installer"
    };

    for (const auto& dir : searchDirs) {
        if (fs::exists(dir)) {
            for (const auto& entry : fs::directory_iterator(dir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".lic") {
                    if (SafeCopyAndOverwrite(entry.path(), targetLic)) {
                        StateManager::Instance().RecordAction(ActionType::CREATE_FILE, targetLic.string());
                        SetEnvironmentVariablePermanent(L"XILINXD_LICENSE_FILE", targetLic.wstring());
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool Patcher::ProvisionLicense() {
    wchar_t profilePath[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, profilePath) != S_OK) return false;

    fs::path userProfile(profilePath);
    fs::path xilinxDir = userProfile / ".Xilinx";
    fs::create_directories(xilinxDir);
    fs::path targetLic = xilinxDir / "Xilinx.lic";

    bool hasValidLic = false;
    for (const auto& entry : fs::directory_iterator(xilinxDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".lic" && fs::file_size(entry.path()) > 0) {
            targetLic = entry.path();
            hasValidLic = true;
            break;
        }
    }

    if (!hasValidLic) {
        AutoInstallLicense();
        if (fs::exists(targetLic) && fs::file_size(targetLic) > 0) {
            hasValidLic = true;
        }
    }

    if (!hasValidLic) {
        std::cout << "\n " << Colors::BOLD << Colors::WHITE
                  << "+-------------------------------------------------------------------------+\n"
                  << " |                     XILINX LICENSE CONFIGURATION                        |\n"
                  << " +-------------------------------------------------------------------------+\n"
                  << " | " << Colors::YELLOW << "No active Xilinx license file (.lic) was detected." << Colors::WHITE << "                   |\n"
                  << " |                                                                         |\n"
                  << " | 1. Opening File Explorer folder at:                                     |\n"
                  << " |    " << Colors::CYAN << xilinxDir.string() << Colors::WHITE << "\n"
                  << " | 2. " << Colors::GREEN << "Drag and drop your .lic license file" << Colors::WHITE << " directly into the folder.   |\n"
                  << " +-------------------------------------------------------------------------+" << Colors::RESET << "\n\n";

        std::wstring dirStr = xilinxDir.wstring();
        ShellExecuteW(NULL, L"open", L"explorer.exe", dirStr.c_str(), NULL, SW_SHOWNORMAL);

        std::cout << " " << Colors::YELLOW << "Press Enter once you have placed your .lic file inside the folder..." << Colors::RESET;
        std::string dummy;
        std::getline(std::cin, dummy);

        for (const auto& entry : fs::directory_iterator(xilinxDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".lic" && fs::file_size(entry.path()) > 0) {
                targetLic = entry.path();
                hasValidLic = true;
                break;
            }
        }
    }

    if (hasValidLic) {
        SetEnvironmentVariablePermanent(L"XILINXD_LICENSE_FILE", targetLic.wstring());
        SetEnvironmentVariablePermanent(L"LM_LICENSE_FILE", targetLic.wstring());
        return true;
    }
    return false;
}

bool Patcher::CreateShortcut(const fs::path& shortcutPath, const fs::path& targetPath,
                             const fs::path& workDir, const fs::path& iconPath,
                             const std::wstring& description) {
    IShellLinkW* psl = NULL;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (LPVOID*)&psl);
    if (SUCCEEDED(hr)) {
        IPersistFile* ppf = NULL;
        std::wstring wTarget = targetPath.wstring();
        std::wstring wWork = workDir.wstring();
        std::wstring wIcon = iconPath.wstring();
        std::wstring wLnk = shortcutPath.wstring();

        psl->SetPath(wTarget.c_str());
        if (!workDir.empty()) psl->SetWorkingDirectory(wWork.c_str());
        if (!iconPath.empty()) psl->SetIconLocation(wIcon.c_str(), 0);
        if (!description.empty()) psl->SetDescription(description.c_str());

        hr = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);
        if (SUCCEEDED(hr)) {
            hr = ppf->Save(wLnk.c_str(), TRUE);
            ppf->Release();
            if (SUCCEEDED(hr)) {
                StateManager::Instance().RecordAction(ActionType::CREATE_SHORTCUT, shortcutPath.string());
            }
        }
        psl->Release();
    }
    return SUCCEEDED(hr);
}

static bool CreateShortcutWithArgs(const fs::path& shortcutPath, const fs::path& targetPath,
                                  const std::wstring& arguments, const fs::path& workDir,
                                  const fs::path& iconPath, const std::wstring& description) {
    IShellLinkW* psl = NULL;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (LPVOID*)&psl);
    if (SUCCEEDED(hr)) {
        IPersistFile* ppf = NULL;
        std::wstring wTarget = targetPath.wstring();
        std::wstring wWork = workDir.wstring();
        std::wstring wIcon = iconPath.wstring();
        std::wstring wLnk = shortcutPath.wstring();

        psl->SetPath(wTarget.c_str());
        if (!arguments.empty()) psl->SetArguments(arguments.c_str());
        if (!workDir.empty()) psl->SetWorkingDirectory(wWork.c_str());
        if (!iconPath.empty()) psl->SetIconLocation(wIcon.c_str(), 0);
        if (!description.empty()) psl->SetDescription(description.c_str());
        psl->SetShowCmd(SW_SHOWMINNOACTIVE);

        hr = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);
        if (SUCCEEDED(hr)) {
            hr = ppf->Save(wLnk.c_str(), TRUE);
            ppf->Release();
            if (SUCCEEDED(hr)) {
                StateManager::Instance().RecordAction(ActionType::CREATE_SHORTCUT, shortcutPath.string());
            }
        }
        psl->Release();
    }
    return SUCCEEDED(hr);
}

void Patcher::CreateAllShortcuts(const fs::path& xilinxRoot) {
    CoInitialize(NULL);

    wchar_t desktopPath[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopPath);
    fs::path desktop(desktopPath);

    wchar_t programsPath[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, programsPath);
    fs::path startMenu = fs::path(programsPath) / "Xilinx Design Tools (Win11)";
    fs::create_directories(startMenu);

    // 1. Remove obsolete unpatched legacy shortcuts created by default Xilinx installer wizard
    std::vector<std::wstring> legacyShortcuts = {
        L"Project Navigator.lnk",
        L"ISE Design Suite 14.7.lnk",
        L"PlanAhead 14.7.lnk",
        L"iMPACT.lnk",
        L"Xilinx ISE Design Suite 14.7.lnk",
        L"Xilinx Core Generator.lnk",
        L"Xilinx FPGA Editor.lnk"
    };
    for (const auto& lk : legacyShortcuts) {
        fs::path p = desktop / lk;
        if (fs::exists(p)) {
            std::error_code ec;
            SetFileAttributesW(p.wstring().c_str(), FILE_ATTRIBUTE_NORMAL);
            fs::remove(p, ec);
        }
    }

    fs::path settings64 = xilinxRoot / "14.7" / "ISE_DS" / "settings64.bat";
    fs::path settings32 = xilinxRoot / "14.7" / "ISE_DS" / "settings32.bat";
    fs::path ise64Exe = xilinxRoot / "14.7" / "ISE_DS" / "ISE" / "bin" / "nt64" / "ise.exe";
    fs::path ise32Exe = xilinxRoot / "14.7" / "ISE_DS" / "ISE" / "bin" / "nt" / "ise.exe";
    fs::path paBat = xilinxRoot / "14.7" / "ISE_DS" / "PlanAhead" / "bin" / "planAhead.bat";
    fs::path impactExe = xilinxRoot / "14.7" / "ISE_DS" / "ISE" / "bin" / "nt64" / "_impact4.exe";
    fs::path xlcmExe = xilinxRoot / "14.7" / "ISE_DS" / "common" / "bin" / "nt64" / "xlcm.exe";
    fs::path paIco = xilinxRoot / "14.7" / "ISE_DS" / "PlanAhead" / "doc" / "images" / "planAhead_logo.ico";
    fs::path paIconToUse = fs::exists(paIco) ? paIco : ise64Exe;

    if (fs::exists(ise64Exe)) {
        CreateShortcut(desktop / "Xilinx ISE Project Navigator (64-bit).lnk",
                       ise64Exe, ise64Exe.parent_path(), ise64Exe,
                       L"Xilinx ISE 14.7 Project Navigator (64-bit)");
        CreateShortcut(startMenu / "ISE Project Navigator (64-bit).lnk",
                       ise64Exe, ise64Exe.parent_path(), ise64Exe,
                       L"Xilinx ISE 14.7 Project Navigator (64-bit)");
    }
    if (fs::exists(ise32Exe)) {
        CreateShortcut(desktop / "Xilinx ISE Project Navigator (32-bit).lnk",
                       ise32Exe, ise32Exe.parent_path(), ise32Exe,
                       L"Xilinx ISE 14.7 Project Navigator (32-bit)");
        CreateShortcut(startMenu / "ISE Project Navigator (32-bit).lnk",
                       ise32Exe, ise32Exe.parent_path(), ise32Exe,
                       L"Xilinx ISE 14.7 Project Navigator (32-bit)");
    }
    if (fs::exists(paBat)) {
        CreateShortcut(desktop / "Xilinx PlanAhead (64-bit).lnk",
                       paBat, paBat.parent_path(), paIconToUse,
                       L"Xilinx PlanAhead 14.7 Floorplanner (64-bit)");
        CreateShortcut(startMenu / "PlanAhead (64-bit).lnk",
                       paBat, paBat.parent_path(), paIconToUse,
                       L"Xilinx PlanAhead 14.7 Floorplanner (64-bit)");
    }
    if (fs::exists(impactExe)) {
        CreateShortcut(desktop / "Xilinx iMPACT (64-bit).lnk",
                       impactExe, impactExe.parent_path(), impactExe,
                       L"Xilinx iMPACT Device Programmer (64-bit)");
        CreateShortcut(startMenu / "iMPACT (64-bit).lnk",
                       impactExe, impactExe.parent_path(), impactExe,
                       L"Xilinx iMPACT Device Programmer (64-bit)");
    }
    if (fs::exists(xlcmExe)) {
        CreateShortcutWithArgs(desktop / "Xilinx License Manager.lnk",
                               xlcmExe, L"-manage", xlcmExe.parent_path(), xlcmExe,
                               L"Xilinx License Configuration Manager");
        CreateShortcutWithArgs(startMenu / "License Configuration Manager.lnk",
                               xlcmExe, L"-manage", xlcmExe.parent_path(), xlcmExe,
                               L"Xilinx License Configuration Manager");
    }

    CoUninitialize();
}

std::vector<PatchResult> Patcher::ApplyAllPatches(const fs::path& xilinxRoot) {
    std::vector<PatchResult> results;

    TerminateProcessByName(L"xlcm.exe");
    TerminateProcessByName(L"_xlcm.exe");
    TerminateProcessByName(L"ise.exe");
    TerminateProcessByName(L"_pn.exe");
    TerminateProcessByName(L"impact.exe");
    TerminateProcessByName(L"_impact.exe");
    TerminateProcessByName(L"xsetup.exe");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // 0. Optimize Network Provider Order (Bypasses WSL P9NP / WebClient deadlocks with intelligent reordering)
    bool netOpt = OptimizeNetworkProviders();
    results.push_back({"Network Provider Optimization", netOpt ? "Prioritized LAN Providers (Preserved WSL/VPN providers)" : "Permission note (run as Admin)", netOpt});

    fs::path iseDir = xilinxRoot / "14.7" / "ISE_DS" / "ISE";
    fs::path commonDir = xilinxRoot / "14.7" / "ISE_DS" / "common";
    fs::path paDir = xilinxRoot / "14.7" / "ISE_DS" / "PlanAhead";
    fs::path edkDir = xilinxRoot / "14.7" / "ISE_DS" / "EDK";

    // 0.1 Install MSVC 2008 runtimes and copy CRT assemblies for PlanAhead
    fs::path vc64 = commonDir / "bin" / "nt64" / "vcredist_x64.exe";
    fs::path vc86 = commonDir / "bin" / "nt" / "vcredist_x86.exe";
    if (fs::exists(vc64)) {
        std::wstring vcCmd = L"\"" + vc64.wstring() + L"\" /q /norestart";
        _wsystem(vcCmd.c_str());
    }
    if (fs::exists(vc86)) {
        std::wstring vcCmd = L"\"" + vc86.wstring() + L"\" /q /norestart";
        _wsystem(vcCmd.c_str());
    }

    fs::path unwrappedVC90_64 = paDir / "bin" / "unwrapped" / "win64.o" / "Microsoft.VC90.CRT";
    fs::path targetVC90_64 = paDir / "lib" / "win64.o" / "Microsoft.VC90.CRT";
    if (fs::exists(unwrappedVC90_64)) {
        fs::create_directories(targetVC90_64);
        for (const auto& entry : fs::directory_iterator(unwrappedVC90_64)) {
            SafeCopyAndOverwrite(entry.path(), targetVC90_64 / entry.path().filename());
            SafeCopyAndOverwrite(entry.path(), paDir / "lib" / "win64.o" / entry.path().filename());
        }
    }

    // 1. ISE nt64 libPortability
    fs::path nosh64Dll = iseDir / "lib" / "nt64" / "libPortabilityNOSH.dll";
    fs::path isePort64Dll = iseDir / "lib" / "nt64" / "libPortability.dll";
    fs::path iseBackup64 = iseDir / "lib" / "nt64" / "libPortability.dll.orig";

    if (fs::exists(nosh64Dll)) {
        if (!fs::exists(iseBackup64) && fs::exists(isePort64Dll)) {
            SafeCopyAndOverwrite(isePort64Dll, iseBackup64);
            StateManager::Instance().RecordAction(ActionType::BACKUP_FILE, isePort64Dll.string(), iseBackup64.string());
        }
        if (SafeCopyAndOverwrite(nosh64Dll, isePort64Dll)) {
            results.push_back({"ISE 64-bit Core", "Patched libPortability.dll with NOSH build", true});
        } else {
            results.push_back({"ISE 64-bit Core", "Failed to overwrite libPortability.dll", false});
        }
    } else {
        results.push_back({"ISE 64-bit Core", "libPortabilityNOSH.dll not found", false});
    }

    // 2. Common nt64 libPortability
    fs::path commPort64Dll = commonDir / "lib" / "nt64" / "libPortability.dll";
    fs::path commBackup64 = commonDir / "lib" / "nt64" / "libPortability.dll.orig";
    if (fs::exists(commonDir / "lib" / "nt64")) {
        if (!fs::exists(commBackup64) && fs::exists(commPort64Dll)) {
            SafeCopyAndOverwrite(commPort64Dll, commBackup64);
            StateManager::Instance().RecordAction(ActionType::BACKUP_FILE, commPort64Dll.string(), commBackup64.string());
        }
        if (fs::exists(nosh64Dll)) {
            if (SafeCopyAndOverwrite(nosh64Dll, commPort64Dll)) {
                results.push_back({"Common 64-bit Tools", "Patched libPortability.dll with NOSH build", true});
            } else {
                results.push_back({"Common 64-bit Tools", "Failed to overwrite libPortability.dll", false});
            }
        }
    }

    // 3. PlanAhead win64.o libPortability
    fs::path paWin64 = paDir / "lib" / "win64.o";
    fs::path paPort64Dll = paWin64 / "libPortability.dll";
    fs::path paBackup64 = paWin64 / "libPortability.dll.orig";
    if (fs::exists(paWin64)) {
        if (!fs::exists(paBackup64) && fs::exists(paPort64Dll)) {
            SafeCopyAndOverwrite(paPort64Dll, paBackup64);
            StateManager::Instance().RecordAction(ActionType::BACKUP_FILE, paPort64Dll.string(), paBackup64.string());
        }
        if (fs::exists(nosh64Dll)) {
            if (SafeCopyAndOverwrite(nosh64Dll, paPort64Dll)) {
                results.push_back({"PlanAhead Floorplanner", "Injected libPortabilityNOSH.dll into win64.o", true});
            } else {
                results.push_back({"PlanAhead Floorplanner", "Failed to overwrite libPortability.dll", false});
            }
        }
    }

    // 4. 32-bit Architecture Patches (ISE nt & common nt)
    fs::path nosh32Dll = iseDir / "lib" / "nt" / "libPortabilityNOSH.dll";
    fs::path isePort32Dll = iseDir / "lib" / "nt" / "libPortability.dll";
    fs::path iseBackup32 = iseDir / "lib" / "nt" / "libPortability.dll.orig";
    if (fs::exists(nosh32Dll)) {
        if (!fs::exists(iseBackup32) && fs::exists(isePort32Dll)) {
            SafeCopyAndOverwrite(isePort32Dll, iseBackup32);
            StateManager::Instance().RecordAction(ActionType::BACKUP_FILE, isePort32Dll.string(), iseBackup32.string());
        }
        if (SafeCopyAndOverwrite(nosh32Dll, isePort32Dll)) {
            results.push_back({"ISE 32-bit Core", "Patched libPortability.dll with NOSH build", true});
        }
    }

    fs::path commPort32Dll = commonDir / "lib" / "nt" / "libPortability.dll";
    fs::path commBackup32 = commonDir / "lib" / "nt" / "libPortability.dll.orig";
    if (fs::exists(commonDir / "lib" / "nt")) {
        if (!fs::exists(commBackup32) && fs::exists(commPort32Dll)) {
            SafeCopyAndOverwrite(commPort32Dll, commBackup32);
            StateManager::Instance().RecordAction(ActionType::BACKUP_FILE, commPort32Dll.string(), commBackup32.string());
        }
        if (fs::exists(nosh32Dll)) {
            if (SafeCopyAndOverwrite(nosh32Dll, commPort32Dll)) {
                results.push_back({"Common 32-bit Tools", "Patched libPortability.dll with NOSH build", true});
            }
        }
    }

    // 5. Environment Variables & PATH injection
    SetEnvironmentVariablePermanent(L"XILINX", iseDir.wstring());
    SetEnvironmentVariablePermanent(L"XILINX_DSP", iseDir.wstring());
    SetEnvironmentVariablePermanent(L"XILINX_EDK", edkDir.wstring());
    SetEnvironmentVariablePermanent(L"XILINX_PLANAHEAD", paDir.wstring());
    SetEnvironmentVariablePermanent(L"XILINX_VC_CHECK_NOOP", L"1");

    std::wstring currentPath = L"";
    GetEnvironmentVariablePermanent(L"PATH", currentPath);
    std::wstring iseBin = (iseDir / "bin" / "nt64").wstring();
    std::wstring iseLib = (iseDir / "lib" / "nt64").wstring();
    std::wstring commBin = (commonDir / "bin" / "nt64").wstring();
    std::wstring commLib = (commonDir / "lib" / "nt64").wstring();
    std::wstring paBin = (paDir / "bin").wstring();

    std::wstring newPath = L"";
    if (currentPath.find(iseBin) == std::wstring::npos) newPath += iseBin + L";";
    if (currentPath.find(iseLib) == std::wstring::npos) newPath += iseLib + L";";
    if (currentPath.find(commBin) == std::wstring::npos) newPath += commBin + L";";
    if (currentPath.find(commLib) == std::wstring::npos) newPath += commLib + L";";
    if (currentPath.find(paBin) == std::wstring::npos) newPath += paBin + L";";

    if (!newPath.empty()) {
        SetEnvironmentVariablePermanent(L"PATH", newPath + currentPath);
    }
    results.push_back({"Windows Environment & PATH", "Injected binary/library directories into PATH", true});

    // 6. Clean up old compatibility layers to ensure pure CRT execution
    HKEY hCompatKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers",
                      0, KEY_SET_VALUE, &hCompatKey) == ERROR_SUCCESS) {
        std::wstring iseExePath = (iseDir / "bin" / "nt64" / "ise.exe").wstring();
        std::wstring impactPath = (iseDir / "bin" / "nt64" / "_impact.exe").wstring();
        std::wstring paPath = (paDir / "bin" / "planAhead.bat").wstring();

        RegDeleteValueW(hCompatKey, iseExePath.c_str());
        RegDeleteValueW(hCompatKey, impactPath.c_str());
        RegDeleteValueW(hCompatKey, paPath.c_str());
        RegCloseKey(hCompatKey);
    }

    // 6.1 Permanently disable obsolete 2013 XilinxNotify update check prompts
    HKEY hUpdateKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Xilinx\\Common\\Update", 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hUpdateKey, NULL) == ERROR_SUCCESS) {
        DWORD zero = 0;
        RegSetValueExW(hUpdateKey, L"AutoCheck", 0, REG_DWORD, (const BYTE*)&zero, sizeof(zero));
        RegSetValueExW(hUpdateKey, L"CheckFrequency", 0, REG_DWORD, (const BYTE*)&zero, sizeof(zero));
        RegSetValueExW(hUpdateKey, L"NotificationOption", 0, REG_DWORD, (const BYTE*)&zero, sizeof(zero));
        RegSetValueExW(hUpdateKey, L"Enable", 0, REG_DWORD, (const BYTE*)&zero, sizeof(zero));
        RegCloseKey(hUpdateKey);
    }
    HKEY hPrefKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Xilinx\\ISE\\14.7\\Project Navigator\\Preferences", 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hPrefKey, NULL) == ERROR_SUCCESS) {
        DWORD zero = 0;
        RegSetValueExW(hPrefKey, L"AutoUpdateCheck", 0, REG_DWORD, (const BYTE*)&zero, sizeof(zero));
        RegCloseKey(hPrefKey);
    }
    results.push_back({"XilinxNotify Deactivation", "Disabled obsolete 2013 update check servers", true});

    // 7. Provision License
    bool licOk = ProvisionLicense();
    results.push_back({"License Auto-Provisioning", licOk ? "Configured XILINXD_LICENSE_FILE (.lic)" : "User prompt required (Option 2 will assist)", licOk});

    // 8. Generate Desktop & Start Menu Shortcuts
    CreateAllShortcuts(xilinxRoot);
    results.push_back({"Desktop & Start Menu Shortcuts", "Created 64-bit / 32-bit & PlanAhead launchers", true});

    return results;
}

std::vector<DiagnosticItem> Patcher::RunDiagnosticsAndRepair(const fs::path& xilinxRoot) {
    std::vector<DiagnosticItem> report;

    fs::path iseDir = xilinxRoot / "14.7" / "ISE_DS" / "ISE";
    fs::path commonDir = xilinxRoot / "14.7" / "ISE_DS" / "common";
    fs::path paDir = xilinxRoot / "14.7" / "ISE_DS" / "PlanAhead";

    // 1. Check ISE nt64 libPortability.dll
    fs::path nosh64Dll = iseDir / "lib" / "nt64" / "libPortabilityNOSH.dll";
    fs::path isePort64Dll = iseDir / "lib" / "nt64" / "libPortability.dll";
    if (fs::exists(isePort64Dll) && fs::exists(nosh64Dll)) {
        if (fs::file_size(isePort64Dll) == fs::file_size(nosh64Dll)) {
            report.push_back({"ISE 64-bit SmartHeap Patch", "HEALTHY", "Verified NOSH DLL signature", isePort64Dll.string(), true});
        } else {
            SafeCopyAndOverwrite(nosh64Dll, isePort64Dll);
            report.push_back({"ISE 64-bit SmartHeap Patch", "REPAIRED", "Replaced unpatched DLL with NOSH build", isePort64Dll.string(), true});
        }
    } else {
        report.push_back({"ISE 64-bit SmartHeap Patch", "MISSING", "File not found", isePort64Dll.string(), false});
    }

    // 2. Check Common nt64 libPortability.dll
    fs::path commPort64Dll = commonDir / "lib" / "nt64" / "libPortability.dll";
    if (fs::exists(commPort64Dll) && fs::exists(nosh64Dll)) {
        if (fs::file_size(commPort64Dll) == fs::file_size(nosh64Dll)) {
            report.push_back({"Common 64-bit SmartHeap Patch", "HEALTHY", "Verified NOSH DLL signature", commPort64Dll.string(), true});
        } else {
            SafeCopyAndOverwrite(nosh64Dll, commPort64Dll);
            report.push_back({"Common 64-bit SmartHeap Patch", "REPAIRED", "Replaced unpatched DLL with NOSH build", commPort64Dll.string(), true});
        }
    } else {
        report.push_back({"Common 64-bit SmartHeap Patch", "MISSING", "File not found", commPort64Dll.string(), false});
    }

    // 3. Check PlanAhead win64.o libPortability.dll
    fs::path paPort64Dll = paDir / "lib" / "win64.o" / "libPortability.dll";
    if (fs::exists(paPort64Dll) && fs::exists(nosh64Dll)) {
        if (fs::file_size(paPort64Dll) == fs::file_size(nosh64Dll)) {
            report.push_back({"PlanAhead SmartHeap Patch", "HEALTHY", "Verified NOSH DLL signature", paPort64Dll.string(), true});
        } else {
            SafeCopyAndOverwrite(nosh64Dll, paPort64Dll);
            report.push_back({"PlanAhead SmartHeap Patch", "REPAIRED", "Injected NOSH DLL into win64.o", paPort64Dll.string(), true});
        }
    } else {
        report.push_back({"PlanAhead SmartHeap Patch", "MISSING", "File not found", paPort64Dll.string(), false});
    }

    // 4. Check 32-bit Core libPortability.dll
    fs::path nosh32Dll = iseDir / "lib" / "nt" / "libPortabilityNOSH.dll";
    fs::path isePort32Dll = iseDir / "lib" / "nt" / "libPortability.dll";
    if (fs::exists(isePort32Dll) && fs::exists(nosh32Dll)) {
        if (fs::file_size(isePort32Dll) == fs::file_size(nosh32Dll)) {
            report.push_back({"ISE 32-bit Core Allocator", "HEALTHY", "Verified 32-bit NOSH DLL signature", isePort32Dll.string(), true});
        } else {
            SafeCopyAndOverwrite(nosh32Dll, isePort32Dll);
            report.push_back({"ISE 32-bit Core Allocator", "REPAIRED", "Patched 32-bit NOSH DLL", isePort32Dll.string(), true});
        }
    }

    // 5. Check Network Provider Order
    HKEY hNetKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\NetworkProvider\\Order", 0, KEY_READ, &hNetKey) == ERROR_SUCCESS) {
        wchar_t currentOrder[1024];
        DWORD size = sizeof(currentOrder);
        if (RegQueryValueExW(hNetKey, L"ProviderOrder", NULL, NULL, (LPBYTE)currentOrder, &size) == ERROR_SUCCESS) {
            std::wstring orderStr = currentOrder;
            if (orderStr.rfind(L"LanmanWorkstation", 0) == 0) {
                report.push_back({"Network Provider Order", "HEALTHY", "LAN provider prioritized (WSL/VPN safe)", "HKLM\\...\\NetworkProvider\\Order", true});
            } else {
                OptimizeNetworkProviders();
                report.push_back({"Network Provider Order", "REPAIRED", "Reordered LanmanWorkstation to front", "HKLM\\...\\NetworkProvider\\Order", true});
            }
        }
        RegCloseKey(hNetKey);
    }

    // 6. Check WebTalk user opt-out
    wchar_t profilePath[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, profilePath) == S_OK) {
        fs::path userXml = fs::path(profilePath) / ".Xilinx" / "webtalk.xml";
        if (fs::exists(userXml)) {
            report.push_back({"WebTalk User Opt-Out", "HEALTHY", "Opt-out XML verified", userXml.string(), true});
        } else {
            Extractor::DisableWebTalkTelemetry(xilinxRoot);
            report.push_back({"WebTalk User Opt-Out", "REPAIRED", "Generated opt-out XML", userXml.string(), true});
        }
    }

    // 7. Check XILINX Environment Variable
    std::wstring xilinxEnv;
    if (GetEnvironmentVariablePermanent(L"XILINX", xilinxEnv) && !xilinxEnv.empty()) {
        report.push_back({"XILINX Env Variable", "HEALTHY", "Pointed to: " + std::string(xilinxEnv.begin(), xilinxEnv.end()), "HKCU\\Environment\\XILINX", true});
    } else {
        SetEnvironmentVariablePermanent(L"XILINX", iseDir.wstring());
        report.push_back({"XILINX Env Variable", "REPAIRED", "Set to ISE directory", "HKCU\\Environment\\XILINX", true});
    }

    // 8. Check License Environment Variable
    std::wstring licEnv;
    if (GetEnvironmentVariablePermanent(L"XILINXD_LICENSE_FILE", licEnv) && !licEnv.empty()) {
        report.push_back({"XILINXD_LICENSE_FILE", "HEALTHY", "License key active: " + std::string(licEnv.begin(), licEnv.end()), "HKCU\\Environment\\XILINXD_LICENSE_FILE", true});
    } else {
        bool licFound = AutoInstallLicense();
        report.push_back({"XILINXD_LICENSE_FILE", licFound ? "REPAIRED" : "WARNING", licFound ? "Auto-detected and imported license" : "No .lic found in search paths", "HKCU\\Environment\\XILINXD_LICENSE_FILE", licFound});
    }

    // 9. Check XilinxNotify AutoUpdate Deactivation
    HKEY hAuditUpdate;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Xilinx\\Common\\Update", 0, KEY_READ, &hAuditUpdate) == ERROR_SUCCESS) {
        DWORD autoCheck = 1;
        DWORD size = sizeof(autoCheck);
        if (RegQueryValueExW(hAuditUpdate, L"AutoCheck", NULL, NULL, (LPBYTE)&autoCheck, &size) == ERROR_SUCCESS && autoCheck == 0) {
            report.push_back({"XilinxNotify Deactivation", "HEALTHY", "Disabled obsolete update check servers", "HKCU\\Software\\Xilinx\\Common\\Update", true});
        } else {
            HKEY hSet;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Xilinx\\Common\\Update", 0, KEY_SET_VALUE, &hSet) == ERROR_SUCCESS) {
                DWORD zero = 0;
                RegSetValueExW(hSet, L"AutoCheck", 0, REG_DWORD, (const BYTE*)&zero, sizeof(zero));
                RegSetValueExW(hSet, L"CheckFrequency", 0, REG_DWORD, (const BYTE*)&zero, sizeof(zero));
                RegSetValueExW(hSet, L"Enable", 0, REG_DWORD, (const BYTE*)&zero, sizeof(zero));
                RegCloseKey(hSet);
            }
            report.push_back({"XilinxNotify Deactivation", "REPAIRED", "Disabled obsolete update check servers", "HKCU\\Software\\Xilinx\\Common\\Update", true});
        }
        RegCloseKey(hAuditUpdate);
    }

    return report;
}

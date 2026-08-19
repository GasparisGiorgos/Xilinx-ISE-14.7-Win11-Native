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

#include "drivers.hpp"

bool DriverManager::CheckHVCIStatus() {
    HKEY hKey;
    const wchar_t* path = L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity";
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD val = 0;
        DWORD valSize = sizeof(val);
        LONG res = RegQueryValueExW(hKey, L"Enabled", NULL, NULL, (LPBYTE)&val, &valSize);
        RegCloseKey(hKey);
        return (res == ERROR_SUCCESS && val == 1);
    }
    return false;
}

std::vector<std::pair<std::string, bool>> DriverManager::InstallXilinxDrivers(const fs::path& xilinxRoot) {
    std::vector<std::pair<std::string, bool>> results;
    fs::path commonNt64 = xilinxRoot / "14.7" / "ISE_DS" / "common" / "bin" / "nt64";
    fs::path iseNt64 = xilinxRoot / "14.7" / "ISE_DS" / "ISE" / "bin" / "nt64";

    fs::path wdregDir = fs::exists(commonNt64 / "wdreg.exe") ? commonNt64 : iseNt64;
    fs::path wdregExe = wdregDir / "wdreg.exe";

    if (fs::exists(wdregExe)) {
        std::wstring cmd1 = L"\"" + wdregExe.wstring() + L"\" -inf \"" + (wdregDir / "windrvr6.inf").wstring() + L"\" install >nul 2>&1";
        std::wstring cmd2 = L"\"" + wdregExe.wstring() + L"\" -inf \"" + (wdregDir / "xusbdrvr.inf").wstring() + L"\" install >nul 2>&1";
        
        int r1 = _wsystem(cmd1.c_str());
        int r2 = _wsystem(cmd2.c_str());
        results.push_back({"wdreg WinDriver & XUSB Driver Registration", (r1 == 0 || r2 == 0)});
    } else {
        results.push_back({"wdreg.exe Driver Utility", false});
    }

    fs::path installerHelper = iseNt64 / "install_drivers.exe";
    if (fs::exists(installerHelper)) {
        std::wstring cmd3 = L"\"" + installerHelper.wstring() + L"\" >nul 2>&1";
        _wsystem(cmd3.c_str());
        results.push_back({"Xilinx install_drivers.exe Helper", true});
    }

    return results;
}

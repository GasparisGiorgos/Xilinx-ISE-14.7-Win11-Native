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

#pragma once
#include "common.hpp"

struct PatchResult {
    std::string component;
    std::string description;
    bool success;
};

struct DiagnosticItem {
    std::string component;
    std::string status;
    std::string actionTaken;
    std::string pathOrKey;
    bool healthy;
};

class Patcher {
public:
    static fs::path DetectXilinxRoot();
    static std::vector<PatchResult> ApplyAllPatches(const fs::path& xilinxRoot);
    static std::vector<DiagnosticItem> RunDiagnosticsAndRepair(const fs::path& xilinxRoot);
    static void CreateAllShortcuts(const fs::path& xilinxRoot);
    static bool AutoInstallLicense();
    static bool ProvisionLicense();
    static bool OptimizeNetworkProviders();
    static bool RestoreNetworkProviders();

    static bool GetEnvironmentVariablePermanent(const std::wstring& name, std::wstring& outValue);
    static bool SetEnvironmentVariablePermanent(const std::wstring& name, const std::wstring& value);
    static bool CreateShortcut(const fs::path& shortcutPath, const fs::path& targetPath,
                                const fs::path& workDir = "", const fs::path& iconPath = "",
                                const std::wstring& description = L"");
    static void PurgeAllXilinxEnvironmentAndShortcuts();
};

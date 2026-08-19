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

struct ArchiveValidationResult {
    bool valid;
    bool isVmArchive;
    uint64_t fileSize;
    std::string archiveName;
    std::string errorReason;
};

class Extractor {
public:
    static ArchiveValidationResult ValidateArchive(const fs::path& tarPath);
    static bool ExtractPackage(const fs::path& tarPath, const fs::path& destDir);
    static fs::path FindInstallerArchive(const fs::path& installerDir);
    static fs::path FindSetupExecutable(const fs::path& extractedDir);
    static void ConfigurePermissions(const fs::path& targetDir);
    static void DisableWebTalkTelemetry(const fs::path& extractedDir);
};

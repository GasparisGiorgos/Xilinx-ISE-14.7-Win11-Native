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

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>

namespace fs = std::filesystem;

// ANSI Colors
namespace Colors {
    inline const std::string CYAN = "\033[96m";
    inline const std::string GREEN = "\033[92m";
    inline const std::string YELLOW = "\033[93m";
    inline const std::string RED = "\033[91m";
    inline const std::string MAGENTA = "\033[95m";
    inline const std::string BOLD = "\033[1m";
    inline const std::string DIM = "\033[2m";
    inline const std::string WHITE = "\033[97m";
    inline const std::string RESET = "\033[0m";
}

inline void EnableVTMode() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode)) return;
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
}

inline void ClearScreen() {
    std::cout << "\033[2J\033[1;1H";
}

// Disk space check (returns free gigabytes on target drive)
inline double GetDriveFreeSpaceGB(const fs::path& p) {
    std::wstring rootStr = p.root_path().wstring();
    if (rootStr.empty()) rootStr = L"C:\\";
    ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes, totalNumberOfFreeBytes;
    if (GetDiskFreeSpaceExW(rootStr.c_str(), &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
        return static_cast<double>(freeBytesAvailable.QuadPart) / (1024.0 * 1024.0 * 1024.0);
    }
    return -1.0;
}

// Win32 Process Tree Traversal - finds all descendant PIDs originating from rootPid
inline std::set<DWORD> GetDescendantProcessIds(DWORD rootPid) {
    std::set<DWORD> descendants;
    if (rootPid == 0) return descendants;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return descendants;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    std::vector<PROCESSENTRY32W> allProcs;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            allProcs.push_back(pe);
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);

    // BFS to find all children & grandchildren recursively
    std::vector<DWORD> queue = { rootPid };
    size_t head = 0;
    while (head < queue.size()) {
        DWORD currentParent = queue[head++];
        for (const auto& proc : allProcs) {
            if (proc.th32ParentProcessID == currentParent && proc.th32ProcessID != currentParent) {
                if (descendants.find(proc.th32ProcessID) == descendants.end()) {
                    descendants.insert(proc.th32ProcessID);
                    queue.push_back(proc.th32ProcessID);
                }
            }
        }
    }
    return descendants;
}

// Scoped process termination: Only kills targetExe if its PID is a confirmed descendant of parentPid
inline int TerminateDescendantProcesses(DWORD parentPid, const std::wstring& targetExe) {
    if (parentPid == 0) return 0;
    auto descendants = GetDescendantProcessIds(parentPid);
    if (descendants.empty()) return 0;

    int killedCount = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (descendants.find(pe.th32ProcessID) != descendants.end()) {
                if (_wcsicmp(pe.szExeFile, targetExe.c_str()) == 0) {
                    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (hProc) {
                        TerminateProcess(hProc, 0);
                        CloseHandle(hProc);
                        killedCount++;
                    }
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return killedCount;
}

inline void TerminateProcessByName(const std::wstring& processName) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, processName.c_str()) == 0) {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProc) {
                    TerminateProcess(hProc, 0);
                    CloseHandle(hProc);
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
}

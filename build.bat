@echo off
rem Copyright (C) 2026 Giorgos Gasparis <gasp.giorgos@gmail.com>
rem
rem This program is free software: you can redistribute it and/or modify
rem it under the terms of the GNU General Public License as published by
rem the Free Software Foundation, either version 3 of the License, or
rem (at your option) any later version.
rem
rem This program is distributed in the hope that it will be useful,
rem but WITHOUT ANY WARRANTY; without even the implied warranty of
rem MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
rem GNU General Public License for more details.
rem
rem You should have received a copy of the GNU General Public License
rem along with this program.  If not, see <https://www.gnu.org/licenses/>.

title Building Xilinx ISE 14.7 Windows 11 Native Deployer
cd /d "%~dp0"

echo =========================================================================
echo       Xilinx ISE 14.7 Native Windows 11 Deployer - C++20 Build System
echo =========================================================================

set "PATH=W:\Users\Gio\w64devkit\bin;%USERPROFILE%\w64devkit\bin;C:\w64devkit\bin;%PATH%"

echo [1/3] Verifying C++ Compiler ...
g++.exe --version >nul 2>&1
if errorlevel 1 (
    echo [ERROR] MinGW-w64 compiler g++ not found.
    pause
    exit /b 1
)

echo [2/3] Compiling Windows UAC Manifest Resource ...
windres.exe src\resource.rc -O coff -o src\resource.res >nul 2>&1

echo [3/3] Compiling Native C++20 Static PE Binary ...
set "SRC=src\main.cpp src\extractor.cpp src\patcher.cpp src\drivers.cpp src\state_manager.cpp"
if exist src\resource.res set "SRC=src\main.cpp src\extractor.cpp src\patcher.cpp src\drivers.cpp src\state_manager.cpp src\resource.res"

g++.exe -O2 -std=c++20 -static %SRC% -o Xilinx_Win11_Deployer.exe -lole32 -luuid -lshell32 -ladvapi32 -luser32

if errorlevel 1 (
    echo [ERROR] Compilation failed.
    pause
    exit /b 1
)

echo =========================================================================
echo  [SUCCESS] Standalone Executable Compiled: Xilinx_Win11_Deployer.exe
echo =========================================================================
for %%I in (Xilinx_Win11_Deployer.exe) do echo  File Size: %%~zI bytes ^| Timestamp: %%~tI
echo.

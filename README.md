# Xilinx ISE 14.7 - Windows 11 Native Deployment Suite

A standalone deployment, patching, and rollback tool written in C++20 for running Xilinx ISE Design Suite 14.7 bare-metal on 64-bit Windows 11 (24H2 / 25H2+) without VirtualBox VMs, Linux emulation, or Docker.

---

## Overview

AMD/Xilinx deprecated ISE 14.7 for Windows 7 in 2013, providing a slow VirtualBox Linux VM workaround for Windows 10/11 users. This project resolves the underlying Win32 runtime, memory allocator, and network provider issues to run the 64-bit toolchain natively on modern Windows 11 systems.

### What This Tool Does

- **Fixes File Dialog Crashes**: Replaces the buggy MicroQuill SmartHeap memory allocator (`libPortability.dll`) with Xilinx's native non-SmartHeap CRT build (`libPortabilityNOSH.dll`) across 32-bit and 64-bit tools, fixing access violation crashes (`0xC0000005`) on `File > Open Project` and `Save As`.
- **Prevents Installer Freezes**: Bypasses dead 2013 WebTalk telemetry endpoints by writing opt-out configurations and running a process tree guard that prevents the setup wizard from hanging at 82% and 90%.
- **Resolves Startup Deadlocks**: Reorders Windows Network Providers non-destructively so NetBIOS queries resolve immediately without waiting on inactive WSL (`P9NP`) or VPN hooks.
- **Configures 64-Bit Environment**: Sets all required environment variables (`XILINX`, `XILINX_DSP`, `XILINX_VC_CHECK_NOOP=1`), updates user PATH, and generates clean desktop shortcuts for 64-bit Project Navigator, PlanAhead, and iMPACT.
- **Safe Rollback**: Tracks every modification in a transaction ledger (`deployer_state.json`), allowing full reversal and clean uninstallation at any time.

---

## How to Use

### 1. Download the Installer
Download the official **Bare-Metal Multi-OS installer archive**:
* Filename: `Xilinx_ISE_DS_Win_14.7_1015_1.tar` (~6.18 GB)
* Place the file in the `installer/` folder next to the deployer, or keep it in your `Downloads` folder (the deployer detects it automatically).
* *Note: Do not download the Windows 10 VirtualBox VM edition (`.ova` / `.vmdk`), as it is not the bare-metal installer.*

### 2. Run the Deployer
Launch `Xilinx_Win11_Deployer.exe` (it will automatically request Administrator elevation).

### 3. Step-by-Step Installation

1. **Select Option `[1]` (Extract & Launch Installer)**:
   * The deployer validates the archive, checks for at least 25 GB free disk space, and extracts the installer with a real-time progress bar.
   * When the official setup wizard opens:
     * Choose **ISE WebPACK (Free)** or your licensed edition.
     * Set the installation directory (e.g. `C:\Xilinx` or `W:\Xilinx` - ensure no spaces in the path).
     * **Uncheck "Install Cable Drivers"** (prevents the 91% installer freeze; drivers are configured in Step 2).
     * Finish the setup wizard and return to the deployer terminal.

2. **Select Option `[2]` (Apply All Patches & Create Shortcuts)**:
   * Automatically replaces 32-bit and 64-bit `libPortability.dll` with the native NOSH builds across ISE, Common Tools, and PlanAhead.
   * Optimizes the Windows network provider order safely.
   * Sets all system environment variables and PATH entries.
   * Generates 64-bit shortcuts on your Desktop for Project Navigator, PlanAhead, and iMPACT.

3. **Optional Management Options**:
   * **Option `[3]` (Diagnostic Audit & Repair)**: Scans your installation to verify all DLLs, registry keys, and shortcuts are intact, repairing any broken components.
   * **Option `[4]` (Transactional Rollback)**: Completely reverts all patches, restores backed-up original DLLs, clears shortcuts, and removes added registry variables.

---

## Building from Source

### Prerequisites
* **MinGW-w64** (GCC with C++20 support) or **w64devkit**
* Windows 10 or Windows 11 (64-bit)

### Build Command
Run the included build script:

```cmd
build.bat
```

Or compile directly with `g++`:

```cmd
windres src/resource.rc -O coff -o src/resource.res
g++ -O2 -std=c++20 -static src/main.cpp src/extractor.cpp src/patcher.cpp src/drivers.cpp src/state_manager.cpp src/resource.res -o Xilinx_Win11_Deployer.exe -lole32 -luuid -lshell32 -ladvapi32 -luser32
```

---

## Technical Details

For in-depth analysis of Windows 11 failure modes, memory allocation internals, network provider hooks, and codebase architecture, see [ARCHITECTURE.md](ARCHITECTURE.md).

---

## License & Disclaimer

This project is licensed under the **GNU General Public License v3.0 (GPL-3.0)** - see the [LICENSE](LICENSE) file for details.

```text
Copyright (C) 2026 Giorgos Gasparis <gasp.giorgos@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.
```

Xilinx ISE Design Suite, PlanAhead, and iMPACT are property of Advanced Micro Devices, Inc. (AMD). All patches use official alternate binaries included in original Xilinx distributions.

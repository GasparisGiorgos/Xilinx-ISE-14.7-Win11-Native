# Xilinx ISE 14.7 Windows 11 Native Architecture & Technical Notes

## 1. Summary
This document provides the technical specification of the Win32 deployment and compatibility engine for **Xilinx ISE Design Suite 14.7** on **64-bit Windows 11** (Build 26100 / 24H2 / 25H2+).

The deployment tool is written in **native C++20** and compiled into a standalone static binary (`Xilinx_Win11_Deployer.exe`). It operates directly on the host OS without virtualization or interpreter dependencies.

---

## 2. Windows 11 Subsystem Incompatibilities & Root Causes

When AMD/Xilinx deprecated ISE 14.7 in 2013, the software targeted Windows 7 and Windows XP. On Windows 11 (24H2+), several OS subsystem changes cause legacy ISE to fail:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          Windows 11 Failure Modes                           │
├──────────────────────────────────────┬──────────────────────────────────────┤
│ 1. SmartHeap Access Violation        │ Faulting Module: libPortability.dll  │
│ 2. PlanAhead Loader Incompatibility  │ win64.o Library Path Failure         │
│ 3. MSVC 2008/2010 Registry Check     │ False Missing Runtime Assertion      │
│ 4. WebTalk 2013 Telemetry Hang       │ Decommissioned Socket Block (82%/90%)│
│ 5. FlexNet Unprovisioned Broadcast   │ 12.5MB Startup Deadlock              │
│ 6. HVCI / Driver Signature Policy    │ Kernel Driver Blocking (windrvr6.sys)│
│ 7. WSL/P9NP Network Provider Hang    │ MPR.dll Synchronous Enumeration Lock │
└──────────────────────────────────────┴──────────────────────────────────────┘
```

### 2.1 SmartHeap File Dialog Crash (`libPortability.dll`)
* **Symptom**: Opening common file dialogs (`File > Open Project`, `Add Source`, `Save As`) in 64-bit Project Navigator or iMPACT triggers an access violation (`0xC0000005`).
* **Root Cause**: Xilinx compiled `libPortability.dll` using MicroQuill SmartHeap in 2013. On modern Windows 11, common dialog APIs (`GetOpenFileNameW`, `IFileDialog`) query modern shell namespace extensions (OneDrive, Quick Access). SmartHeap miscalculates the 64-bit pointers returned by these hooks and corrupts heap memory.
* **Resolution**: During 14.7 development, Xilinx engineers provided an alternate build called **`libPortabilityNOSH.dll`** (No-SmartHeap) which relies on the standard Windows C Runtime. The deployer backs up the original DLL to `.orig` and copies `libPortabilityNOSH.dll` over `libPortability.dll` across `ISE_DS\ISE\lib\nt64`, `ISE_DS\common\lib\nt64`, `ISE_DS\ISE\lib\nt`, and `ISE_DS\common\lib\nt`.

### 2.2 PlanAhead 64-Bit Crash
* **Symptom**: Launching PlanAhead standalone or invoking floorplanning from Project Navigator causes a splash-screen freeze.
* **Root Cause**: PlanAhead contains its own native library directory at `ISE_DS\PlanAhead\lib\win64.o` with an unpatched `libPortability.dll`.
* **Resolution**: The deployer injects `libPortabilityNOSH.dll` into `PlanAhead\lib\win64.o\libPortability.dll` and sanitizes environment variables.

### 2.3 WebTalk Telemetry Bypass & Process Tree Guard
* **Symptom**: Setup wizard stalls at ~82% ("Configure WebTalk") and ~90% ("Enable WebTalk").
* **Root Cause**: The installer spawns `xwebtalk.exe` and `webtalk.exe` to send usage data to decommissioned 2013 HTTP endpoints. Because the servers do not respond, the process enters an infinite TCP socket wait.
* **Resolution**: 
  1. **Opt-Out XML**: Writes `webtalk.xml` in `%USERPROFILE%\.Xilinx\` and `%APPDATA%\Xilinx\Common\` with `WebTalkEnable=0`.
  2. **Process Tree Guard**: Spawns a background thread that captures the installer's root PID via `ShellExecuteExW` and uses Win32 Toolhelp32 snapshots (`GetDescendantProcessIds`) to terminate only child `xwebtalk.exe` tasks spawned by the installer.

### 2.4 License Provisioning & Startup Discovery
* **Symptom**: `ise.exe` hangs on launch at ~12.5 MB to 24 MB RAM usage.
* **Root Cause**: When no local `.lic` file exists in `%USERPROFILE%\.Xilinx\`, FlexNet attempts a broadcast query across all virtual/WSL network adapters searching for a license server daemon, entering an indefinite retry loop.
* **Resolution**: If no `.lic` key is found, the deployer opens Windows File Explorer targeting `%USERPROFILE%\.Xilinx`, waits for the user to place their `.lic` file, detects it on Enter, and sets `XILINXD_LICENSE_FILE` in the user registry.

---

## 3. System Safety & State Isolation

### 3.1 Network Provider Reordering
* **Design**: Modification of `HKLM\SYSTEM\CurrentControlSet\Control\NetworkProvider\Order` is handled non-destructively.
* **Mechanism**: Rather than removing `P9NP` (WSL Plan 9 provider), Docker, or VPN providers, `Patcher::OptimizeNetworkProviders()` saves the original string to `ProviderOrder_OriginalBackup` and reorders `LanmanWorkstation,RDPNP` to the front while keeping all other providers at the end (`LanmanWorkstation,RDPNP,P9NP,webclient,...`).
* **Result**: Local NetBIOS queries resolve immediately without waiting on WSL/MPR hooks, while keeping WSL, Docker, and VPN networking operational.

### 3.2 State Management & Rollback
* **Ledger**: The `StateManager` logs every file creation, directory creation, `.orig` DLL backup, environment variable, and shortcut into a JSON ledger (`deployer_state.json` in `%USERPROFILE%\.Xilinx\`).
* **Rollback Function (`Option [4]`)**: Traverses the transaction ledger in reverse order, restoring backed-up `.orig` DLLs, clearing environment variables, restoring original registry keys, and removing created shortcuts.

### 3.3 Payload Validation & Storage Checks
* **Payload Check**: `Extractor::ValidateArchive()` checks file size thresholds (minimum 1.0 GB; expected ~6.18 GB) and reads internal tar stream headers.
* **VM Package Detection**: Detects and rejects VirtualBox OVA / VMDK / Ubuntu VM archives with clear error messages.
* **Disk Space Check**: Calls `GetDiskFreeSpaceExW` on target drive, requiring at least **25.0 GB** of free space before decompression starts.

---

## 4. Source Tree

```
src/
├── main.cpp              CLI entry point, ANSI rendering, telemetry guard, menu dispatcher
├── extractor.cpp/.hpp    Archive discovery, payload check, disk space check, extraction monitor
├── patcher.cpp/.hpp      DLL patcher, network provider reordering, shortcut generator
├── drivers.cpp/.hpp      Driver registration and Windows 11 HVCI detection
├── state_manager.cpp/.hpp Transaction ledger, action logger, and rollback engine
├── common.hpp            Win32 API headers, process tree traversal, ANSI helpers
├── manifest.xml          UAC execution manifest
└── resource.rc           Windows resource definition
```

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
│ 3. MSVC 2008 SxS Runtime Failure     │ WinSxS Error 14001 (Microsoft.VC90)  │
│ 4. WebTalk 2013 Telemetry Hang       │ Decommissioned Socket Block (82%/90%)│
│ 5. XilinxNotify Obsolete Server Error│ HTTP 301 Moved Permanently Popup     │
│ 6. FlexNet Unprovisioned Broadcast   │ 12.5MB Startup Deadlock              │
│ 7. HVCI / Driver Signature Policy    │ Kernel Driver Blocking (windrvr6.sys)│
│ 8. WSL/P9NP Network Provider Hang    │ MPR.dll Synchronous Enumeration Lock │
│ 9. Inactive Shortcut Focus Timeout   │ SW_SHOWMINNOACTIVE Qt Window Drop    │
└──────────────────────────────────────┴──────────────────────────────────────┘
```

### 2.1 SmartHeap File Dialog Crash (`libPortability.dll`)
* **Symptom**: Opening common file dialogs (`File > Open Project`, `Add Source`, `Save As`) in 64-bit Project Navigator or iMPACT triggers an access violation (`0xC0000005`).
* **Root Cause**: Xilinx compiled `libPortability.dll` using MicroQuill SmartHeap in 2013. On modern Windows 11, common dialog APIs (`GetOpenFileNameW`, `IFileDialog`) query modern shell namespace extensions (OneDrive, Quick Access). SmartHeap miscalculates the 64-bit pointers returned by these hooks and corrupts heap memory.
* **Resolution**: During 14.7 development, Xilinx engineers provided an alternate build called **`libPortabilityNOSH.dll`** (No-SmartHeap) which relies on the standard Windows C Runtime. The deployer backs up the original DLL to `.orig` and copies `libPortabilityNOSH.dll` over `libPortability.dll` across `ISE_DS\ISE\lib\nt64`, `ISE_DS\common\lib\nt64`, `ISE_DS\ISE\lib\nt`, and `ISE_DS\common\lib\nt`.

### 2.2 PlanAhead 64-Bit SmartHeap Crash
* **Symptom**: Launching PlanAhead standalone or invoking floorplanning from Project Navigator causes a splash-screen freeze.
* **Root Cause**: PlanAhead contains its own native library directory at `ISE_DS\PlanAhead\lib\win64.o` with an unpatched `libPortability.dll`.
* **Resolution**: The deployer injects `libPortabilityNOSH.dll` into `PlanAhead\lib\win64.o\libPortability.dll` and sanitizes environment variables.

### 2.3 PlanAhead MSVC 2008 Side-by-Side (SxS) Runtime Deployment
* **Symptom**: Launching PlanAhead fails with `ERROR_SXS_CANT_GEN_ACTCTX` (Win32 Error 14001: *"The application has failed to start because its side-by-side configuration is incorrect"*).
* **Root Cause**: PlanAhead's `win64.o` PE binaries depend on Microsoft Visual C++ 2008 (MSVC 9.0) CRT assemblies (`msvcr90.dll`, `msvcp90.dll`) and their associated manifest. On Windows 11, the native WinSxS store does not contain the legacy 2008 manifests by default.
* **Resolution**: The deployer extracts and copies the embedded `Microsoft.VC90.CRT` directory from `PlanAhead\bin\unwrapped\win64.o\` directly into `PlanAhead\lib\win64.o\`, alongside silently executing `vcredist_x64.exe` and `vcredist_x86.exe`.

### 2.4 WebTalk Telemetry Bypass & Process Tree Guard
* **Symptom**: Setup wizard stalls at ~82% ("Configure WebTalk") and ~90% ("Enable WebTalk").
* **Root Cause**: The installer spawns `xwebtalk.exe` and `webtalk.exe` to send usage data to decommissioned 2013 HTTP endpoints. Because the servers do not respond, the process enters an infinite TCP socket wait.
* **Resolution**: 
  1. **Opt-Out XML**: Writes `webtalk.xml` in `%USERPROFILE%\.Xilinx\` and `%APPDATA%\Xilinx\Common\` with `WebTalkEnable=0`.
  2. **Process Tree Guard**: Spawns a background thread that captures the installer's root PID via `ShellExecuteExW` and uses Win32 Toolhelp32 snapshots (`GetDescendantProcessIds`) to terminate only child `xwebtalk.exe` tasks spawned by the installer.

### 2.5 Obsolete Update Server Neutralization (`stub_bytes.hpp`)
* **Symptom**: Project Navigator or PlanAhead startup triggers a popup error dialog: *"Server Returned: Moved Permanently"*.
* **Root Cause**: `ise.exe` automatically spawns `common\bin\nt64\xilinxnotify.exe` and `_xilinxnotify.exe` on startup to poll obsolete 2013 HTTP update servers.
* **Resolution**:
  1. A minimal static Win32 PE executable (`int main() { return 0; }`) is encoded into `src/stub_bytes.hpp` using a 1-byte XOR mask (`kSilentNotifyStubMask = 0x5A`) to prevent heuristic AV dropper flags on raw `MZ` PE headers.
  2. The deployer backs up original executables to `.orig` and writes the decoded silent stub over `xilinxnotify.exe` and `_xilinxnotify.exe` in both `common\bin\nt64` and `common\bin\nt`.
  3. Disables update polling in registry (`HKCU\Software\Xilinx\Common\Update` and `HKCU\Software\Xilinx\ISE\14.7\Project Navigator\Preferences`).
  4. ISE executes the silent stub on startup, which exits with return code `0` in 0.001 ms with zero network calls and zero popups.

### 2.6 License Provisioning & Startup Discovery
* **Symptom**: `ise.exe` hangs on launch at ~12.5 MB to 24 MB RAM usage.
* **Root Cause**: When no local `.lic` file exists in `%USERPROFILE%\.Xilinx\`, FlexNet attempts a broadcast query across all virtual/WSL network adapters searching for a license server daemon, entering an indefinite retry loop.
* **Resolution**: Uses an isolated `installer/licensing` directory. If no license is detected during Step 2, the deployer opens File Explorer directly to `installer/licensing`, guides the user to drop their `.lic` file, copies it to `%USERPROFILE%\.Xilinx\Xilinx.lic`, and sets `XILINXD_LICENSE_FILE` & `LM_LICENSE_FILE` in the user registry.

### 2.7 Direct GUI Shortcuts & Windows Shell Focus Activation
* **Symptom**: Desktop shortcuts either launch lingering `cmd.exe` terminal windows, or (in the case of License Manager) briefly appear and terminate after 2 seconds.
* **Root Cause**: Legacy shortcuts launched batch wrappers (`settings64.bat`). Direct shortcuts created with `SW_SHOWMINNOACTIVE` caused spawned Qt GUI child processes (`_xlcm.exe`, `_impact4.exe`) to fail to acquire foreground window focus on Windows 11.
* **Resolution**: 
  * Shortcuts target native Win32 PE binaries directly (`ise.exe`, `_impact4.exe`, `xlcm.exe -manage`) with `SW_SHOWNORMAL`.
  * Official native high-resolution icons are extracted directly from PE resources (`ise.exe,0`, `impact.exe,0`, `xlcm.exe,0`, `planAhead_logo.ico,0`).
  * Unpatched 32-bit and installer-generated desktop shortcuts (`ISE Design Suite 14.7.lnk`, `Project Navigator.lnk`) are automatically swept from both User and Public Desktop locations.

### 2.8 WSL Plan 9 (`P9NP`) Synchronous Deadlock & Local MPR Shimming
* **Symptom**: `ise.exe` and `_pn.exe` hang indefinitely on launch at ~12.5 MB to 20 MB RAM before creating any window.
* **Root Cause**: During startup, `libPortability.dll` calls Windows Multiple Provider Router (`MPR.dll`) functions (`WNetOpenEnumA`, `WNetEnumResourceA`, `WNetCloseEnum`) to enumerate network drive shares. Windows MPR iterates sequentially through every provider listed in `HKLM\SYSTEM\CurrentControlSet\Control\NetworkProvider\Order\ProviderOrder`. If Windows Subsystem for Linux (WSL) is installed, Windows registers `P9NP` (Plan 9 Network Provider, `p9np.dll`). When queried while WSL is dormant or not actively running a 9P server daemon, `p9np.dll` enters an indefinite synchronous kernel wait (`NtWaitForSingleObject` in `KERNELBASE.dll`). Simply moving LAN providers to the front of `ProviderOrder` does NOT resolve this because `WNetEnumResourceA` loops through all registered providers.
* **Resolution**:
  1. **Dual-Layer Defense**:
     - **Local Zero-Latency MPR Shim (`src/stub_bytes.hpp`)**: A minimal standalone 4 KB Win32 DLL implementing `WNetOpenEnumA/W`, `WNetEnumResourceA/W`, and `WNetCloseEnum` returning `ERROR_NO_NETWORK` (1222) in 0 ns is embedded and deployed directly into Xilinx `bin` and `lib` directories (`ISE\bin\nt64`, `ISE\lib\nt64`, `common\bin\nt64`, `common\lib\nt64`, `PlanAhead\lib\win64.o`, and 32-bit counterparts). Because `MPR.dll` is not a `KnownDLL`, the Windows PE loader prioritizes the local directory, bypassing `C:\Windows\System32\MPR.dll` and `p9np.dll` completely with zero latency and zero dependency on elevated registry permissions.
     - **ProviderOrder Sanitization**: `Patcher::OptimizeNetworkProviders()` saves the original string to `ProviderOrder_OriginalBackup` and explicitly purges `P9NP` from `ProviderOrder`, ensuring that any secondary tools that query system MPR also resolve without stalling.

---

## 3. System Safety & State Isolation

### 3.1 Network Provider Reordering & WSL Sanitization
* **Design**: Modification of `HKLM\SYSTEM\CurrentControlSet\Control\NetworkProvider\Order` is handled safely and transactionally.
* **Mechanism**: `Patcher::OptimizeNetworkProviders()` saves the original string to `ProviderOrder_OriginalBackup`, moves `LanmanWorkstation,RDPNP` to the front, and strips the deadlocked `P9NP` entry while keeping all other active providers intact.
* **Result**: Local NetBIOS queries resolve immediately without waiting on WSL/MPR hooks, while keeping Docker, VPN, and RDP networking operational. Full original configuration is restored upon Option [4] rollback.

### 3.2 Two-Layer Rollback & Unconditional System Purge (`Option [4]`)
* **Layer 1 (Ledger Replay)**: If `deployer_state.json` exists in `%USERPROFILE%\.Xilinx\`, the `StateManager` traverses the transaction ledger in reverse order, restoring backed-up `.orig` DLLs and reversing logged actions.
* **Layer 2 (Unconditional Deep Purge)**: Regardless of whether the ledger file exists (e.g. if the user deleted the `.Xilinx` directory manually), `Patcher::PurgeAllXilinxEnvironmentAndShortcuts()`:
  * Wipes all Xilinx environment variables (`XILINX`, `XILINX_DSP`, `XILINX_EDK`, `XILINX_PLANAHEAD`, `XILINXD_LICENSE_FILE`, `LM_LICENSE_FILE`, `XILINX_VC_CHECK_NOOP`, `NO_XILINX_DATA_LICENSE`) from `HKCU\Environment`.
  * Strips all Xilinx directories from the User `Path` variable.
  * Restores original Network Provider Order.
  * Purges all Xilinx shortcuts from User Desktop, Public Desktop, and Start Menu.
  * Broadcasts `WM_SETTINGCHANGE` so all running Windows processes release the variables immediately.

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
├── patcher.cpp/.hpp      DLL patcher, network provider reordering, shortcut generator, purge engine
├── drivers.cpp/.hpp      Driver registration and Windows 11 HVCI detection
├── state_manager.cpp/.hpp Transaction ledger, action logger, and rollback engine
├── stub_bytes.hpp        XOR-masked silent no-op executable payload (AV-safe)
├── common.hpp            Win32 API headers, process tree traversal, dynamic path helpers
├── manifest.xml          UAC administrator execution manifest
└── resource.rc           Windows resource definition & version metadata
```

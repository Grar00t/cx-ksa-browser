# CX Build Installation

## Installed mode

CX Build uses an Inno Setup per-user installer. The default destination is:

`%LOCALAPPDATA%\Programs\CX Build`

The destination can be changed in the installer. A Start Menu shortcut and an
uninstall shortcut are created. Adding the install directory to the current
user's `PATH` is optional and unchecked by default.

The installer does not bundle advertising software, third-party offers,
updaters, scheduled tasks, services, or background agents. CX Build has no
application auto-update mechanism by design.

Uninstall removes the program files, Start Menu shortcuts, installer
registration, the optional PATH entry if the installer added it, and the local
WebView2 data directory under the install directory. User data stored under
`%APPDATA%\CX Build` is intentionally not deleted by uninstall.

## Portable mode

Use the generated `CX-Build-Portable-0.9.0` folder or its ZIP archive. Keep
`cx.exe` and `run_portable.bat` together and start the application through
`run_portable.bat`.

The launcher redirects `APPDATA` and `LOCALAPPDATA` into a `data` folder
beside the executable. This keeps CX-owned database, configuration, MCP
allowlist, and local logs with the portable copy instead of the user's normal
profile. The folder is relocatable and is intended for writable USB storage.

Portable mode still requires the Microsoft Edge WebView2 Runtime to be
available on the Windows machine because CX uses WebView2 as its renderer.

## Building packages

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File installer\build_installer.ps1
```

Outputs are written to `dist\`.

Code signing is attempted only when both `signtool.exe` and a valid current
user Code Signing certificate are available. If no signing certificate exists,
the package remains unsigned and the build reports `UNAVAILABLE`.

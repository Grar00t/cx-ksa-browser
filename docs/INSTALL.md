# CX Build Installation and Portable Mode

## Requirements

CX currently targets Windows x64 and uses the Microsoft Edge WebView2 Runtime. The runtime must be available for the browser renderer to initialize.

## Installed mode

The Inno Setup installer is per-user by default. The default destination is:

    %LOCALAPPDATA%\Programs\CX Build

The user can select another destination. The installer creates Start Menu shortcuts for CX Build and uninstall.

Adding the CX directory to the current user's PATH is optional and unchecked by default.

The installer does not define an advertising bundle, third-party offer, service, scheduled task, or application auto-updater.

## Uninstall behavior

Verified P09 uninstall behavior removes the program directory, Start Menu shortcuts, installer registration, and an optional PATH entry if the installer added it.

User application data under APPDATA\CX Build is intentionally retained so uninstall is not an implicit destructive data wipe.

## Portable mode

Use the generated CX-Build-Portable folder or ZIP. Keep cx.exe and run_portable.bat together and launch through run_portable.bat.

The launcher redirects APPDATA and LOCALAPPDATA into a data folder beside cx.exe. This keeps CX-owned SQLite state, JSON configuration, MCP allowlist, and logs with the portable folder.

Portable execution was verified from a relocated writable folder. The P09 machine had no removable DriveType 2 volume attached, so physical USB execution was not claimed as verified.

## Build packages

Install Inno Setup 6, build Release, then run:

    .\installer\build_installer.ps1

Outputs are written to dist.

## Signing

The package script attempts Authenticode signing when signtool.exe and a valid current-user code-signing certificate are available. If no certificate is available, it reports signing as unavailable and leaves the package unsigned.

## Verification

P09 includes installer\verify_package.ps1 for a real install, launch, close, uninstall, PATH, Start Menu, registry, unexpected-file, background-process, and portable-data verification cycle.

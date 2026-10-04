# Project State

## Current Status
Phase: P09 implemented and verified on branch `prompt-P09`.
CX now has a real Windows installer build, clean uninstall flow, optional
per-user PATH integration, Start Menu shortcuts, and a relocatable portable
package.

## Packaging
Tool: Inno Setup 6.7.3.

Installer source:
- `installer/cx-installer.iss`
- `installer/build_installer.ps1`
- `installer/verify_package.ps1`

Portable launcher:
- `portable/run_portable.bat`

Install documentation:
- `docs/INSTALL.md`

Generated `dist/` artifacts are intentionally ignored by Git.

## Installed Mode
Default install destination:
`%LOCALAPPDATA%\Programs\CX Build`

The destination is user-selectable. Verification installed successfully into a
custom temporary directory containing spaces.

The installer creates:
- CX Build Start Menu shortcut.
- Uninstall CX Build Start Menu shortcut.
- standard Inno uninstall registration.
- optional current-user PATH entry only when the unchecked `addtopath` task is selected.

The PATH entry is removed on uninstall. Verification also compared PATH token
sets before and after uninstall and observed no non-CX entry changes.

## Package Contents
The package contains CX-owned files only:
- `cx.exe`
- LICENSE
- NOTICE
- PRIVACY.md
- SECURITY.md
- `docs/INSTALL.md`

Inno adds its own uninstaller files. Verification observed zero unexpected
installed files before launching the application.

No advertising bundle, third-party offer, updater, scheduled task, or service
is defined by P09. There is no application auto-update mechanism in the
installer by design.

## Portable Mode
The generated portable folder/ZIP contains `cx.exe`, the project documents,
and `run_portable.bat`.

The launcher redirects `APPDATA` and `LOCALAPPDATA` to a `data` directory
beside the executable. Verification launched CX from a relocated temporary
folder and observed the SQLite database under:
`data\Roaming\CX Build\data.db`.

No physical removable drive was present during verification. Windows reported
C: and D: as fixed disks (DriveType 3), so the physical-USB criterion remains
NOT_VERIFIED rather than being inferred from the relocated-folder test.

## Process Exit Verification
Installed and portable CX were both launched and closed using WM_CLOSE.
After close, verification found:
- 0 matching `cx.exe` processes.
- 0 CX-owned `msedgewebview2.exe` processes tied to the tested package path.

## Install/Uninstall Verification
A real silent install/uninstall cycle verified:
- custom install location works.
- Start Menu shortcut exists after install.
- optional PATH task adds the custom install directory.
- one CX uninstall registration exists while installed.
- uninstall completes successfully.
- install directory is gone after uninstall.
- Start Menu shortcut is gone after uninstall.
- CX uninstall registration count is zero after uninstall.
- temporary CX PATH entry is gone after uninstall.
- final machine test state contains no CX P09 temporary install residue.

## Artifacts
Installer:
- file: `CX-Build-Setup-0.9.0.exe`
- size: 2,661,132 bytes (~2.54 MiB)
- SHA256: `A5954B4802BD434C0831B928FF4755C0E027C7872E76D84B30DDCBF12E4998AB`

Portable ZIP:
- file: `CX-Build-Portable-0.9.0.zip`
- size: 726,757 bytes
- SHA256: `1BECE37823C72A8F5808739CEEE073046AAFA272369410E4164BBBBD2FF3FE43`

## Code Signing
`signtool.exe` is installed and available.
No valid CurrentUser Code Signing certificate was present.
Result: `NotSigned` / signing acceptance is unavailable, not passed.

The build script will attempt SHA-256 Authenticode signing automatically when a
valid user Code Signing certificate is available.

## Regression Verification On 2026-10-04
- PASS: CTest 1/1.
- PASS: direct GoogleTest 52/52.
- PASS: installer compile with Inno Setup 6.7.3.
- PASS: installer size is below 10 MiB.
- PASS: install + uninstall cleanup verification.
- PASS: Start Menu shortcut.
- PASS: optional PATH add/remove.
- PASS: zero unexpected installed files.
- PASS: zero CX-owned background processes after close.
- PASS: portable launch from relocated folder.
- PASS: portable CX data redirected beside the executable.
- NOT_VERIFIED: physical USB media; no removable drive was connected.
- UNAVAILABLE: code signing certificate.

## Acceptance Criteria
- [x] Installer <10MB.
- [x] Install + uninstall leaves no tested CX registry/PATH/Start Menu junk.
- [~] Portable mode works from a relocated folder; physical USB media not available to verify.
- [x] No CX-owned background processes remain after close.
- [ ] Code signing: signtool available, signing certificate unavailable.

## Receipt
`docs/P09_RECEIPT.json`

## Next Prompt
P10 (not started).

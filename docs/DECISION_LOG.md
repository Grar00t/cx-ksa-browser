# Decision Log

## 2026-10-01 - Bootstrap
- Project name: CX Build - Made with love in KSA.
- Repository: `cx-ksa-browser`.
- Architecture target: Windows hybrid browser shell.
- Renderer: Microsoft WebView2.
- Native language: C++20.
- License: MIT.
- Privacy posture: local-first with no telemetry or cloud sync by default.
- Initial GitHub visibility: private unless explicitly changed by the user.


## 2026-10-04 - P08 Settings & Persistence
- Canonical P08 config path: `%APPDATA%\CX Build\config.json`.
- Configuration is versioned and split into General, Appearance, Privacy, and Advanced categories.
- Settings import/export is local-only; UNC paths and remote drives are rejected.
- Settings synchronization has no transport or API in P08 and is unsupported by design.
- Database backup/restore uses SQLite backup snapshots, staged verification, and rollback safety.
- Restore accepts only integrity-checked databases with the expected CX schema and migration version >= 3.


## 2026-10-04 - P09 Packaging & Installer
- Packaging tool: Inno Setup 6.7.3.
- Default installation is per-user under `%LOCALAPPDATA%\Programs\CX Build`, with a user-selectable destination.
- Start Menu shortcuts are installed; PATH integration is optional and unchecked by default.
- P09 defines no updater, service, scheduled task, advertising bundle, or third-party offer.
- Portable mode redirects APPDATA and LOCALAPPDATA into a data directory beside `cx.exe`.
- Generated `dist/` packages are build artifacts and are not committed.
- Code signing is conditional: signtool is available, but no valid CurrentUser code-signing certificate was present on 2026-10-04.
- Physical USB execution remains unverified because no removable DriveType 2 volume was connected; relocated-folder portable execution passed.

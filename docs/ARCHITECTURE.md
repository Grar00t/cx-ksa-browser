# CX Build Architecture

## Overview

CX Build is a Windows-native C++20 application with a Win32 shell and Microsoft WebView2 renderer. Its design separates browser state, local storage, permissions, MCP process control, configuration, UI, and packaging into explicit components.

## Component map

Application shell:
- src/main.cpp creates the process-level services and application window.
- src/app_window.cpp owns the main Win32 browser window and WebView2 controller.

Browser domain:
- TabManager owns logical tabs and immediate persistence.
- NavigationController owns per-tab back and forward state and address normalization.
- HistoryService and BookmarkService isolate local history and bookmark operations.

Storage:
- Database wraps SQLite and schema migrations.
- SQLite uses WAL, synchronous FULL, foreign keys, prepared statements, and transaction helpers.
- Schema version 3 includes settings, tabs, history, permissions, bookmarks, and migration metadata.

Configuration:
- ConfigManager owns versioned JSON configuration at APPDATA\CX Build\config.json.
- Settings schema covers General, Appearance, Privacy, and Advanced categories.
- BackupRestore uses SQLite online backup plus integrity and CX-schema checks.

Agent:
- PermissionManager is deny-by-default and stores each grant in SQLite.
- AgentCore has an explicit stopped or running lifecycle.
- LocalLogger writes local metadata logs.

MCP:
- AllowlistManager owns config\mcp_allowlist.json.
- Only absolute executable paths ending in .exe are accepted.
- McpClient starts an allowlisted process directly with CreateProcessW and communicates over stdio.
- RateLimiter enforces the request-rate boundary.
- P10 tightened the JSON parser so trailing commas fail closed.

UI:
- SettingsWindow provides Privacy & Security, Agent Permissions, MCP Allowlist, and Data & Storage pages.
- `ui/najdi_theme` owns the CX-native warm-charcoal/sand visual tokens, Unicode font application, compact owner-drawn controls, and themed tab painting.
- PrivacyDashboard reports current local state.
- AllowlistDialog manages MCP entries.
- History and bookmark dialogs remain local.
- The design language is documented in `docs/UI_DESIGN.md`; Arabic rendering is supported by Unicode controls, while full RTL localization is not yet claimed.

Packaging:
- Inno Setup produces a per-user installer.
- run_portable.bat redirects APPDATA and LOCALAPPDATA into the portable folder.
- No application auto-update mechanism is present.

## Trust boundaries

First-party core: C++ code in src except the renderer boundary. P10 automated security tests verify that a local-only core scenario does not create new process-owned established TCP or UDP endpoints.

Renderer: WebView2 is an external Microsoft runtime. It receives user-requested web destinations and can also have runtime-level Microsoft or Windows networking. This boundary prevents CX from making a truthful whole-process-tree zero-network claim.

Web content: remote websites are untrusted. Unsafe explicit schemes are rejected by the navigation layer, but normal browser content executes inside WebView2 and remains subject to renderer security behavior.

Local MCP executables: explicitly user-allowlisted local programs are separate processes and must be treated as trusted only to the extent the user trusts the selected executable.

## Data flow

User action -> Win32 UI -> browser, agent, MCP, config, or storage service -> local persistence or explicit renderer/process action.

No first-party cloud database, account service, telemetry collector, sync service, or update service is part of the architecture.

## Failure posture

Configuration corruption falls back to defaults while preserving a corrupt copy when possible. MCP allowlist parse errors fail closed. Missing permissions deny. Invalid local MCP paths deny. Database restore stages and verifies a replacement and can roll back to the previous database.

## Tests

The single GoogleTest executable contains unit, integration, Win32 UI smoke, security, and performance tests. Coverage uses OpenCppCoverage on src and intentionally excludes only main.cpp and app_window.cpp from the line-coverage gate because those two files are the executable entrypoint and WebView2 host shell rather than linked test-library code.

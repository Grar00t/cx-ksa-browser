# Project State

## Current Status
Phase: P07 implemented on branch `prompt-P07`.
CX now has a modeless Settings & Privacy window with four visible tabs,
privacy-first defaults, direct SQLite persistence, and a live privacy dashboard.

## Settings Window
Open from `Browser > Settings & Privacy...`.

Visible tabs:
- Privacy & Security
- Agent Permissions
- MCP Allowlist
- Data & Storage

Privacy settings are explicitly enumerated in code and rendered from the same
schema:
- `privacy.save_history` default OFF
- `privacy.restore_session` default OFF
- `privacy.clear_history_on_exit` default ON

Sync is displayed as disabled by design and CX application telemetry upload is
displayed as none; neither is hidden behind another setting.

## Immediate Persistence And Enforcement
Privacy toggles write to the existing SQLite `settings` table immediately.
Agent capability checkboxes write to the SQLite `permissions` table
immediately. Revoking `agent.run` stops the agent and MCP; revoking
`mcp.connect` stops MCP immediately.

History recording reads `privacy.save_history` at each visit.
Session restore reads `privacy.restore_session` at launch.
Exit cleanup reads `privacy.clear_history_on_exit` after the browser loop.

The P05 MCP allowlist remains persisted in its mandated local JSON file rather
than being duplicated into SQLite.

## Privacy Dashboard
The dashboard reports actual current state:
- enabled agent capability IDs
- active MCP server or none
- number of MCP allowlist entries
- total local data size under `%APPDATA%\CX Build`

Directory-size measurement runs on a worker thread and posts its result back to
the settings window, so filesystem traversal does not block the UI thread.

## Verification On 2026-10-01
- PASS: Release build produced `build/Release/cx.exe`.
- PASS: CTest passed.
- PASS: direct GoogleTest run: 41 tests, 41 passed.
- PASS: missing history setting records no visits; explicit enable records them.
- PASS: missing session-restore setting discards prior tabs on next launch.
- PASS: dashboard summary reflects a granted permission and MCP allowlist count.
- PASS: asynchronous local-size refresh returned to the caller under 100 ms.
- PASS: actual Settings window opened in 64.7208 ms in an isolated AppData.
- PASS: actual Settings TabControl reported exactly four pages.
- PASS: actual privacy checkbox was found and clicked.
- PASS: isolated SQLite immediately contained `privacy.save_history=1`.
- PASS: application remained alive after the UI interaction.

## Next Prompt
P08.

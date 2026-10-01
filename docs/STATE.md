# Project State

## Current Status
Phase: P04 implemented on branch `prompt-P04`.
CX now has a deny-by-default local agent foundation with persisted consent,
a fixed action allowlist, local-only agent logging, and UI revocation.

## Agent Lifecycle
The agent is stopped when CX starts.
`Agent > Start Agent...` requires explicit `agent.run` consent.
A denied start leaves the lifecycle in `Stopped` state without crashing.
`Agent > Stop Agent` stops a running agent.

## Permissions
SQLite migration v2 adds the `permissions` table.
Missing permission rows are denied.
A granted capability is reusable until the user revokes it.
A denial is stored as `granted=0`.
If consent cannot be persisted, the capability remains denied.

Capabilities:
- `agent.run`
- `browser.read_page`
- `browser.navigate`
- `tabs.manage`
- `clipboard.write`
- `native_messaging.connect`
- `mcp.connect`

Every allowlisted action maps to exactly one capability.
Unknown actions are rejected before any permission prompt.
`Agent > Revoke All Permissions...` stops the agent and writes all
persisted grants back to denied after an explicit confirmation dialog.

## Logging
Agent logs are append-only local files at:
`%APPDATA%\CX Build\logs\agent.log`

The agent/logger code contains no HTTP, socket, telemetry, analytics,
or upload path. Log writes use the local filesystem only.

## Build And Test
`cmake -B build`
`cmake --build build`
`ctest --output-on-failure`

Or:
`powershell -ExecutionPolicy Bypass -File scripts/build.ps1`

## Verified On 2026-10-01
- PASS: Release build produced `build/Release/cx.exe`.
- PASS: `ctest --output-on-failure` passed.
- PASS: GoogleTest direct run: 17 tests, 17 passed.
- PASS: deny-by-default prevents agent start without consent.
- PASS: separate allowlisted actions require separate consent.
- PASS: unknown action is denied by the allowlist.
- PASS: rejected permission leaves the process and agent stable.
- PASS: migration version observed: 2.

- PASS: actual `CX Agent Permission` dialog observed from the running app.
- PASS: actual denial flow left `cx.exe` alive.
- PASS: actual `Revoke CX Agent Permissions` UI observed.
- PASS: after revoke, SQLite reported no granted permissions.
- PASS: local log observed at the required AppData path.
- PASS: 20 repeated agent-test runs showed 0 TCP connections and
  0 UDP endpoints for the test process.

## Known Limitation
P04 agent permissions and logs are local-only. The full `cx.exe` still
inherits the P02 Evergreen WebView2 Runtime startup network behavior.
That runtime traffic is separate from the P04 agent/logging subsystem.

## Next Prompt
P05.

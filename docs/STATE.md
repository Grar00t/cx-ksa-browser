# Project State

## Current Status
Phase: P05 implemented on branch `prompt-P05`.
CX now has a deny-by-default local MCP stdio integration on top of the
P04 agent permission model. MCP servers are local executables selected by
the user and persisted in an explicit JSON allowlist.

## MCP Transport
`McpClient` launches only an allowlisted absolute `.exe` path with
`CreateProcessW`; there is no shell or PATH lookup.
Transport is stdin/stdout using newline-delimited JSON frames.
Messages are capped at 4 MiB and payload bodies are not written to logs.
Removing an active server from the allowlist prevents further requests.

## Allowlist And User Action
Runtime allowlist path:
`%APPDATA%\CX Build\config\mcp_allowlist.json`

Repository template:
`config/mcp_allowlist.json`

A missing config is created empty. Invalid JSON loads fail-closed.
Unknown server IDs are rejected before MCP consent is requested.
The UI is available at `MCP > Allowed Servers...` and lets the user
add a local executable, remove it, connect, and disconnect.

A server process is not spawned merely because it is allowlisted.
Connection requires all of:
1. an explicitly selected allowlisted server,
2. a running agent,
3. granted `mcp.connect` permission,
4. an explicit Connect action from the MCP allowlist window.

## Rate Limiting And Logging
Each server has an independent fixed-window limiter of 10 requests per
second. The limiter resets when a server starts or stops.

MCP logs are append-only local files at:
`%APPDATA%\CX Build\logs\mcp.log`

Every request attempt logs server ID, byte count, and outcome. Request
payloads are not logged.

## Build And Test
`cmake -B build`
`cmake --build build --config Release`
`ctest --test-dir build -C Release --output-on-failure`
`build\tests-bin\storage_tests.exe`

## Verified On 2026-10-01
- PASS: Release build produced `build/Release/cx.exe`.
- PASS: `ctest` passed.
- PASS: direct GoogleTest run: 27 tests, 27 passed.
- PASS: server outside the allowlist is rejected before MCP consent.
- PASS: allowlisted server does not spawn before explicit `Start`.
- PASS: stdio request/response round-trip works against a real child process.
- PASS: the 11th request inside one second is rejected for that server.
- PASS: rate-limit buckets are independent per server.
- PASS: every request attempt is recorded in the local MCP log.
- PASS: request JSON bodies are absent from the log.
- PASS: removing a running server blocks another request and stops it.
- PASS: malformed allowlist JSON fails closed.
- PASS: UTF-8 BOM allowlist input loads.

## Known Limitation
The MCP transport intentionally implements stdio only. Remote MCP
transports are out of P05 scope. The WebView2 runtime behavior documented
in P04 remains separate from the local MCP subsystem.

## Next Prompt
P06.

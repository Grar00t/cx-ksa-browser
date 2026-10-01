# Project State

## Current Status
Phase: P03 implemented on branch `prompt-P03`.
The Windows shell now opens a local SQLite database before starting the UI.

## Storage
Default path: `%APPDATA%\CX Build\data.db`.
SQLite 3.53.4 amalgamation is vendored under `third_party/sqlite`.
GoogleTest 1.18.0 is vendored under `third_party/googletest`.

Schema migration v1 creates:
- `settings`: local key/value settings.
- `tabs`: ordered tab/session state.
- `history`: local browsing history.
- `schema_migrations`: applied migration versions.

## Transaction Safety
SQLite is configured with WAL mode, `synchronous=FULL`, foreign keys enabled,
a 5-second busy timeout, prepared statements for CRUD, transactional migrations,
and explicit `BEGIN IMMEDIATE / COMMIT / ROLLBACK`.
`Transaction` rolls back automatically if it leaves scope without commit.

## Build And Test
`cmake -B build`
`cmake --build build`
`ctest --output-on-failure`

Or:
`powershell -ExecutionPolicy Bypass -File scripts/build.ps1`

## Verified On 2026-10-01
- PASS: CMake configure completed.
- PASS: Release and Debug storage targets compile.
- PASS: `build/Release/cx.exe` produced.
- PASS: `ctest --output-on-failure` passes without requiring `-C`.
- PASS: GoogleTest direct run: 8 tests, 8 passed.
- PASS: database exists at `C:\Users\A\AppData\Roaming\CX Build\data.db`.
- PASS: tables observed: `history`, `schema_migrations`, `settings`, `tabs`.
- PASS: migration version observed: 1.
- PASS: journal mode observed: WAL; synchronous setting observed: FULL (2).
- PASS: 20 repeated storage-test runs showed 0 TCP connections and 0 UDP endpoints.

## Known Limitation
The P03 storage layer itself performs no network I/O. The full `cx.exe` still
inherits the P02 Evergreen WebView2 Runtime behavior previously observed to make
Microsoft HTTPS connections during WebView startup. P03 does not claim that
the full browser process is network-silent.

## Next Prompt
P04.

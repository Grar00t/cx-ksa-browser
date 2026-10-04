# Project State

## Current Status
Phase: P08 implemented and verified on branch `prompt-P08`.
CX now has a versioned local JSON configuration subsystem plus local-only
settings import/export and SQLite database backup/restore.

## Configuration
Default path:
`%APPDATA%\CX Build\config.json`

Version 1 categories:
- General: startup behavior, default search provider.
- Appearance: theme, font-size percentage.
- Privacy: cookie policy, cache policy.
- Advanced: developer tools, experimental features.

Defaults are privacy-first:
- blank startup
- search disabled
- block-all cookies
- memory-only cache
- developer tools off
- experimental features off

Writes use a temporary file plus `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING |
MOVEFILE_WRITE_THROUGH)`. Import is parsed and validated before replacing the
current settings. Invalid imports leave the active settings unchanged.

A corrupt config is copied to `config.json.corrupt` when possible and the
active config falls back to validated defaults.

## Local-Only Boundary
Config import/export and database backup/restore accept absolute local
filesystem paths only. UNC paths and remote drives are rejected. P08 contains
no sync client or network transport.

Settings synchronization is therefore unsupported by design rather than merely
disabled by a UI toggle.

## Database Backup And Restore
Backup uses SQLite's online backup API to create a staged local snapshot and
runs `PRAGMA integrity_check` before promotion.

Restore:
1. validates the backup before closing the active database,
2. stages and re-validates a copy,
3. preserves the current database as `.pre-restore`,
4. swaps the staged database into place,
5. reopens and verifies it,
6. rolls back to the preserved database if reopen/verification fails.

Verification also requires the CX schema tables and migration version >= 3, so
an unrelated but internally valid SQLite database is rejected.

## Build And Test
`cmake --build build --config Release`
`ctest --test-dir build -C Release --output-on-failure`
`build\tests-bin\storage_tests.exe`

## Verified On 2026-10-04
- PASS: Release build produced `build/Release/cx.exe`.
- PASS: CTest: 1/1 test target passed.
- PASS: direct GoogleTest run: 52 tests from 13 suites, 52 passed.
- PASS: P08 config tests: 11/11 passed.
- PASS: exact default config path is under `%APPDATA%\CX Build\config.json`.
- PASS: every P08 setting round-trips through JSON persistence.
- PASS: corrupt config falls back to defaults and a subsequent load succeeds.
- PASS: font-size validation rejects values outside 75..200.
- PASS: import/export is local-only and validates before applying.
- PASS: invalid import leaves current settings unchanged.
- PASS: database backup/restore restores settings, history, and bookmarks from
  the snapshot after later mutations.
- PASS: corrupt backup is rejected without changing the open database.
- PASS: valid non-CX SQLite is rejected without changing the open database.
- PASS: UNC backup/restore paths are rejected by design.
- PASS: `git diff --check` reported no whitespace errors before finalization.

## Acceptance Criteria
- [x] Every setting is saved and restored.
- [x] Corrupt config gracefully falls back to defaults.
- [x] Backup/restore works without data loss in the tested snapshot and failure
  cases.
- [x] Settings sync is impossible through P08 APIs by design.
- [x] Unit tests cover config validation and persistence.

## Known Build Warning
MSBuild emits MSB8029 about an intermediate/output directory being considered
under a Temporary directory. It did not fail the Release build or tests, but it
remains a build-environment warning rather than a P08 acceptance failure.

## Next Prompt
P09 (not started).

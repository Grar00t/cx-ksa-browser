# Contributing to CX Build

CX Build accepts changes that preserve its local-first and evidence-driven design.

## Before changing code

Work from a dedicated branch. Inspect the current state, tests, and relevant design documents first. Do not silently replace the renderer, storage format, permission model, MCP transport, project identity, or privacy defaults.

## Engineering requirements

Changes should:
- keep C++20 and the existing CMake build unless an explicit architecture decision changes them
- preserve deny-by-default agent and MCP behavior
- avoid adding telemetry, cloud synchronization, hidden network clients, advertising bundles, or auto-update behavior
- keep MCP launch paths explicit and local
- use prepared SQLite operations and transactional changes where persistence is involved
- fail closed when parsing security-sensitive configuration

## Tests

Every behavior change should include a regression test when practical.

Before submitting:

    cmake -S . -B build
    cmake --build build --config Release
    ctest --test-dir build -C Release --output-on-failure
    .\build\tests-bin\storage_tests.exe

For changes included in measured source:

    cmake --build build --config Debug
    pwsh -NoProfile -ExecutionPolicy Bypass -File .\scripts\run_coverage.ps1 -Minimum 80

Do not lower the coverage threshold or expand exclusions to make a failing patch appear green without an explicit reviewed reason.

## Security changes

Permission, MCP, networking, storage, backup and restore, parsing, renderer, and installer changes require particular care. Document any new trust boundary in SECURITY.md or docs/ARCHITECTURE.md.

Security-sensitive parsers should reject ambiguous or malformed input rather than normalize it silently.

## Evidence

Commit messages should describe the implemented change. State files and receipts must distinguish observed results from intended behavior. Performance or security claims need a reproducible test, hash, receipt, or CI result.

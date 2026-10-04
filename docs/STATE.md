# Project State

## Current Status
Phase: P10 implemented locally on branch `prompt-P10`; GitHub Actions verification is pending the first push.

CX now includes final user/developer documentation, real screenshots, expanded unit/integration/UI/security/performance tests, an enforced line-coverage gate, Windows packaging, portable packaging, and a GitHub Actions workflow.

## Documentation
User-facing:
- `README.md` with real CX screenshots.
- `docs/INSTALL.md`.
- `docs/USER_GUIDE.md`.
- `docs/PRIVACY_POLICY.md`.

Developer-facing:
- `docs/ARCHITECTURE.md`.
- `docs/BUILD.md`.
- `CONTRIBUTING.md`.
- `SECURITY.md`.

Screenshots:
- `docs/screenshots/cx-main.png`.
- `docs/screenshots/settings-privacy.png`.

## Final Test Suite
The GoogleTest executable now contains unit, integration, Win32 UI smoke,
security, parser/fail-closed, persistence, crash-recovery, and performance tests.

Local Release verification on 2026-10-04:
- PASS: Release build produced `build/Release/cx.exe`.
- PASS: CTest 1/1.
- PASS: GoogleTest 71/71 from 18 suites.

Security verification:
- PASS: local first-party core test observed no increase in process-owned established TCP or UDP endpoints.
- PASS: remote/unsafe inputs fail closed in tested config/MCP/navigation paths.
- PASS: static source search found no WinHTTP, WinINet, URLDownloadToFile, WSAStartup, socket(), or connect() client calls in `src/`.
- FIXED: MCP JSON parser previously accepted trailing commas; P10 now rejects them and includes regression coverage.

Network boundary:
- The local-core result is not a claim that the complete browser process tree is network silent.
- User-requested browsing is network activity.
- Microsoft WebView2 remains an external runtime and may contact websites or Microsoft/Windows infrastructure.

## Coverage
Tool: OpenCppCoverage 0.9.9.0.

Gate:
`pwsh -NoProfile -ExecutionPolicy Bypass -File .\scripts\run_coverage.ps1 -Minimum 80`

Measured scope:
- `src/`
- excluding only `src/main.cpp` and `src/app_window.cpp`, the executable entrypoint and WebView2 host shell.

Final successful local gate:
- PASS: 81.26% line coverage.
- covered: 2359.
- valid: 2903.
- threshold: 80%.

The coverage script performs a clean Debug rebuild of `storage_tests` before measurement and rejects empty/invalid 0/0 reports.

## Performance
Final successful coverage-gate run:
- New-tab P95: 2.7493 ms, threshold <100 ms.
- 100 config save/load round trips: 199.718 ms, threshold <1000 ms.

Release regression run also passed both performance tests.

## Packaging Regression
Final local P10 package rebuild:
- installer: `CX-Build-Setup-0.9.0.exe`
- size: 2,662,218 bytes (<10 MiB)
- SHA256: `16629AFCD3814088BE35F434A16EAD13EC433EC7F000BC028D2499D2B75D9E3B`
- signing: unavailable / NotSigned because no valid code-signing certificate is present.

Portable ZIP:
- `CX-Build-Portable-0.9.0.zip`
- size: 749,693 bytes
- SHA256: `7F9F1E822AD93A3E473497BA2D587DB638D19DD63F7424CA162A001454273245`

Package verifier:
- PASS: install/uninstall cleanup.
- PASS: Start Menu shortcut.
- PASS: optional PATH add/remove.
- PASS: zero unexpected installed files.
- PASS: zero CX-owned processes after close.
- PASS: portable launch and local data redirection.
- NOT_VERIFIED: physical USB media; no removable drive was connected.
- UNAVAILABLE: Authenticode signing certificate.

## CI
`.github/workflows/ci.yml` defines Windows jobs for:
1. Release build + CTest + direct GoogleTest.
2. clean Debug coverage gate at >=80%.
3. installer/portable package build after build and coverage succeed.
4. upload of Cobertura coverage and release-candidate package artifacts.

CI status: PENDING_FIRST_PUSH.

## Acceptance Criteria
- [x] All requested documentation exists and is updated.
- [x] Measured line coverage >80%: 81.26%.
- [ ] GitHub Actions pipeline green: pending first push.
- [x] No critical first-party issue is known from the completed automated/static P10 checks; this is not a penetration-test claim.
- [ ] Release candidate ready: local RC passes; final status waits for green GitHub CI.

## Known Boundaries
- WebView2 whole-process-tree zero-network is not claimed.
- Physical USB execution remains unverified from P09.
- Code signing remains unavailable without a certificate.
- Local Visual Studio emits MSB8029; it has not failed build/tests.

## Receipt
`docs/P10_RECEIPT.json`

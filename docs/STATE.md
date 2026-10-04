# Project State

## Current Status
Phase: P11 design-system work is verified locally on `prompt-P11-design-system`, based on the prior Najdi UI branch without merging to `main`.

CX now includes final user/developer documentation, real screenshots, expanded unit/integration/UI/security/performance tests, an enforced line-coverage gate, Windows packaging, portable packaging, and a GitHub Actions workflow.

## Documentation
User-facing:
- `README.md` with real CX screenshots.
- `docs/INSTALL.md`.
- `docs/USER_GUIDE.md`.
- `docs/PRIVACY_POLICY.md`.

Developer-facing:
- `docs/ARCHITECTURE.md`.
- `docs/UI_DESIGN.md`.
- `docs/BUILD.md`.
- `CONTRIBUTING.md`.
- `SECURITY.md`.

Screenshots:
- `docs/screenshots/cx-main.png`.
- `docs/screenshots/settings-privacy.png`.

## P11 Design System
Verified on `prompt-P11-design-system`:
- central tokens: `src/ui/design_tokens.h`
- token categories: color, spacing, typography, radius, border, focus, density, window/settings layout
- native renderer: `src/ui/najdi_theme.{h,cpp}`
- 3 px control radius / 4 px maximum radius; 1 px standard borders
- sand focus treatment and olive primary-action treatment
- browser chrome and Settings metrics consume shared density/spacing tokens instead of duplicated hardcoded sizes where practical
- `theme::ApplyLayoutDirection` provides an RTL/LTR Win32 mirroring hook; `DesignSystemTest.RtlDirectionCanBeAppliedAndRemoved` passed
- WebView2 integration was not changed
- visual inspection completed on the locally built `build/Release/cx.exe`
- added-line diff scan: no new URLs, derived-browser asset references, telemetry, analytics, crash-upload, remote-update, or cloud-sync references
- Cppcheck completed on the changed first-party C++ sources; it reported one pre-existing performance warning in `AppWindow::OpenLibraryUrl(std::string url)`

P11 local verification on 2026-10-04:
- PASS: Release configure/build, exit code 0
- PASS: CTest 1/1
- PASS: GoogleTest 73/73 from 19 suites
- PASS: line coverage 80.92% (2524/3119), threshold 80%
- New-tab P95 during coverage run: 2.6786 ms
- 100 config save/load round trips during coverage run: 250.012 ms

## Najdi UI Refinement
Verified on `task/najdi-ui-20261004`:
- shared native palette in `src/ui/najdi_theme.{h,cpp}`
- warm-charcoal main/settings surfaces with sand active/focus accents
- compact owner-drawn browser/settings push buttons and custom tab painting
- dark native title bars where supported by Windows DWM
- Unicode Segoe UI font application; full Arabic RTL localization is not claimed
- no new telemetry, sync, updater, cloud, or third-party UI asset dependency
- visual inspection completed from the locally built `cx.exe` for the main window and Settings & Privacy window
- repository search result for `allam`: no match; offline/local ALLaM retrieval remains a product direction, not a shipped repository capability in this revision

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

Latest successful local gate on `prompt-P11-design-system`:
- PASS: 80.92% line coverage.
- covered: 2524.
- valid: 3119.
- threshold: 80%.

P10 baseline before the UI refinement was 81.26% (2359/2903).

The coverage script performs a clean Debug rebuild of `storage_tests` before measurement and rejects empty/invalid 0/0 reports.

## Performance
Latest successful coverage-gate run on the P11 design-system branch:
- New-tab P95: 2.6786 ms, threshold <100 ms.
- 100 config save/load round trips: 250.012 ms, threshold <1000 ms.

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

CI status: PASS on GitHub Actions run `37186418199` for commit `13d6c1f23ff4c692082a4138a0a01c35e1d6981b`. All three jobs passed: Windows build and tests, coverage gate, and installer/portable artifacts.

## Acceptance Criteria
- [x] All requested documentation exists and is updated.
- [x] Measured line coverage >80% on the current P11 branch: 80.92%.
- [x] GitHub Actions pipeline green: run `37186418199` passed all three jobs.
- [x] No critical first-party issue is known from the completed automated/static P10 checks; this is not a penetration-test claim.
- [x] Release candidate ready: local RC verification passed and GitHub CI run `37186418199` passed.

## Known Boundaries
- WebView2 whole-process-tree zero-network is not claimed.
- Physical USB execution remains unverified from P09.
- Code signing remains unavailable without a certificate.
- Offline/local ALLaM retrieval is not implemented in this repository revision.
- Local Visual Studio emits MSB8029; it has not failed build/tests.

## Receipt
`docs/P10_RECEIPT.json`

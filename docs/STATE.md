# Project State

## Current Status
Phase: P02 implemented and built on Windows. CMake + native C++20 Win32 + WebView2 skeleton are present on branch `prompt-P02`.

## Build Commands
`cmake -B build`
`cmake --build build --config Release`
`powershell -ExecutionPolicy Bypass -File scripts/build.ps1`

## P02 Verification
- PASS: CMake configure completed with Visual Studio 18 2026 / MSVC 19.51.
- PASS: Release build produced `build/Release/cx.exe`.
- PASS: runtime window measured exactly 1024x768, title `CX Build - P02`.
- PASS: a WebView2 child process started and the host code navigates only to `about:blank`; HTTP/HTTPS navigation and new-window requests are blocked by the host.
- FAIL: zero-network acceptance is not satisfied by the Evergreen WebView2 Runtime on this machine. Its child process established HTTPS connections to Microsoft-owned runtime infrastructure during startup despite background-networking/component-update/sync flags and a sink proxy.

## Dependency
Pinned NuGet SDK: `Microsoft.Web.WebView2 1.0.4258.31`. First configure downloads the package into `build/packages`; subsequent configures reuse it.

## Privacy Notes
CX host code contains no telemetry, analytics, crash upload, cloud sync, or application network client. WebView2 itself follows Windows diagnostic-data behavior, which the host application cannot fully disable.

## Blocker
Strict acceptance of "no telemetry or network connection" conflicts with using the Evergreen WebView2 Runtime under the current Windows diagnostic/runtime behavior. Meeting that requirement requires either an OS/runtime policy that blocks WebView2 diagnostics or changing the rendering-engine requirement.

## Next Prompt
P03 after deciding whether WebView2 runtime diagnostic traffic is acceptable or must be eliminated by architecture/policy.

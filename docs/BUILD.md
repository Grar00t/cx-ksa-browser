# CX Build Build and Test Guide

## Requirements

Supported build host: Windows x64.

Required:
- Visual Studio C++ toolchain with Windows SDK
- CMake 3.24 or newer
- Git

The project targets C++20. The pinned WebView2 SDK is Microsoft.Web.WebView2 1.0.4258.31. The build downloads the pinned NuGet package when it is not already present under the build tree.

Vendored test and storage dependencies include SQLite and GoogleTest.

Optional tooling:
- Inno Setup 6 for installer packaging
- OpenCppCoverage 0.9.9 or compatible for the coverage gate
- signtool.exe plus a valid code-signing certificate for Authenticode signing

## Configure

From the repository root:

    cmake -S . -B build

## Release build

    cmake --build build --config Release

Expected application:

    build\Release\cx.exe

## Debug build

    cmake --build build --config Debug

Debug PDBs are used by OpenCppCoverage.

## Tests

CTest:

    ctest --test-dir build -C Release --output-on-failure

Direct GoogleTest output:

    .\build\tests-bin\storage_tests.exe

P10 currently contains unit, integration, UI smoke, security, and performance cases in the same test executable.

## Coverage

Install OpenCppCoverage and run:

    pwsh -NoProfile -ExecutionPolicy Bypass -File .\scripts\run_coverage.ps1 -Minimum 80

The script exports coverage\coverage.xml in Cobertura format and fails below the requested threshold.

Coverage scope is src excluding only main.cpp and app_window.cpp. The excluded files are the executable entrypoint and WebView2 host shell; application services and Win32 settings and MCP UI remain inside the measurement.

Locally verified P10 result on 2026-10-04: 81.26 percent, 2359 of 2903 measured lines.

## Installer and portable package

Install Inno Setup 6 and run:

    pwsh -NoProfile -ExecutionPolicy Bypass -File .\installer\build_installer.ps1

Artifacts are written under dist and are ignored by Git.

## CI

.github\workflows\ci.yml defines Windows jobs for:
- Release build and test execution
- Debug coverage gate at 80 percent
- installer and portable-package generation after build and coverage succeed

The release-candidate packages are uploaded as GitHub Actions artifacts.

## Build warning

The current local Visual Studio environment emits MSB8029 concerning an intermediate or output directory being considered under a temporary directory. It has not caused a build or test failure, but it remains an environment warning and should not be silently described as resolved.

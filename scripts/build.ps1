param(
  [ValidateSet('Debug','Release')]
  [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Push-Location $Root
try {
  cmake -S . -B build
  if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE."
  }

  cmake --build build --config $Configuration
  if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE."
  }

  $Exe = Join-Path $Root "build\$Configuration\cx.exe"
  if (-not (Test-Path $Exe)) {
    throw "Expected executable not found: $Exe"
  }
  ctest --test-dir build -C $Configuration --output-on-failure
  if ($LASTEXITCODE -ne 0) {
    throw "CTest failed with exit code $LASTEXITCODE."
  }
  Write-Host "Built and tested: $Exe"
}
finally {
  Pop-Location
}

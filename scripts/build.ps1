param(
  [ValidateSet('Debug','Release')]
  [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Push-Location $Root
try {
  cmake -S . -B build
  cmake --build build --config $Configuration
  $Exe = Join-Path $Root "build\$Configuration\cx.exe"
  if (-not (Test-Path $Exe)) {
    throw "Expected executable not found: $Exe"
  }
  Write-Host "Built: $Exe"
}
finally {
  Pop-Location
}

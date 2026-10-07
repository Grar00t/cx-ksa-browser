param(
    [double]$Minimum = 80.0,
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repo $BuildDir
$tests = Join-Path $build "tests-bin\storage_tests.exe"
$outDir = Join-Path $repo "coverage"
$outFile = Join-Path $outDir "coverage.xml"

$toolCandidates = @(
    (Get-Command OpenCppCoverage.exe -ErrorAction SilentlyContinue).Source,
    "C:\Program Files\OpenCppCoverage\OpenCppCoverage.exe",
    "C:\Program Files (x86)\OpenCppCoverage\OpenCppCoverage.exe"
) | Where-Object { $_ -and (Test-Path $_) }

if (-not $toolCandidates) {
    throw "OpenCppCoverage.exe was not found."
}
$tool = @($toolCandidates)[0]

& cmake --build $build --config Debug --target storage_tests --clean-first
if ($LASTEXITCODE -ne 0) {
    throw "Clean Debug test build failed with exit code $LASTEXITCODE."
}
if (-not (Test-Path $tests)) {
    throw "Test executable not found after Debug build: $tests"
}

New-Item -ItemType Directory -Force $outDir | Out-Null
Remove-Item $outFile -Force -ErrorAction SilentlyContinue

$src = Join-Path $repo "src"
$appWindow = Join-Path $src "app_window.cpp"
$main = Join-Path $src "main.cpp"
$relativeTests = ".\$BuildDir\tests-bin\storage_tests.exe"
Push-Location $repo
try {
    & $tool --quiet --sources $src --excluded_sources $appWindow --excluded_sources $main --export_type "cobertura:$outFile" -- $relativeTests
}
finally {
    Pop-Location
}

if ($LASTEXITCODE -ne 0) {
    throw "Coverage test run failed with exit code $LASTEXITCODE."
}
if (-not (Test-Path $outFile)) {
    throw "Coverage report was not generated."
}

[xml]$xml = Get-Content $outFile
$rate = [double]$xml.coverage.'line-rate'
$percent = [math]::Round($rate * 100.0, 2)
$covered = [int]$xml.coverage.'lines-covered'
$valid = [int]$xml.coverage.'lines-valid'

if ($valid -le 0 -or $covered -lt 0 -or $covered -gt $valid) {
    throw "Coverage report is empty or invalid: covered=$covered valid=$valid."
}

Write-Host "P10_LINE_COVERAGE_PERCENT=$percent"
Write-Host "P10_LINES_COVERED=$covered"
Write-Host "P10_LINES_VALID=$valid"
Write-Host "Coverage scope: src/ excluding only main.cpp and app_window.cpp entrypoint/UI host."

if ($percent -lt $Minimum) {
    throw "Line coverage $percent% is below required $Minimum%."
}

[pscustomobject]@{
    Percent = $percent
    Covered = $covered
    Valid = $valid
    Minimum = $Minimum
    Report = $outFile
} | Format-List

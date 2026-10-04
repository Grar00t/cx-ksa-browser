param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo "build\$Configuration\cx.exe"
$dist = Join-Path $repo "dist"
$portableRoot = Join-Path $dist "CX-Build-Portable-0.9.0"
$portableZip = Join-Path $dist "CX-Build-Portable-0.9.0.zip"
$iss = Join-Path $PSScriptRoot "cx-installer.iss"

if (-not (Test-Path $exe)) {
    & cmake --build (Join-Path $repo "build") --config $Configuration
    if ($LASTEXITCODE -ne 0) {
        throw "CX build failed with exit code $LASTEXITCODE"
    }
}

$isccCandidates = @(
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe")
) | Where-Object { $_ -and (Test-Path $_) }

if (-not $isccCandidates) {
    throw "Inno Setup 6 ISCC.exe was not found."
}
$iscc = @($isccCandidates)[0]

$signTool = (Get-Command signtool.exe -ErrorAction SilentlyContinue).Source
$cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert -ErrorAction SilentlyContinue |
    Where-Object { $_.NotAfter -gt (Get-Date) } |
    Sort-Object NotAfter -Descending |
    Select-Object -First 1

$signing = "UNAVAILABLE"
if ($signTool -and $cert) {
    & $signTool sign /sha1 $cert.Thumbprint /fd SHA256 /td SHA256 /tr http://timestamp.digicert.com $exe
    if ($LASTEXITCODE -ne 0) {
        throw "cx.exe signing was available but failed."
    }
    $signing = "SIGNED:$($cert.Thumbprint)"
}

New-Item -ItemType Directory -Force -Path $dist | Out-Null
Remove-Item -Recurse -Force $portableRoot -ErrorAction SilentlyContinue
Remove-Item -Force $portableZip -ErrorAction SilentlyContinue
Get-ChildItem $dist -Filter "CX-Build-Setup-*.exe" -ErrorAction SilentlyContinue |
    Remove-Item -Force

New-Item -ItemType Directory -Force -Path $portableRoot | Out-Null
Copy-Item $exe (Join-Path $portableRoot "cx.exe")
Copy-Item (Join-Path $repo "portable\run_portable.bat") $portableRoot
Copy-Item (Join-Path $repo "LICENSE") $portableRoot
Copy-Item (Join-Path $repo "NOTICE") $portableRoot
Copy-Item (Join-Path $repo "PRIVACY.md") $portableRoot
Copy-Item (Join-Path $repo "SECURITY.md") $portableRoot
Copy-Item (Join-Path $repo "docs\INSTALL.md") $portableRoot

Compress-Archive -Path (Join-Path $portableRoot "*") -DestinationPath $portableZip -CompressionLevel Optimal

& $iscc $iss
if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup compilation failed with exit code $LASTEXITCODE"
}

$installer = Get-ChildItem $dist -Filter "CX-Build-Setup-*.exe" |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if (-not $installer) {
    throw "Installer output not found."
}

if ($signTool -and $cert) {
    & $signTool sign /sha1 $cert.Thumbprint /fd SHA256 /td SHA256 /tr http://timestamp.digicert.com $installer.FullName
    if ($LASTEXITCODE -ne 0) {
        throw "Installer signing was available but failed."
    }
}

[pscustomobject]@{
    Installer = $installer.FullName
    InstallerBytes = $installer.Length
    InstallerSHA256 = (Get-FileHash $installer.FullName -Algorithm SHA256).Hash
    PortableZip = $portableZip
    PortableBytes = (Get-Item $portableZip).Length
    PortableSHA256 = (Get-FileHash $portableZip -Algorithm SHA256).Hash
    Signing = $signing
} | ConvertTo-Json -Depth 3

param(
    [string]$Installer = "",
    [string]$PortableSource = ""
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $repo "dist"

if (-not $Installer) {
    $Installer = (Get-ChildItem $dist -Filter "CX-Build-Setup-*.exe" |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1).FullName
}
if (-not $PortableSource) {
    $portableCandidate = Get-ChildItem $dist -Directory -Filter "CX-Build-Portable-*" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($portableCandidate) {
        $PortableSource = $portableCandidate.FullName
    }
}

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw $Message
    }
}

function Path-Contains {
    param([string]$PathValue, [string]$Candidate)
    if (-not $PathValue) { return $false }
    $wanted = $Candidate.TrimEnd('\')
    foreach ($part in ($PathValue -split ';')) {
        if ($part.Trim().Trim('"').TrimEnd('\') -ieq $wanted) {
            return $true
        }
    }
    return $false
}

if (-not ("CXNative" -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class CXNative {
    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool PostMessage(
        IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
}
"@
}

function Close-CxProcess {
    param(
        [System.Diagnostics.Process]$Process,
        [string]$Label
    )

    $deadline = (Get-Date).AddSeconds(10)
    while (-not $Process.HasExited -and
           $Process.MainWindowHandle -eq [IntPtr]::Zero -and
           (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 100
        $Process.Refresh()
    }

    Assert-True (-not $Process.HasExited) "$Label exited before close verification."
    Assert-True ($Process.MainWindowHandle -ne [IntPtr]::Zero) "$Label did not expose a main window."

    [void][CXNative]::PostMessage(
        $Process.MainWindowHandle, 0x0010,
        [IntPtr]::Zero, [IntPtr]::Zero)

    Assert-True ($Process.WaitForExit(10000)) "$Label did not exit after WM_CLOSE."
}

function Start-CxIsolated {
    param(
        [string]$Exe,
        [string]$WorkingDirectory,
        [string]$DataRoot
    )

    $roaming = Join-Path $DataRoot "Roaming"
    $local = Join-Path $DataRoot "Local"
    New-Item -ItemType Directory -Force $roaming, $local | Out-Null

    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $Exe
    $psi.WorkingDirectory = $WorkingDirectory
    $psi.UseShellExecute = $false
    $psi.Environment["APPDATA"] = $roaming
    $psi.Environment["LOCALAPPDATA"] = $local
    return [System.Diagnostics.Process]::Start($psi)
}

function Get-CxUninstallEntries {
    $root = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall"
    if (-not (Test-Path $root)) { return @() }
    return @(
        Get-ChildItem $root -ErrorAction SilentlyContinue |
        ForEach-Object {
            $p = Get-ItemProperty $_.PSPath -ErrorAction SilentlyContinue
            if ($p.DisplayName -like "CX Build*") { $_.PSChildName }
        }
    )
}

$installerItem = Get-Item $Installer
Assert-True ($installerItem.Length -lt 10MB) "Installer is not below 10 MB."
Assert-True (Test-Path $PortableSource) "Portable source folder is missing."

$existingEntries = Get-CxUninstallEntries
Assert-True ($existingEntries.Count -eq 0) "A CX Build installer registration already exists; refusing to overwrite it."

$beforePath = [Environment]::GetEnvironmentVariable("Path", "User")
$testRoot = Join-Path $env:TEMP ("cx-p09-verify-" + [guid]::NewGuid().ToString("N"))
$installDir = Join-Path $testRoot "Installed CX"
$isolatedData = Join-Path $testRoot "InstalledData"
New-Item -ItemType Directory -Force $testRoot | Out-Null

try {
    $setupProcess = Start-Process -FilePath $Installer -ArgumentList @(
        "/VERYSILENT",
        "/SUPPRESSMSGBOXES",
        "/NORESTART",
        ('/DIR="' + $installDir + '"'),
        "/TASKS=addtopath"
    ) -Wait -PassThru
    Assert-True ($setupProcess.ExitCode -eq 0) "Installer returned exit code $($setupProcess.ExitCode)."
    Assert-True (Test-Path (Join-Path $installDir "cx.exe")) "Installed cx.exe is missing."

    $startMenu = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\CX Build\CX Build.lnk"
    Assert-True (Test-Path $startMenu) "Start Menu shortcut is missing."

    $afterInstallPath = [Environment]::GetEnvironmentVariable("Path", "User")
    Assert-True (Path-Contains $afterInstallPath $installDir) "Optional PATH task did not add install directory."

    $installedEntries = Get-CxUninstallEntries
    Assert-True ($installedEntries.Count -eq 1) "Expected exactly one CX Build uninstall registration."

    $allowedInstalledFiles = @(
        ".cx-path-added",
        "cx.exe",
        "LICENSE",
        "NOTICE",
        "PRIVACY.md",
        "SECURITY.md",
        "docs\INSTALL.md",
        "unins000.dat",
        "unins000.exe",
        "unins000.msg"
    )
    $unexpectedFiles = @(
        Get-ChildItem $installDir -Recurse -File |
        ForEach-Object {
            $_.FullName.Substring($installDir.Length).TrimStart('\')
        } |
        Where-Object { $_ -notin $allowedInstalledFiles }
    )
    Assert-True ($unexpectedFiles.Count -eq 0) "Installer placed unexpected files: $($unexpectedFiles -join ', ')"

    $installedProcess = Start-CxIsolated (Join-Path $installDir "cx.exe") $installDir $isolatedData
    Close-CxProcess $installedProcess "Installed CX"

    Start-Sleep -Seconds 2
    $cxLeft = @(
        Get-CimInstance Win32_Process -Filter "Name='cx.exe'" -ErrorAction SilentlyContinue |
        Where-Object {
            $_.ExecutablePath -and
            $_.ExecutablePath.StartsWith($installDir, [StringComparison]::OrdinalIgnoreCase)
        }
    )
    $wvLeft = @(
        Get-CimInstance Win32_Process -Filter "Name='msedgewebview2.exe'" -ErrorAction SilentlyContinue |
        Where-Object {
            $_.CommandLine -and
            $_.CommandLine.IndexOf($installDir, [StringComparison]::OrdinalIgnoreCase) -ge 0
        }
    )
    Assert-True ($cxLeft.Count -eq 0) "CX process remained after window close."
    Assert-True ($wvLeft.Count -eq 0) "CX-owned WebView2 subprocess remained after window close."

    $uninstaller = Get-ChildItem $installDir -Filter "unins*.exe" | Select-Object -First 1
    Assert-True ($null -ne $uninstaller) "Uninstaller executable was not created."

    $uninstallProcess = Start-Process -FilePath $uninstaller.FullName -ArgumentList @(
        "/VERYSILENT",
        "/SUPPRESSMSGBOXES",
        "/NORESTART"
    ) -Wait -PassThru
    Assert-True ($uninstallProcess.ExitCode -eq 0) "Uninstaller returned exit code $($uninstallProcess.ExitCode)."

    $deadline = (Get-Date).AddSeconds(10)
    while ((Test-Path $installDir) -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 200
    }

    Assert-True (-not (Test-Path $installDir)) "Install directory remained after uninstall."
    Assert-True (-not (Test-Path $startMenu)) "Start Menu shortcut remained after uninstall."
    Assert-True ((Get-CxUninstallEntries).Count -eq 0) "CX uninstall registry entry remained."

    $afterUninstallPath = [Environment]::GetEnvironmentVariable("Path", "User")
    Assert-True (-not (Path-Contains $afterUninstallPath $installDir)) "PATH still contains the removed install directory."

    $beforeTokens = @($beforePath -split ';' | ForEach-Object { $_.Trim().Trim('"').TrimEnd('\') } | Where-Object { $_ })
    $afterTokens = @($afterUninstallPath -split ';' | ForEach-Object { $_.Trim().Trim('"').TrimEnd('\') } | Where-Object { $_ })
    $pathDiff = @(Compare-Object $beforeTokens $afterTokens)
    Assert-True ($pathDiff.Count -eq 0) "Uninstall changed user PATH entries other than removing CX."

    $removable = Get-CimInstance Win32_LogicalDisk -Filter "DriveType=2" -ErrorAction SilentlyContinue |
        Where-Object { $_.FreeSpace -gt 20MB } |
        Select-Object -First 1

    if ($removable) {
        $portableTarget = Join-Path ($removable.DeviceID + "\") ("CX-P09-Portable-Test-" + [guid]::NewGuid().ToString("N"))
        $portableMedia = "REMOVABLE:$($removable.DeviceID)"
    } else {
        $portableTarget = Join-Path $testRoot ("USB-SIMULATED-" + [guid]::NewGuid().ToString("N"))
        $portableMedia = "SIMULATED_NO_REMOVABLE_DRIVE"
    }

    Copy-Item $PortableSource $portableTarget -Recurse
    $portableExe = Join-Path $portableTarget "cx.exe"
    $portableBat = Join-Path $portableTarget "run_portable.bat"
    Assert-True (Test-Path $portableExe) "Portable cx.exe is missing."
    Assert-True (Test-Path $portableBat) "Portable launcher is missing."

    $portableBatchProcess = Start-Process cmd.exe -ArgumentList "/d", "/c", ('"' + $portableBat + '"') -WorkingDirectory $portableTarget -PassThru

    $portableCx = $null
    $deadline = (Get-Date).AddSeconds(10)
    while (-not $portableCx -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 100
        $portableCx = Get-Process cx -ErrorAction SilentlyContinue |
            Where-Object {
                try { $_.Path -ieq $portableExe } catch { $false }
            } |
            Select-Object -First 1
    }
    Assert-True ($null -ne $portableCx) "Portable launcher did not start cx.exe."
    Close-CxProcess $portableCx "Portable CX"
    [void]$portableBatchProcess.WaitForExit(10000)

    Start-Sleep -Seconds 2
    $portableCxLeft = @(
        Get-CimInstance Win32_Process -Filter "Name='cx.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.ExecutablePath -ieq $portableExe }
    )
    $portableWvLeft = @(
        Get-CimInstance Win32_Process -Filter "Name='msedgewebview2.exe'" -ErrorAction SilentlyContinue |
        Where-Object {
            $_.CommandLine -and
            $_.CommandLine.IndexOf($portableTarget, [StringComparison]::OrdinalIgnoreCase) -ge 0
        }
    )
    Assert-True ($portableCxLeft.Count -eq 0) "Portable CX process remained after close."
    Assert-True ($portableWvLeft.Count -eq 0) "Portable WebView2 subprocess remained after close."

    $portableData = Join-Path $portableTarget "data\Roaming\CX Build\data.db"
    Assert-True (Test-Path $portableData) "Portable mode did not place CX database under its local data folder."

    [pscustomobject]@{
        InstallerBytes = $installerItem.Length
        InstallerUnder10MB = $true
        InstallUninstallClean = $true
        StartMenuShortcut = $true
        OptionalPathAddedAndRemoved = $true
        UnexpectedInstalledFiles = 0
        BackgroundProcessesAfterClose = 0
        PortableLaunch = $true
        PortableDataLocal = $true
        PortableMedia = $portableMedia
        CodeSigningStatus = (Get-AuthenticodeSignature $Installer).Status.ToString()
    } | ConvertTo-Json -Depth 3
}
finally {
    if (Test-Path $installDir) {
        $cleanupUninstaller = Get-ChildItem $installDir -Filter "unins*.exe" -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($cleanupUninstaller) {
            Start-Process -FilePath $cleanupUninstaller.FullName -ArgumentList @(
                "/VERYSILENT",
                "/SUPPRESSMSGBOXES",
                "/NORESTART"
            ) -Wait -ErrorAction SilentlyContinue | Out-Null
        }
    }

    $currentPath = [Environment]::GetEnvironmentVariable("Path", "User")
    if (Path-Contains $currentPath $installDir) {
        $cleanedParts = @(
            $currentPath -split ';' |
            Where-Object { $_.Trim().Trim('"').TrimEnd('\') -ine $installDir.TrimEnd('\') }
        )
        [Environment]::SetEnvironmentVariable(
            "Path", ($cleanedParts -join ';'), "User")
    }

    $cleanupStartMenu = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\CX Build"
    Remove-Item -Recurse -Force $cleanupStartMenu -ErrorAction SilentlyContinue

    $uninstallRoot = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall"
    Get-ChildItem $uninstallRoot -ErrorAction SilentlyContinue |
        ForEach-Object {
            $p = Get-ItemProperty $_.PSPath -ErrorAction SilentlyContinue
            if ($p.DisplayName -like "CX Build*" -and
                $p.InstallLocation -and
                $p.InstallLocation.StartsWith(
                    $testRoot, [StringComparison]::OrdinalIgnoreCase)) {
                Remove-Item $_.PSPath -Recurse -Force -ErrorAction SilentlyContinue
            }
        }

    if (Test-Path $testRoot) {
        Remove-Item -Recurse -Force $testRoot -ErrorAction SilentlyContinue
    }
}

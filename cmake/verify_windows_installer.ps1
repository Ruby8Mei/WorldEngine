param(
    [Parameter(Mandatory = $true)]
    [string]$InstallerPath,
    [string]$TestRoot = (Join-Path ([System.IO.Path]::GetTempPath()) ("inop-installer-" + [guid]::NewGuid().ToString("N")))
)

$ErrorActionPreference = "Stop"
$InstallerPath = (Resolve-Path -LiteralPath $InstallerPath).Path
$TestRoot = [System.IO.Path]::GetFullPath($TestRoot)
$TempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
if (-not $TestRoot.StartsWith($TempRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
    [System.IO.Path]::GetFileName($TestRoot) -notmatch "^inop-installer-[a-f0-9]{32}$") {
    throw "Test root must be an INOP installer directory directly under the system temporary folder"
}
$InstallDir = Join-Path $TestRoot "app"
$GuiScript = Join-Path $TestRoot "quit-immediately.txt"
$SourceGuiScript = Join-Path $PSScriptRoot "..\gui-scripts\quit-immediately.txt"
$StartMenuDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\INOP"
$Markers = @(
    "setup\operator-preset.json",
    "inop_rotors.json",
    "inop_reflectors.json",
    "inop_keysheet.json",
    "inop_settings.json",
    "inop.gui.json"
)

function Invoke-CheckedProcess {
    param(
        [string]$FilePath,
        [string[]]$ArgumentList
    )
    $Process = Start-Process -FilePath $FilePath -ArgumentList $ArgumentList -Wait -PassThru
    if ($Process.ExitCode -ne 0) {
        throw "Process failed with exit code $($Process.ExitCode): $FilePath"
    }
}

function Install-Package {
    Invoke-CheckedProcess $InstallerPath @(
        "/VERYSILENT",
        "/SUPPRESSMSGBOXES",
        "/NORESTART",
        "/DIR=`"$InstallDir`""
    )
}

function Uninstall-Package {
    $Uninstaller = Get-ChildItem -LiteralPath $InstallDir -Filter "unins*.exe" -File | Select-Object -First 1
    if (-not $Uninstaller) {
        throw "Package uninstaller was not found"
    }
    Invoke-CheckedProcess $Uninstaller.FullName @(
        "/VERYSILENT",
        "/SUPPRESSMSGBOXES",
        "/NORESTART"
    )
}

function Assert-Installed {
    $Executable = Join-Path $InstallDir "inop.exe"
    if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
        throw "Installed executable was not found"
    }
    if (-not (Test-Path -LiteralPath (Join-Path $InstallDir "fonts\CrimsonPro.ttf") -PathType Leaf)) {
        throw "Installed font bundle was not found"
    }
    if (-not (Test-Path -LiteralPath (Join-Path $InstallDir "LICENSE") -PathType Leaf)) {
        throw "Installed licence was not found"
    }
    if (-not (Test-Path -LiteralPath (Join-Path $StartMenuDir "INOP.lnk") -PathType Leaf)) {
        throw "Start menu shortcut was not found"
    }
    $Shell = New-Object -ComObject WScript.Shell
    $Shortcut = $Shell.CreateShortcut((Join-Path $StartMenuDir "INOP.lnk"))
    if ([System.IO.Path]::GetFullPath($Shortcut.WorkingDirectory) -ne [System.IO.Path]::GetFullPath($InstallDir)) {
        throw "Start menu shortcut uses the wrong working directory"
    }
}

function Assert-MarkersPreserved {
    foreach ($Marker in $Markers) {
        if (-not (Test-Path -LiteralPath (Join-Path $InstallDir $Marker) -PathType Leaf)) {
            throw "Operator data was removed: $Marker"
        }
    }
}

if (Test-Path -LiteralPath $TestRoot) {
    throw "Test root already exists: $TestRoot"
}

try {
    New-Item -ItemType Directory -Path $TestRoot | Out-Null
    Copy-Item -LiteralPath $SourceGuiScript -Destination $GuiScript

    Install-Package
    Assert-Installed

    $Executable = Join-Path $InstallDir "inop.exe"
    Push-Location $InstallDir
    try {
        & $Executable --help
        if ($LASTEXITCODE -ne 0) {
            throw "Installed help launch failed"
        }
        & $Executable --self-test --no-color
        if ($LASTEXITCODE -ne 0) {
            throw "Installed self test failed"
        }
        Invoke-CheckedProcess $Executable @("--gui-script", "`"$GuiScript`"")
    }
    finally {
        Pop-Location
    }

    foreach ($Marker in $Markers) {
        $MarkerPath = Join-Path $InstallDir $Marker
        $MarkerDir = Split-Path -Parent $MarkerPath
        [System.IO.Directory]::CreateDirectory($MarkerDir) | Out-Null
        [System.IO.File]::WriteAllText($MarkerPath, "installer preservation marker")
    }

    Install-Package
    Assert-Installed
    Assert-MarkersPreserved

    Uninstall-Package
    if (Test-Path -LiteralPath $Executable) {
        throw "Installed executable remained after uninstall"
    }
    Assert-MarkersPreserved
    if (Test-Path -LiteralPath (Join-Path $StartMenuDir "INOP.lnk")) {
        throw "Start menu shortcut remained after uninstall"
    }

    Install-Package
    Assert-Installed
    Assert-MarkersPreserved
    & $Executable --help
    if ($LASTEXITCODE -ne 0) {
        throw "Reinstalled application launch failed"
    }

    Uninstall-Package
    Assert-MarkersPreserved
    Write-Output "Installer lifecycle validation passed"
}
finally {
    if (Test-Path -LiteralPath $TestRoot) {
        Remove-Item -LiteralPath $TestRoot -Recurse -Force
    }
}

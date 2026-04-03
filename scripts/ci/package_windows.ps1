param(
    [string]$BuildDir = "build/windows-release",
    [string]$DistDir = "dist/windows",
    [string]$AppName = "CuteXMPP",
    [string]$MsysPrefix = "C:/msys64/ucrt64"
)

$ErrorActionPreference = "Stop"

function Resolve-ProjectPath {
    param([string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $script:RootDir $Path))
}

$RootDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\\.."))
$BuildDir = Resolve-ProjectPath $BuildDir
$DistDir = Resolve-ProjectPath $DistDir
$PackageDir = Join-Path $DistDir "$AppName-windows-x64"
$ExePath = Join-Path $BuildDir "$AppName.exe"
$QtBinDir = [System.IO.Path]::GetFullPath((Join-Path $MsysPrefix "bin"))
$CryptoPluginSource = [System.IO.Path]::GetFullPath((Join-Path $MsysPrefix "share\\qt6\\plugins\\crypto\\libqca-ossl.dll"))
$ArchivePath = Join-Path $DistDir "$AppName-windows-x64.zip"

if (-not (Test-Path $ExePath)) {
    throw "Build output is missing: $ExePath"
}

$windeployqt = @(
    (Join-Path $QtBinDir "windeployqt.exe"),
    (Join-Path $QtBinDir "windeployqt6.exe")
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $windeployqt) {
    throw "windeployqt was not found under $QtBinDir"
}

if (Test-Path $DistDir) {
    Remove-Item -Recurse -Force $DistDir
}

New-Item -ItemType Directory -Force $PackageDir | Out-Null
Copy-Item $ExePath (Join-Path $PackageDir "$AppName.exe")

& $windeployqt --force --no-translations (Join-Path $PackageDir "$AppName.exe")

$extraDlls = @(
    "libQXmppQt6.dll",
    "libqca-qt6.dll",
    "Qt6Xml.dll",
    "Qt6Core5Compat.dll",
    "libgcc_s_seh-1.dll",
    "libstdc++-6.dll",
    "libwinpthread-1.dll",
    "libcrypto-3-x64.dll",
    "libssl-3-x64.dll"
)

foreach ($dll in $extraDlls) {
    $source = Join-Path $QtBinDir $dll
    if (Test-Path $source) {
        Copy-Item $source (Join-Path $PackageDir $dll) -Force
    }
}

$cryptoDir = Join-Path $PackageDir "crypto"
New-Item -ItemType Directory -Force $cryptoDir | Out-Null
if (Test-Path $CryptoPluginSource) {
    Copy-Item $CryptoPluginSource (Join-Path $cryptoDir "libqca-ossl.dll") -Force
}

if (Test-Path $ArchivePath) {
    Remove-Item -Force $ArchivePath
}

Compress-Archive -Path $PackageDir -DestinationPath $ArchivePath -Force

Write-Host "[INFO] Created $PackageDir"
Write-Host "[INFO] Created $ArchivePath"

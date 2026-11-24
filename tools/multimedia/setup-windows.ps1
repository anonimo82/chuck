param([string]$MsysRoot = "C:\msys64")
$ErrorActionPreference = "Stop"
$log = Join-Path $PSScriptRoot "setup-windows.log"
Start-Transcript -Path $log -Force
try {
    if (-not [Environment]::Is64BitOperatingSystem) { throw "Windows x64 required." }
    if (!(Test-Path "$MsysRoot\usr\bin\bash.exe")) {
        if (!(Get-Command winget -ErrorAction SilentlyContinue)) { throw "Install MSYS2 from https://www.msys2.org/ first." }
        if ($MsysRoot -ne "C:\msys64") { throw "Install MSYS2 at the specified custom path first." }
        & winget install --id MSYS2.MSYS2 --exact --accept-package-agreements --accept-source-agreements
        if ($LASTEXITCODE -ne 0) { throw "MSYS2 installation failed: $LASTEXITCODE" }
    }
    $bash = "$MsysRoot\usr\bin\bash.exe"
    if (!(Test-Path $bash)) { throw "MSYS2 not found at $MsysRoot" }
    $env:MSYSTEM = "UCRT64"
    $env:CHERE_INVOKING = "1"
    & $bash -lc 'pacman -Syu --noconfirm'
    if ($LASTEXITCODE -ne 0) { throw "MSYS2 update interrupted; rerun this script." }
    & $bash -lc 'pacman -Syu --noconfirm'
    if ($LASTEXITCODE -ne 0) { throw "MSYS2 update failed; rerun this script." }
    & $bash -lc 'pacman -S --needed --noconfirm git mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-python mingw-w64-ucrt-x86_64-imagemagick mingw-w64-ucrt-x86_64-librsvg mingw-w64-ucrt-x86_64-cairo mingw-w64-ucrt-x86_64-assimp mingw-w64-ucrt-x86_64-libsndfile mingw-w64-ucrt-x86_64-ffmpeg'
    if ($LASTEXITCODE -ne 0) { throw "Dependency installation failed." }
    Write-Host "Dependencies installed. Run build-and-test.ps1 next."
} finally { Stop-Transcript }

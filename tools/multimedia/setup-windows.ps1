param([string]$MsysRoot = "C:\msys64")
$ErrorActionPreference = "Stop"
$log = Join-Path $PSScriptRoot "setup-windows.log"
Start-Transcript -Path $log -Force
try {
    if (-not [Environment]::Is64BitOperatingSystem) { throw "Windows x64 required." }
    if (!(Test-Path "$MsysRoot\usr\bin\bash.exe")) {
        # Use the official stable release directly, also on systems without winget.
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $release = Invoke-RestMethod -Uri "https://api.github.com/repos/msys2/msys2-installer/releases/latest" -Headers @{"User-Agent"="ChucK-Multimedia-Setup"}
        $asset = $release.assets | Where-Object { $_.name -match '^msys2-x86_64-\d{8}\.exe$' } | Select-Object -First 1
        if (!$asset) { throw "Official MSYS2 x64 installer not found." }
        $checksum = $release.assets | Where-Object { $_.name -eq ($asset.name + ".sha256") } | Select-Object -First 1
        if (!$checksum) { throw "Official MSYS2 checksum not found." }
        $temp = Join-Path ([IO.Path]::GetTempPath()) ([Guid]::NewGuid().ToString())
        New-Item -ItemType Directory -Path $temp | Out-Null
        try {
            $installer = Join-Path $temp $asset.name
            $hashfile = Join-Path $temp $checksum.name
            Write-Host "Downloading official MSYS2 installer..."
            Invoke-WebRequest -UseBasicParsing -Uri $asset.browser_download_url -OutFile $installer
            Invoke-WebRequest -UseBasicParsing -Uri $checksum.browser_download_url -OutFile $hashfile
            $expected = [regex]::Match((Get-Content $hashfile -Raw), '(?i)\b[a-f0-9]{64}\b').Value
            if (!$expected -or (Get-FileHash -Algorithm SHA256 $installer).Hash -ne $expected) { throw "MSYS2 installer checksum mismatch." }
            $target = $MsysRoot.Replace('\','/')
            $process = Start-Process -FilePath $installer -ArgumentList @('in','--confirm-command','--accept-messages','--root',('"' + $target + '"')) -Wait -PassThru
            if ($process.ExitCode -ne 0) { throw "MSYS2 installation failed: $($process.ExitCode)" }
        } finally {
            Remove-Item -LiteralPath $temp -Recurse -Force
        }
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

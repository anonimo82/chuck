param([string]$MsysRoot = "C:\msys64", [string]$ChuckExe = "")
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$build = Join-Path $root "build/multimedia-windows"
New-Item -ItemType Directory -Force -Path $build | Out-Null
Start-Transcript -Path (Join-Path $PSScriptRoot "build-and-test.log") -Force
function Run-Native {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed: $LASTEXITCODE" }
}
try {
    $bin = Join-Path $MsysRoot "ucrt64/bin"
    if (!(Test-Path "$bin/cmake.exe")) { throw "Run setup-windows.ps1 first." }
    $env:PATH = "$bin;$MsysRoot\usr\bin;$env:PATH"
    $env:PKG_CONFIG = "$bin/pkg-config.exe"
    # ImageMagick loads format-specific DLLs dynamically, outside the PE import graph.
    $coder = Get-ChildItem -Path "$MsysRoot/ucrt64/lib/ImageMagick-*/modules-*/coders" -Directory | Where-Object { Test-Path (Join-Path $_.FullName "png.dll") } | Select-Object -First 1
    if ($coder) {
        $env:MAGICK_CODER_MODULE_PATH = $coder.FullName
        Write-Host "ImageMagick coders: $env:MAGICK_CODER_MODULE_PATH"
    }
    $env:MAGICK_HOME = Join-Path $MsysRoot "ucrt64"
    $config = Join-Path $MsysRoot "ucrt64/etc/ImageMagick-7"
    if (Test-Path $config) { $env:MAGICK_CONFIGURE_PATH = $config }
    $midi = Join-Path $build "midifile"
    if (!(Test-Path "$midi/include/MidiFile.h")) {
        Run-Native "$MsysRoot/usr/bin/git.exe" @("clone","--depth","1","https://github.com/craigsapp/midifile.git",$midi)
    }
    Run-Native "$MsysRoot/usr/bin/git.exe" @("-C",$midi,"rev-parse","HEAD")
    Run-Native "$bin/cmake.exe" @("-S",$PSScriptRoot,"-B","$build/native","-G","Ninja","-DCMAKE_CXX_COMPILER=$bin/g++.exe","-DMIDIFILE_SOURCE_DIR=$midi","-DCMAKE_BUILD_TYPE=Release")
    Run-Native "$bin/cmake.exe" @("--build","$build/native")
    Run-Native "$bin/cmake.exe" @("--install","$build/native","--prefix","$build/install")
    if (!$ChuckExe) {
        $candidate = Join-Path $root "src/visual-studio/x64/Release/chuck.exe"
        if (Test-Path $candidate) { $ChuckExe = $candidate }
        else {
            $cmd = Get-Command chuck.exe -ErrorAction SilentlyContinue
            if ($cmd) { $ChuckExe = $cmd.Source }
        }
    }
    if (!$ChuckExe) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio/Installer/vswhere.exe"
        if (Test-Path $vswhere) {
            $msbuild = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
            if ($msbuild) {
                Run-Native $msbuild @("$root/src/visual-studio/chuck.vcxproj","/p:Configuration=Release","/p:Platform=x64")
                if (Test-Path $candidate) { $ChuckExe = $candidate }
            }
        }
    }
    if (!$ChuckExe -or !(Test-Path $ChuckExe)) { throw "No compatible chuck.exe found. Supply -ChuckExe or install Visual Studio C++ Build Tools with v142 and Windows SDK." }
    $env:CHUCK_EXE = (Resolve-Path $ChuckExe).Path
    $env:CHUGIN_DIR = "$build/install/chugins"
    # ChucK uses LOAD_LIBRARY_SEARCH_DEFAULT_DIRS, which excludes PATH.
    # Resolve the transitive PE import graph and stage MSYS2 DLLs in _deps.
    $deps = Join-Path $env:CHUGIN_DIR "_deps"
    New-Item -ItemType Directory -Force -Path $deps | Out-Null
    $queue = New-Object 'System.Collections.Generic.Queue[string]'
    $seen = @{}
    Get-ChildItem -LiteralPath $env:CHUGIN_DIR -Filter "*.chug" -File | ForEach-Object { $queue.Enqueue($_.FullName) }
    while ($queue.Count -gt 0) {
        $file = $queue.Dequeue()
        $imports = & "$bin/objdump.exe" -p $file
        if ($LASTEXITCODE -ne 0) { throw "Cannot inspect DLL imports of $file" }
        foreach ($line in $imports) {
            if ($line -match '^\s*DLL Name:\s*(\S+\.dll)\s*$') {
                $name = $Matches[1]
                if ($seen.ContainsKey($name)) { continue }
                $seen[$name] = $true
                $source = Join-Path $bin $name
                if (Test-Path -LiteralPath $source -PathType Leaf) {
                    Copy-Item -LiteralPath $source -Destination $deps -Force
                    $queue.Enqueue($source)
                    Write-Host "Staged dependency: $name"
                } elseif (!(Test-Path (Join-Path "$env:SystemRoot/System32" $name)) -and $name -notmatch '^(api-ms-|ext-ms-)') {
                    throw "Unresolved DLL dependency: $name (imported by $file)"
                }
            }
        }
    }
    Run-Native $env:CHUCK_EXE @("--version")
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $probe = & $env:CHUCK_EXE "--chugin-path-auto-load:$env:CHUGIN_DIR" --chugin-probe 2>&1
        $probeExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    $probe | ForEach-Object { Write-Host $_ }
    if ($probeExit -ne 0) { throw "Chugin probe failed: $probeExit" }
    foreach ($module in @("RasterIO","VectorIO","ModelIO","AudioIO","MidiFileIO","VideoIO")) {
        if (($probe -join "`n") -notmatch ([regex]::Escape("$module.chug") + '\s+\[OK\]')) {
            throw "Chugin probe did not confirm $module"
        }
    }
    Run-Native "$bin/python.exe" @("$PSScriptRoot/raster/test_raster.py")
    Run-Native "$bin/python.exe" @("$PSScriptRoot/audio/test_audio.py")
    Run-Native "$bin/python.exe" @("$PSScriptRoot/test_multimedia.py")
    Write-Host "All multimedia smoke tests passed."
} finally { Stop-Transcript }

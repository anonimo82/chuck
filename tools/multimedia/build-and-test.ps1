param([string]$MsysRoot = "C:\msys64", [string]$ChuckExe = "", [string]$CMakeExe = "", [ValidateSet("Full","Local","Incremental","Failed")][string]$Mode = "Full", [string]$Since = "", [ValidateSet("raster","audio","legacy","text-core","text-recent","text-page-breaks","text-page-furniture","text-page-numbers","text-split-paragraphs","text-split-tables","text-frame-foundation","text-frame-flow","text-frame-mixed","text-frame-render","text-frame-native","text-accessibility","text-flow-txt","text-flow-interop","text-flow-performance","text-flow-consolidation","text-flow-hit-test","text-flow-selection","text-flow-edit","text-flow-styled-edit","text-flow-edit-consolidation","vector-shape-edit","vector-shape-transform","vector-shape-style","vector-shape-group","vector-editable-svg","midi-capture-events","midi-capture-save","midi-capture-session","text-capture-unicode","text-capture-integration","raster-capture-foundation","raster-camera-capture","raster-screen-capture","raster-capture-image","raster-capture-consolidation","video-camera-session","video-screen-recording","video-audio-capture","video-av-recording","video-capture-consolidation","video-opengl-framebuffer","video-chugl-texture","video-offscreen-render","video-indexed-av","video-graphics-consolidation","transport-framing","transport-loopback","transport-session","transport-endpoints","transport-consolidation","transport-pcm","transport-pcm-stream","transport-pcm-buffer","transport-midi","transport-audio-midi","transport-raster","transport-raster-stream","transport-vector","transport-model","transport-graphics","transport-video-frame","transport-video-stream","transport-video-buffer","transport-audio-video","transport-av-consolidation","transport-text","transport-text-fragments","transport-text-buffer","transport-heterogeneous","transport-raster-lossless","transport-raster-png","transport-raster-photo","transport-raster-compressed-fragments","transport-raster-auto","transport-video-auto-fragments","transport-video-high-resolution","transport-video-intraframe","transport-video-interframe","transport-video-consolidation","transport-mesh-attributes","transport-model-materials","transport-scene-nodes","transport-model-animation","transport-model-fragments","transport-final-consolidation","geometry-guides","geometry-transforms","geometry-deformation","geometry-raster-warp","geometry-projection","geometry-vector-paths","geometry-path-operations","geometry-selection-paths")][string[]]$Group = @())
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
$originalPath = $env:PATH
try {
    $bin = Join-Path $MsysRoot "ucrt64/bin"
    if (!$CMakeExe) {
        $nativeCMake = Join-Path $env:ProgramFiles "CMake/bin/cmake.exe"
        if (Test-Path -LiteralPath $nativeCMake -PathType Leaf) { $CMakeExe = $nativeCMake }
        else { $CMakeExe = "$bin/cmake.exe" }
    }
    if (!(Test-Path -LiteralPath $CMakeExe -PathType Leaf)) { throw "CMake not found: $CMakeExe" }
    $CMakeExe = (Resolve-Path -LiteralPath $CMakeExe).Path
    Write-Host "CMake: $CMakeExe"
    Run-Native $CMakeExe @("--version")
    foreach ($tool in @("g++.exe","ninja.exe","pkg-config.exe")) {
        if (!(Test-Path "$bin/$tool")) { throw "Missing $bin/$tool. Run setup-windows.ps1 first." }
    }
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
    Run-Native $CMakeExe @("-S",$PSScriptRoot,"-B","$build/native","-G","Ninja","-DCMAKE_CXX_COMPILER=$bin/g++.exe","-DCMAKE_MAKE_PROGRAM=$bin/ninja.exe","-DPKG_CONFIG_EXECUTABLE=$bin/pkg-config.exe","-DMIDIFILE_SOURCE_DIR=$midi","-DCMAKE_BUILD_TYPE=Release")
    Run-Native $CMakeExe @("--build","$build/native")
    Run-Native $CMakeExe @("--install","$build/native","--prefix","$build/install")
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
    # Isolate validation, not build-tool or ChucK discovery. Keep MSYS2 git
    # available for the smoke runner and UCRT64 ffmpeg for media fixtures.
    $env:PATH = "$bin;$MsysRoot\usr\bin;$env:SystemRoot\System32;$env:SystemRoot"
    Write-Host "Isolated validation PATH: $env:PATH"
    Run-Native $env:CHUCK_EXE @("--version")
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $probe = & $env:CHUCK_EXE "--chugin-path-auto-load:$env:CHUGIN_DIR" --chugin-probe 2>&1
        $probeExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    $probe | ForEach-Object { Write-Host $_ }
    if ($probeExit -ne 0) { throw "Chugin probe failed: $probeExit" }
    foreach ($module in @("RasterIO","VectorIO","ModelIO","AudioIO","MidiFileIO","VideoIO","SubtitleIO","KeyframeIO","TextIO","MediaTransport","MediaGeometry")) {
        if (($probe -join "`n") -notmatch ([regex]::Escape("$module.chug") + '\s+\[OK\]')) {
            throw "Chugin probe did not confirm $module"
        }
    }
    $smokeArgs = @("$PSScriptRoot/run_smoke_tests.py","--mode",$Mode,"--state",(Join-Path $build "smoke-state.json"))
    if ($Since) { $smokeArgs += @("--since",$Since) }
    foreach ($g in $Group) { $smokeArgs += @("--group",$g) }
    Run-Native "$bin/python.exe" $smokeArgs
    if ($Mode -in @("Local","Incremental")) { Write-Host "Local smoke validation finished; see summary for PASS or SKIP." }
    else { Write-Host "All multimedia smoke tests passed." }
} finally {
    $env:PATH = $originalPath
    Stop-Transcript
}

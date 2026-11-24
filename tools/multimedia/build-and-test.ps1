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
    Run-Native $env:CHUCK_EXE @("--version")
    Run-Native "$bin/python.exe" @("$PSScriptRoot/raster/test_raster.py")
    Run-Native "$bin/python.exe" @("$PSScriptRoot/audio/test_audio.py")
    Run-Native "$bin/python.exe" @("$PSScriptRoot/test_multimedia.py")
    Write-Host "All multimedia smoke tests passed."
} finally { Stop-Transcript }

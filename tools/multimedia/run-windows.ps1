param([string]$MsysRoot = "C:\msys64", [string]$ChuckExe = "")
$ErrorActionPreference = "Stop"
& "$PSScriptRoot/setup-windows.ps1" -MsysRoot $MsysRoot
& "$PSScriptRoot/build-and-test.ps1" -MsysRoot $MsysRoot -ChuckExe $ChuckExe

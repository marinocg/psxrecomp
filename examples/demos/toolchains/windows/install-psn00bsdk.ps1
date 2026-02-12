$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$SdkRoot = Join-Path $ScriptDir 'psn00bsdk'
$InstallPrefix = Join-Path $SdkRoot 'local'

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw 'Git is required. Install Git for Windows first.'
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw 'CMake is required. Install CMake first.'
}

if (-not (Test-Path $SdkRoot)) {
    git clone --recursive https://github.com/Lameguy64/PSn00bSDK.git $SdkRoot
}

Push-Location $SdkRoot
cmake -S . -B build
cmake --build build --config Release
cmake --install build --prefix $InstallPrefix
Pop-Location

$PathToAdd = "$InstallPrefix\bin"
[Environment]::SetEnvironmentVariable('PSN00BSDK', $InstallPrefix, 'User')

$CurrentPath = [Environment]::GetEnvironmentVariable('Path', 'User')
if ($CurrentPath -notlike "*$PathToAdd*") {
    [Environment]::SetEnvironmentVariable('Path', "$CurrentPath;$PathToAdd", 'User')
}

Write-Host 'Installed PSn00bSDK.'
Write-Host "PSN00BSDK set to: $InstallPrefix"
Write-Host 'Open a new PowerShell window before building.'
Write-Host 'Install mkpsxiso separately if it is not already available in PATH.'

$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$SdkRoot = Join-Path $ScriptDir 'psn00bsdk'
$InstallPrefix = Join-Path $SdkRoot 'local'
$ExtractDir = Join-Path $SdkRoot '_prebuilt_extract'

New-Item -ItemType Directory -Force -Path $SdkRoot | Out-Null
New-Item -ItemType Directory -Force -Path $InstallPrefix | Out-Null

function Install-FromPrebuilt {
    try {
        $release = Invoke-RestMethod -Uri 'https://api.github.com/repos/Lameguy64/PSn00bSDK/releases/latest'
    }
    catch {
        return $false
    }

    $asset = $release.assets |
        Where-Object {
            $_.name -match 'windows|win64|win32' -and
            $_.name -match '(zip)$'
        } |
        Select-Object -First 1

    if (-not $asset) {
        return $false
    }

    $archivePath = Join-Path $SdkRoot $asset.name
    Write-Host "Downloading prebuilt PSn00bSDK asset: $($asset.name)"
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $archivePath

    if (Test-Path $ExtractDir) {
        Remove-Item -Recurse -Force $ExtractDir
    }
    Expand-Archive -Path $archivePath -DestinationPath $ExtractDir -Force

    $includeDir = Get-ChildItem -Path $ExtractDir -Directory -Recurse |
        Where-Object { $_.Name -eq 'include' } |
        Select-Object -First 1

    if (-not $includeDir) {
        return $false
    }

    $candidateRoot = Split-Path -Parent $includeDir.FullName
    if (-not (Test-Path (Join-Path $candidateRoot 'bin')) -or -not (Test-Path (Join-Path $candidateRoot 'lib'))) {
        return $false
    }

    if (Test-Path $InstallPrefix) {
        Get-ChildItem -Force $InstallPrefix | Remove-Item -Recurse -Force
    }
    Copy-Item -Recurse -Force (Join-Path $candidateRoot '*') $InstallPrefix
    return $true
}

$usedPrebuilt = Install-FromPrebuilt
if (-not $usedPrebuilt) {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        throw 'Git is required. Install Git for Windows first.'
    }

    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        throw 'CMake is required. Install CMake first.'
    }

    if (-not (Test-Path (Join-Path $SdkRoot '.git'))) {
        if (Test-Path $SdkRoot) {
            Remove-Item -Recurse -Force $SdkRoot
        }
        git clone --recursive https://github.com/Lameguy64/PSn00bSDK.git $SdkRoot
    }

    Push-Location $SdkRoot
    cmake -S . -B build
    cmake --build build --config Release
    cmake --install build --prefix $InstallPrefix
    Pop-Location
}

$PathToAdd = "$InstallPrefix\bin"
[Environment]::SetEnvironmentVariable('PSN00BSDK', $InstallPrefix, 'User')

$CurrentPath = [Environment]::GetEnvironmentVariable('Path', 'User')
if (-not $CurrentPath) {
    $CurrentPath = ''
}
if ($CurrentPath -notlike "*$PathToAdd*") {
    [Environment]::SetEnvironmentVariable('Path', "$CurrentPath;$PathToAdd", 'User')
}

Write-Host 'Installed PSn00bSDK.'
Write-Host "PSN00BSDK set to: $InstallPrefix"
Write-Host 'Open a new PowerShell window before building.'
Write-Host 'Install mkpsxiso separately if it is not already available in PATH.'

# Creates the Windows release ZIP (VST3 + Standalone) from a Release build.
# Usage: pwsh Scripts/package-release.ps1 [-BuildDir <path>]
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build')
)
$ErrorActionPreference = 'Stop'

$ProjectDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$BuildDir = (Resolve-Path $BuildDir).Path

# Read the version from CMakeLists.txt: project(ICStandardGuitar VERSION x.y.z ...)
$cmake = Get-Content (Join-Path $ProjectDir 'CMakeLists.txt') -Raw
if ($cmake -notmatch 'project\(\s*ICStandardGuitar\s+VERSION\s+([0-9.]+)') {
    throw 'Could not read the project version from CMakeLists.txt'
}
$Version = $Matches[1]

$PackageName = "IC-Standard-Guitar-v$Version-Windows-x64"
$Artefacts = Join-Path $BuildDir 'ICStandardGuitar_artefacts\Release'
$Vst3Src = Join-Path $Artefacts 'VST3\IC Standard Guitar.vst3'
$ExeSrc = Join-Path $Artefacts 'Standalone\IC Standard Guitar.exe'
$DistDir = Join-Path $ProjectDir 'dist'
$StageDir = Join-Path $DistDir $PackageName
$ZipPath = Join-Path $DistDir "$PackageName.zip"

if (-not (Test-Path $Vst3Src) -or -not (Test-Path $ExeSrc)) {
    Write-Error "Release artefacts not found in $Artefacts`nRun: cmake -B build -G `"Visual Studio 17 2022`" -A x64 -DJUCE_DIR=C:/path/to/JUCE; cmake --build build --config Release"
}

if (Test-Path $StageDir) { Remove-Item $StageDir -Recurse -Force }
if (Test-Path $ZipPath) { Remove-Item $ZipPath -Force }
New-Item -ItemType Directory -Force $StageDir | Out-Null

Copy-Item $Vst3Src -Destination $StageDir -Recurse
Copy-Item $ExeSrc -Destination $StageDir
Copy-Item (Join-Path $ProjectDir 'README.md') -Destination $StageDir
Copy-Item (Join-Path $ProjectDir 'LICENSE') -Destination $StageDir

Compress-Archive -Path $StageDir -DestinationPath $ZipPath
Remove-Item $StageDir -Recurse -Force

Write-Host "Created release package: $ZipPath"

param(
    [string]$Configuration = "Release",
    [string]$N8roRelease = "C:\n8ro"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = "$repoRoot\build-nmake"
$cmakeCandidates = @(
    "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "C:\Program Files\CMake\bin\cmake.exe"
)
$vcvarsCandidates = @(
    "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
)

$cmake = $cmakeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $cmake) {
    throw "CMake was not found."
}

$vcvars = $vcvarsCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $vcvars) {
    throw "Visual Studio vcvars64.bat was not found."
}

function Invoke-DevCommand([string]$Command) {
    & cmd.exe /d /c "call ""$vcvars"" >nul && $Command"
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $Command"
    }
}

Invoke-DevCommand """$cmake"" -S ""$repoRoot"" -B ""$buildDir"" -G ""NMake Makefiles"" -D CMAKE_BUILD_TYPE=""$Configuration"" -D N8RO_RELEASE=""$N8roRelease"""
Invoke-DevCommand """$cmake"" --build ""$buildDir"""

$dll = "$buildDir\bin\student-com-kinematic-plugin.dll"
if (-not (Test-Path $dll)) {
    throw "Build finished but DLL was not found: $dll"
}

Write-Host "[OK] Built $dll"

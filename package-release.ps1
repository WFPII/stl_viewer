<#
.SYNOPSIS
    Build a release zip for GitHub distribution.
.DESCRIPTION
    1. Builds the project in Release mode
    2. Collects the exe + all required DLLs
    3. Packages everything into a portable zip
.EXAMPLE
    .\package-release.ps1                    # Default: stl-viewer-1.0.0-win-x64.zip
    .\package-release.ps1 -Version "1.2.0"   # Custom version
#>

param(
    [string]$Version = "1.0.0",
    [string]$VcpkgRoot = $env:VCPKG_ROOT
)

$ErrorActionPreference = "Stop"
$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectDir

$ReleaseName = "stl-viewer-$Version-win-x64"
$StageDir    = "dist\$ReleaseName"
$ZipFile     = "dist\$ReleaseName.zip"

Write-Host ""
Write-Host "====================================================" -ForegroundColor Cyan
Write-Host "   Packaging STL Viewer v$Version for release        " -ForegroundColor Cyan
Write-Host "====================================================" -ForegroundColor Cyan
Write-Host ""

# ── Step 1: Build Release ────────────────────────────────────────────────────

Write-Host "[1/4] Building Release..." -ForegroundColor Yellow

# Make sure setup has been run first (ImGui + stb downloaded)
if (-not (Test-Path "imgui\imgui.cpp")) {
    Write-Host "  Running setup.ps1 first..." -ForegroundColor Gray
    & "$ProjectDir\setup.ps1" -Release
} else {
    # Just build
    $BuildDir = "build\release"

    # Detect generator
    $HasNinja = [bool](Get-Command ninja -ErrorAction SilentlyContinue)
    $Generator = if ($HasNinja) { "Ninja" } else { "MinGW Makefiles" }

    # Try to set up MSVC if available
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                    -property installationPath 2>$null
        if ($vsPath) {
            $vcvarsall = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"
            if ((Test-Path $vcvarsall) -and -not (Get-Command "cl" -ErrorAction SilentlyContinue)) {
                cmd /c "`"$vcvarsall`" x64 >nul 2>&1 && set" | ForEach-Object {
                    if ($_ -match "^([^=]+)=(.*)$") {
                        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
                    }
                }
            }
        }
    }

    cmake -B $BuildDir -G $Generator `
        -DCMAKE_BUILD_TYPE=Release `
        -DCMAKE_TOOLCHAIN_FILE="$VcpkgRoot\scripts\buildsystems\vcpkg.cmake" .

    cmake --build $BuildDir --config Release
}

Write-Host "  Build complete." -ForegroundColor Green
Write-Host ""

# ── Step 2: Find the exe ─────────────────────────────────────────────────────

Write-Host "[2/4] Collecting files..." -ForegroundColor Yellow

$exe = Get-ChildItem -Path "build" -Filter "stl_viewer.exe" -Recurse | Select-Object -First 1
if (-not $exe) {
    Write-Host "ERROR: stl_viewer.exe not found in build/" -ForegroundColor Red
    exit 1
}

Write-Host "  Found: $($exe.FullName)" -ForegroundColor Gray

# ── Step 3: Stage files ──────────────────────────────────────────────────────

Write-Host "[3/4] Staging release..." -ForegroundColor Yellow

# Clean and create stage directory
if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
New-Item -ItemType Directory -Force -Path $StageDir | Out-Null

# Copy exe
Copy-Item $exe.FullName "$StageDir\stl_viewer.exe"

# Collect DLLs from the exe's directory and vcpkg installed bin
$exeDir = $exe.DirectoryName
$dllSearchPaths = @($exeDir)

# Add vcpkg installed bin directories
$vcpkgBin = Get-ChildItem -Path "build" -Filter "*.dll" -Recurse -ErrorAction SilentlyContinue
foreach ($dll in $vcpkgBin) {
    if ($dll.DirectoryName -notin $dllSearchPaths) {
        $dllSearchPaths += $dll.DirectoryName
    }
}

# Also check vcpkg_installed
$vcpkgInstalled = Get-ChildItem -Path "build" -Directory -Filter "vcpkg_installed" -Recurse | Select-Object -First 1
if ($vcpkgInstalled) {
    $binDir = Join-Path $vcpkgInstalled.FullName "x64-windows\bin"
    if (Test-Path $binDir) {
        $dllSearchPaths += $binDir
    }
    # Also check debug bin for any misplaced DLLs
    $dbgBin = Join-Path $vcpkgInstalled.FullName "x64-windows\debug\bin"
    if (Test-Path $dbgBin) {
        $dllSearchPaths += $dbgBin
    }
}

# Copy all DLLs from search paths
$copiedDlls = @()
foreach ($searchPath in $dllSearchPaths) {
    $dlls = Get-ChildItem -Path $searchPath -Filter "*.dll" -ErrorAction SilentlyContinue
    foreach ($dll in $dlls) {
        if ($dll.Name -notin $copiedDlls) {
            Copy-Item $dll.FullName "$StageDir\$($dll.Name)"
            $copiedDlls += $dll.Name
            Write-Host "  DLL: $($dll.Name)" -ForegroundColor Gray
        }
    }
}

# For MinGW builds, also grab runtime DLLs
$mingwDlls = @(
    "libstdc++-6.dll",
    "libgcc_s_seh-1.dll",
    "libwinpthread-1.dll"
)

$mingwBin = Split-Path (Get-Command g++ -ErrorAction SilentlyContinue).Source -ErrorAction SilentlyContinue
if ($mingwBin) {
    foreach ($dllName in $mingwDlls) {
        $dllPath = Join-Path $mingwBin $dllName
        if ((Test-Path $dllPath) -and ($dllName -notin $copiedDlls)) {
            Copy-Item $dllPath "$StageDir\$dllName"
            $copiedDlls += $dllName
            Write-Host "  DLL: $dllName (MinGW runtime)" -ForegroundColor Gray
        }
    }
}

Write-Host "  Exe + $($copiedDlls.Count) DLLs staged." -ForegroundColor Green

# Create a short README for the release
$readmeContent = @"
STL Viewer & Exporter v$Version
================================

A lightweight desktop app for viewing STL files in 3D and
batch-exporting them as PNG images.

USAGE
-----
  stl_viewer.exe                    Launch empty
  stl_viewer.exe model.stl          Open a file
  stl_viewer.exe C:\Models\         Open a folder

You can also drag & drop STL files or folders onto the window.

CONTROLS
--------
  Left-drag in viewport     Orbit camera
  Scroll wheel              Zoom in/out
  Ctrl+O                    Browse for file
  Ctrl+Shift+O              Browse for folder
  Ctrl+E                    Export current model
  Ctrl+Shift+E              Export all models

LICENSE
-------
MIT License. See https://github.com/YOUR_USERNAME/stl-viewer
"@

$readmeContent | Out-File -FilePath "$StageDir\README.txt" -Encoding UTF8

# ── Step 4: Create zip ───────────────────────────────────────────────────────

Write-Host "[4/4] Creating zip..." -ForegroundColor Yellow

if (Test-Path $ZipFile) { Remove-Item -Force $ZipFile }
Compress-Archive -Path "$StageDir\*" -DestinationPath $ZipFile -CompressionLevel Optimal

$zipSize = (Get-Item $ZipFile).Length / 1MB
Write-Host ""
Write-Host "====================================================" -ForegroundColor Green
Write-Host "   Release package created!                          " -ForegroundColor Green
Write-Host "====================================================" -ForegroundColor Green
Write-Host ""
Write-Host "  File:  $ZipFile" -ForegroundColor White
Write-Host "  Size:  $([math]::Round($zipSize, 1)) MB" -ForegroundColor White
Write-Host ""
Write-Host "Contents:" -ForegroundColor Cyan
Get-ChildItem $StageDir | ForEach-Object {
    $size = if ($_.Length -gt 1MB) { "$([math]::Round($_.Length/1MB, 1)) MB" }
            elseif ($_.Length -gt 1KB) { "$([math]::Round($_.Length/1KB, 0)) KB" }
            else { "$($_.Length) B" }
    Write-Host "  $($_.Name)  ($size)" -ForegroundColor Gray
}
Write-Host ""
Write-Host "Upload to GitHub:" -ForegroundColor Cyan
Write-Host "  1. Go to your repo > Releases > 'Create a new release'" -ForegroundColor White
Write-Host "  2. Tag: v$Version" -ForegroundColor White
Write-Host "  3. Paste RELEASE.md contents into the description" -ForegroundColor White
Write-Host "  4. Attach: $ZipFile" -ForegroundColor White
Write-Host ""

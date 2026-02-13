<#
.SYNOPSIS
    One-command setup for STL Viewer on Windows.
.DESCRIPTION
    - Detects available C++ compiler (MSVC, MinGW, Clang)
    - Installs vcpkg if not present
    - Downloads Dear ImGui and stb_image_write
    - Configures and builds with CMake
.EXAMPLE
    .\setup.ps1              # Debug build (auto-detect compiler)
    .\setup.ps1 -Release     # Release build
    .\setup.ps1 -Generator "MinGW Makefiles"   # Force MinGW
#>

param(
    [switch]$Release,
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$Generator = ""
)

$ErrorActionPreference = "Stop"
$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectDir

$ImGuiVersion = "v1.91.8"
$BuildType = if ($Release) { "Release" } else { "Debug" }

Write-Host ""
Write-Host "====================================================" -ForegroundColor Cyan
Write-Host "       STL Viewer & Exporter - Windows Setup         " -ForegroundColor Cyan
Write-Host "====================================================" -ForegroundColor Cyan
Write-Host ""

# ── Warn about OneDrive paths ────────────────────────────────────────────────

if ($ProjectDir -match "OneDrive") {
    Write-Host "WARNING: Project is inside a OneDrive folder." -ForegroundColor Yellow
    Write-Host "  OneDrive can cause build failures due to file locking and long paths." -ForegroundColor Yellow
    Write-Host "  Recommended: Copy the project to C:\Projects\stl_viewer\ or similar." -ForegroundColor Yellow
    Write-Host ""
    $continue = Read-Host "Continue anyway? [y/N]"
    if ($continue -ne "y" -and $continue -ne "Y") { exit 0 }
    Write-Host ""
}

# ── Check prerequisites ──────────────────────────────────────────────────────

function Test-Command($cmd) {
    return [bool](Get-Command $cmd -ErrorAction SilentlyContinue)
}

Write-Host "Checking prerequisites..." -ForegroundColor Yellow

# CMake
if (-not (Test-Command "cmake")) {
    Write-Host "ERROR: cmake not found." -ForegroundColor Red
    Write-Host "  Install: winget install Kitware.CMake" -ForegroundColor Gray
    Write-Host "  Then restart your terminal." -ForegroundColor Gray
    exit 1
}
Write-Host "  cmake: OK" -ForegroundColor Green

# Git
if (-not (Test-Command "git")) {
    Write-Host "ERROR: git not found." -ForegroundColor Red
    Write-Host "  Install: winget install Git.Git" -ForegroundColor Gray
    exit 1
}
Write-Host "  git:   OK" -ForegroundColor Green

# ── Detect C++ compiler ─────────────────────────────────────────────────────

$CompilerType = ""
$VsDevCmd = ""

# Check 1: Is cl.exe already on PATH? (running from Developer Command Prompt)
if (Test-Command "cl") {
    $CompilerType = "MSVC"
    Write-Host "  C++:   MSVC (cl.exe on PATH)" -ForegroundColor Green
}

# Check 2: Can we find Visual Studio via vswhere?
if (-not $CompilerType) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                    -property installationPath 2>$null
        if ($vsPath) {
            $CompilerType = "MSVC-VSWHERE"

            # Find vcvarsall.bat to set up the environment
            $vcvarsall = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"
            if (Test-Path $vcvarsall) {
                $VsDevCmd = $vcvarsall
                Write-Host "  C++:   MSVC found at $vsPath" -ForegroundColor Green
            }
        }
    }
}

# Check 3: Check for VS Build Tools specifically
if (-not $CompilerType) {
    $btPath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools"
    $vcvarsall = "$btPath\VC\Auxiliary\Build\vcvarsall.bat"
    if (Test-Path $vcvarsall) {
        $CompilerType = "MSVC-BUILDTOOLS"
        $VsDevCmd = $vcvarsall
        Write-Host "  C++:   MSVC Build Tools found" -ForegroundColor Green
    }
}

# Check 4: MinGW g++
if (-not $CompilerType -and (Test-Command "g++")) {
    $CompilerType = "MINGW"
    Write-Host "  C++:   MinGW (g++)" -ForegroundColor Green
}

# Check 5: Clang
if (-not $CompilerType -and (Test-Command "clang++")) {
    $CompilerType = "CLANG"
    Write-Host "  C++:   Clang" -ForegroundColor Green
}

if (-not $CompilerType) {
    Write-Host ""
    Write-Host "ERROR: No C++ compiler found!" -ForegroundColor Red
    Write-Host ""
    Write-Host "Install ONE of the following:" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  Option A - Visual Studio Build Tools (recommended):" -ForegroundColor Cyan
    Write-Host "    winget install Microsoft.VisualStudio.2022.BuildTools" -ForegroundColor White
    Write-Host "    Then run the VS Installer and add 'Desktop development with C++'" -ForegroundColor Gray
    Write-Host ""
    Write-Host "  Option B - MinGW-w64 via MSYS2:" -ForegroundColor Cyan
    Write-Host "    winget install MSYS2.MSYS2" -ForegroundColor White
    Write-Host "    Then in MSYS2:  pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja" -ForegroundColor Gray
    Write-Host ""
    exit 1
}

# Check for Ninja (preferred generator for speed)
$HasNinja = Test-Command "ninja"
if ($HasNinja) {
    Write-Host "  ninja: OK" -ForegroundColor Green
} else {
    Write-Host "  ninja: not found (will use fallback generator)" -ForegroundColor Gray
}

Write-Host ""

# ── Determine CMake generator ───────────────────────────────────────────────

if (-not $Generator) {
    switch ($CompilerType) {
        { $_ -match "MSVC" } {
            if ($HasNinja) {
                $Generator = "Ninja"
            } else {
                # Use VS generator — need to find the version
                $vsVersion = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
                    -latest -property catalog_productLineVersion 2>$null
                if ($vsVersion -eq "2022") {
                    $Generator = "Visual Studio 17 2022"
                } elseif ($vsVersion -eq "2019") {
                    $Generator = "Visual Studio 16 2019"
                } else {
                    $Generator = "Ninja"
                }
            }
        }
        "MINGW"  { $Generator = if ($HasNinja) { "Ninja" } else { "MinGW Makefiles" } }
        "CLANG"  { $Generator = if ($HasNinja) { "Ninja" } else { "Unix Makefiles" } }
    }
}

Write-Host "Using generator: $Generator" -ForegroundColor Cyan
Write-Host ""

# ── Set up MSVC environment if needed ────────────────────────────────────────

if ($VsDevCmd -and $CompilerType -match "MSVC" -and -not (Test-Command "cl")) {
    Write-Host "Setting up MSVC environment..." -ForegroundColor Yellow

    # Run vcvarsall and capture the environment
    $envBefore = @{}
    Get-ChildItem env: | ForEach-Object { $envBefore[$_.Name] = $_.Value }

    cmd /c "`"$VsDevCmd`" x64 >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match "^([^=]+)=(.*)$") {
            $name = $matches[1]
            $value = $matches[2]
            if ($envBefore[$name] -ne $value) {
                [Environment]::SetEnvironmentVariable($name, $value, "Process")
            }
        }
    }

    if (Test-Command "cl") {
        Write-Host "  cl.exe now available" -ForegroundColor Green
    } else {
        Write-Host "  WARNING: Failed to set up MSVC environment" -ForegroundColor Yellow
    }
    Write-Host ""
}

# ── Install vcpkg ────────────────────────────────────────────────────────────

if (-not $VcpkgRoot -or -not (Test-Path "$VcpkgRoot\vcpkg.exe")) {
    $DefaultVcpkg = "$env:USERPROFILE\vcpkg"

    if (Test-Path "$DefaultVcpkg\vcpkg.exe") {
        $VcpkgRoot = $DefaultVcpkg
        Write-Host "Found vcpkg at $VcpkgRoot" -ForegroundColor Green
    } else {
        Write-Host "Installing vcpkg to $DefaultVcpkg..." -ForegroundColor Yellow
        git clone https://github.com/microsoft/vcpkg.git $DefaultVcpkg
        & "$DefaultVcpkg\bootstrap-vcpkg.bat" -disableMetrics
        $VcpkgRoot = $DefaultVcpkg
    }

    $env:VCPKG_ROOT = $VcpkgRoot
    [Environment]::SetEnvironmentVariable("VCPKG_ROOT", $VcpkgRoot, "User")
    Write-Host "Set VCPKG_ROOT=$VcpkgRoot" -ForegroundColor Green
} else {
    Write-Host "vcpkg found at $VcpkgRoot" -ForegroundColor Green
}
Write-Host ""

# ── Download Dear ImGui ──────────────────────────────────────────────────────

$ImGuiDir = "$ProjectDir\imgui"

if (-not (Test-Path "$ImGuiDir\imgui.cpp")) {
    Write-Host "Downloading Dear ImGui $ImGuiVersion..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Force -Path $ImGuiDir | Out-Null

    $BaseUrl = "https://raw.githubusercontent.com/ocornut/imgui/$ImGuiVersion"

    $coreFiles = @(
        "imgui.cpp", "imgui.h", "imgui_internal.h",
        "imgui_demo.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp",
        "imconfig.h", "imstb_rectpack.h", "imstb_textedit.h", "imstb_truetype.h"
    )
    $backendFiles = @(
        "imgui_impl_glfw.cpp", "imgui_impl_glfw.h",
        "imgui_impl_opengl3.cpp", "imgui_impl_opengl3.h", "imgui_impl_opengl3_loader.h"
    )

    foreach ($f in $coreFiles) {
        Write-Host "  imgui/$f"
        Invoke-WebRequest -Uri "$BaseUrl/$f" -OutFile "$ImGuiDir\$f" -UseBasicParsing
    }
    foreach ($f in $backendFiles) {
        Write-Host "  imgui/$f (backend)"
        Invoke-WebRequest -Uri "$BaseUrl/backends/$f" -OutFile "$ImGuiDir\$f" -UseBasicParsing
    }
    Write-Host "Done." -ForegroundColor Green
} else {
    Write-Host "Dear ImGui already present." -ForegroundColor Green
}
Write-Host ""

# ── Download stb_image_write ─────────────────────────────────────────────────

$StbFile = "$ProjectDir\stb\stb_image_write.h"

if ((Get-Content $StbFile -Raw -ErrorAction SilentlyContinue) -match "PLACEHOLDER") {
    Write-Host "Downloading stb_image_write.h..." -ForegroundColor Yellow
    Invoke-WebRequest `
        -Uri "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h" `
        -OutFile $StbFile -UseBasicParsing
    Write-Host "Done." -ForegroundColor Green
} else {
    Write-Host "stb_image_write.h already present." -ForegroundColor Green
}
Write-Host ""

# ── Configure & Build ────────────────────────────────────────────────────────

$BuildDir = "build\$($BuildType.ToLower())"

Write-Host "Configuring ($BuildType) with $Generator..." -ForegroundColor Yellow

$cmakeArgs = @(
    "-B", $BuildDir,
    "-G", $Generator,
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot\scripts\buildsystems\vcpkg.cmake"
)

# Add architecture for VS generators
if ($Generator -match "Visual Studio") {
    $cmakeArgs += @("-A", "x64")
}

cmake @cmakeArgs .

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "CMake configure failed!" -ForegroundColor Red
    Write-Host ""
    Write-Host "Troubleshooting:" -ForegroundColor Yellow
    Write-Host "  1. If on OneDrive: copy project to C:\Projects\stl_viewer\" -ForegroundColor Gray
    Write-Host "  2. Delete build/ folder and try again" -ForegroundColor Gray
    Write-Host "  3. Try: .\setup.ps1 -Generator 'Ninja'" -ForegroundColor Gray
    Write-Host "  4. Run from 'Developer PowerShell for VS 2022'" -ForegroundColor Gray
    exit 1
}

Write-Host ""
Write-Host "Building..." -ForegroundColor Yellow

$buildArgs = @("--build", $BuildDir)
if ($Generator -match "Visual Studio") {
    $buildArgs += @("--config", $BuildType)
}

cmake @buildArgs

if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed!" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "====================================================" -ForegroundColor Green
Write-Host "                  Build complete!                     " -ForegroundColor Green
Write-Host "====================================================" -ForegroundColor Green
Write-Host ""

# Find the exe
$exe = Get-ChildItem -Path $BuildDir -Filter "stl_viewer.exe" -Recurse | Select-Object -First 1
if ($exe) {
    $exeRel = Resolve-Path -Relative $exe.FullName
    Write-Host "Run:" -ForegroundColor Cyan
    Write-Host "  $exeRel" -ForegroundColor White
    Write-Host "  $exeRel model.stl" -ForegroundColor White
    Write-Host "  $exeRel C:\path\to\stl\folder\" -ForegroundColor White
} else {
    Write-Host "Exe location: $BuildDir" -ForegroundColor Cyan
}

Write-Host ""
Write-Host "In VS Code: F5 to debug, Ctrl+Shift+B to build" -ForegroundColor Cyan
Write-Host ""

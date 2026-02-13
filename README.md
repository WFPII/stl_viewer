# STL Viewer & Exporter

A C++ OpenGL GUI application for viewing STL files in real-time 3D and batch-exporting them as PNG images. Built with Dear ImGui, GLFW, and GLEW.

## Features

- **Real-time 3D preview** with Phong shading (ambient + diffuse + specular)
- **Mouse orbit controls** — left-drag to rotate, scroll to zoom
- **Drag & drop** STL files or folders directly onto the window
- **Batch export** — load a folder of STLs and export them all at once
- **Customizable** — model color, background, wireframe, lighting, camera angle
- **Configurable export resolution** (default 1920×1080)
- **Native file dialogs** on Windows
- **Cross-platform** — Windows, Linux, macOS

## Windows + VS Code Setup

### Prerequisites

1. **Visual Studio 2022** (or Build Tools) with "Desktop development with C++" workload
   ```powershell
   winget install Microsoft.VisualStudio.2022.BuildTools
   ```
2. **CMake** (3.21+)
   ```powershell
   winget install Kitware.CMake
   ```
3. **Git**
   ```powershell
   winget install Git.Git
   ```
4. **VS Code extensions** — open the project folder and accept the recommended extensions:
   - C/C++ (`ms-vscode.cpptools`)
   - CMake Tools (`ms-vscode.cmake-tools`)

### Quick Start (PowerShell)

```powershell
# Clone or extract the project, then:
cd stl_viewer

# Run the setup script (installs vcpkg, downloads deps, builds)
.\setup.ps1

# Or for a Release build:
.\setup.ps1 -Release
```

The setup script will:
1. Install **vcpkg** (if not already installed) and set `VCPKG_ROOT`
2. Download **Dear ImGui** and **stb_image_write**
3. Install **GLFW3** and **GLEW** via vcpkg manifest mode
4. Configure and build with CMake + MSVC

### Using VS Code

1. Open the `stl_viewer` folder in VS Code
2. Install the recommended extensions when prompted
3. CMake Tools will auto-detect the project and configure it
4. **Ctrl+Shift+B** → Build (Debug)
5. **F5** → Debug with breakpoints
6. Select `windows-release` preset in CMake Tools status bar for Release builds

### Running

```powershell
.\build\debug\Debug\stl_viewer.exe                    # Launch empty
.\build\debug\Debug\stl_viewer.exe model.stl           # Open a file
.\build\debug\Debug\stl_viewer.exe C:\Models\          # Open a folder
```

## Linux / macOS Setup

```bash
# Install dependencies
# Ubuntu:  sudo apt install cmake g++ libglfw3-dev libglew-dev libgl1-mesa-dev
# Fedora:  sudo dnf install cmake gcc-c++ glfw-devel glew-devel mesa-libGL-devel
# macOS:   brew install cmake glfw glew

chmod +x setup.sh
./setup.sh

./build/stl_viewer
```

## Controls

| Action | Control |
|--------|---------|
| Orbit camera | Left-click drag in viewport |
| Zoom | Scroll wheel |
| Open file | Ctrl+O (Windows native dialog) |
| Open folder | Ctrl+Shift+O |
| Export current | Ctrl+E |
| Export all | Ctrl+Shift+E |

## GUI Layout

```
┌──────────────────┬──────────────────────────────────┐
│  Load Files      │                                  │
│  ─────────────   │                                  │
│  Models          │        3D Viewport               │
│  ─────────────   │    (mouse orbit / zoom)          │
│  Appearance      │                                  │
│  Camera          │                                  │
│  Lighting        │                                  │
│  ─────────────   │                                  │
│  Export          │                                  │
│  ─────────────   │                                  │
│  Status bar      │                                  │
└──────────────────┴──────────────────────────────────┘
```

## Project Structure

```
stl_viewer/
├── CMakeLists.txt          # Build config (vcpkg + MSVC aware)
├── CMakePresets.json        # VS Code / CLI presets
├── vcpkg.json               # Dependency manifest
├── setup.ps1                # Windows one-click setup
├── setup.sh                 # Linux/macOS setup
├── .vscode/
│   ├── settings.json        # CMake + IntelliSense config
│   ├── launch.json          # F5 debug configurations
│   ├── tasks.json           # Ctrl+Shift+B build tasks
│   └── extensions.json      # Recommended extensions
├── src/
│   ├── main.cpp             # GUI + app logic (ImGui + GLFW)
│   ├── stl_loader.cpp       # Binary & ASCII STL parser
│   ├── renderer.cpp         # OpenGL 3.3 Phong renderer + FBO
│   └── exporter.cpp         # PNG export via stb_image_write
├── include/
│   ├── stl_loader.h
│   ├── renderer.h
│   └── exporter.h
├── imgui/                   # Downloaded by setup script
├── stb/
│   └── stb_image_write.h   # Downloaded by setup script
└── .gitignore
```

## Troubleshooting

**"cmake not found"**: Make sure CMake is in your PATH. Restart your terminal after installing.

**"No C++ compiler detected"**: Install Visual Studio 2022 Build Tools with the C++ workload, or ensure `cl.exe` is on your PATH (run from "Developer Command Prompt").

**vcpkg issues**: Delete `vcpkg_installed/` and `build/` and re-run `setup.ps1`. Check that `$env:VCPKG_ROOT` is set correctly.

**IntelliSense errors in VS Code**: Select the CMake kit (status bar) → choose the Visual Studio compiler. Run "CMake: Configure" from the command palette.

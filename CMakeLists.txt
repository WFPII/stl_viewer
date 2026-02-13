cmake_minimum_required(VERSION 3.21)

# ── vcpkg toolchain (auto-detected from VCPKG_ROOT or CMAKE_TOOLCHAIN_FILE) ─
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain file")
endif()

project(STLViewer VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies (via vcpkg) ─────────────────────────────────────────────────
find_package(OpenGL REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(GLEW CONFIG REQUIRED)

# ── Dear ImGui (vendored in imgui/) ──────────────────────────────────────────
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/imgui_impl_opengl3.cpp
)

# ── Main executable ──────────────────────────────────────────────────────────
add_executable(stl_viewer
    src/main.cpp
    src/stl_loader.cpp
    src/renderer.cpp
    src/exporter.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(stl_viewer PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/stb
    ${IMGUI_DIR}
)

target_link_libraries(stl_viewer PRIVATE
    OpenGL::GL
    glfw
    GLEW::GLEW
)

# ── Platform-specific ────────────────────────────────────────────────────────
if(WIN32)
    target_link_libraries(stl_viewer PRIVATE comdlg32 ole32 shell32)
    # Use WinMain entry on Release, console on Debug (for logging)
    set_target_properties(stl_viewer PROPERTIES
        WIN32_EXECUTABLE $<CONFIG:Release>
    )
    # Copy DLLs next to exe automatically
    add_custom_command(TARGET stl_viewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:stl_viewer> $<TARGET_FILE_DIR:stl_viewer>
        COMMAND_EXPAND_LISTS
    )
elseif(APPLE)
    find_library(COCOA_LIBRARY Cocoa)
    target_link_libraries(stl_viewer PRIVATE ${COCOA_LIBRARY})
endif()

# Suppress MSVC warnings for third-party code
if(MSVC)
    target_compile_options(stl_viewer PRIVATE /W3 /wd4244 /wd4267 /wd4996)
    target_compile_definitions(stl_viewer PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(stl_viewer PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ── Install ──────────────────────────────────────────────────────────────────
install(TARGETS stl_viewer RUNTIME DESTINATION bin)

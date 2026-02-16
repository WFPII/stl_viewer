/*
 * STL Viewer & Exporter
 * =====================
 * A C++ GUI application for viewing and exporting STL files to PNG images.
 *
 * Features:
 *   - Real-time 3D preview with Phong shading
 *   - Mouse orbit/zoom controls
 *   - Load single files or entire folders
 *   - Batch export all loaded STLs to PNG
 *   - Adjustable colors, lighting, camera, resolution
 *   - Wireframe overlay toggle
 *
 * Controls:
 *   - Left-click drag:  Orbit camera
 *   - Scroll wheel:     Zoom in/out
 *   - Ctrl+O:           Open file
 *   - Ctrl+Shift+O:     Open folder
 *   - Ctrl+E:           Export current
 *   - Ctrl+Shift+E:     Export all
 */

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#endif

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "stl_loader.h"
#include "renderer.h"
#include "exporter.h"

#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;

// Global panel width — updated by drawUI, read by main loop
float g_panelWidth = 320.0f;

// ── App State ───────────────────────────────────────────────────────────────

struct AppState {
    std::vector<STLModel>  models;
    int                    currentModel   = -1;
    RenderSettings         settings;
    Renderer               renderer;

    // File browser state
    char inputPath[512]    = "";
    char outputDir[512]    = "";
    bool recursive         = false;
    bool exportToSourceDir = true;  // Export PNGs next to their source STL files

    // Mouse orbit
    bool   dragging        = false;
    double lastMouseX      = 0, lastMouseY = 0;

    // Status
    std::string statusMsg  = "Ready. Load an STL file or folder to begin.";
    int         exportedCount = 0;
    int         totalToExport = 0;
    bool        exporting     = false;
};

// ── Native file dialogs (cross-platform) ────────────────────────────────────

#ifdef _WIN32

static bool g_comInitialized = false;

static void ensureComInit() {
    if (!g_comInitialized) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        g_comInitialized = true;
    }
}

static std::string nativeOpenFile() {
    ensureComInit();
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = "STL Files\0*.stl\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) return std::string(filename);
    return "";
}

static std::string nativeOpenFolder() {
    ensureComInit();
    char path[MAX_PATH] = "";
    BROWSEINFOA bi{};
    bi.hwndOwner = nullptr;
    bi.lpszTitle = "Select STL Folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        if (SHGetPathFromIDListA(pidl, path)) {
            CoTaskMemFree(pidl);
            return std::string(path);
        }
        CoTaskMemFree(pidl);
    }
    return "";
}

static std::string nativeSaveFile(const std::string& defaultName) {
    ensureComInit();
    char filename[MAX_PATH];
    strncpy(filename, defaultName.c_str(), MAX_PATH - 1);
    filename[MAX_PATH - 1] = '\0';
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = "PNG Files\0*.png\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = "png";
    if (GetSaveFileNameA(&ofn)) return std::string(filename);
    return "";
}
#else
// On Linux/macOS, fall back to typed paths in the GUI (or zenity if available)
static std::string nativeOpenFile() { return ""; }
static std::string nativeOpenFolder() { return ""; }
static std::string nativeSaveFile(const std::string&) { return ""; }
#endif

// ── Load helpers ────────────────────────────────────────────────────────────

static void loadSingleFile(AppState& app, const std::string& path) {
    STLModel model;
    if (model.load(path)) {
        app.models.push_back(std::move(model));
        app.currentModel = (int)app.models.size() - 1;
        app.renderer.uploadModel(app.models[app.currentModel]);
        app.statusMsg = "Loaded: " + app.models.back().filename +
                        " (" + std::to_string(app.models.back().triangles.size()) + " triangles)";
    } else {
        app.statusMsg = "Failed to load: " + path;
    }
}

static void loadFolder(AppState& app, const std::string& dir, bool recursive) {
    auto files = findSTLFiles(dir, recursive);
    if (files.empty()) {
        app.statusMsg = "No STL files found in: " + dir;
        return;
    }

    int loaded = 0;
    for (const auto& f : files) {
        STLModel model;
        if (model.load(f)) {
            app.models.push_back(std::move(model));
            loaded++;
        }
    }

    if (loaded > 0) {
        app.currentModel = (int)app.models.size() - 1;
        app.renderer.uploadModel(app.models[app.currentModel]);
        app.statusMsg = "Loaded " + std::to_string(loaded) + " of " +
                        std::to_string(files.size()) + " STL files from: " + dir;
    } else {
        app.statusMsg = "Failed to load any files from: " + dir;
    }
}

// ── Export helpers ──────────────────────────────────────────────────────────

static void exportCurrent(AppState& app) {
    if (app.currentModel < 0 || app.currentModel >= (int)app.models.size()) return;

    auto& model = app.models[app.currentModel];

    std::string outPath;
    if (app.exportToSourceDir) {
        // Export next to the original STL file
        outPath = Exporter::deriveOutputPath(model.fullpath, "");
    } else {
        outPath = Exporter::deriveOutputPath(model.fullpath, app.outputDir);
    }

    // Try native save-as dialog on Windows
    std::string nativePath = nativeSaveFile(outPath);
    if (!nativePath.empty()) outPath = nativePath;

    std::vector<unsigned char> pixels;
    if (app.renderer.renderToBuffer(model, app.settings,
                                     app.settings.exportWidth, app.settings.exportHeight,
                                     pixels)) {
        if (Exporter::savePNG(outPath, app.settings.exportWidth, app.settings.exportHeight, pixels)) {
            app.statusMsg = "Exported: " + outPath;
        } else {
            app.statusMsg = "Export failed: " + outPath;
        }
    }

    // Re-upload current model for viewport display
    app.renderer.uploadModel(app.models[app.currentModel]);
}

static void exportAll(AppState& app) {
    if (app.models.empty()) return;

    app.exporting = true;
    app.exportedCount = 0;
    app.totalToExport = (int)app.models.size();

    int success = 0, failed = 0;

    for (size_t i = 0; i < app.models.size(); ++i) {
        auto& model = app.models[i];

        std::string outPath;
        if (app.exportToSourceDir) {
            outPath = Exporter::deriveOutputPath(model.fullpath, "");
        } else {
            outPath = Exporter::deriveOutputPath(model.fullpath, app.outputDir);
        }

        std::vector<unsigned char> pixels;
        if (app.renderer.renderToBuffer(model, app.settings,
                                         app.settings.exportWidth, app.settings.exportHeight,
                                         pixels)) {
            if (Exporter::savePNG(outPath, app.settings.exportWidth, app.settings.exportHeight, pixels)) {
                success++;
            } else {
                failed++;
            }
        } else {
            failed++;
        }
        app.exportedCount = (int)i + 1;
    }

    // Restore viewport model
    if (app.currentModel >= 0 && app.currentModel < (int)app.models.size()) {
        app.renderer.uploadModel(app.models[app.currentModel]);
    }

    app.exporting = false;
    app.statusMsg = "Batch export: " + std::to_string(success) + " exported, " +
                    std::to_string(failed) + " failed";
}

// ── Mouse orbit handling (polled in main loop, not via callbacks) ────────────
// We avoid GLFW callbacks for mouse/scroll because ImGui installs its own
// callbacks via ImGui_ImplGlfw_InitForOpenGL(window, true). Overwriting them
// prevents ImGui from receiving input, which breaks all buttons/sliders/etc.

static void handleMouseInput(GLFWwindow* window, AppState& app) {
    ImGuiIO& io = ImGui::GetIO();

    // Don't orbit if ImGui wants the mouse (hovering panel, using widget, etc.)
    if (io.WantCaptureMouse) {
        app.dragging = false;
        return;
    }

    // Left mouse button for orbiting
    bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);

    if (leftDown) {
        if (app.dragging) {
            double dx = mouseX - app.lastMouseX;
            double dy = mouseY - app.lastMouseY;
            app.settings.azimuth   += (float)dx * 0.3f;
            app.settings.elevation += (float)dy * 0.3f;
            app.settings.elevation = std::clamp(app.settings.elevation, -89.0f, 89.0f);
        }
        app.dragging = true;
    } else {
        app.dragging = false;
    }
    app.lastMouseX = mouseX;
    app.lastMouseY = mouseY;

    // Scroll zoom (read from ImGui's captured scroll data when not over UI)
    float scrollY = io.MouseWheel;
    if (scrollY != 0.0f) {
        app.settings.distance -= scrollY * 0.3f;
        app.settings.distance = std::clamp(app.settings.distance, 0.5f, 20.0f);
    }
}

static void dropCallback(GLFWwindow* window, int count, const char** paths) {
    auto* app = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    for (int i = 0; i < count; ++i) {
        std::string path = paths[i];
        if (fs::is_directory(path)) {
            loadFolder(*app, path, app->recursive);
        } else {
            auto ext = fs::path(path).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".stl") {
                loadSingleFile(*app, path);
            }
        }
    }
}

// ── ImGui UI ────────────────────────────────────────────────────────────────

static void drawUI(AppState& app) {
    // Get the full window size for the panel
    ImGuiIO& io = ImGui::GetIO();
    float windowHeight = io.DisplaySize.y;

    // Side panel — fixed to left, full height
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(0, windowHeight), ImGuiCond_Always); // 0 width = auto-fit
    ImGui::SetNextWindowSizeConstraints(ImVec2(320, windowHeight), ImVec2(480, windowHeight));

    ImGuiWindowFlags panelFlags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_AlwaysVerticalScrollbar;
    ImGui::Begin("STL Viewer", nullptr, panelFlags);

    ImGui::Text("STL Viewer & Exporter");
    ImGui::Separator();
    ImGui::Spacing();

    // ── File Loading ────────────────────────────────────
    if (ImGui::CollapsingHeader("Load Files", ImGuiTreeNodeFlags_DefaultOpen)) {

        // Browse File button — opens native file picker and loads immediately
        if (ImGui::Button("Browse File...", ImVec2(-1, 0))) {
            std::string path = nativeOpenFile();
            if (!path.empty()) {
                strncpy(app.inputPath, path.c_str(), sizeof(app.inputPath) - 1);
                loadSingleFile(app, path);
            }
        }

        // Browse Folder button — opens native folder picker and loads immediately
        if (ImGui::Button("Browse Folder...", ImVec2(-1, 0))) {
            std::string dir = nativeOpenFolder();
            if (!dir.empty()) {
                strncpy(app.inputPath, dir.c_str(), sizeof(app.inputPath) - 1);
                loadFolder(app, dir, app.recursive);
            }
        }

        ImGui::Checkbox("Include subfolders", &app.recursive);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Manual path entry (fallback)
        ImGui::TextDisabled("Or enter path manually:");
        ImGui::InputText("##path", app.inputPath, sizeof(app.inputPath));

        if (ImGui::Button("Load File", ImVec2(145, 0))) {
            std::string path(app.inputPath);
            if (!path.empty()) loadSingleFile(app, path);
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Folder", ImVec2(145, 0))) {
            std::string dir(app.inputPath);
            if (!dir.empty()) loadFolder(app, dir, app.recursive);
        }

        ImGui::Spacing();
        ImGui::TextWrapped("Tip: You can also drag & drop STL files or folders onto the window.");
    }

    ImGui::Separator();

    // ── Model List ──────────────────────────────────────
    if (ImGui::CollapsingHeader("Models", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("%d model(s) loaded", (int)app.models.size());

        if (ImGui::BeginListBox("##models", ImVec2(-1, 150))) {
            for (int i = 0; i < (int)app.models.size(); ++i) {
                bool selected = (i == app.currentModel);
                std::string label = app.models[i].filename +
                    " (" + std::to_string(app.models[i].triangles.size()) + " tri)";
                if (ImGui::Selectable(label.c_str(), selected)) {
                    app.currentModel = i;
                    app.renderer.uploadModel(app.models[i]);
                }
            }
            ImGui::EndListBox();
        }

        if (ImGui::Button("Clear All")) {
            app.models.clear();
            app.currentModel = -1;
            app.statusMsg = "All models cleared.";
        }
    }

    ImGui::Separator();

    // ── Appearance ──────────────────────────────────────
    if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Model Color", app.settings.modelColor);
        ImGui::ColorEdit3("Background", app.settings.bgColor);
        ImGui::Checkbox("Wireframe", &app.settings.wireframe);
        if (app.settings.wireframe) {
            ImGui::ColorEdit3("Edge Color", app.settings.edgeColor);
            ImGui::SliderFloat("Edge Width", &app.settings.edgeWidth, 0.5f, 5.0f);
        }
    }

    // ── Camera ──────────────────────────────────────────
    if (ImGui::CollapsingHeader("Camera")) {
        ImGui::SliderFloat("Elevation", &app.settings.elevation, -89.0f, 89.0f);
        ImGui::SliderFloat("Azimuth",   &app.settings.azimuth, -180.0f, 180.0f);
        ImGui::SliderFloat("Distance",  &app.settings.distance, 0.5f, 20.0f);
        ImGui::SliderFloat("FOV",       &app.settings.fov, 10.0f, 120.0f);

        if (ImGui::Button("Reset Camera")) {
            app.settings.elevation = 30.0f;
            app.settings.azimuth   = -45.0f;
            app.settings.distance  = 3.0f;
            app.settings.fov       = 45.0f;
        }
    }

    // ── Lighting ────────────────────────────────────────
    if (ImGui::CollapsingHeader("Lighting")) {
        ImGui::SliderFloat3("Light Dir", app.settings.lightDir, -1.0f, 1.0f);
        ImGui::SliderFloat("Ambient",  &app.settings.ambientStr,  0.0f, 1.0f);
        ImGui::SliderFloat("Diffuse",  &app.settings.diffuseStr,  0.0f, 1.0f);
        ImGui::SliderFloat("Specular", &app.settings.specularStr, 0.0f, 1.0f);
        ImGui::SliderFloat("Shininess", &app.settings.shininess, 1.0f, 128.0f);
    }

    ImGui::Separator();

    // ── Export ───────────────────────────────────────────
    if (ImGui::CollapsingHeader("Export", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputInt("Width",  &app.settings.exportWidth);
        ImGui::InputInt("Height", &app.settings.exportHeight);
        app.settings.exportWidth  = std::max(app.settings.exportWidth, 64);
        app.settings.exportHeight = std::max(app.settings.exportHeight, 64);

        ImGui::Spacing();

        // Export destination toggle
        ImGui::Checkbox("Save next to source STL files", &app.exportToSourceDir);

        if (!app.exportToSourceDir) {
            ImGui::InputText("##outdir", app.outputDir, sizeof(app.outputDir));
            ImGui::SameLine();
            if (ImGui::Button("Browse...##outdir")) {
                std::string dir = nativeOpenFolder();
                if (!dir.empty()) {
                    strncpy(app.outputDir, dir.c_str(), sizeof(app.outputDir) - 1);
                }
            }
        } else {
            ImGui::TextDisabled("PNGs will be saved in the same folder as each STL.");
        }

        ImGui::Spacing();

        bool hasModel = !app.models.empty() && app.currentModel >= 0;

        if (!hasModel) ImGui::BeginDisabled();
        if (ImGui::Button("Export Current", ImVec2(145, 0))) {
            exportCurrent(app);
        }
        if (!hasModel) ImGui::EndDisabled();

        ImGui::SameLine();

        if (app.models.empty()) ImGui::BeginDisabled();
        if (ImGui::Button("Export All", ImVec2(145, 0))) {
            exportAll(app);
        }
        if (app.models.empty()) ImGui::EndDisabled();

        if (app.exporting) {
            float progress = app.totalToExport > 0
                ? (float)app.exportedCount / (float)app.totalToExport : 0;
            ImGui::ProgressBar(progress);
        }
    }

    ImGui::Separator();

    // ── Status bar ──────────────────────────────────────
    ImGui::TextWrapped("%s", app.statusMsg.c_str());

    // Update the panel width for viewport calculation in main loop
    g_panelWidth = ImGui::GetWindowSize().x;

    ImGui::End();
}

// ── Entry point ─────────────────────────────────────────────────────────────
// Shared logic in appMain(); main() and WinMain() both call it.

static int appMain(int argc, char** argv) {
    // Init GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1400, 900, "STL Viewer & Exporter", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create window" << std::endl;
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // VSync

    // Init GLEW
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        return 1;
    }

    // Init ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    // Tweak ImGui style
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding   = 4.0f;
    style.FrameRounding    = 3.0f;
    style.GrabRounding     = 3.0f;
    style.WindowBorderSize = 0.0f;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // App state
    AppState app;
    if (!app.renderer.init()) {
        std::cerr << "Failed to initialize renderer" << std::endl;
        return 1;
    }

    glfwSetWindowUserPointer(window, &app);
    // Only install drop callback — ImGui handles mouse/keyboard/scroll via its own callbacks
    glfwSetDropCallback(window, dropCallback);

    // Load from command line args
    for (int i = 1; i < argc; ++i) {
        std::string path = argv[i];
        if (fs::is_directory(path)) {
            loadFolder(app, path, false);
        } else {
            loadSingleFile(app, path);
        }
    }

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Start ImGui frame (must happen before checking ImGui state)
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Handle mouse orbit/zoom (polled, not via callbacks)
        handleMouseInput(window, app);

        // Keyboard shortcuts (need NewFrame to have been called)
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) {
            if (io.KeyShift) {
                std::string dir = nativeOpenFolder();
                if (!dir.empty()) loadFolder(app, dir, app.recursive);
            } else {
                std::string file = nativeOpenFile();
                if (!file.empty()) loadSingleFile(app, file);
            }
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_E)) {
            if (io.KeyShift) exportAll(app);
            else exportCurrent(app);
        }

        drawUI(app);

        ImGui::Render();

        // Get window size
        int winW, winH;
        glfwGetFramebufferSize(window, &winW, &winH);

        // Clear the entire window first
        glViewport(0, 0, winW, winH);
        glClearColor(
            app.settings.bgColor[0],
            app.settings.bgColor[1],
            app.settings.bgColor[2],
            app.settings.bgColor[3]);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // 3D viewport (right side of the panel)
        float panelWidth = g_panelWidth;
        int vpX = (int)panelWidth;
        int vpW = winW - vpX;
        int vpH = winH;

        if (vpW > 0 && vpH > 0) {
            glViewport(vpX, 0, vpW, vpH);
            glScissor(vpX, 0, vpW, vpH);
            glEnable(GL_SCISSOR_TEST);
            glEnable(GL_DEPTH_TEST);
            app.renderer.render(app.settings, vpW, vpH);
            glDisable(GL_SCISSOR_TEST);
        }

        // Render ImGui on top (draws the panel over the left side)
        glDisable(GL_DEPTH_TEST);
        glViewport(0, 0, winW, winH);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Cleanup
    app.renderer.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

int main(int argc, char** argv) {
    return appMain(argc, argv);
}

// WinMain only needed for MSVC Release builds where WIN32_EXECUTABLE hides the console
#if defined(_MSC_VER)
int WINAPI WinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/,
                   LPSTR /*lpCmdLine*/, int /*nShowCmd*/) {
    int argc;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);

    std::vector<std::string> argStrings(argc);
    std::vector<char*> argvPtrs(argc);
    for (int i = 0; i < argc; ++i) {
        int size = WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, nullptr, 0, nullptr, nullptr);
        argStrings[i].resize(size);
        WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, argStrings[i].data(), size, nullptr, nullptr);
        argvPtrs[i] = argStrings[i].data();
    }
    LocalFree(argvW);

    return appMain(argc, argvPtrs.data());
}
#endif

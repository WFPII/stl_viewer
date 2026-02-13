#pragma once

#include "stl_loader.h"
#include <GL/glew.h>

struct RenderSettings {
    // Camera
    float elevation   = 30.0f;
    float azimuth     = -45.0f;
    float distance    = 3.0f;
    float fov         = 45.0f;

    // Colors (RGBA 0-1)
    float modelColor[4]  = {0.310f, 0.765f, 0.969f, 1.0f};   // #4FC3F7
    float bgColor[4]     = {0.118f, 0.118f, 0.180f, 1.0f};    // #1e1e2e
    float edgeColor[4]   = {0.004f, 0.341f, 0.608f, 1.0f};    // #01579B

    // Lighting
    float lightDir[3]    = {0.5f, 0.8f, 1.0f};
    float ambientStr     = 0.25f;
    float diffuseStr     = 0.70f;
    float specularStr    = 0.40f;
    float shininess      = 32.0f;

    // Display
    bool  wireframe      = false;
    float edgeWidth       = 1.0f;

    // Export
    int   exportWidth    = 1920;
    int   exportHeight   = 1080;
};

class Renderer {
public:
    bool init();
    void shutdown();

    void uploadModel(const STLModel& model);
    void render(const RenderSettings& settings, int viewportWidth, int viewportHeight);

    // Offscreen render to framebuffer for export
    bool renderToBuffer(const STLModel& model, const RenderSettings& settings,
                        int width, int height,
                        std::vector<unsigned char>& pixels);

private:
    GLuint shaderProgram = 0;
    GLuint vao = 0, vbo = 0;
    GLuint fbo = 0, rbo = 0, fboTex = 0;

    size_t currentVertexCount = 0;
    float  modelCenterX = 0, modelCenterY = 0, modelCenterZ = 0;
    float  modelSpan = 1.0f;

    // Shader uniform locations
    GLint uModel, uView, uProjection;
    GLint uModelColor, uLightDir, uViewPos;
    GLint uAmbient, uDiffuse, uSpecular, uShininess;

    bool compileShaders();
    void setupBuffers();
    void setupFBO(int width, int height);

    void setUniforms(const RenderSettings& settings, int vpWidth, int vpHeight);
};

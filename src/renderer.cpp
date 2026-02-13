#include "renderer.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <array>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ── Shader sources ──────────────────────────────────────────────────────────

static const char* vertexShaderSrc = R"(
#version 330 core
layout(location = 0) in vec3 aNormal;
layout(location = 1) in vec3 aPos;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 FragPos;
out vec3 Normal;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    Normal = mat3(transpose(inverse(uModel))) * aNormal;
    gl_Position = uProjection * uView * worldPos;
}
)";

static const char* fragmentShaderSrc = R"(
#version 330 core
in vec3 FragPos;
in vec3 Normal;

uniform vec4 uModelColor;
uniform vec3 uLightDir;
uniform vec3 uViewPos;
uniform float uAmbient;
uniform float uDiffuse;
uniform float uSpecular;
uniform float uShininess;

out vec4 FragColor;

void main() {
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(uLightDir);

    // Ambient
    vec3 ambient = uAmbient * uModelColor.rgb;

    // Diffuse (two-sided)
    float diff = max(abs(dot(norm, lightDir)), 0.0);
    vec3 diffuse = uDiffuse * diff * uModelColor.rgb;

    // Specular
    vec3 viewDir = normalize(uViewPos - FragPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(abs(dot(norm, halfDir)), 0.0), uShininess);
    vec3 specular = uSpecular * spec * vec3(1.0);

    vec3 result = ambient + diffuse + specular;
    FragColor = vec4(result, uModelColor.a);
}
)";

// ── Matrix math (minimal, no GLM dependency) ────────────────────────────────

using Mat4 = std::array<float, 16>;

static Mat4 mat4Identity() {
    Mat4 m{};
    m[0] = m[5] = m[10] = m[15] = 1.0f;
    return m;
}

static Mat4 mat4Perspective(float fovDeg, float aspect, float near, float far) {
    Mat4 m{};
    float tanHalf = std::tan(fovDeg * (float)M_PI / 360.0f);
    m[0]  = 1.0f / (aspect * tanHalf);
    m[5]  = 1.0f / tanHalf;
    m[10] = -(far + near) / (far - near);
    m[11] = -1.0f;
    m[14] = -(2.0f * far * near) / (far - near);
    return m;
}

static Mat4 mat4LookAt(float eyeX, float eyeY, float eyeZ,
                        float ctrX, float ctrY, float ctrZ,
                        float upX,  float upY,  float upZ) {
    float fx = ctrX - eyeX, fy = ctrY - eyeY, fz = ctrZ - eyeZ;
    float flen = std::sqrt(fx*fx + fy*fy + fz*fz);
    fx /= flen; fy /= flen; fz /= flen;

    // side = f × up
    float sx = fy*upZ - fz*upY, sy = fz*upX - fx*upZ, sz = fx*upY - fy*upX;
    float slen = std::sqrt(sx*sx + sy*sy + sz*sz);
    sx /= slen; sy /= slen; sz /= slen;

    // u = s × f
    float ux = sy*fz - sz*fy, uy = sz*fx - sx*fz, uz = sx*fy - sy*fx;

    Mat4 m{};
    m[0] = sx;  m[4] = sy;  m[8]  = sz;  m[12] = -(sx*eyeX + sy*eyeY + sz*eyeZ);
    m[1] = ux;  m[5] = uy;  m[9]  = uz;  m[13] = -(ux*eyeX + uy*eyeY + uz*eyeZ);
    m[2] = -fx; m[6] = -fy; m[10] = -fz; m[14] =  (fx*eyeX + fy*eyeY + fz*eyeZ);
    m[3] = 0;   m[7] = 0;   m[11] = 0;   m[15] = 1.0f;
    return m;
}

static Mat4 mat4Translate(float x, float y, float z) {
    Mat4 m = mat4Identity();
    m[12] = x; m[13] = y; m[14] = z;
    return m;
}

// ── Renderer implementation ─────────────────────────────────────────────────

bool Renderer::init() {
    if (!compileShaders()) return false;
    setupBuffers();
    return true;
}

void Renderer::shutdown() {
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (fbo) glDeleteFramebuffers(1, &fbo);
    if (rbo) glDeleteRenderbuffers(1, &rbo);
    if (fboTex) glDeleteTextures(1, &fboTex);
    if (shaderProgram) glDeleteProgram(shaderProgram);
}

bool Renderer::compileShaders() {
    auto compile = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetShaderInfoLog(s, 512, nullptr, log);
            std::cerr << "Shader compile error: " << log << std::endl;
            glDeleteShader(s);
            return 0;
        }
        return s;
    };

    GLuint vs = compile(GL_VERTEX_SHADER, vertexShaderSrc);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragmentShaderSrc);
    if (!vs || !fs) return false;

    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vs);
    glAttachShader(shaderProgram, fs);
    glLinkProgram(shaderProgram);

    GLint ok;
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(shaderProgram, 512, nullptr, log);
        std::cerr << "Program link error: " << log << std::endl;
        return false;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    // Cache uniform locations
    uModel      = glGetUniformLocation(shaderProgram, "uModel");
    uView       = glGetUniformLocation(shaderProgram, "uView");
    uProjection = glGetUniformLocation(shaderProgram, "uProjection");
    uModelColor = glGetUniformLocation(shaderProgram, "uModelColor");
    uLightDir   = glGetUniformLocation(shaderProgram, "uLightDir");
    uViewPos    = glGetUniformLocation(shaderProgram, "uViewPos");
    uAmbient    = glGetUniformLocation(shaderProgram, "uAmbient");
    uDiffuse    = glGetUniformLocation(shaderProgram, "uDiffuse");
    uSpecular   = glGetUniformLocation(shaderProgram, "uSpecular");
    uShininess  = glGetUniformLocation(shaderProgram, "uShininess");

    return true;
}

void Renderer::setupBuffers() {
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
}

void Renderer::uploadModel(const STLModel& model) {
    currentVertexCount = model.vertexCount;
    modelCenterX = model.bounds.centerX();
    modelCenterY = model.bounds.centerY();
    modelCenterZ = model.bounds.centerZ();
    modelSpan    = model.bounds.span();
    if (modelSpan < 1e-6f) modelSpan = 1.0f;

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 model.glVertices.size() * sizeof(float),
                 model.glVertices.data(),
                 GL_STATIC_DRAW);

    // Normal attribute (location 0): offset 0, stride 24 bytes
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Position attribute (location 1): offset 12, stride 24 bytes
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

void Renderer::setUniforms(const RenderSettings& s, int vpWidth, int vpHeight) {
    glUseProgram(shaderProgram);

    // Model matrix: center the model at origin, scale to unit size
    float scale = 2.0f / modelSpan;
    Mat4 model = mat4Identity();
    model[0] = model[5] = model[10] = scale;
    model[12] = -modelCenterX * scale;
    model[13] = -modelCenterY * scale;
    model[14] = -modelCenterZ * scale;

    // Camera position from spherical coordinates
    float elevRad = s.elevation * (float)M_PI / 180.0f;
    float azimRad = s.azimuth   * (float)M_PI / 180.0f;
    float eyeX = s.distance * std::cos(elevRad) * std::sin(azimRad);
    float eyeY = s.distance * std::sin(elevRad);
    float eyeZ = s.distance * std::cos(elevRad) * std::cos(azimRad);

    Mat4 view = mat4LookAt(eyeX, eyeY, eyeZ, 0, 0, 0, 0, 1, 0);

    float aspect = (float)vpWidth / (float)vpHeight;
    Mat4 proj = mat4Perspective(s.fov, aspect, 0.01f, 100.0f);

    glUniformMatrix4fv(uModel, 1, GL_FALSE, model.data());
    glUniformMatrix4fv(uView, 1, GL_FALSE, view.data());
    glUniformMatrix4fv(uProjection, 1, GL_FALSE, proj.data());

    glUniform4fv(uModelColor, 1, s.modelColor);
    glUniform3fv(uLightDir, 1, s.lightDir);
    glUniform3f(uViewPos, eyeX, eyeY, eyeZ);
    glUniform1f(uAmbient, s.ambientStr);
    glUniform1f(uDiffuse, s.diffuseStr);
    glUniform1f(uSpecular, s.specularStr);
    glUniform1f(uShininess, s.shininess);
}

void Renderer::render(const RenderSettings& s, int vpWidth, int vpHeight) {
    glViewport(0, 0, vpWidth, vpHeight);
    glEnable(GL_DEPTH_TEST);

    if (currentVertexCount == 0) return;

    setUniforms(s, vpWidth, vpHeight);

    glBindVertexArray(vao);

    // Solid pass
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)currentVertexCount);

    // Wireframe overlay
    if (s.wireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glLineWidth(s.edgeWidth);
        // Temporarily change color
        glUniform4fv(uModelColor, 1, s.edgeColor);
        glUniform1f(uAmbient, 1.0f);
        glUniform1f(uDiffuse, 0.0f);
        glUniform1f(uSpecular, 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)currentVertexCount);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    glBindVertexArray(0);
}

// ── Offscreen rendering (FBO) ───────────────────────────────────────────────

void Renderer::setupFBO(int width, int height) {
    if (fbo) {
        glDeleteFramebuffers(1, &fbo);
        glDeleteRenderbuffers(1, &rbo);
        glDeleteTextures(1, &fboTex);
    }

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &fboTex);
    glBindTexture(GL_TEXTURE_2D, fboTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTex, 0);

    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool Renderer::renderToBuffer(const STLModel& model, const RenderSettings& s,
                               int width, int height,
                               std::vector<unsigned char>& pixels) {
    setupFBO(width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Framebuffer not complete!" << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    // Upload and render
    uploadModel(model);

    // Clear the FBO
    glClearColor(s.bgColor[0], s.bgColor[1], s.bgColor[2], s.bgColor[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    render(s, width, height);

    // Read pixels
    pixels.resize(width * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // Flip vertically (OpenGL reads bottom-up)
    std::vector<unsigned char> row(width * 4);
    for (int y = 0; y < height / 2; ++y) {
        int top = y * width * 4;
        int bot = (height - 1 - y) * width * 4;
        memcpy(row.data(), &pixels[top], width * 4);
        memcpy(&pixels[top], &pixels[bot], width * 4);
        memcpy(&pixels[bot], row.data(), width * 4);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

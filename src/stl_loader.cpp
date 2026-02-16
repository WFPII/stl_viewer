#include "stl_loader.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

// ── BoundingBox ─────────────────────────────────────────────────────────────

float BoundingBox::span() const {
    float dx = maxX - minX;
    float dy = maxY - minY;
    float dz = maxZ - minZ;
    return std::max({dx, dy, dz});
}

// ── Helpers ─────────────────────────────────────────────────────────────────

static bool isBinarySTL(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) return false;

    // Read header (80 bytes) + triangle count (4 bytes)
    char header[80];
    file.read(header, 80);
    if (!file) return false;

    uint32_t numTriangles = 0;
    file.read(reinterpret_cast<char*>(&numTriangles), 4);
    if (!file) return false;

    // Check if file size matches expected binary size
    file.seekg(0, std::ios::end);
    auto fileSize = file.tellg();
    auto expectedSize = 80 + 4 + (numTriangles * 50);  // 50 bytes per triangle

    // Also check if header starts with "solid" — ambiguous, but if size matches binary, treat as binary
    bool startsWithSolid = (std::strncmp(header, "solid", 5) == 0);

    if (fileSize == static_cast<std::streampos>(expectedSize) && numTriangles > 0) {
        return true;
    }

    return !startsWithSolid;
}

static bool loadBinarySTL(const std::string& filepath, std::vector<Triangle>& triangles) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) return false;

    char header[80];
    file.read(header, 80);

    uint32_t numTriangles = 0;
    file.read(reinterpret_cast<char*>(&numTriangles), 4);

    triangles.resize(numTriangles);

    for (uint32_t i = 0; i < numTriangles; ++i) {
        file.read(reinterpret_cast<char*>(triangles[i].normal.data()), 12);
        file.read(reinterpret_cast<char*>(triangles[i].v0.data()), 12);
        file.read(reinterpret_cast<char*>(triangles[i].v1.data()), 12);
        file.read(reinterpret_cast<char*>(triangles[i].v2.data()), 12);

        uint16_t attrByteCount;
        file.read(reinterpret_cast<char*>(&attrByteCount), 2);

        if (!file) return false;
    }

    return true;
}

static bool loadASCIISTL(const std::string& filepath, std::vector<Triangle>& triangles) {
    std::ifstream file(filepath);
    if (!file) return false;

    std::string line;
    Triangle currentTri{};
    int vertexIndex = 0;

    while (std::getline(file, line)) {
        // Trim leading whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        if (line.rfind("facet normal", 0) == 0) {
            std::istringstream iss(line.substr(12));
            iss >> currentTri.normal[0] >> currentTri.normal[1] >> currentTri.normal[2];
            vertexIndex = 0;
        } else if (line.rfind("vertex", 0) == 0) {
            std::istringstream iss(line.substr(6));
            float x, y, z;
            iss >> x >> y >> z;
            if (vertexIndex == 0) currentTri.v0 = {x, y, z};
            else if (vertexIndex == 1) currentTri.v1 = {x, y, z};
            else if (vertexIndex == 2) currentTri.v2 = {x, y, z};
            vertexIndex++;
        } else if (line.rfind("endfacet", 0) == 0) {
            triangles.push_back(currentTri);
        }
    }

    return !triangles.empty();
}

// Recompute normals if they're all zero (some exporters do this)
static void fixNormals(std::vector<Triangle>& triangles) {
    for (auto& tri : triangles) {
        float len = tri.normal[0]*tri.normal[0] + tri.normal[1]*tri.normal[1] + tri.normal[2]*tri.normal[2];
        if (len < 1e-6f) {
            // Cross product of (v1-v0) x (v2-v0)
            float ux = tri.v1[0] - tri.v0[0], uy = tri.v1[1] - tri.v0[1], uz = tri.v1[2] - tri.v0[2];
            float vx = tri.v2[0] - tri.v0[0], vy = tri.v2[1] - tri.v0[1], vz = tri.v2[2] - tri.v0[2];
            float nx = uy*vz - uz*vy;
            float ny = uz*vx - ux*vz;
            float nz = ux*vy - uy*vx;
            float nlen = std::sqrt(nx*nx + ny*ny + nz*nz);
            if (nlen > 1e-6f) {
                tri.normal = {nx/nlen, ny/nlen, nz/nlen};
            }
        }
    }
}

// ── STLModel ────────────────────────────────────────────────────────────────

bool STLModel::load(const std::string& filepath) {
    filename = fs::path(filepath).filename().string();
    fullpath = fs::absolute(filepath).string();
    triangles.clear();

    bool ok;
    if (isBinarySTL(filepath)) {
        ok = loadBinarySTL(filepath, triangles);
    } else {
        ok = loadASCIISTL(filepath, triangles);
    }

    if (!ok || triangles.empty()) return false;

    fixNormals(triangles);
    computeBounds();
    buildGLData();
    return true;
}

void STLModel::computeBounds() {
    if (triangles.empty()) return;

    bounds.minX = bounds.minY = bounds.minZ =  1e30f;
    bounds.maxX = bounds.maxY = bounds.maxZ = -1e30f;

    auto updateBounds = [&](const std::array<float,3>& v) {
        bounds.minX = std::min(bounds.minX, v[0]);
        bounds.minY = std::min(bounds.minY, v[1]);
        bounds.minZ = std::min(bounds.minZ, v[2]);
        bounds.maxX = std::max(bounds.maxX, v[0]);
        bounds.maxY = std::max(bounds.maxY, v[1]);
        bounds.maxZ = std::max(bounds.maxZ, v[2]);
    };

    for (const auto& tri : triangles) {
        updateBounds(tri.v0);
        updateBounds(tri.v1);
        updateBounds(tri.v2);
    }
}

void STLModel::buildGLData() {
    // Interleaved: [nx, ny, nz, vx, vy, vz] per vertex, 3 vertices per triangle
    vertexCount = triangles.size() * 3;
    glVertices.resize(vertexCount * 6);

    size_t idx = 0;
    for (const auto& tri : triangles) {
        // Vertex 0
        glVertices[idx++] = tri.normal[0]; glVertices[idx++] = tri.normal[1]; glVertices[idx++] = tri.normal[2];
        glVertices[idx++] = tri.v0[0];     glVertices[idx++] = tri.v0[1];     glVertices[idx++] = tri.v0[2];
        // Vertex 1
        glVertices[idx++] = tri.normal[0]; glVertices[idx++] = tri.normal[1]; glVertices[idx++] = tri.normal[2];
        glVertices[idx++] = tri.v1[0];     glVertices[idx++] = tri.v1[1];     glVertices[idx++] = tri.v1[2];
        // Vertex 2
        glVertices[idx++] = tri.normal[0]; glVertices[idx++] = tri.normal[1]; glVertices[idx++] = tri.normal[2];
        glVertices[idx++] = tri.v2[0];     glVertices[idx++] = tri.v2[1];     glVertices[idx++] = tri.v2[2];
    }
}

// ── Directory scanning ──────────────────────────────────────────────────────

std::vector<std::string> findSTLFiles(const std::string& directory, bool recursive) {
    std::vector<std::string> files;

    try {
        if (recursive) {
            for (const auto& entry : fs::recursive_directory_iterator(directory)) {
                if (entry.is_regular_file()) {
                    auto ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".stl") {
                        files.push_back(entry.path().string());
                    }
                }
            }
        } else {
            for (const auto& entry : fs::directory_iterator(directory)) {
                if (entry.is_regular_file()) {
                    auto ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".stl") {
                        files.push_back(entry.path().string());
                    }
                }
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error: " << e.what() << std::endl;
    }

    std::sort(files.begin(), files.end());
    return files;
}

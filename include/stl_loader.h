#pragma once

#include <string>
#include <vector>
#include <array>
#include <cstdint>

struct Triangle {
    std::array<float, 3> normal;
    std::array<float, 3> v0, v1, v2;
};

struct BoundingBox {
    float minX, minY, minZ;
    float maxX, maxY, maxZ;

    float centerX() const { return (minX + maxX) * 0.5f; }
    float centerY() const { return (minY + maxY) * 0.5f; }
    float centerZ() const { return (minZ + maxZ) * 0.5f; }
    float span()    const;
};

struct STLModel {
    std::string           filename;
    std::string           fullpath;   // Full path to the original STL file
    std::vector<Triangle> triangles;
    BoundingBox           bounds;

    // OpenGL buffer data (interleaved: normal + vertex)
    std::vector<float>    glVertices;   // nx,ny,nz, vx,vy,vz per vertex
    size_t                vertexCount = 0;

    bool load(const std::string& filepath);
    void computeBounds();
    void buildGLData();
};

// Utility: collect all .stl files in a directory (optionally recursive)
std::vector<std::string> findSTLFiles(const std::string& directory, bool recursive = false);

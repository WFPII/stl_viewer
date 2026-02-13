#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "exporter.h"

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace Exporter {

bool savePNG(const std::string& filepath,
             int width, int height,
             const std::vector<unsigned char>& pixels) {
    // Ensure output directory exists
    auto parent = fs::path(filepath).parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent);
    }

    int ok = stbi_write_png(filepath.c_str(), width, height, 4, pixels.data(), width * 4);
    if (ok) {
        auto size = fs::file_size(filepath);
        std::cout << "Exported: " << filepath << " (" << size / 1024 << " KB)" << std::endl;
    } else {
        std::cerr << "Failed to write: " << filepath << std::endl;
    }
    return ok != 0;
}

std::string deriveOutputPath(const std::string& stlPath, const std::string& outputDir) {
    fs::path p(stlPath);
    std::string stem = p.stem().string();

    if (outputDir.empty()) {
        // Save next to original
        return (p.parent_path() / (stem + ".png")).string();
    } else {
        return (fs::path(outputDir) / (stem + ".png")).string();
    }
}

} // namespace Exporter

#pragma once

#include <string>
#include <vector>

namespace Exporter {

// Save RGBA pixel buffer to PNG
bool savePNG(const std::string& filepath,
             int width, int height,
             const std::vector<unsigned char>& pixels);

// Derive output path: replace .stl extension with .png, optionally into a different directory
std::string deriveOutputPath(const std::string& stlPath,
                             const std::string& outputDir = "");

} // namespace Exporter

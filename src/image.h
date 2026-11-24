// Image decoding (JPEG/PNG/GIF/BMP via stb_image, WebP via libwebp) to 8-bit
// gray, flattened on white and resampled to the requested size.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

bool imageInfo(const std::string &data, int &w, int &h);
bool imageDecodeGray(const std::string &data, int outW, int outH, std::vector<uint8_t> &out);

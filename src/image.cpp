#include "image.h"

#include <cstdlib>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_MAX_DIMENSIONS 8192
#include "stb_image.h"

#include "webp/decode.h"

namespace {

const long long kMaxPixels = 16LL * 1000 * 1000; // decoded RAM guard (device has 512 MB)

bool isWebp(const std::string &d)
{
    return d.size() > 12 && memcmp(d.data(), "RIFF", 4) == 0 && memcmp(d.data() + 8, "WEBP", 4) == 0;
}

// Area-average downscale / nearest upscale of a gray image.
void resample(const uint8_t *src, int sw, int sh, int dw, int dh, std::vector<uint8_t> &out)
{
    out.resize((size_t)dw * dh);
    for (int y = 0; y < dh; ++y) {
        int y0 = (int)((long long)y * sh / dh), y1 = (int)((long long)(y + 1) * sh / dh);
        if (y1 <= y0)
            y1 = y0 + 1;
        if (y1 > sh)
            y1 = sh;
        for (int x = 0; x < dw; ++x) {
            int x0 = (int)((long long)x * sw / dw), x1 = (int)((long long)(x + 1) * sw / dw);
            if (x1 <= x0)
                x1 = x0 + 1;
            if (x1 > sw)
                x1 = sw;
            unsigned sum = 0;
            for (int yy = y0; yy < y1; ++yy) {
                const uint8_t *row = src + (size_t)yy * sw;
                for (int xx = x0; xx < x1; ++xx)
                    sum += row[xx];
            }
            out[(size_t)y * dw + x] = (uint8_t)(sum / (unsigned)((y1 - y0) * (x1 - x0)));
        }
    }
}

} // namespace

bool imageInfo(const std::string &data, int &w, int &h)
{
    if (isWebp(data))
        return WebPGetInfo((const uint8_t *)data.data(), data.size(), &w, &h) != 0;
    int comp;
    return stbi_info_from_memory((const stbi_uc *)data.data(), (int)data.size(), &w, &h, &comp) != 0;
}

bool imageDecodeGray(const std::string &data, int outW, int outH, std::vector<uint8_t> &out)
{
    if (outW <= 0 || outH <= 0 || data.empty())
        return false;
    int w = 0, h = 0;
    if (!imageInfo(data, w, h) || w <= 0 || h <= 0 || (long long)w * h > kMaxPixels)
        return false;

    std::vector<uint8_t> gray((size_t)w * h);
    if (isWebp(data)) {
        uint8_t *rgba = WebPDecodeRGBA((const uint8_t *)data.data(), data.size(), &w, &h);
        if (!rgba)
            return false;
        for (size_t i = 0, n = (size_t)w * h; i < n; ++i) {
            const uint8_t *p = rgba + i * 4;
            unsigned lum = (p[0] * 77u + p[1] * 150u + p[2] * 29u) >> 8;
            gray[i] = (uint8_t)((lum * p[3] + 255u * (255u - p[3])) / 255u);
        }
        WebPFree(rgba);
    } else {
        int comp = 0;
        stbi_uc *ga = stbi_load_from_memory((const stbi_uc *)data.data(), (int)data.size(), &w, &h, &comp, 2);
        if (!ga)
            return false;
        for (size_t i = 0, n = (size_t)w * h; i < n; ++i) {
            unsigned lum = ga[i * 2], a = ga[i * 2 + 1];
            gray[i] = (uint8_t)((lum * a + 255u * (255u - a)) / 255u);
        }
        stbi_image_free(ga);
    }
    resample(gray.data(), w, h, outW, outH, out);
    return true;
}

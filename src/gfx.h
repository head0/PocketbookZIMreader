// Thin drawing layer. Implemented by pb_main.cpp (inkview) on the device and
// by host/host_main.cpp (stb_truetype -> PNG) for testing on a PC.
#pragma once
#include <cstdint>

enum FontFace { FACE_REG = 0, FACE_BOLD = 1, FACE_ITALIC = 2, FACE_BOLDITALIC = 3, FACE_MONO = 4 };

int gfxWidth();
int gfxHeight();
void gfxFont(int face, int px);
int gfxTextWidth(const char *utf8);
void gfxText(int x, int y, const char *utf8); // (x, y) = top-left of the line box
void gfxFill(int x, int y, int w, int h, int gray); // gray: 0 black .. 255 white
void gfxGray(int x, int y, int w, int h, const uint8_t *pixels); // 8-bit gray, row-major

// PC test harness: renders the app into PNG files using stb_truetype.
// usage: host_test <outdir> <scan-dir> [WxH] [action...]
//   actions: shot | next | prev | back | link:<n> | href:<h> | search:<q> | menu:<id> | tap:<x>,<y>
#include "../src/app.h"
#include "../src/gfx.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static int gW = 1072, gH = 1448;
static std::vector<uint8_t> fb;
static std::vector<std::string> fontData(5);
static stbtt_fontinfo fonts[5];
static int curFace = 0, curPx = 30;

static std::string slurp(const char *p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int gfxWidth() { return gW; }
int gfxHeight() { return gH; }
void gfxFont(int face, int px)
{
    curFace = face;
    curPx = px;
}
static unsigned nextCp(const char *&s)
{
    unsigned char c = (unsigned char)*s++;
    if (c < 0x80)
        return c;
    int n = (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : 0;
    unsigned cp = c & (0x3F >> n);
    while (n-- > 0 && (*s & 0xC0) == 0x80)
        cp = (cp << 6) | ((unsigned char)*s++ & 0x3F);
    return cp;
}
int gfxTextWidth(const char *s)
{
    float sc = stbtt_ScaleForPixelHeight(&fonts[curFace], (float)curPx);
    float w = 0;
    while (*s) {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&fonts[curFace], (int)nextCp(s), &adv, &lsb);
        w += adv * sc;
    }
    return (int)(w + 0.5f);
}
void gfxFill(int x, int y, int w, int h, int gray)
{
    for (int yy = std::max(0, y); yy < std::min(gH, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(gW, x + w); ++xx)
            fb[(size_t)yy * gW + xx] = (uint8_t)gray;
}
void gfxGray(int x, int y, int w, int h, const uint8_t *p)
{
    for (int yy = 0; yy < h; ++yy)
        for (int xx = 0; xx < w; ++xx)
            if (y + yy >= 0 && y + yy < gH && x + xx >= 0 && x + xx < gW)
                fb[(size_t)(y + yy) * gW + x + xx] = p[(size_t)yy * w + xx];
}
void gfxText(int x, int y, const char *s)
{
    stbtt_fontinfo *f = &fonts[curFace];
    float sc = stbtt_ScaleForPixelHeight(f, (float)curPx);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(f, &asc, &desc, &gap);
    int base = y + curPx * 15 / 100 + (int)(asc * sc);
    float fx = (float)x;
    while (*s) {
        int cp = (int)nextCp(s);
        int adv, lsb, w, h, ox, oy;
        stbtt_GetCodepointHMetrics(f, cp, &adv, &lsb);
        unsigned char *bm = stbtt_GetCodepointBitmap(f, sc, sc, cp, &w, &h, &ox, &oy);
        for (int yy = 0; yy < h; ++yy)
            for (int xx = 0; xx < w; ++xx) {
                int px = (int)fx + ox + xx, py = base + oy + yy;
                if (px >= 0 && px < gW && py >= 0 && py < gH) {
                    uint8_t &d = fb[(size_t)py * gW + px];
                    d = (uint8_t)(d * (255 - bm[yy * w + xx]) / 255);
                }
            }
        stbtt_FreeBitmap(bm, nullptr);
        fx += adv * sc;
    }
}

struct HostPlatform : Platform {
    App *app = nullptr;
    std::string outdir;
    int shots = 0;
    void askText(const std::string &t, std::function<void(const std::string &)>) override
    {
        printf("[askText] %s\n", t.c_str());
    }
    void showMenu() override { printf("[menu]\n"); }
    void busy(bool) override {}
    void present(bool full) override { printf("[present %s] page %d/%d  title=%s\n", full ? "full" : "part", app->page() + 1, app->pageCount(), app->title().c_str()); }
    void message(const std::string &m) override { printf("[message] %s\n", m.c_str()); }
    void quit() override { printf("[quit]\n"); }
    void shot()
    {
        char name[512];
        snprintf(name, sizeof name, "%s/shot%02d.png", outdir.c_str(), shots++);
        stbi_write_png(name, gW, gH, 1, fb.data(), gW);
        printf("[shot] %s\n", name);
    }
};

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s outdir scandir [WxH] [actions]\n", argv[0]);
        return 2;
    }
    int ai = 3;
    if (ai < argc && sscanf(argv[ai], "%dx%d", &gW, &gH) == 2)
        ++ai;
    fb.assign((size_t)gW * gH, 255);
    const char *paths[5] = {
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Italic.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-BoldItalic.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    };
    for (int i = 0; i < 5; ++i) {
        fontData[i] = slurp(paths[i]);
        if (fontData[i].empty() || !stbtt_InitFont(&fonts[i], (const unsigned char *)fontData[i].data(), 0)) {
            fprintf(stderr, "font missing: %s\n", paths[i]);
            return 1;
        }
    }
    HostPlatform pf;
    pf.outdir = argv[1];
    App app(pf, std::string(argv[1]) + "/state.cfg", {argv[2]});
    pf.app = &app;
    app.start();
    for (; ai < argc; ++ai) {
        std::string a = argv[ai];
        printf("> %s\n", a.c_str());
        if (a == "shot")
            pf.shot();
        else if (a == "next")
            app.nextPage();
        else if (a == "prev")
            app.prevPage();
        else if (a == "back")
            app.back();
        else if (a.compare(0, 5, "href:") == 0)
            app.followLink(a.substr(5));
        else if (a.compare(0, 7, "search:") == 0)
            app.search(a.substr(7));
        else if (a.compare(0, 5, "menu:") == 0)
            app.menu(atoi(a.c_str() + 5));
        else if (a.compare(0, 4, "tap:") == 0) {
            int x, y;
            if (sscanf(a.c_str() + 4, "%d,%d", &x, &y) == 2)
                app.tap(x, y);
        } else if (a.compare(0, 5, "link:") == 0) {
            // tap the n-th link fragment of the current page
            int n = atoi(a.c_str() + 5), k = 0;
            bool done = false;
            for (const Frag &f : app.pages()[(size_t)app.page()].frags)
                if (f.link >= 0 && k++ == n) {
                    int W = gfxWidth(), ui = std::max(14, W / 36);
                    app.tap(W * 4 / 100 + f.x + f.w / 2, ui * 24 / 10 + ui / 4 + f.y + f.h / 2);
                    done = true;
                    break;
                }
            if (!done)
                printf("no such link\n");
        }
    }
    app.saveState();
    return 0;
}

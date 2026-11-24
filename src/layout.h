// Flows a Doc into fixed-size pages.
#pragma once
#include "html.h"

#include <functional>
#include <string>
#include <vector>

struct Frag {
    enum Kind { TEXT, IMAGE, RULE };
    Kind kind = TEXT;
    int x = 0, y = 0, w = 0, h = 0; // y is page-relative
    unsigned char face = 0;
    short px = 0;
    int link = -1;
    std::string text; // TEXT: string to draw, IMAGE: source path
};

struct Page {
    std::vector<Frag> frags;
};

struct LayoutParams {
    int width = 0, height = 0; // content box
    int basePx = 30;
    // Returns intrinsic size of an image whose HTML gave none. May fail.
    std::function<bool(const std::string &src, int &w, int &h)> imageSize;
};

void layoutDoc(const Doc &doc, const LayoutParams &p, std::vector<Page> &pages);

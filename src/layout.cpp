#include "layout.h"

#include "gfx.h"

#include <algorithm>
#include <unordered_map>

namespace {

struct Piece {
    std::string text;
    unsigned char face;
    short px;
    int link;
    int w;
};

// A unit is a run of pieces with no breaking opportunity inside.
struct Unit {
    std::vector<Piece> pieces;
    int w = 0;
    bool spaceBefore = false;
    bool newline = false; // forced break before this unit
};

struct Line {
    std::vector<Frag> frags; // y relative to line top
    int h = 0;
    bool keepWithNext = false;
};

size_t utf8Len(unsigned char c)
{
    if (c < 0x80)
        return 1;
    if ((c >> 5) == 6)
        return 2;
    if ((c >> 4) == 14)
        return 3;
    if ((c >> 3) == 30)
        return 4;
    return 1;
}

struct Flow {
    const LayoutParams &p;
    std::vector<Page> &pages;
    std::unordered_map<std::string, int> widthCache;
    int curFace = -1, curPx = -1;
    int y = 0; // cursor on current page

    Flow(const LayoutParams &pp, std::vector<Page> &pg) : p(pp), pages(pg) { pages.emplace_back(); }

    int measure(const std::string &s, int face, int px)
    {
        std::string key;
        key.reserve(s.size() + 3);
        key += (char)('0' + face);
        key += (char)(px & 0xff);
        key += (char)((px >> 8) + 1);
        key += s;
        auto it = widthCache.find(key);
        if (it != widthCache.end())
            return it->second;
        if (face != curFace || px != curPx) {
            gfxFont(face, px);
            curFace = face;
            curPx = px;
        }
        int w = gfxTextWidth(s.c_str());
        if (widthCache.size() > 60000)
            widthCache.clear();
        widthCache.emplace(std::move(key), w);
        return w;
    }

    static int lineHeight(int px) { return px + px * 3 / 10; }

    void newPage()
    {
        pages.emplace_back();
        y = 0;
    }

    void gap(int g)
    {
        if (y > 0)
            y = std::min(p.height, y + g);
    }

    void emitLines(std::vector<Line> &lines)
    {
        for (size_t i = 0; i < lines.size(); ++i) {
            Line &l = lines[i];
            int need = l.h;
            // keep a heading together with the line that follows it
            if (l.keepWithNext && i + 1 < lines.size())
                need += lines[i + 1].h;
            if (y > 0 && y + need > p.height)
                newPage();
            for (Frag &f : l.frags) {
                f.y += y;
                pages.back().frags.push_back(std::move(f));
            }
            y += l.h;
        }
        lines.clear();
    }

    void textBlock(const Block &b)
    {
        bool heading = b.type == Block::HEADING;
        int px = p.basePx;
        if (heading) {
            static const int pct[] = {150, 150, 128, 114, 105, 100, 100};
            px = p.basePx * pct[std::min(std::max(b.level, 1), 6)] / 100;
        }
        int indentPx = std::min(b.indent, 5) * p.basePx;
        int bulletW = 0;
        if (!b.bullet.empty()) {
            bulletW = measure(b.bullet, FACE_REG, px);
        }
        int left = std::max(indentPx, bulletW);
        int avail = p.width - left;
        if (avail < p.width / 3) {
            left = p.width - p.width / 3;
            avail = p.width / 3;
        }

        // 1. split spans into units
        std::vector<Unit> units;
        bool space = false, newline = false;
        Unit cur;
        auto pushUnit = [&]() {
            if (cur.pieces.empty())
                return;
            units.push_back(std::move(cur));
            cur = Unit();
        };
        for (const Span &s : b.spans) {
            int face = (s.style & ST_MONO) ? FACE_MONO
                                           : ((s.style & ST_BOLD) ? 1 : 0) | ((s.style & ST_ITALIC) ? 2 : 0);
            if (heading && face != FACE_MONO)
                face |= 1;
            int spx = face == FACE_MONO ? px * 88 / 100 : px;
            size_t i = 0;
            const std::string &t = s.text;
            while (i < t.size()) {
                if (t[i] == ' ') {
                    pushUnit();
                    space = true;
                    ++i;
                    continue;
                }
                if (t[i] == '\n') {
                    pushUnit();
                    if (newline) { // empty line
                        Unit e;
                        e.newline = true;
                        Piece pc{"", (unsigned char)face, (short)spx, -1, 0};
                        e.pieces.push_back(pc);
                        units.push_back(std::move(e));
                    }
                    newline = true;
                    space = false;
                    ++i;
                    continue;
                }
                size_t j = i;
                while (j < t.size() && t[j] != ' ' && t[j] != '\n')
                    ++j;
                Piece pc;
                pc.text = t.substr(i, j - i);
                pc.face = (unsigned char)face;
                pc.px = (short)spx;
                pc.link = s.link;
                pc.w = measure(pc.text, face, spx);
                if (cur.pieces.empty()) {
                    cur.spaceBefore = space;
                    cur.newline = newline;
                    space = newline = false;
                }
                cur.pieces.push_back(std::move(pc));
                cur.w += cur.pieces.back().w;
                i = j;
            }
        }
        pushUnit();
        if (units.empty())
            return;

        // 2. break over-long units into character chunks
        for (size_t u = 0; u < units.size(); ++u) {
            if (units[u].w <= avail)
                continue;
            Unit big = std::move(units[u]);
            std::vector<Unit> repl;
            Unit part;
            part.spaceBefore = big.spaceBefore;
            part.newline = big.newline;
            for (Piece &pc : big.pieces) {
                size_t i = 0;
                std::string chunk;
                int chunkW = 0;
                while (i < pc.text.size()) {
                    size_t n = std::min(utf8Len((unsigned char)pc.text[i]), pc.text.size() - i);
                    std::string ch = pc.text.substr(i, n);
                    int cw = measure(ch, pc.face, pc.px);
                    if (part.w + chunkW + cw > avail && (part.w + chunkW) > 0) {
                        if (!chunk.empty()) {
                            part.pieces.push_back(Piece{chunk, pc.face, pc.px, pc.link, chunkW});
                            part.w += chunkW;
                        }
                        repl.push_back(std::move(part));
                        part = Unit();
                        chunk.clear();
                        chunkW = 0;
                    }
                    chunk += ch;
                    chunkW += cw;
                    i += n;
                }
                if (!chunk.empty()) {
                    part.pieces.push_back(Piece{chunk, pc.face, pc.px, pc.link, chunkW});
                    part.w += chunkW;
                }
            }
            if (!part.pieces.empty())
                repl.push_back(std::move(part));
            units.erase(units.begin() + (long)u);
            units.insert(units.begin() + (long)u, std::make_move_iterator(repl.begin()),
                         std::make_move_iterator(repl.end()));
            u += repl.size() ? repl.size() - 1 : 0;
        }

        // 3. fill lines
        std::vector<Line> lines;
        Line line;
        int x = 0, lineMaxPx = 0;
        bool first = true;
        auto finishLine = [&]() {
            if (line.frags.empty() && lineMaxPx == 0)
                lineMaxPx = px;
            int lh = lineHeight(lineMaxPx);
            for (Frag &f : line.frags) {
                f.y = (lh - lineHeight(f.px)) / 2 + (lineMaxPx - f.px) / 3;
                f.h = lineHeight(f.px);
            }
            line.h = lh;
            line.keepWithNext = heading;
            lines.push_back(std::move(line));
            line = Line();
            x = 0;
            lineMaxPx = 0;
        };
        for (Unit &u : units) {
            int spaceW = 0;
            if (u.spaceBefore && x > 0)
                spaceW = measure(" ", u.pieces[0].face, u.pieces[0].px);
            if ((u.newline && !first) || (x > 0 && x + spaceW + u.w > avail)) {
                finishLine();
                spaceW = 0;
            }
            first = false;
            if (line.frags.empty() && lines.empty() && !b.bullet.empty()) {
                Frag bf;
                bf.x = left - bulletW;
                if (bf.x < 0)
                    bf.x = 0;
                bf.w = bulletW;
                bf.face = FACE_REG;
                bf.px = (short)px;
                bf.text = b.bullet;
                line.frags.push_back(bf);
                lineMaxPx = std::max(lineMaxPx, px);
            }
            x += spaceW;
            for (Piece &pc : u.pieces) {
                lineMaxPx = std::max<int>(lineMaxPx, pc.px);
                // merge with the previous fragment when style and link match
                if (!line.frags.empty()) {
                    Frag &pf = line.frags.back();
                    if (pf.face == pc.face && pf.px == pc.px && pf.link == pc.link &&
                        pf.x + pf.w + spaceW == left + x && !(pf.text == b.bullet && line.frags.size() == 1)) {
                        if (spaceW)
                            pf.text += ' ';
                        pf.text += pc.text;
                        pf.w += spaceW + pc.w;
                        x += pc.w;
                        spaceW = 0;
                        continue;
                    }
                }
                Frag f;
                f.x = left + x;
                f.w = pc.w;
                f.face = pc.face;
                f.px = pc.px;
                f.link = pc.link;
                f.text = pc.text;
                line.frags.push_back(std::move(f));
                x += pc.w;
                spaceW = 0;
            }
        }
        if (!line.frags.empty())
            finishLine();

        gap(heading ? p.basePx * 7 / 10 : (b.bullet.empty() ? p.basePx * 5 / 10 : p.basePx * 2 / 10));
        emitLines(lines);
        if (heading && b.level <= 2) {
            Frag r;
            r.kind = Frag::RULE;
            r.x = 0;
            r.w = p.width;
            r.y = y + 2;
            r.h = 2;
            if (y + 6 <= p.height) {
                pages.back().frags.push_back(r);
                y += 6;
            }
        }
    }

    void imageBlock(const Block &b)
    {
        int w = b.imgW, h = b.imgH;
        if ((w <= 0 || h <= 0) && (!p.imageSize || !p.imageSize(b.src, w, h)))
            return;
        if (w <= 0 || h <= 0)
            return;
        // HTML sizes are CSS pixels meant for ~96-160 dpi screens: scale up.
        double scale = std::max(1.0, p.width / 480.0);
        double dw = w * scale, dh = h * scale;
        double maxW = p.width, maxH = p.height * 0.8;
        double k = std::min(1.0, std::min(maxW / dw, maxH / dh));
        int iw = std::max(1, (int)(dw * k)), ih = std::max(1, (int)(dh * k));
        gap(p.basePx / 2);
        if (y > 0 && y + ih > p.height)
            newPage();
        Frag f;
        f.kind = Frag::IMAGE;
        f.x = (p.width - iw) / 2;
        f.y = y;
        f.w = iw;
        f.h = ih;
        f.text = b.src;
        pages.back().frags.push_back(std::move(f));
        y += ih;
        gap(p.basePx / 4);
    }

    void rule()
    {
        gap(p.basePx / 2);
        if (y + 4 > p.height)
            newPage();
        Frag r;
        r.kind = Frag::RULE;
        r.x = 0;
        r.w = p.width;
        r.y = y;
        r.h = 2;
        pages.back().frags.push_back(r);
        y += 4;
    }
};

} // namespace

void layoutDoc(const Doc &doc, const LayoutParams &p, std::vector<Page> &pages)
{
    pages.clear();
    Flow f(p, pages);
    for (const Block &b : doc.blocks) {
        switch (b.type) {
        case Block::PARA:
        case Block::HEADING:
            f.textBlock(b);
            break;
        case Block::IMAGE:
            f.imageBlock(b);
            break;
        case Block::RULE:
            f.rule();
            break;
        }
    }
    while (pages.size() > 1 && pages.back().frags.empty())
        pages.pop_back();
}

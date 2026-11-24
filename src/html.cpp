#include "html.h"

#include <cstdlib>
#include <cstring>

namespace {

struct Ent {
    const char *name;
    unsigned cp;
};
const Ent kEntities[] = {
    {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0},
    {"ndash", 0x2013}, {"mdash", 0x2014}, {"hellip", 0x2026}, {"laquo", 0xAB}, {"raquo", 0xBB},
    {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"bull", 0x2022},
    {"middot", 0xB7}, {"copy", 0xA9}, {"reg", 0xAE}, {"trade", 0x2122}, {"deg", 0xB0},
    {"times", 0xD7}, {"divide", 0xF7}, {"plusmn", 0xB1}, {"minus", 0x2212}, {"euro", 0x20AC},
    {"pound", 0xA3}, {"sect", 0xA7}, {"para", 0xB6}, {"thinsp", 0x2009}, {"ensp", 0x2002},
    {"emsp", 0x2003}, {"larr", 0x2190}, {"rarr", 0x2192}, {"uarr", 0x2191}, {"darr", 0x2193},
    {"harr", 0x2194}, {"prime", 0x2032}, {"Prime", 0x2033}, {"frac12", 0xBD}, {"frac14", 0xBC},
    {"frac34", 0xBE}, {"sup2", 0xB2}, {"sup3", 0xB3}, {"micro", 0xB5}, {"infin", 0x221E},
    {"ne", 0x2260}, {"le", 0x2264}, {"ge", 0x2265}, {"asymp", 0x2248}, {"dagger", 0x2020},
    {"eacute", 0xE9}, {"egrave", 0xE8}, {"ecirc", 0xEA}, {"euml", 0xEB}, {"agrave", 0xE0},
    {"acirc", 0xE2}, {"auml", 0xE4}, {"aacute", 0xE1}, {"ccedil", 0xE7}, {"icirc", 0xEE},
    {"iuml", 0xEF}, {"iacute", 0xED}, {"ocirc", 0xF4}, {"ouml", 0xF6}, {"oacute", 0xF3},
    {"ugrave", 0xF9}, {"ucirc", 0xFB}, {"uuml", 0xFC}, {"uacute", 0xFA}, {"ntilde", 0xF1},
    {"oelig", 0x153}, {"aelig", 0xE6}, {"szlig", 0xDF}, {"Eacute", 0xC9}, {"Egrave", 0xC8},
    {"Ecirc", 0xCA}, {"Agrave", 0xC0}, {"Acirc", 0xC2}, {"Ccedil", 0xC7}, {"Icirc", 0xCE},
    {"Ocirc", 0xD4}, {"Ouml", 0xD6}, {"Uuml", 0xDC}, {"Auml", 0xC4}, {"OElig", 0x152},
    {"alpha", 0x3B1}, {"beta", 0x3B2}, {"gamma", 0x3B3}, {"delta", 0x3B4}, {"pi", 0x3C0},
    {"mu", 0x3BC}, {"sigma", 0x3C3}, {"omega", 0x3C9}, {"lambda", 0x3BB}, {"theta", 0x3B8},
    {"shy", 0}, {"zwj", 0}, {"zwnj", 0}, {"lrm", 0}, {"rlm", 0},
};

void appendUtf8(std::string &s, unsigned cp)
{
    if (cp == 0)
        return;
    if (cp < 0x80)
        s += (char)cp;
    else if (cp < 0x800) {
        s += (char)(0xC0 | (cp >> 6));
        s += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += (char)(0xE0 | (cp >> 12));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x110000) {
        s += (char)(0xF0 | (cp >> 18));
        s += (char)(0x80 | ((cp >> 12) & 0x3F));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    }
}

// Decodes entities in [p, e) and appends to out.
void decodeEntities(const char *p, const char *e, std::string &out)
{
    while (p < e) {
        if (*p != '&') {
            const char *q = (const char *)memchr(p, '&', (size_t)(e - p));
            if (!q)
                q = e;
            out.append(p, (size_t)(q - p));
            p = q;
            continue;
        }
        const char *semi = p + 1;
        while (semi < e && semi - p < 12 && *semi != ';' && *semi != '&' && *semi != ' ')
            ++semi;
        if (semi < e && *semi == ';') {
            if (p[1] == '#') {
                unsigned cp = (p[2] == 'x' || p[2] == 'X') ? (unsigned)strtoul(p + 3, nullptr, 16)
                                                           : (unsigned)strtoul(p + 2, nullptr, 10);
                if (cp == 0xAD || cp == 0x200B || cp == 0x200E || cp == 0x200F || cp == 0xFEFF)
                    cp = 0;
                appendUtf8(out, cp);
                p = semi + 1;
                continue;
            }
            size_t n = (size_t)(semi - p - 1);
            bool found = false;
            for (const Ent &en : kEntities)
                if (strlen(en.name) == n && memcmp(en.name, p + 1, n) == 0) {
                    appendUtf8(out, en.cp);
                    found = true;
                    break;
                }
            if (found) {
                p = semi + 1;
                continue;
            }
        }
        out += '&';
        ++p;
    }
}

bool isSpace(char c) { return c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\f'; }

struct Tag {
    std::string name;
    bool closing = false;
    bool selfClosing = false;
    const char *attrBegin = nullptr, *attrEnd = nullptr;

    bool attr(const char *key, std::string &value) const
    {
        value.clear();
        size_t klen = strlen(key);
        const char *p = attrBegin;
        while (p < attrEnd) {
            while (p < attrEnd && (isSpace(*p) || *p == '/'))
                ++p;
            const char *ns = p;
            while (p < attrEnd && !isSpace(*p) && *p != '=' && *p != '/')
                ++p;
            const char *ne = p;
            while (p < attrEnd && isSpace(*p))
                ++p;
            const char *vs = p, *ve = p;
            if (p < attrEnd && *p == '=') {
                ++p;
                while (p < attrEnd && isSpace(*p))
                    ++p;
                if (p < attrEnd && (*p == '"' || *p == '\'')) {
                    char q = *p++;
                    vs = p;
                    while (p < attrEnd && *p != q)
                        ++p;
                    ve = p;
                    if (p < attrEnd)
                        ++p;
                } else {
                    vs = p;
                    while (p < attrEnd && !isSpace(*p))
                        ++p;
                    ve = p;
                }
            }
            if ((size_t)(ne - ns) == klen && strncasecmp(ns, key, klen) == 0) {
                decodeEntities(vs, ve, value);
                return true;
            }
            if (ne == ns && p < attrEnd)
                ++p;
        }
        return false;
    }
};

bool hasWord(const std::string &hay, const char *word)
{
    size_t n = strlen(word), pos = 0;
    while ((pos = hay.find(word, pos)) != std::string::npos) {
        bool l = pos == 0 || isSpace(hay[pos - 1]);
        bool r = pos + n == hay.size() || isSpace(hay[pos + n]);
        if (l && r)
            return true;
        pos += n;
    }
    return false;
}

bool in(const std::string &n, const char *const *list)
{
    for (; *list; ++list)
        if (n == *list)
            return true;
    return false;
}

const char *const kVoid[] = {"br", "hr", "img", "meta", "link", "input", "wbr", "source", "area",
                             "col", "base", "embed", "track", "param", nullptr};
const char *const kSkip[] = {"script", "style", "noscript", "svg", "math", "template", "select",
                             "button", "audio", "video", "iframe", "object", "form", "textarea",
                             "map", "canvas", nullptr};
const char *const kBlock[] = {"p", "div", "section", "article", "header", "footer", "ul", "ol",
                              "dl", "table", "tr", "blockquote", "figure", "figcaption", "details",
                              "summary", "center", "address", "nav", "aside", "main", "body",
                              "html", "caption", "thead", "tbody", "tfoot", "fieldset", "hgroup",
                              nullptr};
const char *const kSkipClasses[] = {"noprint", "navbox", "mw-editsection", "mw-empty-elt",
                                    "navigation-not-searchable", "mw-jump-link", "noexcerpt",
                                    "sistersitebox", "nomobile", nullptr};

struct Open {
    std::string name;
    bool skip = false;
    unsigned char style = 0; // style bits this element added
    bool link = false;
    bool pre = false;
    bool list = false;
    bool indent = false;
    bool row = false;
    bool heading = false;
};

struct Parser {
    Doc &doc;
    std::vector<Open> stack;
    std::vector<int> listCounters; // -1 = unordered
    Block cur;
    bool pendingSpace = false;
    int skipDepth = 0, preDepth = 0, indent = 0, headingLevel = 0, rowDepth = 0;
    int bold = 0, italic = 0, mono = 0;
    int link = -1;
    bool inTitle = false, inHead = false;
    std::string pendingBullet;

    explicit Parser(Doc &d) : doc(d) {}

    unsigned char style() const
    {
        return (unsigned char)((bold > 0 || headingLevel ? ST_BOLD : 0) | (italic > 0 ? ST_ITALIC : 0) |
                               (mono > 0 || preDepth > 0 ? ST_MONO : 0));
    }

    bool curEmpty() const
    {
        for (auto &s : cur.spans)
            for (char c : s.text)
                if (c != ' ' && c != '\n')
                    return false;
        return true;
    }

    void flush()
    {
        if (!curEmpty()) {
            // trim trailing whitespace / breaks
            while (!cur.spans.empty()) {
                std::string &t = cur.spans.back().text;
                while (!t.empty() && (t.back() == ' ' || t.back() == '\n'))
                    t.pop_back();
                if (t.empty())
                    cur.spans.pop_back();
                else
                    break;
            }
            cur.type = headingLevel ? Block::HEADING : Block::PARA;
            cur.level = headingLevel;
            cur.indent = indent;
            cur.bullet = pendingBullet;
            pendingBullet.clear();
            doc.blocks.push_back(std::move(cur));
        }
        cur = Block();
        pendingSpace = false;
    }

    void addText(const std::string &t, bool raw = false)
    {
        if (t.empty())
            return;
        unsigned char st = style();
        if (cur.spans.empty() || cur.spans.back().style != st || cur.spans.back().link != link) {
            Span s;
            s.style = st;
            s.link = link;
            cur.spans.push_back(s);
        }
        std::string &dst = cur.spans.back().text;
        if (raw) {
            dst += t;
            return;
        }
        for (char c : t) {
            if (isSpace(c)) {
                pendingSpace = true;
                continue;
            }
            if (pendingSpace) {
                if (!curEmpty() || !dst.empty())
                    dst += ' ';
                pendingSpace = false;
            }
            dst += c;
        }
    }

    void text(const char *p, const char *e)
    {
        if (inTitle) {
            decodeEntities(p, e, doc.title);
            return;
        }
        if (skipDepth > 0 || inHead)
            return;
        std::string t;
        decodeEntities(p, e, t);
        if (preDepth > 0) {
            // keep line structure, expand tabs
            std::string line;
            for (char c : t) {
                if (c == '\n') {
                    addText(line, true);
                    addText("\n", true);
                    line.clear();
                } else if (c == '\t')
                    line += "    ";
                else if (c == ' ')
                    line += "\xC2\xA0"; // keep runs of spaces
                else if (c != '\r')
                    line += c;
            }
            addText(line, true);
        } else {
            addText(t);
        }
    }

    void image(const Tag &t)
    {
        std::string src, alt, w, h, cls;
        t.attr("src", src);
        t.attr("alt", alt);
        t.attr("width", w);
        t.attr("height", h);
        t.attr("class", cls);
        if (src.empty())
            t.attr("data-src", src);
        int iw = atoi(w.c_str()), ih = atoi(h.c_str());
        bool svg = src.size() > 4 && src.find(".svg") != std::string::npos &&
                   src.find(".svg.png") == std::string::npos && src.find(".svg.webp") == std::string::npos;
        bool math = cls.find("math") != std::string::npos;
        if (math || svg || src.compare(0, 5, "data:") == 0 || src.empty()) {
            if (math && !alt.empty()) {
                // LaTeX source: strip the {\displaystyle ...} wrapper
                if (alt.compare(0, 15, "{\\displaystyle ") == 0 && alt.back() == '}')
                    alt = alt.substr(15, alt.size() - 16);
                ++italic;
                addText(" " + alt + " ");
                --italic;
            }
            return;
        }
        if ((iw > 0 && iw < 48) || (ih > 0 && ih < 48))
            return; // icons, flags, bullets
        if (rowDepth > 0 && ((iw > 0 && iw < 100) || (ih > 0 && ih < 100)))
            return;
        Block saved = cur; // image splits the current paragraph
        bool hadText = !curEmpty();
        std::string bullet = pendingBullet;
        if (hadText)
            flush();
        Block b;
        b.type = Block::IMAGE;
        b.src = src;
        b.imgW = iw;
        b.imgH = ih;
        b.indent = 0;
        doc.blocks.push_back(b);
        if (!hadText) {
            cur = saved;
            pendingBullet = bullet;
        }
    }

    void open(const Tag &t)
    {
        const std::string &n = t.name;
        if (n == "head") {
            inHead = true;
            return;
        }
        if (n == "title" && inHead) {
            inTitle = true;
            return;
        }
        if (n == "body")
            inHead = false;
        if (inHead)
            return;

        bool isVoid = in(n, kVoid) || t.selfClosing;
        if (skipDepth > 0) {
            if (!isVoid) {
                Open o;
                o.name = n;
                stack.push_back(o);
            }
            return;
        }

        if (n == "br") {
            if (!curEmpty())
                addText("\n", true);
            pendingSpace = false;
            return;
        }
        if (n == "hr") {
            flush();
            Block b;
            b.type = Block::RULE;
            doc.blocks.push_back(b);
            return;
        }
        if (n == "img") {
            image(t);
            return;
        }
        if (isVoid)
            return;

        Open o;
        o.name = n;

        std::string cls, st;
        bool hidden = in(n, kSkip);
        if (!hidden && t.attr("class", cls))
            for (const char *const *c = kSkipClasses; *c && !hidden; ++c)
                hidden = hasWord(cls, *c);
        if (!hidden && t.attr("style", st)) {
            std::string compact;
            for (char c : st)
                if (!isSpace(c))
                    compact += (char)tolower((unsigned char)c);
            hidden = compact.find("display:none") != std::string::npos;
        }
        if (!hidden) {
            std::string dummy;
            hidden = t.attr("hidden", dummy);
        }
        if (hidden) {
            o.skip = true;
            ++skipDepth;
            stack.push_back(o);
            return;
        }

        if (n.size() == 2 && n[0] == 'h' && n[1] >= '1' && n[1] <= '6') {
            flush();
            headingLevel = n[1] - '0';
            o.heading = true;
        } else if (n == "li") {
            flush();
            if (!listCounters.empty() && listCounters.back() >= 0)
                pendingBullet = std::to_string(++listCounters.back()) + ". ";
            else
                pendingBullet = "\xE2\x80\xA2 ";
        } else if (n == "ul" || n == "ol") {
            flush();
            listCounters.push_back(n == "ol" ? 0 : -1);
            o.list = true;
            if (listCounters.size() > 1 || n == "ol" || true) {
                ++indent;
                o.indent = true;
            }
        } else if (n == "dd" || n == "blockquote") {
            flush();
            ++indent;
            o.indent = true;
        } else if (n == "dt") {
            flush();
            o.style = ST_BOLD;
            ++bold;
        } else if (n == "pre") {
            flush();
            ++preDepth;
            o.pre = true;
        } else if (n == "tr") {
            flush();
            ++rowDepth;
            o.row = true;
        } else if (n == "td" || n == "th") {
            if (!curEmpty()) {
                unsigned char save = (unsigned char)bold;
                int saveLink = link;
                bold = 0;
                link = -1;
                addText(" \xE2\x94\x82 ", true);
                bold = save;
                link = saveLink;
                pendingSpace = false;
            }
            if (n == "th") {
                o.style = ST_BOLD;
                ++bold;
            }
        } else if (in(n, kBlock)) {
            flush();
        } else if (n == "b" || n == "strong") {
            o.style = ST_BOLD;
            ++bold;
        } else if (n == "i" || n == "em" || n == "cite" || n == "dfn" || n == "var") {
            o.style = ST_ITALIC;
            ++italic;
        } else if (n == "code" || n == "tt" || n == "kbd" || n == "samp") {
            o.style = ST_MONO;
            ++mono;
        } else if (n == "a") {
            std::string href;
            if (link < 0 && t.attr("href", href) && !href.empty()) {
                std::string tmp;
                if (href.compare(0, 4, "app:") == 0 || href.compare(0, 5, "open:") == 0 ||
                    resolveHref("x", href, tmp)) {
                    doc.links.push_back(href);
                    link = (int)doc.links.size() - 1;
                    o.link = true;
                }
            }
        }
        stack.push_back(o);
    }

    void closeOne()
    {
        Open o = stack.back();
        stack.pop_back();
        if (o.skip) {
            --skipDepth;
            return;
        }
        if (skipDepth > 0)
            return;
        if (o.style & ST_BOLD)
            --bold;
        if (o.style & ST_ITALIC)
            --italic;
        if (o.style & ST_MONO)
            --mono;
        if (o.link)
            link = -1;
        if (o.heading) {
            flush();
            headingLevel = 0;
        }
        if (o.pre) {
            flush();
            --preDepth;
        }
        if (o.list) {
            flush();
            if (!listCounters.empty())
                listCounters.pop_back();
        }
        if (o.row) {
            flush();
            --rowDepth;
        }
        if (o.indent) {
            flush();
            if (indent > 0)
                --indent;
        }
        if (o.name == "li" || o.name == "dt" || o.name == "dd" || o.name == "td" || o.name == "th" ||
            in(o.name, kBlock)) {
            if (o.name != "td" && o.name != "th")
                flush();
        }
    }

    void close(const std::string &n)
    {
        if (n == "head") {
            inHead = false;
            inTitle = false;
            return;
        }
        if (n == "title") {
            inTitle = false;
            return;
        }
        if (inHead)
            return;
        int found = -1;
        for (int i = (int)stack.size() - 1; i >= 0; --i) {
            if (stack[(size_t)i].name == n) {
                found = i;
                break;
            }
            // do not let a stray inline close tag escape a hidden subtree or a table cell
            if (stack[(size_t)i].skip)
                return;
        }
        if (found < 0)
            return;
        while ((int)stack.size() > found)
            closeOne();
    }
};

} // namespace

void parseHtml(const std::string &html, Doc &doc)
{
    Parser ps(doc);
    const char *p = html.data(), *end = p + html.size();
    while (p < end) {
        const char *lt = (const char *)memchr(p, '<', (size_t)(end - p));
        if (!lt) {
            ps.text(p, end);
            break;
        }
        if (lt > p)
            ps.text(p, lt);
        p = lt;
        if (end - p >= 4 && memcmp(p, "<!--", 4) == 0) {
            const char *c = (const char *)memmem(p + 4, (size_t)(end - p - 4), "-->", 3);
            p = c ? c + 3 : end;
            continue;
        }
        if (p + 1 < end && (p[1] == '!' || p[1] == '?')) {
            const char *c = (const char *)memchr(p, '>', (size_t)(end - p));
            p = c ? c + 1 : end;
            continue;
        }
        const char *q = p + 1;
        Tag t;
        if (q < end && *q == '/') {
            t.closing = true;
            ++q;
        }
        if (q >= end || !isalpha((unsigned char)*q)) {
            ps.text(p, p + 1); // literal '<'
            ++p;
            continue;
        }
        const char *ns = q;
        while (q < end && !isSpace(*q) && *q != '>' && *q != '/')
            ++q;
        t.name.assign(ns, (size_t)(q - ns));
        for (char &c : t.name)
            c = (char)tolower((unsigned char)c);
        t.attrBegin = q;
        // find the closing '>' while honouring quoted attribute values
        char quote = 0;
        while (q < end && (quote || *q != '>')) {
            if (quote) {
                if (*q == quote)
                    quote = 0;
            } else if (*q == '"' || *q == '\'')
                quote = *q;
            ++q;
        }
        t.attrEnd = q;
        if (q > t.attrBegin && q[-1] == '/') {
            t.selfClosing = true;
            --t.attrEnd;
        }
        p = q < end ? q + 1 : end;

        if (t.closing) {
            ps.close(t.name);
        } else {
            ps.open(t);
            // raw text elements: jump to the matching end tag
            if (t.name == "script" || t.name == "style") {
                std::string endTag = "</" + t.name;
                const char *c = p;
                while (true) {
                    c = (const char *)memchr(c, '<', (size_t)(end - c));
                    if (!c || (size_t)(end - c) < endTag.size()) {
                        c = end;
                        break;
                    }
                    if (strncasecmp(c, endTag.c_str(), endTag.size()) == 0)
                        break;
                    ++c;
                }
                p = c;
            }
        }
    }
    ps.flush();
}

std::string htmlEscape(const std::string &s)
{
    std::string o;
    for (char c : s) {
        switch (c) {
        case '&': o += "&amp;"; break;
        case '<': o += "&lt;"; break;
        case '>': o += "&gt;"; break;
        case '"': o += "&quot;"; break;
        default: o += c;
        }
    }
    return o;
}

bool resolveHref(const std::string &basePath, const std::string &hrefIn, std::string &out)
{
    std::string href = hrefIn;
    size_t hash = href.find('#');
    if (hash != std::string::npos)
        href.erase(hash);
    size_t qm = href.find('?');
    if (qm != std::string::npos)
        href.erase(qm);
    if (href.empty())
        return false;
    if (href.compare(0, 2, "//") == 0)
        return false;
    // scheme?
    // Only well-known schemes count: article names may contain ':'.
    static const char *const schemes[] = {"http:", "https:", "mailto:", "ftp:", "tel:", "geo:", "data:",
                                          "javascript:", "irc:", "magnet:", "sms:", "news:", "file:", nullptr};
    for (const char *const *s = schemes; *s; ++s)
        if (strncasecmp(href.c_str(), *s, strlen(*s)) == 0)
            return false;
    // percent-decode
    std::string dec;
    for (size_t i = 0; i < href.size(); ++i) {
        if (href[i] == '%' && i + 2 < href.size() + 0 && isxdigit((unsigned char)href[i + 1]) &&
            isxdigit((unsigned char)href[i + 2])) {
            dec += (char)strtol(href.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else
            dec += href[i];
    }

    std::vector<std::string> segs;
    if (dec[0] != '/') {
        size_t start = 0, slash;
        while ((slash = basePath.find('/', start)) != std::string::npos) {
            segs.push_back(basePath.substr(start, slash - start));
            start = slash + 1;
        }
    }
    size_t start = 0;
    while (start <= dec.size()) {
        size_t slash = dec.find('/', start);
        std::string seg = dec.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        bool last = slash == std::string::npos;
        if (seg == "..") {
            if (!segs.empty())
                segs.pop_back();
        } else if (seg == "." || (seg.empty() && !last)) {
            // skip
        } else {
            segs.push_back(seg);
        }
        if (last)
            break;
        start = slash + 1;
    }
    out.clear();
    for (size_t i = 0; i < segs.size(); ++i) {
        if (i)
            out += '/';
        out += segs[i];
    }
    return !out.empty();
}

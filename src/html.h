// Tolerant HTML -> simplified document model (paragraphs, headings, list
// items, images) suitable for paginated e-ink rendering.
#pragma once
#include <string>
#include <vector>

enum SpanStyle { ST_BOLD = 1, ST_ITALIC = 2, ST_MONO = 4 };

struct Span {
    std::string text; // UTF-8, whitespace collapsed; '\n' = forced line break
    unsigned char style = 0;
    int link = -1; // index into Doc::links
};

struct Block {
    enum Type { PARA, HEADING, IMAGE, RULE };
    Type type = PARA;
    int level = 0;  // heading level 1..6
    int indent = 0; // nesting depth
    std::string bullet;
    std::vector<Span> spans;
    // IMAGE
    std::string src;
    int imgW = 0, imgH = 0;
};

struct Doc {
    std::string title;
    std::vector<Block> blocks;
    std::vector<std::string> links; // raw href values
};

void parseHtml(const std::string &html, Doc &doc);
std::string htmlEscape(const std::string &s);
// Percent-decodes and resolves `href` against the directory of `basePath`.
// Returns false for external / unsupported links. Fragment is dropped.
bool resolveHref(const std::string &basePath, const std::string &href, std::string &out);

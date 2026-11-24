#include "app.h"

#include "gfx.h"
#include "image.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>

namespace {

const char *const kFiles = "app:files";
const char *const kSearch = "app:search?";

bool startsWith(const std::string &s, const char *p) { return s.compare(0, strlen(p), p) == 0; }

bool endsWithCI(const std::string &s, const char *suffix)
{
    size_t n = strlen(suffix);
    return s.size() >= n && strcasecmp(s.c_str() + s.size() - n, suffix) == 0;
}

std::string humanSize(uint64_t b)
{
    char buf[32];
    if (b >= (1ull << 30))
        snprintf(buf, sizeof buf, "%.1f Go", b / (double)(1ull << 30));
    else
        snprintf(buf, sizeof buf, "%.0f Mo", b / (double)(1ull << 20));
    return buf;
}

struct ZimFile {
    std::string path, name;
    uint64_t size;
};

void scanDir(const std::string &dir, int depth, std::vector<ZimFile> &out)
{
    DIR *d = opendir(dir.c_str());
    if (!d)
        return;
    std::vector<std::string> subdirs;
    while (struct dirent *de = readdir(d)) {
        std::string name = de->d_name;
        if (name.empty() || name[0] == '.')
            continue;
        std::string full = dir + "/" + name;
        struct stat st;
        if (stat(full.c_str(), &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < 4 && name != "system" && name != "applications")
                subdirs.push_back(full);
        } else if (endsWithCI(name, ".zim")) {
            out.push_back(ZimFile{full, name, (uint64_t)st.st_size});
        } else if (endsWithCI(name, ".zimaa")) {
            // split archive: add up the parts
            uint64_t total = 0;
            std::string base = full.substr(0, full.size() - 2);
            for (char a = 'a'; a <= 'z'; ++a) {
                bool stop = false;
                for (char b = 'a'; b <= 'z'; ++b) {
                    struct stat ps;
                    if (stat((base + a + b).c_str(), &ps) != 0) {
                        stop = true;
                        break;
                    }
                    total += (uint64_t)ps.st_size;
                }
                if (stop)
                    break;
            }
            out.push_back(ZimFile{full, name.substr(0, name.size() - 2), total});
        }
    }
    closedir(d);
    for (auto &s : subdirs)
        scanDir(s, depth + 1, out);
}

std::string trim(const std::string &s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\n' || s[a] == '\t' || s[a] == '\r'))
        ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\n' || s[b - 1] == '\t' || s[b - 1] == '\r'))
        --b;
    return s.substr(a, b - a);
}

// ASCII + Latin-1 supplement (2-byte UTF-8 0xC3 xx) case mapping, enough for
// French/Spanish/German titles.
std::string mapCase(const std::string &s, int mode) // 0 lower, 1 upper, 2 first upper, 3 title case
{
    std::string o = s;
    bool wordStart = true;
    for (size_t i = 0; i < o.size(); ++i) {
        unsigned char c = (unsigned char)o[i];
        bool up = mode == 1 || (mode == 2 && i == 0) || (mode == 3 && wordStart);
        if (c == 0xC3 && i + 1 < o.size()) {
            unsigned char d = (unsigned char)o[i + 1];
            if (up && d >= 0xA0 && d <= 0xBE && d != 0xB7)
                o[i + 1] = (char)(d - 0x20);
            else if (mode == 0 && d >= 0x80 && d <= 0x9E && d != 0x97)
                o[i + 1] = (char)(d + 0x20);
            wordStart = false;
            ++i;
            continue;
        }
        if (up)
            o[i] = (char)toupper(c);
        else if (mode == 0)
            o[i] = (char)tolower(c);
        wordStart = c == ' ' || c == '-' || c == '\'';
    }
    return o;
}

} // namespace

const MenuItem *App::menuItems()
{
    static const MenuItem items[] = {
        {M_SEARCH, "Rechercher un article"},
        {M_HOME, "Page d'accueil du ZIM"},
        {M_RANDOM, "Article au hasard"},
        {M_FIRST_PAGE, "Retour au debut de l'article"},
        {M_FILES, "Changer de fichier ZIM"},
        {M_FONT_UP, "Texte plus grand"},
        {M_FONT_DOWN, "Texte plus petit"},
        {M_QUIT, "Quitter"},
        {0, nullptr},
    };
    return items;
}

App::App(Platform &pf, const std::string &configFile, const std::vector<std::string> &scanDirs)
    : pf_(pf), configFile_(configFile), scanDirs_(scanDirs)
{
    srand((unsigned)time(nullptr));
}

void App::metrics()
{
    W_ = gfxWidth();
    H_ = gfxHeight();
    uiPx_ = std::max(14, W_ / 36);
    if (basePx_ <= 0)
        basePx_ = std::max(14, W_ / 33);
    marginX_ = W_ * 4 / 100;
    barH_ = uiPx_ * 24 / 10;
    footH_ = uiPx_ * 16 / 10;
}

void App::start()
{
    Loc saved;
    {
        std::ifstream in(configFile_.c_str());
        std::string line;
        while (std::getline(in, line)) {
            size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "font")
                basePx_ = atoi(v.c_str());
            else if (k == "zim")
                saved.zim = v;
            else if (k == "url")
                saved.url = v;
            else if (k == "page")
                saved.page = atoi(v.c_str());
        }
    }
    metrics();
    if (basePx_ < 12 || basePx_ > W_ / 8)
        basePx_ = std::max(14, W_ / 33);

    std::string err;
    bool ok = false;
    if (!saved.zim.empty() && !saved.url.empty() && !startsWith(saved.url, "app:")) {
        ok = load(saved, err);
        if (ok)
            page_ = std::min(std::max(saved.page, 0), pageCount() - 1);
    }
    if (!ok) {
        Loc files;
        files.url = kFiles;
        load(files, err);
    }
    cur_.page = page_;
    draw(true);
}

void App::saveState()
{
    std::ofstream out(configFile_.c_str(), std::ios::trunc);
    if (!out)
        return;
    out << "font=" << basePx_ << "\n";
    if (!cur_.zim.empty() && !startsWith(cur_.url, "app:")) {
        out << "zim=" << cur_.zim << "\n";
        out << "url=" << cur_.url << "\n";
        out << "page=" << page_ << "\n";
    }
}

bool App::lookup(zim::Archive &z, const std::string &path, zim::Entry &e)
{
    if (path.empty())
        return false;
    if (z.newNamespaces())
        return z.find('C', path, e);
    if (path.size() > 2 && path[1] == '/' && z.find(path[0], path.substr(2), e))
        return true;
    return z.find('A', path, e);
}

bool App::fetchByRef(const std::string &src, std::string &data)
{
    std::string path, err;
    zim::Entry e;
    if (!zim_ || !resolveHref(basePath_, src, path))
        return false;
    if (!lookup(*zim_, path, e)) {
        // some archives keep '_' in links but ' ' in paths, or the reverse
        std::string alt = path;
        std::replace(alt.begin(), alt.end(), ' ', '_');
        if (alt == path || !lookup(*zim_, alt, e))
            return false;
    }
    return zim_->resolve(e) && zim_->read(e, data, err);
}

bool App::buildFiles(Doc &d)
{
    std::vector<ZimFile> files;
    for (auto &dir : scanDirs_)
        scanDir(dir, 0, files);
    std::sort(files.begin(), files.end(), [](const ZimFile &a, const ZimFile &b) { return a.name < b.name; });

    std::string h = "<h1>Lecteur ZIM</h1>";
    if (files.empty()) {
        h += "<p>Aucun fichier <b>.zim</b> trouv\xC3\xA9.</p>"
             "<p>Branchez la liseuse en USB et copiez vos fichiers ZIM dans sa m\xC3\xA9moire "
             "(par exemple dans un dossier <b>ZIM</b>), puis relancez l'application.</p>"
             "<p>La m\xC3\xA9moire de la liseuse est en FAT32 : un fichier ne peut pas d\xC3\xA9passer "
             "4 Go. Les ZIM plus gros doivent \xC3\xAAtre d\xC3\xA9""coup\xC3\xA9s avec l'outil <b>zimsplit</b> "
             "(fichiers .zimaa, .zimab, ...), ce lecteur sait les relire.</p>";
    } else {
        h += "<p>Touchez un fichier pour l'ouvrir.</p><ul>";
        for (auto &f : files)
            h += "<li><a href=\"open:" + htmlEscape(f.path) + "\">" + htmlEscape(f.name) + "</a> (" +
                 humanSize(f.size) + ")</li>";
        h += "</ul>";
    }
    parseHtml(h, d);
    d.title = "Fichiers ZIM";
    return true;
}

bool App::buildSearch(const std::string &q, Doc &d)
{
    std::vector<zim::Entry> results;
    std::vector<std::string> variants;
    for (int mode : {-1, 2, 3, 0, 1}) {
        std::string v = mode < 0 ? q : mapCase(q, mode);
        if (std::find(variants.begin(), variants.end(), v) == variants.end())
            variants.push_back(v);
    }
    const size_t kMax = 80;
    for (auto &v : variants) {
        std::vector<zim::Entry> part;
        if (results.size() < kMax)
            zim_->titlesWithPrefix(v, kMax - results.size(), part);
        for (auto &e : part) {
            bool dup = false;
            for (auto &r : results)
                if (r.index == e.index)
                    dup = true;
            if (!dup)
                results.push_back(e);
        }
    }
    std::string h = "<h2>Recherche : " + htmlEscape(q) + "</h2>";
    if (zim_->articleCount() == 0)
        h += "<p>Ce fichier ZIM ne contient pas d'index de titres.</p>";
    else if (results.empty())
        h += "<p>Aucun titre ne commence par ce texte.</p>"
             "<p>La recherche porte sur le <b>d\xC3\xA9""but des titres</b> : essayez les premiers mots "
             "exacts du titre, avec les accents.</p>";
    else {
        h += "<ul>";
        for (auto &e : results)
            h += "<li><a href=\"app:entry?" + std::to_string(e.index) + "\">" + htmlEscape(e.title) + "</a></li>";
        h += "</ul>";
        if (results.size() >= kMax)
            h += "<p><i>Liste tronqu\xC3\xA9""e : pr\xC3\xA9""cisez la recherche.</i></p>";
    }
    parseHtml(h, d);
    d.title = "Recherche";
    return true;
}

bool App::buildEntry(zim::Archive &z, const std::string &url, Doc &d, std::string &canonical, std::string &err)
{
    zim::Entry e;
    bool found = false;
    if (url.empty())
        found = z.mainPage(e);
    else if (startsWith(url, "app:entry?"))
        found = z.entryAt((uint32_t)strtoul(url.c_str() + 10, nullptr, 10), e);
    else if (url.size() > 2 && url[1] == '/')
        found = z.find(url[0], url.substr(2), e);
    if (!found && url.empty()) {
        parseHtml("<h1>" + htmlEscape(z.metadata("Title")) + "</h1><p>Ce fichier ZIM n'a pas de page "
                  "d'accueil. Utilisez <b>Chercher</b> ou <b>Menu &gt; Article au hasard</b>.</p>", d);
        d.title = "ZIM";
        canonical.clear();
        basePath_.clear();
        return true;
    }
    if (!found) {
        err = "Article introuvable dans ce fichier ZIM.";
        return false;
    }
    if (!z.resolve(e)) {
        err = "Redirection cass\xC3\xA9""e.";
        return false;
    }
    std::string data;
    if (!z.read(e, data, err))
        return false;

    canonical = std::string(1, e.ns) + "/" + e.url;
    basePath_ = z.newNamespaces() ? e.url : canonical;
    std::string mime = z.mimeType(e);
    if (startsWith(mime, "text/html")) {
        parseHtml(data, d);
    } else if (startsWith(mime, "image/")) {
        Block b;
        b.type = Block::IMAGE;
        b.src = "/" + basePath_;
        d.blocks.push_back(b);
    } else if (startsWith(mime, "text/")) {
        parseHtml("<pre>" + htmlEscape(data) + "</pre>", d);
    } else {
        err = "Type de contenu non g\xC3\xA9r\xC3\xA9 : " + mime;
        return false;
    }
    if (d.title.empty())
        d.title = e.title;
    if (d.blocks.empty()) {
        Doc empty;
        parseHtml("<p><i>(page vide)</i></p>", empty);
        d.blocks = empty.blocks;
    }
    return true;
}

bool App::load(const Loc &loc, std::string &err)
{
    Doc d;
    Loc resolved = loc;
    std::string oldBase = basePath_;

    if (loc.url == kFiles) {
        buildFiles(d);
        resolved.zim = cur_.zim;
    } else {
        // make sure the right archive is open
        std::unique_ptr<zim::Archive> fresh;
        if (!zim_ || zim_->path() != loc.zim) {
            fresh.reset(new zim::Archive());
            if (!fresh->open(loc.zim, err))
                return false;
        }
        zim::Archive &z = fresh ? *fresh : *zim_;
        if (startsWith(loc.url, kSearch)) {
            if (fresh)
                zim_ = std::move(fresh);
            buildSearch(loc.url.substr(strlen(kSearch)), d);
        } else {
            std::string canonical;
            if (!buildEntry(z, loc.url, d, canonical, err)) {
                basePath_ = oldBase;
                return false;
            }
            resolved.url = canonical;
            if (fresh)
                zim_ = std::move(fresh);
        }
    }
    doc_ = std::move(d);
    title_ = doc_.title;
    cur_ = resolved;
    page_ = 0;
    relayout();
    return true;
}

void App::relayout()
{
    metrics();
    LayoutParams p;
    p.width = W_ - 2 * marginX_;
    p.height = H_ - barH_ - footH_ - uiPx_ / 2;
    p.basePx = basePx_;
    p.imageSize = [this](const std::string &src, int &w, int &h) {
        std::string data;
        return fetchByRef(src, data) && imageInfo(data, w, h);
    };
    layoutDoc(doc_, p, pages_);
    if (pages_.empty())
        pages_.emplace_back();
    page_ = std::min(page_, pageCount() - 1);
}

bool App::navigate(const Loc &to, bool pushHistory)
{
    pf_.busy(true);
    Loc prev = cur_;
    prev.page = page_;
    std::string err;
    bool ok = load(to, err);
    pf_.busy(false);
    if (!ok) {
        pf_.message(err.empty() ? "Ouverture impossible." : err);
        return false;
    }
    if (pushHistory && !prev.url.empty()) {
        history_.push_back(prev);
        if (history_.size() > 60)
            history_.erase(history_.begin());
    }
    page_ = std::min(std::max(to.page, 0), pageCount() - 1);
    saveState();
    draw(true);
    return true;
}

bool App::followLink(const std::string &href)
{
    Loc to;
    if (startsWith(href, "open:")) {
        to.zim = href.substr(5);
    } else if (href == kFiles) {
        to.url = kFiles;
    } else if (startsWith(href, "app:entry?")) {
        to.zim = cur_.zim;
        to.url = href;
    } else {
        if (!zim_)
            return false;
        std::string path;
        zim::Entry e;
        bool found = resolveHref(basePath_, href, path) && lookup(*zim_, path, e);
        if (!found && !path.empty()) {
            std::string alt = path;
            std::replace(alt.begin(), alt.end(), ' ', '_');
            found = alt != path && lookup(*zim_, alt, e);
            if (!found) {
                alt = path;
                std::replace(alt.begin(), alt.end(), '_', ' ');
                found = alt != path && lookup(*zim_, alt, e);
            }
        }
        if (!found) {
            pf_.message("Cet article n'est pas inclus dans ce fichier ZIM.");
            return false;
        }
        to.zim = cur_.zim;
        to.url = std::string(1, e.ns) + "/" + e.url;
    }
    return navigate(to, true);
}

void App::search(const std::string &query)
{
    std::string q = trim(query);
    if (q.empty())
        return;
    if (!zim_) {
        pf_.message("Ouvrez d'abord un fichier ZIM.");
        return;
    }
    Loc to;
    to.zim = zim_->path();
    to.url = kSearch + q;
    navigate(to, true);
}

void App::back()
{
    while (!history_.empty()) {
        Loc to = history_.back();
        history_.pop_back();
        if (navigate(to, false))
            return;
    }
}

void App::changeFont(int dir)
{
    int old = basePx_;
    basePx_ = dir > 0 ? basePx_ * 112 / 100 + 1 : basePx_ * 100 / 112;
    basePx_ = std::min(std::max(basePx_, std::max(12, W_ / 70)), W_ / 12);
    if (basePx_ == old)
        return;
    int oldCount = pageCount(), oldPage = page_;
    pf_.busy(true);
    relayout();
    pf_.busy(false);
    page_ = oldCount > 1 ? (int)((long long)oldPage * (pageCount() - 1) / (oldCount - 1)) : 0;
    saveState();
    draw(true);
}

void App::menu(int id)
{
    switch (id) {
    case M_SEARCH:
        if (!zim_) {
            pf_.message("Ouvrez d'abord un fichier ZIM.");
            break;
        }
        pf_.askText("Rechercher un titre", [this](const std::string &q) { search(q); });
        break;
    case M_HOME:
        if (zim_) {
            Loc to;
            to.zim = zim_->path();
            navigate(to, true);
        }
        break;
    case M_RANDOM:
        if (zim_ && zim_->articleCount() > 0) {
            zim::Entry e;
            for (int i = 0; i < 30; ++i) {
                uint32_t pos = (uint32_t)(((uint64_t)rand() * 32768u + (uint64_t)rand()) % zim_->articleCount());
                if (zim_->articleAt(pos, e) && e.ns == zim_->articleNs() && !e.special &&
                    (e.redirect || zim_->isHtml(e))) {
                    Loc to;
                    to.zim = zim_->path();
                    to.url = "app:entry?" + std::to_string(e.index);
                    navigate(to, true);
                    break;
                }
            }
        }
        break;
    case M_FILES: {
        Loc to;
        to.url = kFiles;
        navigate(to, true);
        break;
    }
    case M_FIRST_PAGE:
        if (page_ != 0) {
            page_ = 0;
            draw(true);
        }
        break;
    case M_FONT_UP:
        changeFont(1);
        break;
    case M_FONT_DOWN:
        changeFont(-1);
        break;
    case M_QUIT:
        saveState();
        pf_.quit();
        break;
    }
}

void App::nextPage()
{
    if (page_ + 1 < pageCount()) {
        ++page_;
        draw(false);
    }
}

void App::prevPage()
{
    if (page_ > 0) {
        --page_;
        draw(false);
    }
}

void App::tap(int x, int y)
{
    if (y < barH_) {
        if (x < W_ * 18 / 100)
            back();
        else if (x >= W_ * 78 / 100)
            pf_.showMenu();
        else if (x >= W_ * 52 / 100)
            menu(M_SEARCH);
        return;
    }
    // links: exact hit first, then the nearest one within a finger-sized slack
    const int oy = barH_ + uiPx_ / 4;
    const Frag *best = nullptr;
    long bestDist = 0;
    const int slack = std::max(10, W_ / 60);
    for (const Frag &f : pages_[(size_t)page_].frags) {
        if (f.link < 0 || f.kind != Frag::TEXT)
            continue;
        int fx = marginX_ + f.x, fy = oy + f.y;
        int dx = x < fx ? fx - x : (x > fx + f.w ? x - (fx + f.w) : 0);
        int dy = y < fy ? fy - y : (y > fy + f.h ? y - (fy + f.h) : 0);
        if (dx > slack || dy > slack)
            continue;
        long dist = (long)dx * dx + (long)dy * dy;
        if (!best || dist < bestDist) {
            best = &f;
            bestDist = dist;
        }
    }
    if (best && best->link < (int)doc_.links.size()) {
        std::string href = doc_.links[(size_t)best->link]; // copy: navigation replaces doc_
        followLink(href);
        return;
    }
    if (x < W_ / 3)
        prevPage();
    else
        nextPage();
}

void App::drawBar()
{
    gfxFill(0, 0, W_, barH_, 255);
    gfxFill(0, barH_ - 2, W_, 2, 0);
    int ty = (barH_ - uiPx_ * 13 / 10) / 2;

    gfxFont(FACE_BOLD, uiPx_);
    const char *backLabel = history_.empty() ? "" : "< Retour";
    gfxText(marginX_ / 2, ty, backLabel);
    const char *searchLabel = "Chercher";
    const char *menuLabel = "Menu";
    int menuX = W_ * 78 / 100, searchX = W_ * 52 / 100;
    gfxFill(searchX, uiPx_ / 3, 1, barH_ - uiPx_ * 2 / 3 - 2, 120);
    gfxFill(menuX, uiPx_ / 3, 1, barH_ - uiPx_ * 2 / 3 - 2, 120);
    gfxText(searchX + (menuX - searchX - gfxTextWidth(searchLabel)) / 2, ty, searchLabel);
    gfxText(menuX + (W_ - menuX - gfxTextWidth(menuLabel)) / 2, ty, menuLabel);

    // title, truncated to the room between "back" and "search"
    gfxFont(FACE_REG, uiPx_);
    int tx = W_ * 18 / 100, room = searchX - tx - uiPx_ / 2;
    std::string t = title_;
    if (gfxTextWidth(t.c_str()) > room) {
        while (!t.empty() && gfxTextWidth((t + "...").c_str()) > room) {
            t.pop_back();
            while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80)
                t.pop_back();
        }
        t += "...";
    }
    gfxText(tx, ty, t.c_str());
}

void App::draw(bool full)
{
    metrics();
    gfxFill(0, 0, W_, H_, 255);
    drawBar();

    const int oy = barH_ + uiPx_ / 4;
    int face = -1, px = -1;
    for (const Frag &f : pages_[(size_t)page_].frags) {
        int x = marginX_ + f.x, y = oy + f.y;
        if (f.kind == Frag::RULE) {
            gfxFill(x, y, f.w, f.h, 90);
        } else if (f.kind == Frag::IMAGE) {
            std::string data;
            std::vector<uint8_t> px8;
            if (fetchByRef(f.text, data) && imageDecodeGray(data, f.w, f.h, px8))
                gfxGray(x, y, f.w, f.h, px8.data());
            else {
                gfxFill(x, y, f.w, f.h, 200);
                gfxFill(x + 2, y + 2, f.w - 4, f.h - 4, 255);
            }
        } else if (!f.text.empty()) {
            if (f.face != face || f.px != px) {
                gfxFont(f.face, f.px);
                face = f.face;
                px = f.px;
            }
            gfxText(x, y, f.text.c_str());
            if (f.link >= 0)
                gfxFill(x, y + f.px * 112 / 100, f.w, std::max(1, f.px / 16), 0);
        }
    }

    char buf[48];
    snprintf(buf, sizeof buf, "%d / %d", page_ + 1, pageCount());
    int fpx = uiPx_ * 8 / 10;
    gfxFont(FACE_REG, fpx);
    gfxText((W_ - gfxTextWidth(buf)) / 2, H_ - footH_ + (footH_ - fpx * 13 / 10) / 2, buf);
    pf_.present(full);
}

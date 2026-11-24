// Platform independent application logic: navigation, search, rendering.
#pragma once
#include "layout.h"
#include "zim.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class Platform {
public:
    virtual ~Platform() {}
    virtual void askText(const std::string &title, std::function<void(const std::string &)> done) = 0;
    virtual void showMenu() = 0;
    virtual void busy(bool on) = 0;
    virtual void present(bool fullRefresh) = 0; // push the frame to the screen
    virtual void message(const std::string &text) = 0;
    virtual void quit() = 0;
};

enum MenuId { M_SEARCH = 1, M_HOME, M_RANDOM, M_FILES, M_FIRST_PAGE, M_FONT_UP, M_FONT_DOWN, M_QUIT };
struct MenuItem {
    int id;
    const char *label;
};

class App {
public:
    App(Platform &pf, const std::string &configFile, const std::vector<std::string> &scanDirs);

    void start();
    void draw(bool full);
    void tap(int x, int y);
    void nextPage();
    void prevPage();
    void back();
    void menu(int id);
    void search(const std::string &query);
    void saveState();

    static const MenuItem *menuItems(); // terminated by id 0

    // exposed for tests
    int pageCount() const { return (int)pages_.size(); }
    int page() const { return page_; }
    const std::string &title() const { return title_; }
    const std::vector<Page> &pages() const { return pages_; }
    bool followLink(const std::string &href);

private:
    struct Loc {
        std::string zim, url;
        int page = 0;
    };

    bool navigate(const Loc &to, bool pushHistory);
    bool load(const Loc &loc, std::string &err);
    bool buildFiles(Doc &d);
    bool buildSearch(const std::string &q, Doc &d);
    bool buildEntry(zim::Archive &z, const std::string &url, Doc &d, std::string &canonical, std::string &err);
    bool lookup(zim::Archive &z, const std::string &path, zim::Entry &e);
    bool fetchByRef(const std::string &src, std::string &data);
    void relayout();
    void metrics();
    void drawBar();
    void changeFont(int dir);

    Platform &pf_;
    std::string configFile_;
    std::vector<std::string> scanDirs_;

    std::unique_ptr<zim::Archive> zim_;
    Loc cur_;
    std::vector<Loc> history_;
    Doc doc_;
    std::vector<Page> pages_;
    std::string title_;
    std::string basePath_; // path used to resolve relative links of the current doc
    int page_ = 0;

    int basePx_ = 0, uiPx_ = 0;
    int W_ = 0, H_ = 0, marginX_ = 0, barH_ = 0, footH_ = 0;
};

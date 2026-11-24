// PocketBook (inkview) front-end: events, fonts, menu, keyboard.
#include "app.h"
#include "gfx.h"

#include "inkview.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>

namespace {

std::map<int, ifont *> gFonts; // key: face << 16 | px
ifont *gCurFont = nullptr;

ifont *fontFor(int face, int px)
{
    int key = (face << 16) | px;
    auto it = gFonts.find(key);
    if (it != gFonts.end())
        return it->second;
    if (gFonts.size() > 40) { // font size changed many times: start over
        for (auto &f : gFonts)
            CloseFont(f.second);
        gFonts.clear();
        gCurFont = nullptr;
    }
    const char *name = iv_get_default_font((FONT_TYPE)face);
    ifont *f = OpenFont(name ? name : "LiberationSans", px, 1);
    gFonts[key] = f;
    return f;
}

int gCurPx = 0;

class PbPlatform : public Platform {
public:
    App *app = nullptr;
    std::function<void(const std::string &)> textDone;
    char kbdBuffer[256];
    std::vector<imenu> menuItems;
    int partialCount = 0;

    void askText(const std::string &title, std::function<void(const std::string &)> done) override;
    void showMenu() override;
    void busy(bool on) override
    {
        if (on)
            ShowHourglass();
        else
            HideHourglass();
    }
    void present(bool full) override
    {
        if (full || ++partialCount >= 8) {
            partialCount = 0;
            FullUpdate();
        } else {
            PartialUpdate(0, 0, ScreenWidth(), ScreenHeight());
        }
    }
    void message(const std::string &text) override { Message(ICON_INFORMATION, "Lecteur ZIM", text.c_str(), 4000); }
    void quit() override { CloseApp(); }
};

PbPlatform gPlatform;
std::unique_ptr<App> gApp;
int gDownX = -1, gDownY = -1;

void keyboardHandler(char *text)
{
    auto done = gPlatform.textDone;
    gPlatform.textDone = nullptr;
    if (text && *text && done)
        done(text);
    else if (gApp)
        gApp->draw(true); // cancelled: restore the page under the keyboard
}

void menuHandler(int index)
{
    if (index > 0 && gApp)
        gApp->menu(index);
}

void PbPlatform::askText(const std::string &title, std::function<void(const std::string &)> done)
{
    textDone = done;
    memset(kbdBuffer, 0, sizeof kbdBuffer);
    OpenKeyboard(title.c_str(), kbdBuffer, (int)sizeof kbdBuffer - 1, KBD_NORMAL, keyboardHandler);
}

void PbPlatform::showMenu()
{
    if (menuItems.empty()) {
        static char header[] = "Lecteur ZIM";
        menuItems.push_back(imenu{ITEM_HEADER, 0, header, nullptr});
        for (const MenuItem *m = App::menuItems(); m->id; ++m)
            menuItems.push_back(imenu{ITEM_ACTIVE, (short)m->id, strdup(m->label), nullptr});
        menuItems.push_back(imenu{0, 0, nullptr, nullptr});
    }
    OpenMenu(menuItems.data(), 0, ScreenWidth(), 0, menuHandler);
}

int mainHandler(int type, int par1, int par2)
{
    switch (type) {
    case EVT_INIT:
        SetPanelType(PANEL_DISABLED);
        return 1;
    case EVT_SHOW:
        if (!gApp) {
            std::vector<std::string> dirs;
            dirs.push_back(FLASHDIR);
            dirs.push_back(SDCARDDIR);
            gApp.reset(new App(gPlatform, CONFIGPATH "/zimreader.cfg", dirs));
            gPlatform.app = gApp.get();
            gApp->start();
        } else {
            gApp->draw(true);
        }
        return 1;
    case EVT_EXIT:
        if (gApp)
            gApp->saveState();
        return 1;
    case EVT_POINTERDOWN:
        gDownX = par1;
        gDownY = par2;
        return 1;
    case EVT_POINTERUP:
        if (gApp && gDownX >= 0) {
            int dx = par1 - gDownX, dy = par2 - gDownY;
            int x0 = gDownX, y0 = gDownY;
            gDownX = gDownY = -1;
            if (abs(dx) > ScreenWidth() / 8 && abs(dx) > 2 * abs(dy)) {
                if (dx < 0)
                    gApp->nextPage();
                else
                    gApp->prevPage();
            } else if (abs(dx) < ScreenWidth() / 20 && abs(dy) < ScreenWidth() / 20) {
                gApp->tap(x0, y0);
            }
        }
        return 1;
    case EVT_KEYPRESS:
    case EVT_KEYREPEAT:
        if (!gApp)
            return 0;
        switch (par1) {
        case IV_KEY_NEXT:
        case IV_KEY_NEXT2:
        case IV_KEY_RIGHT:
        case IV_KEY_DOWN:
            gApp->nextPage();
            return 1;
        case IV_KEY_PREV:
        case IV_KEY_PREV2:
        case IV_KEY_LEFT:
        case IV_KEY_UP:
            gApp->prevPage();
            return 1;
        case IV_KEY_MENU:
            if (type == EVT_KEYPRESS)
                gPlatform.showMenu();
            return 1;
        case IV_KEY_BACK:
            if (type == EVT_KEYPRESS)
                gApp->back();
            return 1;
        case IV_KEY_HOME:
            gApp->saveState();
            CloseApp();
            return 1;
        }
        return 0;
    }
    return 0;
}

} // namespace

int gfxWidth() { return ScreenWidth(); }
int gfxHeight() { return ScreenHeight(); }

void gfxFont(int face, int px)
{
    gCurFont = fontFor(face, px);
    gCurPx = px;
    if (gCurFont)
        SetFont(gCurFont, BLACK);
}

int gfxTextWidth(const char *utf8) { return gCurFont ? StringWidth(utf8) : 0; }

void gfxText(int x, int y, const char *utf8)
{
    if (gCurFont)
        DrawString(x, y + gCurPx * 15 / 100, utf8);
}

void gfxFill(int x, int y, int w, int h, int gray)
{
    if (w > 0 && h > 0)
        FillArea(x, y, w, h, (gray << 16) | (gray << 8) | gray);
}

void gfxGray(int x, int y, int w, int h, const uint8_t *pixels)
{
    Stretch(pixels, IMAGE_GRAY8, w, h, w, x, y, w, h, 0);
}

int main()
{
    InkViewMain(mainHandler);
    return 0;
}

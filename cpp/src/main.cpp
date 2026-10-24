// Midnight launcher — C++ / Win32 + Direct2D.
// Этап 5: оформление (темы и цвета), шрифт, иконка, запись настроек, смена скина и плаща.

#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <dwrite_3.h>
#include <dwmapi.h>
#include <winhttp.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <commdlg.h>
#include <bcrypt.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <condition_variable>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "../third_party/json.hpp"
#include "i18n.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")

// Иконка и группировка в панели задач (объявлено в shobjidl.h, который не тянем целиком).
extern "C" HRESULT WINAPI SetCurrentProcessExplicitAppUserModelID(PCWSTR AppID);

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace fs = std::filesystem;

// ── миниатюрный ComPtr ────────────────────────────────────────────────────────
template <class T>
struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    Com(Com&& o) noexcept : p(o.p) { o.p = nullptr; }
    Com& operator=(Com&& o) noexcept {
        if (this != &o) { reset(); p = o.p; o.p = nullptr; }
        return *this;
    }
    ~Com() { reset(); }
    T** put() { reset(); return &p; }
    T* get() const { return p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
    void reset() { if (p) { p->Release(); p = nullptr; } }
};

// ── палитра: пересчитывается ApplyPalette(тема, основной, второстепенный) ─────
static D2D1_COLOR_F C_BG          = D2D1::ColorF(0x0B0B10);
static D2D1_COLOR_F C_SIDE        = D2D1::ColorF(0x101017);
static D2D1_COLOR_F C_CARD        = D2D1::ColorF(0x17171F);
static D2D1_COLOR_F C_CARD_HI     = D2D1::ColorF(0x1F1F2B);
static D2D1_COLOR_F C_BORDER      = D2D1::ColorF(0x26262F);
static D2D1_COLOR_F C_INPUT       = D2D1::ColorF(0x1B1B24);
static D2D1_COLOR_F C_TEXT        = D2D1::ColorF(0xF2F2F7);
static D2D1_COLOR_F C_DIM         = D2D1::ColorF(0x7C7C8A);
static D2D1_COLOR_F C_ACCENT      = D2D1::ColorF(0x7C5CFF);
static D2D1_COLOR_F C_ACCENT_HI   = D2D1::ColorF(0x8F73FF);
static D2D1_COLOR_F C_ACCENT_SOFT = D2D1::ColorF(0x7C5CFF, 0.14f);
static D2D1_COLOR_F C_ACCENT2     = D2D1::ColorF(0x4DA3FF);
static D2D1_COLOR_F C_ACCENT2_HI  = D2D1::ColorF(0x6DB5FF);
static D2D1_COLOR_F C_PINK        = D2D1::ColorF(0x4DA3FF);
static D2D1_COLOR_F C_GREEN       = D2D1::ColorF(0x3DDC84);
static D2D1_COLOR_F C_YELLOW      = D2D1::ColorF(0xFFC14D);
static D2D1_COLOR_F C_RED         = D2D1::ColorF(0xFF5C5C);
static D2D1_COLOR_F C_BLUE        = D2D1::ColorF(0x4DA3FF);
static D2D1_COLOR_F C_WHITE       = D2D1::ColorF(0xFFFFFF);

// пресеты цветов (как в Python-версии)
static const wchar_t* kAccentPresets[6]  = { L"#7C5CFF", L"#4DA3FF", L"#00C2A8",
                                             L"#FF6B9A", L"#FFA53D", L"#3DDC84" };
static const wchar_t* kAccent2Presets[6] = { L"#4DA3FF", L"#7C5CFF", L"#00C2A8",
                                             L"#FF6B9A", L"#FFA53D", L"#3DDC84" };
static const wchar_t* kDefaultAccent  = L"#7C5CFF";
static const wchar_t* kDefaultAccent2 = L"#4DA3FF";

// две темы оформления
struct ThemeDef {
    unsigned bg, side, card, cardHi, input, border, text, dim, green, yellow, red, blue;
};
static const ThemeDef kThemes[2] = {
    { 0x0B0B10, 0x101017, 0x17171F, 0x1F1F2B, 0x1B1B24, 0x26262F,
      0xF2F2F7, 0x7C7C8A, 0x3DDC84, 0xFFC14D, 0xFF5C5C, 0x4DA3FF },   // dark
    { 0xF3F3F9, 0xFFFFFF, 0xFFFFFF, 0xE9E8F4, 0xFFFFFF, 0xD9D7E6,
      0x181728, 0x6F6D82, 0x0F9D52, 0xA9690A, 0xD63B3B, 0x1F6FEB },   // light
};

static bool HexOk(const std::wstring& in, D2D1_COLOR_F& out) {
    std::wstring c;
    for (wchar_t ch : in) if (!iswspace((wint_t)ch)) c += ch;
    if (c.size() == 4 && c[0] == L'#') {
        std::wstring e = L"#";
        for (int i = 1; i < 4; ++i) { e += c[i]; e += c[i]; }
        c = e;
    }
    if (c.size() != 7 || c[0] != L'#') return false;
    auto hv = [](wchar_t ch) -> int {
        if (ch >= L'0' && ch <= L'9') return ch - L'0';
        if (ch >= L'a' && ch <= L'f') return ch - L'a' + 10;
        if (ch >= L'A' && ch <= L'F') return ch - L'A' + 10;
        return -1;
    };
    int v[6];
    for (int i = 0; i < 6; ++i) { v[i] = hv(c[1 + i]); if (v[i] < 0) return false; }
    unsigned rgb = (unsigned)((v[0] << 20) | (v[1] << 16) | (v[2] << 12) |
                              (v[3] << 8) | (v[4] << 4) | v[5]);
    out = D2D1::ColorF(rgb);
    return true;
}

static std::wstring HexStr(const D2D1_COLOR_F& c) {
    auto b = [](float f) { return (unsigned)std::lround(std::clamp(f, 0.f, 1.f) * 255.f); };
    wchar_t buf[10]{};
    swprintf(buf, 10, L"#%02X%02X%02X", b(c.r), b(c.g), b(c.b));
    return buf;
}

static D2D1_COLOR_F MixCol(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t) {
    return D2D1::ColorF(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t);
}
static D2D1_COLOR_F Lighten(const D2D1_COLOR_F& c, float k = 0.15f) {
    return MixCol(c, D2D1::ColorF(0xFFFFFF), k);
}
static D2D1_COLOR_F Darken(const D2D1_COLOR_F& c, float k = 0.15f) {
    return MixCol(c, D2D1::ColorF(0x000000), k);
}

static void ApplyPalette(const std::wstring& theme, const std::wstring& accent,
                         const std::wstring& accent2) {
    bool dark = (theme != L"light");
    const ThemeDef& t = kThemes[dark ? 0 : 1];
    D2D1_COLOR_F a1, a2;
    if (!HexOk(accent, a1)) a1 = D2D1::ColorF(0x7C5CFF);
    if (!HexOk(accent2, a2)) a2 = D2D1::ColorF(0x4DA3FF);
    C_BG = D2D1::ColorF(t.bg);
    C_SIDE = D2D1::ColorF(t.side);
    C_CARD = D2D1::ColorF(t.card);
    C_CARD_HI = D2D1::ColorF(t.cardHi);
    C_INPUT = D2D1::ColorF(t.input);
    C_BORDER = D2D1::ColorF(t.border);
    C_TEXT = D2D1::ColorF(t.text);
    C_DIM = D2D1::ColorF(t.dim);
    C_GREEN = D2D1::ColorF(t.green);
    C_YELLOW = D2D1::ColorF(t.yellow);
    C_RED = D2D1::ColorF(t.red);
    C_BLUE = D2D1::ColorF(t.blue);
    C_ACCENT = a1;
    C_ACCENT_HI = dark ? Lighten(a1, 0.16f) : Darken(a1, 0.14f);
    C_ACCENT_SOFT = D2D1::ColorF(a1.r, a1.g, a1.b, 0.14f);
    C_ACCENT2 = a2;
    C_ACCENT2_HI = dark ? Lighten(a2, 0.16f) : Darken(a2, 0.14f);
    C_PINK = a2;
}

// ── раскладка ─────────────────────────────────────────────────────────────────
static constexpr float SIDE_W    = 230.f;
static constexpr float SIDE_PAD  = 14.f;
static constexpr float NAV_Y0    = 122.f;
static constexpr float NAV_STEP  = 46.f;
static constexpr float NAV_H     = 40.f;
static constexpr float PAGE_Y    = 22.f;
static constexpr float ROW_H     = 88.f;
static constexpr float ROW_GAP   = 10.f;
static constexpr float CAT_TABS_Y   = 84.f;
static constexpr float CAT_SEARCH_Y = 134.f;
static constexpr float CAT_FILTER_Y = 190.f;
static constexpr float CAT_LIST_Y   = 236.f;

static const wchar_t* kNav[4] = { L"Главная", L"Моды", L"Каталог", L"Настройки" };
static const wchar_t* kInstallMarker = L".nexus-ok";
static const char* kMrType[4] = { "mod", "resourcepack", "shader", "datapack" };

// ── прямоугольник для раскладки и попаданий ───────────────────────────────────
struct MRect {
    float x = 0, y = 0, w = 0, h = 0;
    bool hit(float px, float py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

// ── данные ────────────────────────────────────────────────────────────────────
struct Item {
    std::wstring name, desc, slug, iconUrl, pageUrl;
    long long downloads = 0;
    int type = 0;
    Com<ID2D1Bitmap> icon;
    std::atomic<int> iconState{ 0 };
    std::atomic<int> progress{ -1 };   // -1 нет, 0..100, -2 неизвестно
    std::atomic<bool> installed{ false };
};

struct Profile {
    std::wstring name, dir, loader, mc, loaderVer, version;
    bool valid = false;
};

struct ModFile {
    std::wstring name;
    unsigned long long size = 0;
};

// Снимок состава папки сборки для окна «Обзор».
struct OverviewData {
    bool open = false;
    int idx = -1;
    std::wstring name, loader, mc, loaderVer, dir;
    unsigned long long total = 0, modsSize = 0, savesSize = 0;
    unsigned long long files = 0;
    int worlds = 0;
    std::vector<std::pair<std::wstring, unsigned long long>> folders;
    std::vector<ModFile> mods;
    float scrollY = 0.f, scrollMax = 0.f;
};

// Строка дерева зависимостей установленных модов (страница «Моды»).
struct ModTreeRow {
    std::wstring prefix, name, note;
    bool root = false;      // установленный мод верхнего уровня
    bool missing = false;   // обязательная зависимость, которой нет в mods
    bool unknown = false;   // файл не найден на Modrinth
};

struct ModsTreeState {
    bool busy = false;
    bool ready = false;
    int gen = 0;
    std::wstring key;       // профиль + подпись файлов, чтобы не строить заново
    std::wstring note;      // ошибка или пояснение
    std::vector<ModTreeRow> rows;
};

// Состояние одного запуска: подготовка файлов, процесс игры, лог.
struct LaunchState {
    std::atomic<bool> active{ false };   // идёт подготовка или игра
    std::atomic<bool> stop{ false };
    std::atomic<bool> running{ false };  // процесс игры уже стартовал
    std::atomic<int> pct{ -1 };          // -1 — нет прогресса
    std::mutex mu;
    std::wstring msg = L"Готов к запуску";
    std::deque<std::wstring> log;
    HANDLE proc = nullptr;
};

struct DownloadTask {
    std::wstring url;
    fs::path dest;
};

struct HttpResult {
    int status = 0;
    std::string body;
    std::wstring error;
};

struct ImageData {
    int w = 0, h = 0;
    std::vector<BYTE> px;
};

// ── карточка проекта в каталоге ───────────────────────────────────────────────
struct DescBlock {
    std::wstring text;
    int kind = 0;      // 0 — абзац, 1 — заголовок, 2 — пункт, 3 — код, 4 — цитата
};

struct ShotInfo {
    std::wstring url, title;
    ImageData px;
    Com<ID2D1Bitmap> bmp;
};

struct DepRow {
    std::wstring prefix;   // ├─ / └─ / │ с отступами
    std::wstring name, note;
    bool optional = false;
    bool missing = false;
};

struct DetailData {
    std::wstring title, summary, body, author, version, license, depNote, err;
    long long downloads = 0, followers = 0;
    std::vector<std::wstring> cats;
    std::vector<ShotInfo> shots;
    std::vector<DepRow> deps;
};

// плащ игрока (для страницы скина)
struct CapeInfo {
    std::wstring id, alias;
    bool active = false;
};

// ── авторизация ───────────────────────────────────────────────────────────────
struct AuthJob {
    std::atomic<bool> cancel{ false };
};

struct MsAuth {
    std::wstring mcToken, username, uuid, msRefresh;
    bool online = false;
};

struct ElyAuth {
    std::wstring mcToken, clientToken, username, uuid;
    bool online = false;
};

// Состояние окна входа (рисуется поверх основного окна).
struct AuthUi {
    int modal = 0;   // 0 — нет, 1 — выбор способа, 2 — Microsoft, 3 — Ely.by, 4 — аккаунты
    std::wstring code, url = L"microsoft.com/link", status;
    float pct = 0.f;
    bool busy = false;
    bool error = false;
    std::wstring user, pass;
    int focusField = 0;   // 0 — нет, 1 — ник, 2 — пароль
};

struct App {
    HWND hwnd = nullptr;
    float dip = 1.f;

    int page = 0;
    int fromPage = 0;
    float animT = 1.f;
    bool animating = false;
    int hoverNav = -1;

    // каталог
    std::vector<std::shared_ptr<Item>> items;
    int catTab = 0;
    std::wstring query;
    bool searchFocus = false;
    bool onlyProfile = true;
    bool loading = false;
    bool catalogStarted = false;
    std::atomic<int> catGen{ 0 };
    float scrollY = 0.f;
    float scrollMax = 0.f;
    int hoverRow = -1;
    int hoverWidget = 0;
    std::wstring status = L"Готово";
    D2D1_COLOR_F statusCol = C_DIM;

    // страница проекта из каталога
    struct DetailUi {
        bool open = false;
        bool busy = false;
        int gen = 0;
        std::shared_ptr<Item> item;
        std::wstring title, summary, author, version, license, depNote, err;
        long long downloads = 0, followers = 0;
        std::vector<std::wstring> cats;
        std::vector<DescBlock> blocks;
        std::vector<ShotInfo> shots;
        std::vector<DepRow> deps;
        std::vector<MRect> shotRects;   // экранные координаты превью (для клика)
        float scrollY = 0.f, scrollMax = 0.f;
        int shotOpen = -1;              // индекс превью в лайтбоксе
    } detail;

    // профиль и настройки
    Profile profile;
    std::vector<Profile> profiles;
    int profileIdx = 0;
    bool hasProfile = false;
    std::vector<ModFile> modsList;

    // страница «Моды»: прокрутка и дерево зависимостей
    float modsScrollY = 0.f, modsScrollMax = 0.f;
    ModsTreeState modsTree;

    // подтверждение удаления сборки
    struct DelUi { bool open = false; int idx = -1; } delUi;
    OverviewData overview;

    // создание сборок и выбор сборки при установке
    struct BuildUi {
        bool open = false;
        int loader = 0;                 // 0 — vanilla, 1 — fabric, 2 — quilt, 3 — forge, 4 — neoforge
        std::wstring name, mc, status;
        int focus = 0;                  // 1 — название, 2 — версия
        bool busy = false;
    } buildUi;
    struct InstallUi {
        bool open = false;
        std::shared_ptr<Item> item;
        bool busy = false;
        int gen = 0;
        std::vector<int> cand;
        std::vector<std::wstring> labels;
        std::wstring status;
    } installUi;
    std::shared_ptr<LaunchState> javaSt;

    // авторизация
    MsAuth ms;
    ElyAuth ely;
    std::wstring authKind = L"ms";
    AuthUi authUi;
    int authGen = 0;
    std::shared_ptr<AuthJob> authJob;

    // запуск
    std::wstring ramMin = L"512M", ramMax = L"4G";
    std::wstring javaPathCfg, javaArgs, gameArgs, username = L"Player";
    std::wstring lastJava;   // найденная Java (для подсказки на главной)
    std::shared_ptr<LaunchState> launch = std::make_shared<LaunchState>();

    // оформление (этап 5)
    std::wstring lang = L"ru";
    std::wstring theme = L"dark";
    std::wstring accent = kDefaultAccent, accent2 = kDefaultAccent2;
    std::wstring accentEdit = kDefaultAccent, accent2Edit = kDefaultAccent2;
    std::wstring fontFamily = L"Segoe UI";
    Com<IDWriteFontCollection1> fontColl;
    float settingsScrollY = 0.f, settingsScrollMax = 0.f;
    int setFocus = 0;          // id поля настроек с фокусом ввода
    bool javaChecking = false;

    // смена скина и плаща
    bool skinOpen = false;
    int skinVariant = 0;                        // 0 — классический, 1 — тонкий
    std::string skinPng;                        // выбранный/текущий PNG
    std::wstring skinLabel, skinStatus, skinNick;
    D2D1_COLOR_F skinStatusCol = C_DIM;
    bool skinBusy = false;
    int skinFocus = 0;                          // 1 — поле ника
    std::vector<CapeInfo> capes;
    std::vector<unsigned char> skinPrevPx, skinHeadPx;   // premultiplied BGRA
    int skinPrevW = 0, skinPrevH = 0, skinHeadW = 0, skinHeadH = 0;
    Com<ID2D1Bitmap> skinBmp, headBmp;
    std::wstring headKey;                       // для какой учётки подтянута голова в сайдбар

    // логотип в сайдбаре (midnight.png)
    Com<ID2D1Bitmap> logoBmp;
    Com<ID2D1RoundedRectangleGeometry> logoGeo;
    Com<ID2D1Layer> logoLayer;

    // иконки
    std::thread iconThread;
    std::mutex iconMu;
    std::condition_variable iconCv;
    std::deque<std::pair<std::shared_ptr<Item>, std::wstring>> iconQ;
    bool iconQuit = false;

    Com<ID2D1Factory> d2d;
    Com<IDWriteFactory> dw;
    Com<ID2D1HwndRenderTarget> rt;
    Com<ID2D1Layer> layer;

    Com<ID2D1SolidColorBrush> brSide, brCard, brCardHi, brBorder, brInput, brText, brDim,
                              brAccent, brAccentHi, brAccentSoft, brGreen, brYellow, brPink, brRed, brWhite,
                              brOverlay, brSw[6];
    Com<ID2D1LinearGradientBrush> brGrad;

    Com<IDWriteTextFormat> fTitle, fH2, fBody, fBodyWrap, fSmall, fTiny, fTinyC, fLogo, fLogoSub,
                           fBtn, fAvatar, fLogoMark, fDesc, fMono, fRow, fCode, fModal, fModalSub,
                           fField;
    Com<IDWriteInlineObject> ellipsis;
};

// ── строки ────────────────────────────────────────────────────────────────────
static std::wstring W(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

static std::string U8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string r((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), r.data(), n, nullptr, nullptr);
    return r;
}

static std::string UrlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    o.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            o += (char)c;
        } else {
            o += '%';
            o += hex[c >> 4];
            o += hex[c & 15];
        }
    }
    return o;
}

static std::wstring FmtCount(long long n) {
    int l = g_lang.load(std::memory_order_relaxed);
    wchar_t buf[64];
    if (l == LANG_ZH || l == LANG_JA) {
        if (n >= 100000000)
            swprintf(buf, 64, l == LANG_ZH ? L"%.2f亿" : L"%.2f億", (double)n / 1e8);
        else if (n >= 10000)
            swprintf(buf, 64, L"%.1f万", (double)n / 1e4);
        else
            swprintf(buf, 64, L"%lld", n);
        return buf;
    }
    bool comma = (l == LANG_RU || l == LANG_FR || l == LANG_DE);
    const wchar_t* unit = L"";
    if (n >= 1000000) {
        unit = l == LANG_RU ? L" млн" : l == LANG_FR ? L" M" : l == LANG_DE ? L" Mio." : L"M";
        swprintf(buf, 64, L"%.1f", (double)n / 1e6);
    } else if (n >= 1000) {
        unit = l == LANG_RU ? L" тыс." : l == LANG_FR ? L" k" : l == LANG_DE ? L" Tsd." : L"K";
        swprintf(buf, 64, L"%.1f", (double)n / 1e3);
    } else {
        swprintf(buf, 64, L"%lld", n);
    }
    std::wstring s = buf;
    if (comma) std::replace(s.begin(), s.end(), L'.', L',');
    return s + unit;
}

static std::wstring FmtSize(unsigned long long n) {
    int l = g_lang.load(std::memory_order_relaxed);
    bool comma = (l == LANG_RU || l == LANG_FR || l == LANG_DE);
    const wchar_t* mb = l == LANG_RU ? L"МБ" : l == LANG_FR ? L"Mo" : L"MB";
    const wchar_t* kb = l == LANG_RU ? L"КБ" : l == LANG_FR ? L"Ko" : L"KB";
    const wchar_t* b  = l == LANG_RU ? L"Б"  : l == LANG_FR ? L"o"  : L"B";
    wchar_t buf[64];
    if (n >= 1024ull * 1024) swprintf(buf, 64, L"%.1f %s", (double)n / (1024.0 * 1024.0), mb);
    else if (n >= 1024) swprintf(buf, 64, L"%.0f %s", (double)n / 1024.0, kb);
    else swprintf(buf, 64, L"%llu %s", n, b);
    std::wstring s = buf;
    if (comma) std::replace(s.begin(), s.end(), L'.', L',');
    return s;
}

// Склонение по числу для текущего языка.
static std::wstring Plural(unsigned long long n, const wchar_t* one, const wchar_t* few,
                           const wchar_t* many) {
    int l = g_lang.load(std::memory_order_relaxed);
    enum Kind { MOD, FILE, WORLD, OTHER } kind = OTHER;
    if (wcscmp(one, L"мод") == 0) kind = MOD;
    else if (wcscmp(one, L"файл") == 0) kind = FILE;
    else if (wcscmp(one, L"мир") == 0) kind = WORLD;
    switch (l) {
    case LANG_EN:
        if (kind == MOD) return n == 1 ? L"mod" : L"mods";
        if (kind == FILE) return n == 1 ? L"file" : L"files";
        if (kind == WORLD) return n == 1 ? L"world" : L"worlds";
        break;
    case LANG_FR:
        if (kind == MOD) return n <= 1 ? L"mod" : L"mods";
        if (kind == FILE) return n <= 1 ? L"fichier" : L"fichiers";
        if (kind == WORLD) return n <= 1 ? L"monde" : L"mondes";
        break;
    case LANG_DE:
        if (kind == MOD) return n == 1 ? L"Mod" : L"Mods";
        if (kind == FILE) return n == 1 ? L"Datei" : L"Dateien";
        if (kind == WORLD) return n == 1 ? L"Welt" : L"Welten";
        break;
    case LANG_ZH:
        if (kind == MOD) return L"个模组";
        if (kind == FILE) return L"个文件";
        if (kind == WORLD) return L"个世界";
        break;
    case LANG_JA:
        if (kind == MOD) return L"個のMOD";
        if (kind == FILE) return L"個のファイル";
        if (kind == WORLD) return L"個のワールド";
        break;
    default:
        break;
    }
    if (l == LANG_RU) {
        unsigned long long m100 = n % 100, m10 = n % 10;
        if (m100 >= 11 && m100 <= 14) return many;
        if (m10 == 1) return one;
        if (m10 >= 2 && m10 <= 4) return few;
        return many;
    }
    return one;
}

static std::wstring LoaderName(const std::wstring& l) {
    if (l == L"fabric") return L"Fabric";
    if (l == L"quilt") return L"Quilt";
    if (l == L"forge") return L"Forge";
    if (l == L"neoforge") return L"NeoForge";
    return l.empty() ? (g_lang.load(std::memory_order_relaxed) == LANG_RU ? L"ваниль" : L"Vanilla") : l;
}

// ── HTTP ──────────────────────────────────────────────────────────────────────
struct UrlParts {
    std::wstring host, path;
    INTERNET_PORT port = 443;
    bool https = true;
};

static bool ParseUrl(const std::wstring& url, UrlParts& p) {
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{}, path[4096]{}, extra[2048]{};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 4096;
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc)) return false;
    p.host = host;
    p.path = path;
    p.path += extra;
    if (p.path.empty()) p.path = L"/";
    p.port = uc.nPort;
    p.https = uc.nScheme == INTERNET_SCHEME_HTTPS;
    return true;
}

static HttpResult HttpRequest(const std::wstring& method, const std::wstring& url,
                              const std::wstring& apiKey = L"", const std::string& body = {},
                              const std::wstring& ctype = L"application/json",
                              const std::vector<std::pair<std::wstring, std::wstring>>& headers = {}) {
    HttpResult r;
    UrlParts u;
    if (!ParseUrl(url, u)) { r.error = L"плохой URL"; return r; }

    HINTERNET ses = WinHttpOpen(L"MidnightLauncher/2.0 (C++)", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { r.error = L"WinHttpOpen"; return r; }
    WinHttpSetTimeouts(ses, 8000, 8000, 10000, 15000);

    HINTERNET con = WinHttpConnect(ses, u.host.c_str(), u.port, 0);
    if (!con) { WinHttpCloseHandle(ses); r.error = L"нет соединения"; return r; }

    HINTERNET req = WinHttpOpenRequest(con, method.c_str(), u.path.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       u.https ? WINHTTP_FLAG_SECURE : 0);
    if (!req) { WinHttpCloseHandle(con); WinHttpCloseHandle(ses); r.error = L"WinHttpOpenRequest"; return r; }

    WinHttpAddRequestHeaders(req, L"Accept: application/json", -1, WINHTTP_ADDREQ_FLAG_ADD);
    WinHttpAddRequestHeaders(req, L"User-Agent: MidnightLauncher/2.0", -1, WINHTTP_ADDREQ_FLAG_ADD);
    if (!apiKey.empty()) {
        std::wstring h = L"x-api-key: " + apiKey;
        WinHttpAddRequestHeaders(req, h.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);
    }
    if (!ctype.empty() && (!body.empty() || method == L"POST")) {
        std::wstring h = L"Content-Type: " + ctype;
        WinHttpAddRequestHeaders(req, h.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);
    }
    for (auto& h : headers) {
        if (h.first.empty()) continue;
        std::wstring line = h.first + L": " + h.second;
        WinHttpAddRequestHeaders(req, line.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);
    }

    BOOL ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                                 (DWORD)body.size(), (DWORD)body.size(), 0);
    if (!ok || !WinHttpReceiveResponse(req, nullptr)) {
        DWORD e = GetLastError();
        r.error = L"сеть (код " + std::to_wstring(e) + L")";
        WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
        return r;
    }

    DWORD status = 0, slen = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &slen, WINHTTP_NO_HEADER_INDEX);
    r.status = (int)status;

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
        std::string buf(avail, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(req, buf.data(), avail, &read) || read == 0) break;
        buf.resize(read);
        r.body += buf;
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return r;
}

static bool HttpDownload(const std::wstring& url, const fs::path& dest,
                         const std::function<void(long long, long long)>& progress,
                         std::wstring& err) {
    UrlParts u;
    if (!ParseUrl(url, u)) { err = L"плохой URL файла"; return false; }

    HINTERNET ses = WinHttpOpen(L"MidnightLauncher/2.0 (C++)", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { err = L"WinHttpOpen"; return false; }
    WinHttpSetTimeouts(ses, 12000, 12000, 30000, 60000);

    HINTERNET con = WinHttpConnect(ses, u.host.c_str(), u.port, 0);
    if (!con) { WinHttpCloseHandle(ses); err = L"нет соединения"; return false; }

    HINTERNET req = WinHttpOpenRequest(con, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       u.https ? WINHTTP_FLAG_SECURE : 0);
    if (!req) {
        WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
        err = L"WinHttpOpenRequest"; return false;
    }
    WinHttpAddRequestHeaders(req, L"User-Agent: MidnightLauncher/2.0", -1, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req, nullptr)) {
        err = L"файл не скачивается";
        WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
        return false;
    }

    DWORD status = 0, slen = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &slen, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        err = L"сервер вернул " + std::to_wstring(status);
        WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
        return false;
    }

    long long total = 0;
    wchar_t lenBuf[64]{};
    DWORD lenSize = sizeof(lenBuf);
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                            lenBuf, &lenSize, WINHTTP_NO_HEADER_INDEX)) {
        total = _wtoi64(lenBuf);
    }

    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    std::ofstream f(dest, std::ios::binary | std::ios::trunc);
    if (!f) { err = L"не открывается файл для записи"; WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses); return false; }

    long long got = 0;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
        std::string buf(avail, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(req, buf.data(), avail, &read) || read == 0) break;
        f.write(buf.data(), read);
        got += read;
        if (progress) progress(got, total);
    }
    f.close();
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);

    if (got == 0) { err = L"пустой ответ"; fs::remove(dest, ec); return false; }
    return true;
}

// ── картинки (WIC) ────────────────────────────────────────────────────────────
static bool DecodeImage(const std::string& bytes, ImageData& out) {
    if (bytes.empty()) return false;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool needUninit = SUCCEEDED(hr);

    Com<IWICImagingFactory> wic;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(wic.put()));
    if (FAILED(hr)) { if (needUninit) CoUninitialize(); return false; }

    Com<IStream> stream;
    stream.p = SHCreateMemStream((const BYTE*)bytes.data(), (UINT)bytes.size());
    if (!stream) { if (needUninit) CoUninitialize(); return false; }

    Com<IWICBitmapDecoder> dec;
    if (FAILED(wic->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnDemand,
                                            dec.put()))) {
        if (needUninit) CoUninitialize();
        return false;
    }

    Com<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, frame.put()))) { if (needUninit) CoUninitialize(); return false; }

    Com<IWICFormatConverter> conv;
    if (FAILED(wic->CreateFormatConverter(conv.put())) ||
        FAILED(conv->Initialize(frame.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
        if (needUninit) CoUninitialize();
        return false;
    }

    UINT w = 0, h = 0;
    conv->GetSize(&w, &h);
    if (!w || !h) { if (needUninit) CoUninitialize(); return false; }
    out.w = (int)w;
    out.h = (int)h;
    out.px.resize((size_t)w * h * 4);
    if (FAILED(conv->CopyPixels(nullptr, w * 4, (UINT)out.px.size(), out.px.data()))) {
        if (needUninit) CoUninitialize();
        return false;
    }
    if (needUninit) CoUninitialize();
    return true;
}

// ── скин: текстура, рендер и API Minecraft / Ely.by ───────────────────────────
static const wchar_t* const MC_PROFILE_C      = L"https://api.minecraftservices.com/minecraft/profile";
static const wchar_t* const MC_SKINS_C        = L"https://api.minecraftservices.com/minecraft/profile/skins";
static const wchar_t* const MC_CAPES_C        = L"https://api.minecraftservices.com/minecraft/profile/capes/active";
static const wchar_t* const MOJANG_LOOKUP_C   = L"https://api.minecraftservices.com/minecraft/profile/lookup/name";
static const wchar_t* const MOJANG_LOOKUP_ALT_C = L"https://api.mojang.com/users/profiles/minecraft";
static const wchar_t* const SESSION_API_C     = L"https://sessionserver.mojang.com/session/minecraft/profile";
static const wchar_t* const ELY_SKIN_C        = L"https://skinsystem.ely.by";
static const wchar_t* const ELY_AUTH_C        = L"https://authserver.ely.by";

struct SkinPx {
    int w = 0, h = 0;
    std::vector<unsigned char> rgba;      // прямые RGBA
};

// PNG скина в прямые RGBA-пиксели.
static bool DecodeSkinPng(const std::string& bytes, SkinPx& out) {
    if (bytes.empty()) return false;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool needUninit = SUCCEEDED(hr);
    Com<IWICImagingFactory> wic;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(wic.put()));
    if (FAILED(hr)) { if (needUninit) CoUninitialize(); return false; }
    Com<IStream> stream;
    stream.p = SHCreateMemStream((const BYTE*)bytes.data(), (UINT)bytes.size());
    if (!stream) { if (needUninit) CoUninitialize(); return false; }
    Com<IWICBitmapDecoder> dec;
    if (FAILED(wic->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnDemand,
                                            dec.put()))) {
        if (needUninit) CoUninitialize();
        return false;
    }
    Com<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, frame.put()))) { if (needUninit) CoUninitialize(); return false; }
    Com<IWICFormatConverter> conv;
    if (FAILED(wic->CreateFormatConverter(conv.put())) ||
        FAILED(conv->Initialize(frame.get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
        if (needUninit) CoUninitialize();
        return false;
    }
    UINT uw = 0, uh = 0;
    conv->GetSize(&uw, &uh);
    if (!uw || !uh) { if (needUninit) CoUninitialize(); return false; }
    out.w = (int)uw;
    out.h = (int)uh;
    out.rgba.assign((size_t)uw * uh * 4, 0);
    if (FAILED(conv->CopyPixels(nullptr, uw * 4, (UINT)out.rgba.size(), out.rgba.data()))) {
        if (needUninit) CoUninitialize();
        return false;
    }
    if (needUninit) CoUninitialize();
    return true;
}

static bool SkinValid(const SkinPx& s) {
    return s.w == 64 && (s.h == 32 || s.h == 64) &&
           (size_t)s.w * s.h * 4 == s.rgba.size();
}

// Тонкая модель (Alex): на обеих руках справа прозрачная полоса.
static bool SkinIsSlim(const SkinPx& s) {
    if (s.h < 64) return false;
    auto alpha = [&](int x, int y) { return s.rgba[((size_t)y * s.w + x) * 4 + 3]; };
    const int px[2] = { 44 + 3, 36 + 3 };
    const int py[2] = { 20, 52 };
    for (int p = 0; p < 2; ++p)
        for (int i = 0; i < 12; ++i)
            if (alpha(px[p], py[p] + i) > 0) return false;
    return true;
}

// Наложение прямоугольника текстуры на канву (как Image.paste с маской).
static void SkinBlit(const SkinPx& s, int x0, int y0, int w, int h,
                     std::vector<unsigned char>& dst, int dw, int dh,
                     int x, int y, bool flip) {
    for (int j = 0; j < h; ++j) {
        int sy = y0 + j, dy = y + j;
        if (sy < 0 || sy >= s.h || dy < 0 || dy >= dh) continue;
        for (int i = 0; i < w; ++i) {
            int sx = x0 + (flip ? (w - 1 - i) : i), dx = x + i;
            if (sx < 0 || sx >= s.w || dx < 0 || dx >= dw) continue;
            const unsigned char* sp = &s.rgba[((size_t)sy * s.w + sx) * 4];
            unsigned char* dp = &dst[((size_t)dy * dw + dx) * 4];
            float a = sp[3] / 255.f;
            for (int c = 0; c < 4; ++c)
                dp[c] = (unsigned char)std::lround(dp[c] + (sp[c] - dp[c]) * a);
        }
    }
}

// Базовый слой + слой-оверлей (у старых скинов 64×32 его нет).
static void SkinPart(const SkinPx& s, int x0, int y0, int ox, int oy,
                     std::vector<unsigned char>& dst, int dw, int dh,
                     int x, int y, int w, int h, bool legacy, bool flip = false) {
    SkinBlit(s, x0, y0, w, h, dst, dw, dh, x, y, flip);
    if (!legacy) SkinBlit(s, ox, oy, w, h, dst, dw, dh, x, y, flip);
}

// Вид скина спереди (как render_skin); на выходе premultiplied BGRA.
static bool RenderSkin(const SkinPx& s, bool slim, int scale,
                       std::vector<unsigned char>& out, int& ow, int& oh) {
    if (!SkinValid(s) || scale < 1) return false;
    bool legacy = s.h == 32;
    int aw = slim ? 3 : 4;
    int W = aw * 2 + 8, H = 32;
    std::vector<unsigned char> canvas((size_t)W * H * 4, 0);
    int bx = aw;

    SkinPart(s, 8, 8, 40, 8, canvas, W, H, bx, 0, 8, 8, legacy);          // голова
    SkinPart(s, 20, 20, 20, 36, canvas, W, H, bx, 8, 8, 12, legacy);      // тело
    SkinPart(s, 44, 20, 44, 36, canvas, W, H, 0, 8, aw, 12, legacy);      // правая рука
    if (legacy) {
        SkinBlit(s, 44, 20, aw, 12, canvas, W, H, bx + 8, 8, true);
    } else {
        SkinBlit(s, 36, 52, aw, 12, canvas, W, H, bx + 8, 8, false);
        SkinBlit(s, 52, 52, aw, 12, canvas, W, H, bx + 8, 8, false);
    }
    SkinPart(s, 4, 20, 4, 36, canvas, W, H, bx, 20, 4, 12, legacy);       // правая нога
    if (legacy) {
        SkinBlit(s, 4, 20, 4, 12, canvas, W, H, bx + 4, 20, true);
    } else {
        SkinBlit(s, 20, 52, 4, 12, canvas, W, H, bx + 4, 20, false);
        SkinBlit(s, 4, 52, 4, 12, canvas, W, H, bx + 4, 20, false);
    }

    ow = W * scale;
    oh = H * scale;
    out.assign((size_t)ow * oh * 4, 0);
    for (int y = 0; y < oh; ++y)
        for (int x = 0; x < ow; ++x) {
            const unsigned char* sp = &canvas[((size_t)(y / scale) * W + (x / scale)) * 4];
            unsigned char* dp = &out[((size_t)y * ow + x) * 4];
            unsigned a = sp[3];
            dp[0] = (unsigned char)(sp[2] * a / 255);   // B
            dp[1] = (unsigned char)(sp[1] * a / 255);   // G
            dp[2] = (unsigned char)(sp[0] * a / 255);   // R
            dp[3] = (unsigned char)a;
        }
    return true;
}

// Голова скина, обрезанная по кругу (аватарка в сайдбаре), premultiplied BGRA.
static bool RenderSkinHead(const SkinPx& s, int size,
                           std::vector<unsigned char>& out, int& ow, int& oh) {
    if (!SkinValid(s) || size < 1) return false;
    unsigned char head[8 * 8 * 4];
    memset(head, 0, sizeof(head));
    auto put = [&](int dx, int dy, const unsigned char* sp) {
        float a = sp[3] / 255.f;
        unsigned char* dp = &head[(dy * 8 + dx) * 4];
        for (int c = 0; c < 4; ++c)
            dp[c] = (unsigned char)std::lround(dp[c] + (sp[c] - dp[c]) * a);
    };
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            put(x, y, &s.rgba[((size_t)(8 + y) * s.w + (8 + x)) * 4]);
    if (s.h >= 64)
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                put(x, y, &s.rgba[((size_t)(8 + y) * s.w + (40 + x)) * 4]);

    ow = oh = size;
    out.assign((size_t)size * size * 4, 0);
    float r = size / 2.f;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            float dx = x + 0.5f - r, dy = y + 0.5f - r;
            float cov = std::clamp(r - std::sqrt(dx * dx + dy * dy) + 0.5f, 0.f, 1.f);
            const unsigned char* sp = &head[((y * 8 / size) * 8 + (x * 8 / size)) * 4];
            unsigned char* dp = &out[((size_t)y * size + x) * 4];
            unsigned a = (unsigned)(sp[3] * cov);
            dp[0] = (unsigned char)(sp[2] * a / 255);
            dp[1] = (unsigned char)(sp[1] * a / 255);
            dp[2] = (unsigned char)(sp[0] * a / 255);
            dp[3] = (unsigned char)a;
        }
    return true;
}

static std::vector<unsigned char> B64Decode(const std::string& in) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<unsigned char> out;
    int buf = 0, bits = 0;
    for (char c : in) {
        int v = val(c);
        if (v < 0) continue;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((unsigned char)((buf >> bits) & 0xFF)); }
    }
    return out;
}

static std::string RandomAscii(size_t n) {
    static const char* hx = "0123456789abcdef";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> d(0, 15);
    std::string s;
    s.reserve(n);
    for (size_t i = 0; i < n; ++i) s += hx[d(gen)];
    return s;
}

static std::wstring UrlQuoteW(const std::wstring& s) { return W(UrlEncode(U8(s))); }

static std::string GetBytes(const std::wstring& url) {
    HttpResult r = HttpRequest(L"GET", url);
    return r.status == 200 ? r.body : std::string();
}

static std::string GetJsonAuth(const std::wstring& url, const std::wstring& tok, int& status) {
    HttpResult r = HttpRequest(L"GET", url, L"", {}, L"", { { L"Authorization", L"Bearer " + tok } });
    status = r.status;
    return r.body;
}

static std::string Multipart(const std::vector<std::pair<std::string, std::string>>& fields,
                             const std::string& fileField, const std::string& fileName,
                             const std::string& fileCtype, const std::string& fileData,
                             std::wstring& ctypeOut) {
    std::string b = "----Nexus" + RandomAscii(16);
    std::string out;
    for (auto& f : fields) {
        out += "--" + b + "\r\n";
        out += "Content-Disposition: form-data; name=\"" + f.first + "\"\r\n\r\n";
        out += f.second + "\r\n";
    }
    if (!fileField.empty()) {
        out += "--" + b + "\r\n";
        out += "Content-Disposition: form-data; name=\"" + fileField +
               "\"; filename=\"" + fileName + "\"\r\n";
        out += "Content-Type: " + fileCtype + "\r\n\r\n";
        out += fileData + "\r\n";
    }
    out += "--" + b + "--\r\n";
    ctypeOut = W("multipart/form-data; boundary=" + b);
    return out;
}

struct SkinProfile {
    std::string png;
    bool slim = false;
    std::wstring label, error;
    std::vector<CapeInfo> capes;
};

static std::wstring HttpErrorDetail(const std::string& body) {
    try {
        auto j = nlohmann::json::parse(body);
        if (j.is_object()) {
            if (j.contains("errorMessage") && j["errorMessage"].is_string())
                return W(j["errorMessage"].get<std::string>());
            if (j.contains("error") && j["error"].is_string())
                return W(j["error"].get<std::string>());
        }
    } catch (...) {}
    return {};
}

static std::wstring SkinErrMsg(int code, const std::string& body, bool ely) {
    std::wstring d = HttpErrorDetail(body);
    if (ely) {
        if (code == 401 || code == 403) return L"Ely.by отклонил запрос — войдите в аккаунт заново";
        if (code == 404) return L"Ely.by не нашёл профиль аккаунта";
        if (code == 429) return L"Ely.by: слишком часто — подождите минуту";
        if (code == 400) return L"Ely.by не принял файл: " + (d.empty() ? L"PNG 64×32 или 64×64" : d);
        if (code == 0) return L"Ely.by недоступен";
        return L"Ely.by: " + (d.empty() ? std::to_wstring(code) : d);
    }
    if (code == 401) return L"сессия истекла — выйдите и войдите в аккаунт заново";
    if (code == 403) return L"сервис отклонил запрос: нет профиля Java или аккаунт заблокирован";
    if (code == 404) return L"у аккаунта нет профиля Minecraft (нужна лицензия)";
    if (code == 429) return L"слишком часто — подождите минуту и попробуйте снова";
    if (code == 400) return L"скин не принят: " + (d.empty() ? L"проверьте формат PNG 64×64" : d);
    if (code >= 500) return L"серверы Mojang недоступны, попробуйте позже";
    return L"ошибка " + std::to_wstring(code) + (d.empty() ? L"" : L": " + d);
}

// Профиль: текущий скин, модель и плащи.
static SkinProfile FetchSkinProfile(bool ely, const std::wstring& tok, const std::wstring& name) {
    SkinProfile si;
    if (ely) {
        std::string body = GetBytes(std::wstring(ELY_SKIN_C) + L"/textures/" + UrlQuoteW(name));
        std::string url;
        if (!body.empty()) {
            try {
                auto j = nlohmann::json::parse(body);
                auto sk = j.value("SKIN", nlohmann::json::object());
                url = sk.value("url", "");
                if (sk.contains("metadata") && sk["metadata"].is_object())
                    si.slim = sk["metadata"].value("model", "") == "slim";
            } catch (...) {}
        }
        if (!url.empty()) si.png = GetBytes(W(url));
        si.label = si.png.empty() ? L"текущий скин · " + name : L"сейчас в игре · " + name;
        return si;
    }
    int status = 0;
    std::string body = GetJsonAuth(MC_PROFILE_C, tok, status);
    if (status != 200) { si.error = SkinErrMsg(status, body, false); return si; }
    try {
        auto j = nlohmann::json::parse(body);
        std::string url;
        for (auto& sk : j.value("skins", nlohmann::json::array())) {
            if (sk.value("state", "") == "ACTIVE") {
                url = sk.value("url", "");
                si.slim = sk.value("variant", "") == "SLIM";
                break;
            }
        }
        if (!url.empty()) si.png = GetBytes(W(url));
        si.label = si.png.empty() ? L"текущий скин · " + name : L"сейчас в игре · " + name;
        for (auto& c : j.value("capes", nlohmann::json::array())) {
            CapeInfo ci;
            ci.id = W(c.value("id", ""));
            ci.alias = W(c.value("alias", ""));
            if (ci.alias.empty()) ci.alias = ci.id.substr(0, std::min<size_t>(8, ci.id.size()));
            ci.active = c.value("state", "") == "ACTIVE";
            si.capes.push_back(ci);
        }
    } catch (...) {
        si.error = L"не удалось прочитать профиль Minecraft";
    }
    return si;
}

// Скин по нику: официальные API Mojang, иначе Ely.by (он проксирует Mojang).
static bool SkinByName(const std::wstring& name, std::string& png, bool& slim,
                       std::wstring& real, std::wstring& err) {
    std::wstring q = UrlQuoteW(name);
    std::string uid;
    for (const wchar_t* api : { MOJANG_LOOKUP_C, MOJANG_LOOKUP_ALT_C }) {
        std::string b = GetBytes(std::wstring(api) + L"/" + q);
        if (b.empty()) continue;
        try {
            auto j = nlohmann::json::parse(b);
            uid = j.value("id", "");
            real = W(j.value("name", std::string()));
        } catch (...) {}
        if (!uid.empty()) break;
    }
    if (!uid.empty()) {
        std::string prof = GetBytes(std::wstring(SESSION_API_C) + L"/" + W(uid));
        try {
            auto j = nlohmann::json::parse(prof);
            if (real.empty()) real = W(j.value("name", ""));
            for (auto& p : j.value("properties", nlohmann::json::array())) {
                if (p.value("name", "") != "textures") continue;
                auto dec = B64Decode(p.value("value", ""));
                auto t = nlohmann::json::parse(std::string(dec.begin(), dec.end()));
                auto sk = t.value("textures", nlohmann::json::object()).value("SKIN", nlohmann::json::object());
                std::string url = sk.value("url", "");
                if (url.empty()) continue;
                png = GetBytes(W(url));
                if (sk.contains("metadata") && sk["metadata"].is_object())
                    slim = sk["metadata"].value("model", "") == "slim";
                if (!png.empty()) return true;
            }
        } catch (...) {}
    }
    // Ely.by (в т.ч. лицензионные ники)
    std::string body = GetBytes(std::wstring(ELY_SKIN_C) + L"/textures/" + q);
    if (!body.empty()) {
        try {
            auto j = nlohmann::json::parse(body);
            auto sk = j.value("SKIN", nlohmann::json::object());
            std::string url = sk.value("url", "");
            if (!url.empty()) {
                png = GetBytes(W(url));
                if (sk.contains("metadata") && sk["metadata"].is_object())
                    slim = sk["metadata"].value("model", "") == "slim";
                if (!png.empty()) { real = name; return true; }
            }
        } catch (...) {}
    }
    err = L"у игрока нет скина или ник не найден";
    return false;
}

static std::wstring UploadSkinApi(bool ely, const std::wstring& tok, const std::wstring& uid,
                                  const std::string& png, bool slim) {
    std::wstring ct;
    std::string body;
    std::wstring url;
    if (ely) {
        body = Multipart({ { "model", slim ? "slim" : "" } }, "skin", "skin.png", "image/png", png, ct);
        url = std::wstring(ELY_AUTH_C) + L"/api/authlib-injector/api/user/profile/" + uid + L"/skin";
    } else {
        body = Multipart({ { "variant", slim ? "slim" : "classic" } }, "file", "skin.png",
                         "image/png", png, ct);
        url = MC_SKINS_C;
    }
    HttpResult r = HttpRequest(ely ? L"PUT" : L"POST", url, L"", body, ct,
                               { { L"Authorization", L"Bearer " + tok } });
    if (r.status >= 200 && r.status < 300) return {};
    return SkinErrMsg(r.status, r.body, ely);
}

static std::wstring DeleteSkinApi(bool ely, const std::wstring& tok, const std::wstring& uid) {
    std::wstring url = ely ? std::wstring(ELY_AUTH_C) +
                                 L"/api/authlib-injector/api/user/profile/" + uid + L"/skin"
                           : std::wstring(MC_SKINS_C) + L"/active";
    HttpResult r = HttpRequest(L"DELETE", url, L"", {}, L"",
                               { { L"Authorization", L"Bearer " + tok } });
    if (r.status >= 200 && r.status < 300) return {};
    return SkinErrMsg(r.status, r.body, ely);
}

// capeId пустой — «не носить».
static std::wstring CapeApi(const std::wstring& tok, const std::wstring& capeId) {
    if (capeId.empty()) {
        HttpResult r = HttpRequest(L"DELETE", MC_CAPES_C, L"", {}, L"",
                                   { { L"Authorization", L"Bearer " + tok } });
        if (r.status >= 200 && r.status < 300) return {};
        return SkinErrMsg(r.status, r.body, false);
    }
    std::string body = "{\"capeId\":\"" + U8(capeId) + "\"}";
    HttpResult r = HttpRequest(L"PUT", MC_CAPES_C, L"", body, L"application/json",
                               { { L"Authorization", L"Bearer " + tok } });
    if (r.status >= 200 && r.status < 300) return {};
    return SkinErrMsg(r.status, r.body, false);
}

// ── папки и настройки ─────────────────────────────────────────────────────────
static fs::path NexusDir() {
    wchar_t* appdata = nullptr;
    size_t n = 0;
    _wdupenv_s(&appdata, &n, L"APPDATA");
    fs::path p = appdata ? fs::path(appdata) : fs::path();
    free(appdata);
    return p / L".minecraft" / L"nexus";
}

static fs::path McDir() {
    wchar_t* appdata = nullptr;
    size_t n = 0;
    _wdupenv_s(&appdata, &n, L"APPDATA");
    fs::path p = appdata ? fs::path(appdata) : fs::path();
    free(appdata);
    return p / L".minecraft";
}

// ── ресурсы: рядом с exe (папка resources главнее), как в Python-версии ───────
static fs::path ExeDir() {
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0) return fs::current_path();
    return fs::path(std::wstring(buf, n)).parent_path();
}

static fs::path ResPath(const wchar_t* name) {
    std::error_code ec;
    fs::path inRes = ExeDir() / L"resources" / name;
    if (fs::exists(inRes, ec)) return inRes;
    fs::path beside = ExeDir() / name;
    if (fs::exists(beside, ec)) return beside;
    return inRes;
}

static fs::path ResDir(const wchar_t* name) {
    std::error_code ec;
    fs::path inRes = ExeDir() / L"resources" / name;
    if (fs::is_directory(inRes, ec)) return inRes;
    fs::path beside = ExeDir() / name;
    if (fs::is_directory(beside, ec)) return beside;
    return inRes;
}

static void LoadProfiles(App& a) {
    a.profiles.clear();
    std::ifstream f(NexusDir() / L"profiles.json", std::ios::binary);
    if (f) {
        std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        try {
            auto j = nlohmann::json::parse(s);
            if (j.is_array()) {
                for (auto& p : j) {
                    Profile pr;
                    pr.name = W(p.value("name", ""));
                    pr.dir = W(p.value("game_dir", ""));
                    pr.loader = W(p.value("loader", ""));
                    pr.mc = W(p.value("mc_version", ""));
                    pr.loaderVer = W(p.value("loader_version", ""));
                    pr.version = W(p.value("version", ""));
                    pr.valid = !pr.dir.empty();
                    if (pr.name.empty()) pr.name = pr.dir;
                    if (pr.version.empty()) pr.version = pr.mc;
                    a.profiles.push_back(std::move(pr));
                }
            }
        } catch (...) {}
    }
    if (!a.profiles.empty()) {
        a.profileIdx = 0;
        a.profile = a.profiles[0];
        a.hasProfile = a.profile.valid;
    } else {
        a.profile = Profile{};
        a.profileIdx = 0;
        a.hasProfile = false;
    }
}

static void SaveProfiles(App& a) {
    nlohmann::json arr = nlohmann::json::array();
    for (auto& p : a.profiles) {
        nlohmann::json o = nlohmann::json::object();
        o["name"] = U8(p.name);
        o["version"] = U8(p.version.empty() ? p.mc : p.version);
        o["group"] = "default";
        o["game_dir"] = U8(p.dir);
        o["loader"] = U8(p.loader.empty() ? std::wstring(L"vanilla") : p.loader);
        o["loader_version"] = U8(p.loaderVer);
        o["mc_version"] = U8(p.mc);
        arr.push_back(std::move(o));
    }
    std::error_code ec;
    fs::create_directories(NexusDir(), ec);
    std::ofstream f(NexusDir() / L"profiles.json", std::ios::binary | std::ios::trunc);
    if (f) f << arr.dump(2);
}

static void LoadSettings(App& a) {
    fs::path sp = NexusDir() / L"settings.json";
    std::ifstream f(sp, std::ios::binary);
    if (f) {
        std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        try {
            auto j = nlohmann::json::parse(s);
            auto str = [&](const char* k, std::wstring& dst) {
                if (j.contains(k) && j[k].is_string()) {
                    std::wstring v = W(j[k].get<std::string>());
                    if (!v.empty()) dst = v;
                }
            };
            str("theme", a.theme);
            str("accent", a.accent);
            str("accent2", a.accent2);
            str("ram_min", a.ramMin);
            str("ram_max", a.ramMax);
            str("java_path", a.javaPathCfg);
            str("java_args", a.javaArgs);
            str("game_args", a.gameArgs);
            str("username", a.username);
            str("language", a.lang);
        } catch (...) {}
    }
    a.lang = LangCode(LangFromCode(a.lang));
    g_lang.store(LangFromCode(a.lang), std::memory_order_relaxed);
    D2D1_COLOR_F tmp;
    if (a.theme != L"light") a.theme = L"dark";
    if (!HexOk(a.accent, tmp)) a.accent = kDefaultAccent;
    if (!HexOk(a.accent2, tmp)) a.accent2 = kDefaultAccent2;
    a.accentEdit = a.accent;
    a.accent2Edit = a.accent2;
    ApplyPalette(a.theme, a.accent, a.accent2);
}

static void SaveSettings(App& a) {
    fs::path p = NexusDir() / L"settings.json";
    nlohmann::json j = nlohmann::json::object();
    // сохраняем ключи, которые пишет Python-версия (fullscreen и прочее)
    std::ifstream in(p, std::ios::binary);
    if (in) {
        std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        try {
            auto old = nlohmann::json::parse(s);
            if (old.is_object()) j = old;
        } catch (...) {}
    }
    j["ram_min"] = U8(a.ramMin);
    j["ram_max"] = U8(a.ramMax);
    j["java_path"] = U8(a.javaPathCfg);
    j["username"] = U8(a.username);
    j["java_args"] = U8(a.javaArgs);
    j["game_args"] = U8(a.gameArgs);
    // Старый ключ внешнего каталога больше не используется — удаляем.
    j.erase("cf_key");
    j["theme"] = U8(a.theme);
    j["accent"] = U8(a.accent);
    j["accent2"] = U8(a.accent2);
    j["language"] = U8(a.lang);
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (f) f << j.dump(2);
}

static fs::path ContentDir(const Profile& p, int tab, std::wstring& note) {
    fs::path base = p.dir;
    switch (tab) {
    case 0: note = L"mods"; return base / L"mods";
    case 1: note = L"resourcepacks"; return base / L"resourcepacks";
    case 2: note = L"shaderpacks"; return base / L"shaderpacks";
    default: {
        fs::path best;
        fs::file_time_type bestT{};
        std::error_code ec;
        fs::path saves = base / L"saves";
        if (fs::exists(saves, ec)) {
            for (auto& e : fs::directory_iterator(saves, ec)) {
                if (!e.is_directory(ec)) continue;
                auto lvl = e.path() / L"level.dat";
                if (!fs::exists(lvl, ec)) continue;
                auto t = fs::last_write_time(lvl, ec);
                if (best.empty() || t > bestT) { best = e.path(); bestT = t; }
            }
        }
        if (!best.empty()) {
            note = L"мир «" + best.filename().wstring() + L"» / datapacks";
            return best / L"datapacks";
        }
        note = L"datapacks (мир не найден)";
        return base / L"datapacks";
    }
    }
}

static void RefreshMods(App& a) {
    a.modsList.clear();
    if (!a.hasProfile) return;
    std::error_code ec;
    fs::path dir = fs::path(a.profile.dir) / L"mods";
    if (!fs::exists(dir, ec)) return;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        if (e.path().extension() != L".jar") continue;
        ModFile m;
        m.name = e.path().filename().wstring();
        m.size = (unsigned long long)e.file_size(ec);
        a.modsList.push_back(std::move(m));
    }
    std::sort(a.modsList.begin(), a.modsList.end(),
              [](const ModFile& x, const ModFile& y) { return x.name < y.name; });
}

static void SelectProfile(App& a, int idx) {
    if (idx < 0 || (size_t)idx >= a.profiles.size()) return;
    if (a.launch && a.launch->active.load()) {
        a.status = L"Сначала дождитесь завершения запуска";
        a.statusCol = C_YELLOW;
        return;
    }
    a.profileIdx = idx;
    a.profile = a.profiles[(size_t)idx];
    a.hasProfile = a.profile.valid;
    RefreshMods(a);
    a.modsScrollY = 0.f;
    a.modsScrollMax = 0.f;
    ++a.modsTree.gen;
    a.modsTree.busy = false;
    a.modsTree.ready = false;
    a.modsTree.rows.clear();
    a.modsTree.note.clear();
    a.modsTree.key.clear();
    a.launch = std::make_shared<LaunchState>();
    a.status = L"Выбрана сборка «" + a.profile.name + L"»";
    a.statusCol = C_DIM;
}

// true, если child лежит внутри parent (без учёта регистра, по компонентам пути)
static bool PathInside(const fs::path& child, const fs::path& parent) {
    std::error_code ec;
    std::wstring c = fs::absolute(child, ec).lexically_normal().wstring();
    std::wstring p = fs::absolute(parent, ec).lexically_normal().wstring();
    if (c.empty() || p.empty() || c.size() <= p.size()) return false;
    std::wstring cl = c, pl = p;
    std::transform(cl.begin(), cl.end(), cl.begin(),
                   [](wchar_t ch) { return (wchar_t)std::towlower(ch); });
    std::transform(pl.begin(), pl.end(), pl.begin(),
                   [](wchar_t ch) { return (wchar_t)std::towlower(ch); });
    if (cl.compare(0, pl.size(), pl) != 0) return false;
    wchar_t next = cl[pl.size()];
    return next == L'\\' || next == L'/';
}

// Удаляет сборку из profiles.json и её папку (только внутри .minecraft\profiles).
static void DeleteProfile(App& a, int idx) {
    if (idx < 0 || (size_t)idx >= a.profiles.size()) {
        a.delUi.open = false;
        a.delUi.idx = -1;
        return;
    }
    if (idx == a.profileIdx && a.launch && a.launch->active.load()) {
        a.status = L"Нельзя удалить: игра запущена или готовится";
        a.statusCol = C_YELLOW;
        a.delUi.open = false;
        a.delUi.idx = -1;
        return;
    }
    Profile p = a.profiles[(size_t)idx];
    bool wasSelected = (idx == a.profileIdx);
    std::error_code ec;
    bool removedFiles = false;
    fs::path dir(p.dir);
    if (!dir.empty() && fs::exists(dir, ec) && PathInside(dir, McDir() / L"profiles")) {
        fs::remove_all(dir, ec);
        removedFiles = !ec;
    }
    a.profiles.erase(a.profiles.begin() + idx);
    if (idx < a.profileIdx) --a.profileIdx;
    if (a.profileIdx >= (int)a.profiles.size()) a.profileIdx = (int)a.profiles.size() - 1;
    if (a.profileIdx < 0) a.profileIdx = 0;
    a.delUi.open = false;
    a.delUi.idx = -1;
    a.modsScrollY = 0.f;
    if (a.profiles.empty()) {
        a.profile = Profile{};
        a.hasProfile = false;
        a.modsList.clear();
        ++a.modsTree.gen;
        a.modsTree.busy = false;
        a.modsTree.ready = false;
        a.modsTree.rows.clear();
        a.modsTree.note.clear();
        a.modsTree.key.clear();
        if (wasSelected) a.launch = std::make_shared<LaunchState>();
    } else {
        a.profile = a.profiles[(size_t)a.profileIdx];
        a.hasProfile = a.profile.valid;
        RefreshMods(a);
        ++a.modsTree.gen;
        a.modsTree.busy = false;
        a.modsTree.ready = false;
        a.modsTree.rows.clear();
        a.modsTree.note.clear();
        a.modsTree.key.clear();
        if (wasSelected) a.launch = std::make_shared<LaunchState>();
    }
    SaveProfiles(a);
    a.status = removedFiles
        ? L"Сборка «" + p.name + L"» удалена"
        : L"Сборка «" + p.name + L"» удалена из списка";
    a.statusCol = C_GREEN;
    InvalidateRect(a.hwnd, nullptr, FALSE);
}

// ── сеть: каталог ─────────────────────────────────────────────────────────────
static void PostUI(HWND hwnd, std::function<void()> fn) {
    auto* f = new std::function<void()>(std::move(fn));
    if (!PostMessageW(hwnd, WM_APP + 1, 0, (LPARAM)f)) delete f;
}

static void Repaint(HWND hwnd) {
    PostMessageW(hwnd, WM_APP + 2, 0, 0);
}

// ── дерево зависимостей установленных модов (страница «Моды») ─────────────────
// SHA-1 файла через bcrypt: по хешу Modrinth находит проект и его зависимости.
static std::string Sha1File(const fs::path& path) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) != 0) return {};
    DWORD objLen = 0, hashLen = 0, cb = 0;
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(objLen), &cb, 0) != 0 ||
        BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (PUCHAR)&hashLen, sizeof(hashLen), &cb, 0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }
    std::vector<BYTE> obj(objLen), digest(hashLen);
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(alg, &hash, obj.data(), objLen, nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }
    std::ifstream f(path, std::ios::binary);
    bool ok = (bool)f;
    std::vector<char> buf(1 << 20);
    while (ok && f) {
        f.read(buf.data(), (std::streamsize)buf.size());
        std::streamsize n = f.gcount();
        if (n > 0 && BCryptHashData(hash, (PUCHAR)buf.data(), (ULONG)n, 0) != 0) ok = false;
    }
    if (ok) ok = (BCryptFinishHash(hash, digest.data(), hashLen, 0) == 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) return {};
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve((size_t)hashLen * 2);
    for (BYTE b : digest) {
        out += hex[b >> 4];
        out += hex[b & 15];
    }
    return out;
}

static std::wstring ModsKeyFor(const App& a) {
    std::wstring k = a.profile.dir;
    for (auto& m : a.modsList) {
        k += L"\n";
        k += m.name;
        k += L"|";
        k += std::to_wstring(m.size);
    }
    return k;
}

// Строит дерево в фоне: SHA-1 файлов → /v2/version_files → /v2/projects.
static void StartModsTree(App& a, bool force = false) {
    if (!a.hasProfile || a.modsList.empty()) {
        ++a.modsTree.gen;
        a.modsTree.busy = false;
        a.modsTree.ready = true;
        a.modsTree.rows.clear();
        a.modsTree.note.clear();
        a.modsTree.key.clear();
        a.modsScrollY = 0.f;
        a.modsScrollMax = 0.f;
        return;
    }
    std::wstring key = ModsKeyFor(a);
    if (!force && (a.modsTree.busy || (a.modsTree.ready && a.modsTree.key == key))) return;

    int gen = ++a.modsTree.gen;
    a.modsTree.busy = true;
    a.modsTree.ready = false;
    a.modsTree.key = key;
    a.modsTree.note.clear();
    a.modsTree.rows.clear();
    a.modsScrollY = 0.f;
    std::wstring dir = a.profile.dir;
    std::vector<ModFile> files = a.modsList;
    HWND hwnd = a.hwnd;

    std::thread([hwnd, gen, dir, files] {
        std::vector<ModTreeRow> rows;
        std::wstring note;

        std::vector<std::string> hashes(files.size());
        bool anyHash = false;
        for (size_t i = 0; i < files.size(); ++i) {
            hashes[i] = Sha1File(fs::path(dir) / L"mods" / files[i].name);
            if (!hashes[i].empty()) anyHash = true;
        }

        struct Known {
            std::string project;
            std::vector<std::string> req;
        };
        std::vector<Known> known(files.size());
        std::set<std::string> wanted;
        nlohmann::json versions;

        if (anyHash) {
            nlohmann::json body;
            body["hashes"] = hashes;
            body["algorithm"] = "sha1";
            HttpResult r = HttpRequest(L"POST", L"https://api.modrinth.com/v2/version_files",
                                       L"", body.dump());
            if (r.status == 200) {
                versions = nlohmann::json::parse(r.body, nullptr, false);
                if (!versions.is_object()) {
                    versions = nlohmann::json();
                    note = L"Не удалось разобрать ответ Modrinth";
                }
            } else {
                note = L"Modrinth недоступен (код " + std::to_wstring(r.status) +
                       L") — проверьте сеть";
            }
        }

        if (!versions.is_null()) {
            for (size_t i = 0; i < files.size(); ++i) {
                if (hashes[i].empty() || !versions.contains(hashes[i])) continue;
                const auto& v = versions[hashes[i]];
                if (!v.is_object()) continue;
                if (v.contains("project_id") && v["project_id"].is_string())
                    known[i].project = v["project_id"].get<std::string>();
                if (v.contains("dependencies") && v["dependencies"].is_array()) {
                    for (auto& d : v["dependencies"]) {
                        if (!d.is_object()) continue;
                        if (!d.contains("dependency_type") || !d["dependency_type"].is_string() ||
                            d["dependency_type"].get<std::string>() != "required") continue;
                        if (!d.contains("project_id") || !d["project_id"].is_string()) continue;
                        std::string pid = d["project_id"].get<std::string>();
                        if (!pid.empty()) known[i].req.push_back(std::move(pid));
                    }
                }
                if (!known[i].project.empty()) wanted.insert(known[i].project);
                for (auto& p : known[i].req) wanted.insert(p);
            }
        }

        std::map<std::string, std::wstring> titles;
        if (!wanted.empty()) {
            nlohmann::json ids = nlohmann::json::array();
            for (auto& p : wanted) ids.push_back(p);
            std::wstring url = L"https://api.modrinth.com/v2/projects?ids=" +
                               W(UrlEncode(ids.dump()));
            HttpResult r = HttpRequest(L"GET", url);
            if (r.status == 200) {
                auto j = nlohmann::json::parse(r.body, nullptr, false);
                if (j.is_array()) {
                    for (auto& p : j) {
                        if (!p.is_object()) continue;
                        if (!p.contains("id") || !p["id"].is_string()) continue;
                        if (!p.contains("title") || !p["title"].is_string()) continue;
                        std::wstring t = W(p["title"].get<std::string>());
                        if (!t.empty()) titles[p["id"].get<std::string>()] = t;
                    }
                }
            }
        }

        std::map<std::string, int> byProject;
        for (size_t i = 0; i < known.size(); ++i)
            if (!known[i].project.empty()) byProject.emplace(known[i].project, (int)i);

        auto displayName = [&](int i) -> std::wstring {
            if (!known[(size_t)i].project.empty()) {
                auto it = titles.find(known[(size_t)i].project);
                if (it != titles.end()) return it->second;
            }
            std::wstring n = files[(size_t)i].name;
            if (n.size() > 4 && n.substr(n.size() - 4) == L".jar") n.resize(n.size() - 4);
            return n;
        };

        std::vector<int> order(files.size());
        for (size_t i = 0; i < files.size(); ++i) order[i] = (int)i;
        std::sort(order.begin(), order.end(),
                  [&](int l, int r) { return displayName(l) < displayName(r); });

        const int kMaxRows = 160;
        const int kMaxLevel = 3;

        std::function<void(int, int, const std::wstring&, std::set<std::string>&)> addKids;
        addKids = [&](int node, int level, const std::wstring& indent,
                      std::set<std::string>& branch) {
            const auto& k = known[(size_t)node];
            if (k.req.empty()) return;
            struct Kid { std::string pid; int idx = -1; };
            std::vector<Kid> kids;
            std::set<std::string> used;
            for (auto& pid : k.req) {
                if (!used.insert(pid).second) continue;
                auto it = byProject.find(pid);
                if (it != byProject.end()) {
                    if (it->second == node || branch.count(pid)) continue;
                    kids.push_back({ pid, it->second });
                } else {
                    kids.push_back({ pid, -1 });
                }
            }
            for (size_t j = 0; j < kids.size() && (int)rows.size() < kMaxRows; ++j) {
                bool last = (j + 1 == kids.size());
                bool missing = kids[j].idx < 0;
                ModTreeRow row;
                row.prefix = indent + (last ? L"└─ " : L"├─ ");
                auto ti = titles.find(kids[j].pid);
                row.name = ti != titles.end() ? ti->second : W(kids[j].pid);
                row.missing = missing;
                if (missing) row.note = L"не установлен";
                rows.push_back(std::move(row));
                if ((int)rows.size() >= kMaxRows) return;
                if (!missing && level < kMaxLevel) {
                    branch.insert(kids[j].pid);
                    addKids(kids[j].idx, level + 1,
                            indent + (last ? L"   " : L"│  "), branch);
                    branch.erase(kids[j].pid);
                }
            }
        };

        for (int idx : order) {
            if ((int)rows.size() >= kMaxRows) break;
            bool unknown = known[(size_t)idx].project.empty();
            ModTreeRow row;
            row.root = true;
            row.name = displayName(idx);
            row.unknown = unknown;
            if (unknown) row.note = L"нет данных Modrinth";
            rows.push_back(std::move(row));
            if (!unknown) {
                std::set<std::string> branch;
                branch.insert(known[(size_t)idx].project);
                addKids(idx, 1, L"", branch);
            }
        }
        if (note.empty() && rows.empty()) note = L"Нет данных о зависимостях";

        PostUI(hwnd, [hwnd, gen, rows = std::move(rows), note = std::move(note)]() mutable {
            App* a = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (!a || a->modsTree.gen != gen) return;
            a->modsTree.busy = false;
            a->modsTree.ready = true;
            a->modsTree.rows = std::move(rows);
            a->modsTree.note = std::move(note);
            a->modsScrollY = 0.f;
            InvalidateRect(hwnd, nullptr, FALSE);
        });
    }).detach();
}

// ── авторизация: хранение и сеть ──────────────────────────────────────────────
static const wchar_t* kMsDeviceUrl = L"https://login.live.com/oauth20_connect.srf";
static const wchar_t* kMsTokenUrl = L"https://login.live.com/oauth20_token.srf";
static const wchar_t* kMsClientId = L"00000000402b5328";
static const wchar_t* kMsScope = L"service::user.auth.xboxlive.com::MBI_SSL";
static const wchar_t* kMsVerify = L"https://www.microsoft.com/link";
static const wchar_t* kXblUrl = L"https://user.auth.xboxlive.com/user/authenticate";
static const wchar_t* kXstsUrl = L"https://xsts.auth.xboxlive.com/xsts/authorize";
static const wchar_t* kMcAuthUrl = L"https://api.minecraftservices.com/authentication/login_with_xbox";
static const wchar_t* kMcProfileUrl = L"https://api.minecraftservices.com/minecraft/profile";
static const wchar_t* kMcEntUrl = L"https://api.minecraftservices.com/entitlements/mcstore";
static const wchar_t* kElyAuth = L"https://authserver.ely.by";
static const wchar_t* kElyInjectorApi =
    L"https://api.github.com/repos/yushijinhun/authlib-injector/releases/latest";
static const wchar_t* kElyInjectorMeta = L"https://authserver.ely.by/api/authlib-injector";

static fs::path AuthPath() { return NexusDir() / L"auth.json"; }
static fs::path ElyPath() { return NexusDir() / L"ely.json"; }
static fs::path AccountPath() { return NexusDir() / L"account.txt"; }
static fs::path ElyInjectorPath() { return NexusDir() / L"authlib-injector.jar"; }

static std::string ReadTextFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static bool WriteTextFile(const fs::path& p, const std::string& s) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(s.data(), (std::streamsize)s.size());
    return (bool)f;
}

static void LoadAuth(App& a) {
    a.ms = MsAuth{};
    a.ely = ElyAuth{};

    std::string msRaw = ReadTextFile(AuthPath());
    if (!msRaw.empty()) {
        try {
            auto j = nlohmann::json::parse(msRaw);
            auto strv = [&](const char* k) -> std::wstring {
                if (!j.is_object() || !j.contains(k) || !j[k].is_string()) return {};
                return W(j[k].get<std::string>());
            };
            if (j.is_object()) {
                a.ms.mcToken = strv("mc_token");
                a.ms.username = strv("username");
                a.ms.uuid = strv("uuid");
                a.ms.msRefresh = strv("ms_refresh");
                a.ms.online = !a.ms.mcToken.empty() && !a.ms.username.empty();
            }
        } catch (...) {}
    }

    std::string elyRaw = ReadTextFile(ElyPath());
    if (!elyRaw.empty()) {
        try {
            auto j = nlohmann::json::parse(elyRaw);
            auto strv = [&](const char* k) -> std::wstring {
                if (!j.is_object() || !j.contains(k) || !j[k].is_string()) return {};
                return W(j[k].get<std::string>());
            };
            if (j.is_object()) {
                a.ely.mcToken = strv("access_token");
                a.ely.clientToken = strv("client_token");
                a.ely.username = strv("username");
                a.ely.uuid = strv("uuid");
                a.ely.online = !a.ely.mcToken.empty() && !a.ely.username.empty();
            }
        } catch (...) {}
    }

    std::string kind = ReadTextFile(AccountPath());
    kind.erase(std::remove_if(kind.begin(), kind.end(),
                              [](char c) { return c == '\r' || c == '\n' || c == ' '; }),
               kind.end());
    if (kind == "ely" && a.ely.online) a.authKind = L"ely";
    else if (kind == "ms" && a.ms.online) a.authKind = L"ms";
    else if (a.ms.online) a.authKind = L"ms";
    else if (a.ely.online) a.authKind = L"ely";
    else a.authKind = L"ms";
}

static void SaveMsAuth(const App& a) {
    nlohmann::json j;
    j["mc_token"] = a.ms.mcToken.empty() ? nlohmann::json() : nlohmann::json(U8(a.ms.mcToken));
    j["username"] = a.ms.username.empty() ? nlohmann::json() : nlohmann::json(U8(a.ms.username));
    j["uuid"] = a.ms.uuid.empty() ? nlohmann::json() : nlohmann::json(U8(a.ms.uuid));
    j["ms_refresh"] = a.ms.msRefresh.empty() ? nlohmann::json() : nlohmann::json(U8(a.ms.msRefresh));
    WriteTextFile(AuthPath(), j.dump());
}

static void SaveElyAuth(const App& a) {
    nlohmann::json j;
    j["access_token"] = a.ely.mcToken.empty() ? nlohmann::json() : nlohmann::json(U8(a.ely.mcToken));
    j["client_token"] = a.ely.clientToken.empty() ? nlohmann::json() : nlohmann::json(U8(a.ely.clientToken));
    j["username"] = a.ely.username.empty() ? nlohmann::json() : nlohmann::json(U8(a.ely.username));
    j["uuid"] = a.ely.uuid.empty() ? nlohmann::json() : nlohmann::json(U8(a.ely.uuid));
    WriteTextFile(ElyPath(), j.dump());
}

static void RememberKind(const App& a) {
    WriteTextFile(AccountPath(), U8(a.authKind));
}

static bool AuthOnline(const App& a) {
    return a.authKind == L"ely" ? a.ely.online : a.ms.online;
}

static std::wstring AuthName(const App& a) {
    return a.authKind == L"ely" ? a.ely.username : a.ms.username;
}

static std::wstring AuthToken(const App& a) {
    return a.authKind == L"ely" ? a.ely.mcToken : a.ms.mcToken;
}

static std::wstring AuthUuid(const App& a) {
    return a.authKind == L"ely" ? a.ely.uuid : a.ms.uuid;
}

static std::wstring RandomHex(size_t n) {
    std::random_device rd;
    std::mt19937_64 gen(((uint64_t)rd() << 32) ^ rd());
    static const wchar_t* hex = L"0123456789abcdef";
    std::wstring s;
    s.reserve(n);
    for (size_t i = 0; i < n; ++i) s += hex[(size_t)(gen() & 15)];
    return s;
}

static nlohmann::json ParseJson(const std::string& s) {
    try {
        return nlohmann::json::parse(s);
    } catch (...) {
        return nlohmann::json();
    }
}

static std::string JStr(const nlohmann::json& j, const char* k) {
    if (!j.is_object() || !j.contains(k)) return {};
    const auto& v = j[k];
    if (v.is_string()) return v.get<std::string>();
    return {};
}

static int JInt(const nlohmann::json& j, const char* k, int def) {
    if (!j.is_object() || !j.contains(k)) return def;
    const auto& v = j[k];
    if (v.is_number_integer()) return (int)v.get<long long>();
    if (v.is_number_float()) return (int)v.get<double>();
    if (v.is_string()) {
        try { return std::stoi(v.get<std::string>()); } catch (...) {}
    }
    return def;
}

static std::wstring ShortErr(const HttpResult& r) {
    auto j = ParseJson(r.body);
    if (j.is_object()) {
        for (const char* k : { "error_description", "Message", "error", "message" }) {
            std::string v = JStr(j, k);
            if (!v.empty()) return W(v.substr(0, 200));
        }
    }
    if (!r.error.empty()) return r.error;
    if (r.status) return L"код " + std::to_wstring(r.status);
    return L"нет ответа";
}

static std::wstring XerrText(const nlohmann::json& j) {
    if (!j.is_object() || !j.contains("XErr")) return {};
    long long x = 0;
    const auto& v = j["XErr"];
    if (v.is_number_integer()) x = v.get<long long>();
    else if (v.is_string()) {
        try { x = std::stoll(v.get<std::string>()); } catch (...) { return {}; }
    }
    switch (x) {
    case 2148916227: return L"аккаунт заблокирован в Xbox Live";
    case 2148916233:
        return L"у аккаунта нет профиля Xbox — зайдите на minecraft.net и создайте его";
    case 2148916235: return L"Xbox Live недоступен в вашей стране";
    case 2148916236: return L"нужна проверка возраста (Южная Корея)";
    case 2148916237: return L"нужна проверка возраста (Южная Корея)";
    case 2148916238: return L"детский аккаунт — добавьте его в семейную группу Microsoft";
    case 2148916262: return L"неизвестная ошибка Xbox Live, попробуйте позже";
    default: return {};
    }
}

static void PostAuth(HWND hwnd, int gen, std::function<void(App&)> fn) {
    PostUI(hwnd, [hwnd, gen, fn = std::move(fn)] {
        App* a = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (!a || a->authGen != gen) return;
        fn(*a);
        InvalidateRect(hwnd, nullptr, FALSE);
    });
}

static void CloseAuthModal(App& a) {
    a.authUi.modal = 0;
    a.authUi.busy = false;
    a.authUi.focusField = 0;
    if (a.authJob) {
        a.authJob->cancel = true;
        a.authJob.reset();
    }
    ++a.authGen;
}

// Цепочка Xbox Live → XSTS → Minecraft. Возвращает профиль и токен.
static bool XblChain(const std::wstring& msAccess, std::wstring& mcToken, std::wstring& name,
                     std::wstring& uuid, std::wstring& err, const std::shared_ptr<AuthJob>& job,
                     const std::function<void(const std::wstring&, int)>& report) {
    if (report) report(L"Вход в Xbox Live…", 40);

    nlohmann::json xblJson;
    HttpResult last;
    bool gotXbl = false;
    for (const wchar_t* prefix : { L"d=", L"t=", L"" }) {
        if (job && job->cancel.load()) return false;
        nlohmann::json payload;
        payload["Properties"]["AuthMethod"] = "RPS";
        payload["Properties"]["SiteName"] = "user.auth.xboxlive.com";
        payload["Properties"]["RpsTicket"] = U8(std::wstring(prefix) + msAccess);
        payload["RelyingParty"] = "http://auth.xboxlive.com";
        payload["TokenType"] = "JWT";
        last = HttpRequest(L"POST", kXblUrl, L"", payload.dump(), L"application/json",
                           { { L"Accept", L"application/json" } });
        auto j = ParseJson(last.body);
        if (last.status == 200 && j.is_object() && j.contains("Token")) {
            xblJson = std::move(j);
            gotXbl = true;
            break;
        }
        if (last.status && last.status != 400 && last.status != 401) break;
    }
    if (!gotXbl) {
        err = L"Xbox Live отклонил вход: " + ShortErr(last);
        return false;
    }

    std::wstring xblToken = W(JStr(xblJson, "Token"));
    std::wstring uhs;
    try {
        uhs = W(xblJson["DisplayClaims"]["xui"][0].value("uhs", std::string()));
    } catch (...) {}
    if (xblToken.empty() || uhs.empty()) {
        err = L"Xbox Live вернул неожиданный ответ";
        return false;
    }

    if (job && job->cancel.load()) return false;
    if (report) report(L"Получение XSTS…", 60);
    nlohmann::json xstsPayload;
    xstsPayload["Properties"]["SandboxId"] = "RETAIL";
    xstsPayload["Properties"]["UserTokens"] = nlohmann::json::array({ U8(xblToken) });
    xstsPayload["RelyingParty"] = "rp://api.minecraftservices.com/";
    xstsPayload["TokenType"] = "JWT";
    HttpResult xr = HttpRequest(L"POST", kXstsUrl, L"", xstsPayload.dump(), L"application/json",
                                { { L"Accept", L"application/json" } });
    auto xj = ParseJson(xr.body);
    if (xr.status != 200) {
        std::wstring xe = XerrText(xj);
        err = xe.empty() ? L"XSTS: " + ShortErr(xr) : xe;
        return false;
    }
    std::wstring xsts = W(JStr(xj, "Token"));
    if (xsts.empty()) {
        err = L"XSTS не выдал токен";
        return false;
    }

    if (job && job->cancel.load()) return false;
    if (report) report(L"Вход в Minecraft…", 75);
    nlohmann::json authPayload;
    authPayload["identityToken"] = "XBL3.0 x=" + U8(uhs) + ";" + U8(xsts);
    HttpResult ar = HttpRequest(L"POST", kMcAuthUrl, L"", authPayload.dump(), L"application/json",
                                { { L"Accept", L"application/json" } });
    if (ar.status != 200) {
        if (ar.status == 403) err = L"сервис Minecraft отклонил приложение (403)";
        else err = L"Minecraft: " + ShortErr(ar);
        return false;
    }
    auto aj = ParseJson(ar.body);
    mcToken = W(JStr(aj, "access_token"));
    if (mcToken.empty()) {
        err = L"Minecraft не выдал токен доступа";
        return false;
    }

    if (job && job->cancel.load()) return false;
    if (report) report(L"Проверка лицензии…", 88);
    HttpResult ent = HttpRequest(L"GET", kMcEntUrl, L"", {},
                                 L"", { { L"Authorization", L"Bearer " + mcToken },
                                        { L"Accept", L"application/json" } });
    if (ent.status == 200) {
        auto ej = ParseJson(ent.body);
        bool licensed = false;
        if (ej.is_object() && ej.contains("items") && ej["items"].is_array()) {
            for (auto& it : ej["items"]) {
                std::string n = JStr(it, "name");
                if (n == "game_minecraft" || n == "product_minecraft") { licensed = true; break; }
            }
        }
        if (!licensed) {
            err = L"на этом аккаунте нет лицензии Minecraft";
            return false;
        }
    }

    HttpResult pr = HttpRequest(L"GET", kMcProfileUrl, L"", {},
                                L"", { { L"Authorization", L"Bearer " + mcToken },
                                       { L"Accept", L"application/json" } });
    auto pj = ParseJson(pr.body);
    name = W(JStr(pj, "name"));
    uuid = W(JStr(pj, "id"));
    if (pr.status != 200 || name.empty() || uuid.empty()) {
        err = L"не удалось получить профиль Minecraft";
        return false;
    }
    return true;
}

static void MsLoginWorker(HWND hwnd, int gen, std::shared_ptr<AuthJob> job) {
    auto post = [&](std::function<void(App&)> fn) { PostAuth(hwnd, gen, std::move(fn)); };
    auto fail = [&](const std::wstring& msg) {
        post([msg](App& a) {
            a.authUi.busy = false;
            a.authUi.pct = 0.f;
            a.authUi.error = true;
            a.authUi.status = msg;
        });
    };

    post([](App& a) {
        a.authUi.busy = true;
        a.authUi.error = false;
        a.authUi.pct = 0.06f;
        a.authUi.status = L"Запрос кода у Microsoft…";
        a.authUi.code.clear();
    });

    std::string form = "client_id=" + UrlEncode(U8(kMsClientId)) +
                       "&scope=" + UrlEncode(U8(kMsScope)) + "&response_type=device_code";
    HttpResult dr = HttpRequest(L"POST", kMsDeviceUrl, L"", form,
                                L"application/x-www-form-urlencoded");
    auto dj = ParseJson(dr.body);
    std::wstring userCode = W(JStr(dj, "user_code"));
    std::wstring deviceCode = W(JStr(dj, "device_code"));
    if (dr.status != 200 || userCode.empty() || deviceCode.empty()) {
        return fail(L"Microsoft не выдал код устройства: " + ShortErr(dr));
    }
    std::wstring uri = W(JStr(dj, "verification_uri"));
    if (uri.empty()) uri = kMsVerify;
    int interval = std::max(3, JInt(dj, "interval", 5));
    int expires = std::max(60, JInt(dj, "expires_in", 900));

    post([userCode, uri](App& a) {
        a.authUi.code = userCode;
        std::wstring u = uri;
        if (u.rfind(L"https://", 0) == 0) u = u.substr(8);
        while (!u.empty() && u.back() == L'/') u.pop_back();
        a.authUi.url = u;
        a.authUi.pct = 0.14f;
        a.authUi.status = L"Ожидание входа в браузере…";
    });

    nlohmann::json tokens;
    bool authorized = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(expires);
    while (std::chrono::steady_clock::now() < deadline) {
        for (int i = 0; i < interval * 4; ++i) {
            if (job->cancel.load()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        if (job->cancel.load()) return;

        std::string poll = "grant_type=" +
                           UrlEncode("urn:ietf:params:oauth:grant-type:device_code") +
                           "&client_id=" + UrlEncode(U8(kMsClientId)) +
                           "&device_code=" + UrlEncode(U8(deviceCode));
        HttpResult tr = HttpRequest(L"POST", kMsTokenUrl, L"", poll,
                                    L"application/x-www-form-urlencoded");
        auto tj = ParseJson(tr.body);
        if (tr.status == 200 && !JStr(tj, "access_token").empty()) {
            tokens = std::move(tj);
            authorized = true;
            break;
        }
        std::string e = JStr(tj, "error");
        if (e == "authorization_pending") continue;
        if (e == "slow_down") { interval += 5; continue; }
        if (e == "expired_token") return fail(L"код истёк — попробуйте войти снова");
        if (e == "authorization_declined") return fail(L"вход отклонён в браузере");
        if (!e.empty()) {
            std::string d = JStr(tj, "error_description");
            return fail(W(e + (d.empty() ? std::string() : ": " + d)));
        }
        return fail(L"Microsoft: " + ShortErr(tr));
    }
    if (job->cancel.load()) return;
    if (!authorized) return fail(L"время ожидания входа истекло — попробуйте снова");

    std::wstring refresh = W(JStr(tokens, "refresh_token"));
    std::wstring access = W(JStr(tokens, "access_token"));
    std::wstring mcToken, name, uuid, err;
    auto report = [&](const std::wstring& msg, int pct) {
        post([msg, pct](App& a) {
            a.authUi.status = msg;
            a.authUi.pct = pct / 100.f;
        });
    };
    if (!XblChain(access, mcToken, name, uuid, err, job, report)) {
        if (!job->cancel.load()) fail(err);
        return;
    }

    post([mcToken, name, uuid, refresh](App& a) {
        a.ms.mcToken = mcToken;
        a.ms.username = name;
        a.ms.uuid = uuid;
        a.ms.msRefresh = refresh;
        a.ms.online = true;
        SaveMsAuth(a);
        a.authKind = L"ms";
        RememberKind(a);
        a.authUi.busy = false;
        a.authUi.pct = 1.f;
        a.authUi.status = L"Готово";
        a.status = L"Добро пожаловать, " + name + L"!";
        a.statusCol = C_GREEN;
        a.authJob.reset();
        a.authUi.modal = 0;
    });
}

static std::wstring ElyErrorText(const HttpResult& r) {
    auto j = ParseJson(r.body);
    std::string msg = JStr(j, "errorMessage");
    if (msg.empty()) msg = JStr(j, "error");
    std::string low = msg;
    std::transform(low.begin(), low.end(), low.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    if (low.find("two factor") != std::string::npos ||
        low.find("двухфактор") != std::string::npos)
        return L"включена двухфакторная защита: введите пароль как «пароль:код»";
    if (r.status == 404 || low.find("not found") != std::string::npos)
        return L"аккаунт с таким ником или e-mail не найден";
    if (low.find("invalid credentials") != std::string::npos ||
        low.find("invalid password") != std::string::npos || r.status == 401 || r.status == 403)
        return L"неверный логин или пароль";
    if (r.status == 0)
        return L"нет связи с Ely.by — проверьте интернет";
    if (!msg.empty()) return W(msg);
    return L"не удалось войти (код " + std::to_wstring(r.status) + L")";
}

static std::wstring ElyDashed(std::wstring s) {
    s.erase(std::remove(s.begin(), s.end(), L'-'), s.end());
    if (s.size() != 32) return s.empty() ? L"0" : s;
    return s.substr(0, 8) + L"-" + s.substr(8, 4) + L"-" + s.substr(12, 4) + L"-" +
           s.substr(16, 4) + L"-" + s.substr(20);
}

static bool EnsureElyInjectorFile(const std::function<void(int)>& pct, std::wstring& err);

static void ElyLoginWorker(HWND hwnd, int gen, std::shared_ptr<AuthJob> job, std::wstring user,
                           std::wstring pass, std::wstring clientToken) {
    auto post = [&](std::function<void(App&)> fn) { PostAuth(hwnd, gen, std::move(fn)); };
    auto fail = [&](const std::wstring& msg) {
        post([msg](App& a) {
            a.authUi.busy = false;
            a.authUi.pct = 0.f;
            a.authUi.error = true;
            a.authUi.status = msg;
        });
    };

    post([](App& a) {
        a.authUi.busy = true;
        a.authUi.error = false;
        a.authUi.pct = 0.25f;
        a.authUi.status = L"Вход в Ely.by…";
    });

    nlohmann::json payload;
    payload["username"] = U8(user);
    payload["password"] = U8(pass);
    payload["clientToken"] = U8(clientToken);
    payload["requestUser"] = true;
    HttpResult r = HttpRequest(L"POST", std::wstring(kElyAuth) + L"/auth/authenticate", L"",
                               payload.dump(), L"application/json",
                               { { L"Accept", L"application/json" } });
    auto j = ParseJson(r.body);
    if (r.status != 200) return fail(ElyErrorText(r));
    std::wstring tok = W(JStr(j, "accessToken"));
    std::wstring name, id;
    if (j.is_object() && j.contains("selectedProfile") && j["selectedProfile"].is_object()) {
        name = W(JStr(j["selectedProfile"], "name"));
        id = W(JStr(j["selectedProfile"], "id"));
    }
    if (tok.empty() || name.empty()) return fail(L"Ely.by не выдал профиль аккаунта");
    std::wstring ct = W(JStr(j, "clientToken"));
    if (ct.empty()) ct = clientToken;
    std::wstring uuid = ElyDashed(id);

    post([tok, name, uuid, ct](App& a) {
        a.ely.mcToken = tok;
        a.ely.username = name;
        a.ely.uuid = uuid;
        a.ely.clientToken = ct;
        a.ely.online = true;
        SaveElyAuth(a);
        a.authKind = L"ely";
        RememberKind(a);
        a.authUi.busy = false;
        a.authUi.pct = 1.f;
        a.authUi.status = L"Готово";
        a.status = L"Добро пожаловать, " + name + L"!";
        a.statusCol = C_GREEN;
        a.authJob.reset();
        a.authUi.modal = 0;
        // Заранее готовим authlib-injector, чтобы первый запуск не ждал загрузку.
        std::thread([] {
            std::wstring e;
            EnsureElyInjectorFile({}, e);
        }).detach();
    });
}
// Тихий вход при старте: продлевает сохранённые сессии в фоне.
static void MsRefreshWorker(HWND hwnd, int gen, std::shared_ptr<AuthJob> job,
                            std::wstring refresh) {
    std::string form = "grant_type=refresh_token&client_id=" + UrlEncode(U8(kMsClientId)) +
                       "&refresh_token=" + UrlEncode(U8(refresh)) +
                       "&scope=" + UrlEncode(U8(kMsScope));
    HttpResult tr = HttpRequest(L"POST", kMsTokenUrl, L"", form,
                                L"application/x-www-form-urlencoded");
    auto tj = ParseJson(tr.body);
    if (job->cancel.load()) return;
    if (tr.status != 200) {
        std::string e = JStr(tj, "error");
        if (e == "invalid_grant") {
            PostAuth(hwnd, gen, [](App& a) {
                a.ms = MsAuth{};
                std::error_code ec;
                fs::remove(AuthPath(), ec);
                if (a.authKind == L"ms" && !a.ely.online) {
                    a.status = L"Сессия Microsoft истекла — войдите снова";
                    a.statusCol = C_YELLOW;
                }
            });
        }
        return;
    }
    std::wstring newRefresh = W(JStr(tj, "refresh_token"));
    std::wstring access = W(JStr(tj, "access_token"));
    if (access.empty()) return;
    std::wstring mcToken, name, uuid, err;
    if (!XblChain(access, mcToken, name, uuid, err, job, nullptr)) return;
    if (job->cancel.load()) return;
    PostAuth(hwnd, gen, [mcToken, name, uuid, newRefresh, refresh](App& a) {
        a.ms.mcToken = mcToken;
        a.ms.username = name;
        a.ms.uuid = uuid;
        a.ms.msRefresh = newRefresh.empty() ? refresh : newRefresh;
        a.ms.online = true;
        SaveMsAuth(a);
    });
}

static void ElyRefreshWorker(HWND hwnd, int gen, std::shared_ptr<AuthJob> job,
                             std::wstring token, std::wstring clientToken) {
    nlohmann::json payload;
    payload["accessToken"] = U8(token);
    payload["clientToken"] = U8(clientToken);
    payload["requestUser"] = true;
    HttpResult r = HttpRequest(L"POST", std::wstring(kElyAuth) + L"/auth/refresh", L"",
                               payload.dump(), L"application/json",
                               { { L"Accept", L"application/json" } });
    if (job->cancel.load()) return;
    auto j = ParseJson(r.body);
    if (r.status == 200) {
        std::wstring tok = W(JStr(j, "accessToken"));
        std::wstring name, id;
        if (j.is_object() && j.contains("selectedProfile") && j["selectedProfile"].is_object()) {
            name = W(JStr(j["selectedProfile"], "name"));
            id = W(JStr(j["selectedProfile"], "id"));
        }
        PostAuth(hwnd, gen, [tok, name, id, token, clientToken](App& a) {
            if (!tok.empty()) a.ely.mcToken = tok;
            if (!name.empty()) a.ely.username = name;
            if (!id.empty()) a.ely.uuid = ElyDashed(id);
            if (a.ely.clientToken.empty()) a.ely.clientToken = clientToken;
            a.ely.online = !a.ely.mcToken.empty() && !a.ely.username.empty();
            if (a.ely.online) SaveElyAuth(a);
        });
        return;
    }
    // 400/401/403 — токен действительно недействителен; сетевые ошибки не трогаем.
    if (r.status == 400 || r.status == 401 || r.status == 403) {
        PostAuth(hwnd, gen, [](App& a) {
            a.ely = ElyAuth{};
            std::error_code ec;
            fs::remove(ElyPath(), ec);
            if (a.authKind == L"ely" && !a.ms.online) {
                a.status = L"Сессия Ely.by истекла — войдите снова";
                a.statusCol = C_YELLOW;
            }
        });
    }
}

static bool EnsureElyInjectorFile(const std::function<void(int)>& pct, std::wstring& err) {
    static std::mutex mu;
    std::lock_guard<std::mutex> lk(mu);
    std::error_code ec;
    fs::path p = ElyInjectorPath();
    if (fs::exists(p, ec) && fs::file_size(p, ec) > 50000) return true;

    HttpResult r = HttpRequest(L"GET", kElyInjectorApi, L"", {}, L"application/json",
                               { { L"Accept", L"application/vnd.github+json" } });
    if (r.status != 200) {
        err = L"GitHub: " + ShortErr(r);
        return false;
    }
    auto j = ParseJson(r.body);
    std::string url;
    if (j.is_object() && j.contains("assets") && j["assets"].is_array()) {
        for (auto& asset : j["assets"]) {
            std::string n = JStr(asset, "name");
            std::string low = n;
            std::transform(low.begin(), low.end(), low.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            if (low.size() > 4 && low.substr(low.size() - 4) == ".jar") {
                url = JStr(asset, "browser_download_url");
                if (!url.empty()) break;
            }
        }
    }
    if (url.empty()) {
        err = L"в релизе authlib-injector нет jar-файла";
        return false;
    }
    fs::create_directories(p.parent_path(), ec);
    std::wstring e;
    bool ok = HttpDownload(W(url), p, [pct](long long got, long long total) {
        if (pct && total > 0) pct((int)std::clamp(got * 100 / total, 0LL, 100LL));
    }, e);
    if (!ok) {
        err = L"authlib-injector не скачался: " + e;
        return false;
    }
    return true;
}

// Заранее качает authlib-injector после входа Ely.by, чтобы запуск не ждал.

static std::vector<std::shared_ptr<Item>> FetchModrinth(int tab, const Profile* prof,
                                                       const std::wstring& q, std::wstring& err) {
    std::vector<std::shared_ptr<Item>> out;
    std::string facets = "[";
    facets += "[\"project_type:" + std::string(kMrType[tab]) + "\"]";
    if (prof) {
        if (tab == 0 && !prof->loader.empty())
            facets += ",[\"categories:" + U8(prof->loader) + "\"]";
        if (!prof->mc.empty())
            facets += ",[\"versions:" + U8(prof->mc) + "\"]";
        if (tab == 3)
            facets += ",[\"categories:datapack\"]";
    }
    facets += "]";

    std::wstring url = L"https://api.modrinth.com/v2/search?query=" + W(UrlEncode(U8(q))) +
                       L"&facets=" + W(UrlEncode(facets)) + L"&index=downloads&limit=20";
    HttpResult r = HttpRequest(L"GET", url);
    if (r.status != 200) {
        err = L"Modrinth: ошибка " + std::to_wstring(r.status);
        return out;
    }
    try {
        auto j = nlohmann::json::parse(r.body);
        for (auto& h : j["hits"]) {
            auto it = std::make_shared<Item>();
            it->name = W(h.value("title", ""));
            it->desc = W(h.value("description", ""));
            it->slug = W(h.value("slug", ""));
            it->iconUrl = W(h.value("icon_url", ""));
            it->downloads = h.value("downloads", (long long)0);
            it->type = tab;
            it->pageUrl = L"https://modrinth.com/" + W(kMrType[tab]) + L"/" + it->slug;
            out.push_back(it);
        }
    } catch (...) {
        err = L"Modrinth: ответ не разобран";
    }
    return out;
}

// ── установка ─────────────────────────────────────────────────────────────────
static bool JsonHas(const nlohmann::json& obj, const char* key, const std::string& value) {
    if (!obj.contains(key) || !obj[key].is_array()) return false;
    for (auto& x : obj[key]) {
        if (x.is_string() && x.get<std::string>() == value) return true;
    }
    return false;
}

static bool PickModrinthFile(const nlohmann::json& versions, const Profile& p, int tab,
                             std::string& fileUrl, std::string& fileName) {
    if (!versions.is_array() || versions.empty()) return false;

    const nlohmann::json* pick = nullptr;
    for (auto& v : versions) {
        if (!p.mc.empty() && !JsonHas(v, "game_versions", U8(p.mc))) continue;
        if (tab == 0 && !p.loader.empty() && !JsonHas(v, "loaders", U8(p.loader))) continue;
        if (tab == 3 && !JsonHas(v, "loaders", "datapack")) continue;
        pick = &v;
        break;
    }
    if (!pick) return false;

    if (!pick->contains("files") || !(*pick)["files"].is_array() || (*pick)["files"].empty())
        return false;
    for (auto& f : (*pick)["files"]) {
        if (f.value("primary", false)) {
            fileUrl = f.value("url", "");
            fileName = f.value("filename", "");
            break;
        }
    }
    if (fileUrl.empty()) {
        auto& f = (*pick)["files"][0];
        fileUrl = f.value("url", "");
        fileName = f.value("filename", "");
    }
    return !fileUrl.empty();
}

static bool InstallModrinth(Item& it, const Profile& p, int tab, std::wstring& err,
                            std::wstring& dest, HWND hwnd) {
    HttpResult r = HttpRequest(L"GET", L"https://api.modrinth.com/v2/project/" + it.slug + L"/version");
    if (r.status != 200) { err = L"Modrinth: нет версий проекта"; return false; }

    std::string fileUrl, fileName;
    try {
        auto j = nlohmann::json::parse(r.body);
        if (!PickModrinthFile(j, p, tab, fileUrl, fileName)) {
            err = L"Modrinth: нет версии под эту сборку (проверь ядро и версию)";
            return false;
        }
    } catch (...) {
        err = L"Modrinth: ответ не разобран";
        return false;
    }

    std::wstring note;
    fs::path dir = ContentDir(p, tab, note);
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::path target = dir / W(fileName);
    dest = note;

    auto lastPost = std::make_shared<std::chrono::steady_clock::time_point>(
        std::chrono::steady_clock::now());
    std::wstring derr;
    bool ok = HttpDownload(W(fileUrl), target, [&it, hwnd, lastPost](long long got, long long total) {
        int pct = total > 0 ? (int)(got * 100 / total) : -2;
        it.progress = pct;
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - *lastPost).count() > 120) {
            *lastPost = now;
            Repaint(hwnd);
        }
    }, derr);

    if (!ok) { err = L"Скачивание: " + derr; return false; }
    return true;
}

static void StartInstall(App& a, std::shared_ptr<Item> it, int profileIdx = -1) {
    if (it->progress >= 0) return;
    if (a.profiles.empty()) {
        a.status = L"Нет сборки — сначала создайте её";
        a.statusCol = C_RED;
        InvalidateRect(a.hwnd, nullptr, FALSE);
        return;
    }
    if (profileIdx < 0 || (size_t)profileIdx >= a.profiles.size()) profileIdx = a.profileIdx;
    if ((size_t)profileIdx >= a.profiles.size()) {
        a.status = L"Сначала выберите сборку";
        a.statusCol = C_YELLOW;
        InvalidateRect(a.hwnd, nullptr, FALSE);
        return;
    }
    Profile prof = a.profiles[(size_t)profileIdx];
    it->progress = 0;
    a.status = L"Установка «" + it->name + L"» в сборку «" + prof.name + L"»…";
    a.statusCol = C_DIM;
    InvalidateRect(a.hwnd, nullptr, FALSE);

    HWND hwnd = a.hwnd;
    int tab = it->type;
    int target = profileIdx;

    std::thread([hwnd, it, prof, tab, target] {
        std::wstring err, dest;
        bool ok = InstallModrinth(*it, prof, tab, err, dest, hwnd);
        PostUI(hwnd, [hwnd, it, ok, err, dest, target, prof] {
            it->progress = -1;
            App* a = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (a) {
                if (ok) {
                    it->installed = true;
                    a->status = L"Установлено: " + it->name + L"  →  «" + prof.name + L"» / " + dest;
                    a->statusCol = C_GREEN;
                } else {
                    a->status = err;
                    a->statusCol = C_RED;
                }
                if (target == a->profileIdx) RefreshMods(*a);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        });
    }).detach();
}

// ── выбор сборки, в которую ставить мод ───────────────────────────────────────

static void GoInstall(App& a, std::shared_ptr<Item> it, int idx) {
    a.installUi.open = false;
    a.installUi.item.reset();
    StartInstall(a, it, idx);
    InvalidateRect(a.hwnd, nullptr, FALSE);
}

static void OpenInstallPicker(App& a, std::shared_ptr<Item> it) {
    if (it->progress >= 0) return;
    if (a.profiles.empty()) {
        a.status = L"Нет сборки — сначала создайте её";
        a.statusCol = C_YELLOW;
        InvalidateRect(a.hwnd, nullptr, FALSE);
        return;
    }
    a.installUi = App::InstallUi{};
    a.installUi.open = true;
    a.installUi.gen = 1;
    a.installUi.item = it;
    a.installUi.busy = true;
    a.installUi.status = L"Проверяю, каким сборкам подходит…";
    InvalidateRect(a.hwnd, nullptr, FALSE);

    HWND hwnd = a.hwnd;
    int gen = a.installUi.gen;
    int tab = it->type;
    std::vector<Profile> profs = a.profiles;

    std::thread([hwnd, it, tab, profs, gen] {
        bool have = false;
        nlohmann::json versions;
        std::wstring warn;
        HttpResult r = HttpRequest(L"GET", L"https://api.modrinth.com/v2/project/" +
                                            it->slug + L"/version");
        if (r.status == 200) {
            try {
                versions = nlohmann::json::parse(r.body);
                have = versions.is_array();
            } catch (...) {}
        }
        if (!have) warn = L"Не удалось проверить совместимость — проверьте интернет";

        std::vector<int> cand;
        std::vector<std::wstring> labels;
        for (int i = 0; i < (int)profs.size(); ++i) {
            const Profile& p = profs[(size_t)i];
            bool isMod = (tab == 0);
            if (isMod && (p.loader.empty() || p.loader == L"vanilla")) continue;
            std::wstring ver;
            if (isMod) {
                bool ok = false;
                if (have) {
                    for (auto& v : versions) {
                        if (!JsonHas(v, "game_versions", U8(p.mc))) continue;
                        if (!JsonHas(v, "loaders", U8(p.loader))) continue;
                        ok = true;
                        ver = W(v.value("version_number", std::string()));
                        break;
                    }
                } else {
                    ok = true;   // проверить нельзя — показываем все сборки с ядром
                }
                if (!ok) continue;
            } else if (have) {
                for (auto& v : versions)
                    if (JsonHas(v, "game_versions", U8(p.mc))) {
                        ver = W(v.value("version_number", std::string()));
                        break;
                    }
            }
            cand.push_back(i);
            labels.push_back(ver);
        }

        PostUI(hwnd, [hwnd, cand, labels, warn, gen] {
            App* a = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (!a || a->installUi.gen != gen) return;
            a->installUi.busy = false;
            a->installUi.cand = cand;
            a->installUi.labels = labels;
            a->installUi.status = warn;
            InvalidateRect(hwnd, nullptr, FALSE);
        });
    }).detach();
}

// ══════════════════ ЗАПУСК ИГРЫ ══════════════════

static bool ReadJsonFile(const fs::path& p, nlohmann::json& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (s.empty()) return false;
    try {
        out = nlohmann::json::parse(s);
        return true;
    } catch (...) {
        return false;
    }
}

static const std::map<std::string, bool> kNoFeatures;

static bool RulesOk(const nlohmann::json& rules,
                    const std::map<std::string, bool>& features = kNoFeatures) {
    if (rules.is_null() || !rules.is_array() || rules.empty()) return true;
    bool allowed = false;
    for (auto& rule : rules) {
        if (!rule.is_object()) continue;
        bool action = rule.value("action", std::string()) == "allow";
        bool ok = true;
        if (rule.contains("os") && rule["os"].is_object()) {
            const auto& o = rule["os"];
            std::string name = o.value("name", std::string());
            if (!name.empty() && name != "windows") ok = false;
            std::string arch = o.value("arch", std::string());
            if (arch == "x86" || arch == "arm64") ok = false;
        }
        if (rule.contains("features") && rule["features"].is_object()) {
            for (auto it = rule["features"].begin(); it != rule["features"].end(); ++it) {
                bool want = it.value().is_boolean() ? it.value().get<bool>() : false;
                bool have = false;
                auto f = features.find(it.key());
                if (f != features.end()) have = f->second;
                if (have != want) ok = false;
            }
        }
        if (ok) allowed = action;
    }
    return allowed;
}

using SubstCtx = std::vector<std::pair<std::wstring, std::wstring>>;

static std::wstring Subst(std::wstring t, const SubstCtx& ctx) {
    for (auto& kv : ctx) {
        std::wstring ph = L"${" + kv.first + L"}";
        size_t p = 0;
        while ((p = t.find(ph, p)) != std::wstring::npos) {
            t.replace(p, ph.size(), kv.second);
            p += kv.second.size();
        }
    }
    return t;
}

static std::wstring MavenPath(const std::wstring& name) {
    std::vector<std::wstring> parts;
    size_t s = 0;
    for (;;) {
        size_t p = name.find(L':', s);
        parts.push_back(name.substr(s, p == std::wstring::npos ? std::wstring::npos : p - s));
        if (p == std::wstring::npos) break;
        s = p + 1;
    }
    if (parts.size() < 3) return {};
    std::wstring rel;
    size_t g = 0;
    for (;;) {
        size_t p = parts[0].find(L'.', g);
        rel += parts[0].substr(g, p == std::wstring::npos ? std::wstring::npos : p - g) + L"/";
        if (p == std::wstring::npos) break;
        g = p + 1;
    }
    rel += parts[1] + L"/" + parts[2] + L"/" + parts[1] + L"-" + parts[2];
    if (parts.size() > 3) rel += L"-" + parts[3];
    rel += L".jar";
    return rel;
}

static std::vector<std::wstring> NativeTags() {
    return { L"natives-windows", L"natives-windows-64" };
}

static std::vector<DownloadTask> LibFiles(const nlohmann::json& lib) {
    std::vector<DownloadTask> out;
    fs::path libs = McDir() / L"libraries";
    nlohmann::json d = lib.value("downloads", nlohmann::json::object());
    std::wstring name = W(lib.value("name", std::string()));
    std::wstring base = W(lib.value("url", std::string()));
    while (!base.empty() && base.back() == L'/') base.pop_back();

    if (d.is_object() && d.contains("artifact") && d["artifact"].is_object()) {
        const auto& art = d["artifact"];
        std::string path = art.value("path", std::string());
        std::string url = art.value("url", std::string());
        if (!path.empty() && !url.empty()) out.push_back({ W(url), libs / W(path) });
    } else if (!name.empty() && !base.empty()) {
        std::wstring rel = MavenPath(name);
        if (!rel.empty()) out.push_back({ base + L"/" + rel, libs / rel });
    }

    if (d.is_object() && d.contains("classifiers") && d["classifiers"].is_object()) {
        for (auto& tag : NativeTags()) {
            std::string key = U8(tag);
            if (!d["classifiers"].contains(key)) continue;
            const auto& cl = d["classifiers"][key];
            std::string path = cl.value("path", std::string());
            std::string url = cl.value("url", std::string());
            if (!path.empty() && !url.empty()) out.push_back({ W(url), libs / W(path) });
        }
    }

    // очень старый формат: natives { windows: "natives-windows-${arch}" } + name:classifier
    if (lib.contains("natives") && lib["natives"].is_object() && lib["natives"].contains("windows")) {
        std::wstring cls = W(lib["natives"]["windows"].get<std::string>());
        size_t p = 0;
        while ((p = cls.find(L"${arch}", p)) != std::wstring::npos) {
            cls.replace(p, 7, L"64");
            p += 2;
        }
        if (!name.empty() && !base.empty()) {
            std::wstring rel = MavenPath(name + L":" + cls);
            if (!rel.empty()) {
                bool dup = false;
                for (auto& t : out) if (t.dest == libs / rel) dup = true;
                if (!dup) out.push_back({ base + L"/" + rel, libs / rel });
            }
        }
    }
    return out;
}

static nlohmann::json MergeVersionJson(const nlohmann::json& parent, const nlohmann::json& child) {
    nlohmann::json out = parent;
    for (const char* k : { "id", "mainClass", "assetIndex", "assets", "downloads", "javaVersion",
                           "type", "logging", "minecraftArguments" }) {
        if (child.contains(k) && !child[k].is_null()) out[k] = child[k];
    }

    auto libKey = [](const nlohmann::json& l) {
        std::string n = l.value("name", std::string());
        size_t a = n.find(':');
        if (a == std::string::npos) return n;
        size_t b = n.find(':', a + 1);
        return b == std::string::npos ? n : n.substr(0, b);
    };

    std::set<std::string> skip;
    if (child.contains("libraries") && child["libraries"].is_array())
        for (auto& l : child["libraries"]) skip.insert(libKey(l));

    std::vector<nlohmann::json> libs;
    if (parent.contains("libraries") && parent["libraries"].is_array())
        for (auto& l : parent["libraries"]) if (!skip.count(libKey(l))) libs.push_back(l);
    if (child.contains("libraries") && child["libraries"].is_array())
        for (auto& l : child["libraries"]) libs.push_back(l);
    out["libraries"] = libs;

    bool hasP = parent.contains("arguments") && parent["arguments"].is_object();
    bool hasC = child.contains("arguments") && child["arguments"].is_object();
    if (hasP || hasC) {
        nlohmann::json args = nlohmann::json::object();
        for (const char* kind : { "game", "jvm" }) {
            nlohmann::json arr = nlohmann::json::array();
            if (hasP && parent["arguments"].contains(kind) && parent["arguments"][kind].is_array())
                for (auto& x : parent["arguments"][kind]) arr.push_back(x);
            if (hasC && child["arguments"].contains(kind) && child["arguments"][kind].is_array())
                for (auto& x : child["arguments"][kind]) arr.push_back(x);
            args[kind] = arr;
        }
        out["arguments"] = args;
    }
    return out;
}

static nlohmann::json ResolveVersionJson(const fs::path& versions, const std::wstring& id,
                                         std::wstring& root, std::set<std::wstring>& seen) {
    if (id.empty() || seen.count(id)) return nlohmann::json::object();
    seen.insert(id);
    nlohmann::json raw;
    if (!ReadJsonFile(versions / id / (id + L".json"), raw)) return nlohmann::json::object();
    std::string parent = raw.value("inheritsFrom", std::string());
    if (parent.empty()) {
        root = id;
        return raw;
    }
    std::wstring proot;
    nlohmann::json pj = ResolveVersionJson(versions, W(parent), proot, seen);
    if (pj.is_null() || pj.empty()) {
        root = id;
        return raw;
    }
    root = proot;
    return MergeVersionJson(pj, raw);
}

static std::wstring JavaExeIn(const fs::path& root) {
    std::error_code ec;
    for (const wchar_t* rel : { L"bin/javaw.exe", L"bin/java.exe", L"jre/bin/javaw.exe",
                                L"jre/bin/java.exe" }) {
        fs::path p = root / rel;
        if (fs::exists(p, ec)) return p.wstring();
    }
    return {};
}

static int ParseJavaMajor(const std::string& ver) {
    size_t i = 0;
    while (i < ver.size() && !isdigit((unsigned char)ver[i])) ++i;
    if (i >= ver.size()) return 0;
    int first = 0;
    while (i < ver.size() && isdigit((unsigned char)ver[i])) { first = first * 10 + (ver[i] - '0'); ++i; }
    if (first == 1 && i < ver.size() && ver[i] == '.') {
        ++i;
        int second = 0;
        while (i < ver.size() && isdigit((unsigned char)ver[i])) { second = second * 10 + (ver[i] - '0'); ++i; }
        if (second) return second;
    }
    return first;
}

static int JavaMajorOf(const std::wstring& exe) {
    fs::path p(exe);
    std::error_code ec;
    for (fs::path rel : { p.parent_path().parent_path() / L"release", p.parent_path() / L"release" }) {
        if (!fs::exists(rel, ec)) continue;
        std::ifstream f(rel);
        if (!f) continue;
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("JAVA_VERSION=", 0) != 0) continue;
            std::string v = line.substr(13);
            v.erase(std::remove(v.begin(), v.end(), '"'), v.end());
            v.erase(std::remove(v.begin(), v.end(), ' '), v.end());
            int m = ParseJavaMajor(v);
            if (m) return m;
        }
    }
    return 0;
}

static std::string RunCapture(const std::wstring& exe, const std::wstring& args, DWORD timeoutMs) {
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return {};
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{ sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = nullptr;

    PROCESS_INFORMATION pi{};
    std::wstring full = L"\"" + exe + L"\" " + args;
    std::vector<wchar_t> buf(full.begin(), full.end());
    buf.push_back(0);
    std::string out;
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                        nullptr, &si, &pi)) {
        CloseHandle(rd);
        CloseHandle(wr);
        return {};
    }
    CloseHandle(wr);

    char tmp[8192];
    ULONGLONG start = GetTickCount64();
    for (;;) {
        // читаем всё, что накопилось в трубе, чтобы процесс не заблокировался на выводе
        for (;;) {
            DWORD avail = 0;
            if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break;
            DWORD n = 0;
            DWORD want = (DWORD)std::min<size_t>(sizeof(tmp), avail);
            if (!ReadFile(rd, tmp, want, &n, nullptr) || n == 0) break;
            out.append(tmp, n);
        }
        DWORD w = WaitForSingleObject(pi.hProcess, 50);
        if (w == WAIT_OBJECT_0) {
            for (;;) {
                DWORD avail = 0;
                if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break;
                DWORD n = 0;
                DWORD want = (DWORD)std::min<size_t>(sizeof(tmp), avail);
                if (!ReadFile(rd, tmp, want, &n, nullptr) || n == 0) break;
                out.append(tmp, n);
            }
            break;
        }
        if (GetTickCount64() - start > timeoutMs) {
            TerminateProcess(pi.hProcess, 0);
            break;
        }
    }
    if (out.size() > 6000) out = out.substr(out.size() - 6000);
    CloseHandle(rd);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return out;
}

static void CollectJavaExes(const fs::path& base, int depth, std::vector<std::wstring>& out) {
    std::error_code ec;
    if (!fs::is_directory(base, ec)) return;
    for (auto& e : fs::directory_iterator(base, ec)) {
        if (ec) break;
        if (!e.is_directory(ec)) continue;
        fs::path j = e.path() / L"bin" / L"java.exe";
        if (fs::exists(j, ec)) out.push_back(j.wstring());
        else if (depth > 1) CollectJavaExes(e.path(), depth - 1, out);
    }
}

static std::vector<std::wstring> JavaCandidates() {
    std::vector<std::wstring> out;
    wchar_t* env = nullptr;
    size_t n = 0;
    _wdupenv_s(&env, &n, L"JAVA_HOME");
    if (env && *env) {
        fs::path j = fs::path(env) / L"bin" / L"java.exe";
        std::error_code ec;
        if (fs::exists(j, ec)) out.push_back(j.wstring());
    }
    free(env);

    wchar_t found[MAX_PATH]{};
    if (SearchPathW(nullptr, L"java.exe", nullptr, MAX_PATH, found, nullptr))
        out.push_back(found);

    std::vector<fs::path> bases;
    _wdupenv_s(&env, &n, L"ProgramFiles");
    if (env && *env) bases.push_back(fs::path(env));
    free(env);
    _wdupenv_s(&env, &n, L"ProgramFiles(x86)");
    if (env && *env) bases.push_back(fs::path(env));
    free(env);
    _wdupenv_s(&env, &n, L"LOCALAPPDATA");
    if (env && *env) bases.push_back(fs::path(env) / L"Programs");
    free(env);
    _wdupenv_s(&env, &n, L"APPDATA");
    if (env && *env) bases.push_back(fs::path(env) / L".minecraft" / L"runtime");
    free(env);

    const wchar_t* vendors[] = { L"Java", L"Eclipse Adoptium", L"Microsoft", L"Amazon Corretto",
                                 L"Zulu", L"BellSoft", L"Semeru", L"JetBrains", L"jdk", L"jre" };
    for (auto& b : bases)
        for (auto v : vendors) CollectJavaExes(b / v, 2, out);
    return out;
}

static std::wstring FindSystemJava(int minMajor) {
    std::vector<std::wstring> cands = JavaCandidates();
    std::set<std::wstring> seen;
    for (auto& c : cands) {
        if (!seen.insert(c).second) continue;
        int major = JavaMajorOf(c);
        if (!major) {
            std::string out = RunCapture(c, L"-version", 15000);
            size_t p = out.find("version \"");
            if (p != std::string::npos) major = ParseJavaMajor(out.substr(p + 9));
        }
        if (minMajor <= 0 || (major > 0 && major >= minMajor)) return c;
    }
    return {};
}

// Кнопка «Проверить Java» в настройках: путь из настроек, иначе автоопределение.
static void CheckJavaNow(App& a) {
    if (a.javaChecking) return;
    a.javaChecking = true;
    a.status = L"Проверяю Java…";
    a.statusCol = C_DIM;
    Repaint(a.hwnd);
    HWND hwnd = a.hwnd;
    std::wstring cfg = a.javaPathCfg;
    std::thread([hwnd, cfg]() {
        std::wstring exe;
        std::error_code ec;
        if (!cfg.empty() && fs::exists(cfg, ec)) exe = cfg;
        if (exe.empty()) exe = FindSystemJava(8);
        if (exe.empty()) {
            auto c = JavaCandidates();
            if (!c.empty()) exe = c[0];
        }
        int maj = exe.empty() ? 0 : JavaMajorOf(exe);
        PostUI(hwnd, [hwnd, exe, maj]() {
            App* ap = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (!ap) return;
            ap->javaChecking = false;
            if (maj > 0) {
                ap->lastJava = exe;
                ap->status = L"Java " + std::to_wstring(maj) + L" · " + exe;
                ap->statusCol = C_GREEN;
            } else {
                ap->status = L"Java не найдена — укажите путь к java.exe";
                ap->statusCol = C_RED;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        });
    }).detach();
}

static bool DownloadAll(HWND hwnd, const std::shared_ptr<LaunchState>& st,
                        const std::vector<DownloadTask>& tasks, const std::wstring& label,
                        std::wstring& err) {
    if (tasks.empty()) return true;
    std::atomic<size_t> next{ 0 }, done{ 0 };
    std::atomic<int> errors{ 0 };
    std::mutex errMu;
    size_t total = tasks.size();
    unsigned nthreads = (unsigned)std::min<size_t>(8, std::max<size_t>(1, total));
    std::vector<std::thread> pool;

    for (unsigned t = 0; t < nthreads; ++t) {
        pool.emplace_back([&] {
            for (;;) {
                if (st->stop.load()) break;
                size_t i = next.fetch_add(1);
                if (i >= tasks.size()) break;
                const auto& task = tasks[i];
                std::error_code ec;
                if (fs::exists(task.dest, ec) && fs::file_size(task.dest, ec) > 0) {
                    ++done;
                    continue;
                }
                std::wstring e;
                if (!HttpDownload(task.url, task.dest, {}, e)) {
                    if (errors.fetch_add(1) == 0) {
                        std::lock_guard<std::mutex> lk(errMu);
                        if (err.empty()) err = e + L" · " + task.dest.filename().wstring();
                    }
                }
                size_t d = ++done;
                if (d % 16 == 0 || d == total) {
                    st->pct = total ? (int)(d * 100 / total) : 100;
                    {
                        std::lock_guard<std::mutex> lk(st->mu);
                        st->msg = label + L" " + std::to_wstring(d) + L"/" + std::to_wstring(total);
                    }
                    Repaint(hwnd);
                }
            }
        });
    }
    for (auto& th : pool) th.join();
    if (st->stop.load()) {
        if (err.empty()) err = L"отменено";
        return false;
    }
    return errors.load() == 0;
}

static bool ExtractNatives(const nlohmann::json& vj, const fs::path& dest) {
    std::error_code ec;
    fs::create_directories(dest, ec);
    std::vector<fs::path> jars;
    if (vj.contains("libraries") && vj["libraries"].is_array()) {
        for (auto& lib : vj["libraries"]) {
            if (!lib.is_object()) continue;
            if (!RulesOk(lib.value("rules", nlohmann::json()))) continue;
            for (auto& f : LibFiles(lib)) {
                if (f.dest.filename().wstring().find(L"natives-windows") != std::wstring::npos)
                    jars.push_back(f.dest);
            }
        }
    }

    wchar_t sysdir[MAX_PATH]{};
    GetSystemDirectoryW(sysdir, MAX_PATH);
    fs::path tar = fs::path(sysdir) / L"tar.exe";
    if (!fs::exists(tar, ec)) return false;

    bool any = false;
    for (auto& jar : jars) {
        if (!fs::exists(jar, ec)) continue;
        std::wstring cmd = L"\"" + tar.wstring() + L"\" -xf \"" + jar.wstring() + L"\" -C \"" +
                           dest.wstring() + L"\"";
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(0);
        STARTUPINFOW si{ sizeof(si) };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                           nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 60000);
            DWORD code = 1;
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            if (code == 0) any = true;
        }
    }
    return any;
}

static std::wstring QuoteArg(const std::wstring& s) {
    if (s.empty()) return L"\"\"";
    bool need = false;
    for (wchar_t c : s)
        if (c == L' ' || c == L'\t' || c == L'"') { need = true; break; }
    if (!need) return s;

    std::wstring out = L"\"";
    int bs = 0;
    for (wchar_t c : s) {
        if (c == L'\\') {
            ++bs;
        } else if (c == L'"') {
            out.append((size_t)bs * 2 + 1, L'\\');
            out += L'"';
            bs = 0;
        } else {
            out.append((size_t)bs, L'\\');
            out += c;
            bs = 0;
        }
    }
    out.append((size_t)bs * 2, L'\\');
    out += L'"';
    return out;
}

static std::vector<std::wstring> SplitArgs(const std::wstring& s) {
    std::vector<std::wstring> out;
    std::wstring cur;
    bool quote = false;
    for (wchar_t c : s) {
        if (c == L'"') { quote = !quote; continue; }
        if ((c == L' ' || c == L'\t') && !quote) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static std::wstring DecodeText(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (n > 0) {
        std::wstring w((size_t)n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
        return w;
    }
    n = MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

static std::wstring ErrText(DWORD e) {
    wchar_t* b = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, e, 0, (LPWSTR)&b, 0, nullptr);
    std::wstring s = b ? b : (L"код " + std::to_wstring(e));
    if (b) LocalFree(b);
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ')) s.pop_back();
    return s;
}

struct LaunchJob {
    Profile prof;
    fs::path mc, gameDir;
    std::wstring versionId, ramMin, ramMax, javaCfg, javaArgs, gameArgs, username;
    std::wstring authName, authUuid, authToken, userType, elyInjector;
    bool ely = false;
    HWND hwnd = nullptr;
    std::shared_ptr<LaunchState> st;
};

static std::wstring InstallJavaRuntime(const std::wstring& component, HWND hwnd,
                                       const std::shared_ptr<LaunchState>& st, fs::path& mc) {
    {
        std::lock_guard<std::mutex> lk(st->mu);
        st->msg = L"Поиск Java " + component + L"…";
    }
    st->pct = 0;
    Repaint(hwnd);

    HttpResult all = HttpRequest(
        L"GET", L"https://launchermeta.mojang.com/v1/products/java-runtime/"
                L"2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json");
    if (all.status != 200) return {};
    nlohmann::json aj;
    try { aj = nlohmann::json::parse(all.body); } catch (...) { return {}; }

    const std::string plat = "windows-x64";
    std::string comp = U8(component);
    if (!aj.contains(plat) || !aj[plat].contains(comp) || !aj[plat][comp].is_array() ||
        aj[plat][comp].empty())
        return {};
    std::string murl = aj[plat][comp][0].value("manifest", nlohmann::json()).value("url", std::string());
    if (murl.empty()) return {};

    HttpResult mani = HttpRequest(L"GET", W(murl));
    if (mani.status != 200) return {};
    nlohmann::json mj;
    try { mj = nlohmann::json::parse(mani.body); } catch (...) { return {}; }
    if (!mj.contains("files") || !mj["files"].is_object()) return {};

    fs::path dest = mc / L"runtime" / component / L"windows-x64";
    std::vector<DownloadTask> tasks;
    for (auto it = mj["files"].begin(); it != mj["files"].end(); ++it) {
        if (!it.value().is_object()) continue;
        if (it.value().value("type", std::string()) != "file") continue;
        if (!it.value().contains("downloads")) continue;
        const auto& dl = it.value()["downloads"];
        if (!dl.is_object() || !dl.contains("raw")) continue;
        std::string url = dl["raw"].value("url", std::string());
        if (url.empty()) continue;
        tasks.push_back({ W(url), dest / W(it.key()) });
    }
    std::wstring err;
    if (!DownloadAll(hwnd, st, tasks, L"Загрузка Java", err)) return {};
    return JavaExeIn(dest);
}

static std::wstring EnsureJava(const nlohmann::json& vj, const fs::path& mc,
                               const std::wstring& custom, HWND hwnd,
                               const std::shared_ptr<LaunchState>& st) {
    std::error_code ec;
    if (!custom.empty() && fs::exists(custom, ec)) return custom;

    std::string comp;
    int major = 0;
    if (vj.contains("javaVersion") && vj["javaVersion"].is_object()) {
        comp = vj["javaVersion"].value("component", std::string());
        major = vj["javaVersion"].value("majorVersion", 0);
    }

    if (!comp.empty()) {
        fs::path root = mc / L"runtime" / W(comp) / L"windows-x64";
        std::wstring exe = JavaExeIn(root);
        if (!exe.empty()) return exe;
    }
    std::wstring sys = FindSystemJava(major);
    if (!sys.empty()) return sys;
    if (!comp.empty()) {
        fs::path mcopy = mc;
        std::wstring exe = InstallJavaRuntime(W(comp), hwnd, st, mcopy);
        if (!exe.empty()) return exe;
    }
    return {};
}

// ── страница проекта: описание, скриншоты, дерево зависимостей ────────────────
static std::wstring HtmlUnescape(std::wstring s) {
    const struct { const wchar_t* from; const wchar_t* to; } kEnt[] = {
        { L"&nbsp;", L" " }, { L"&amp;", L"&" }, { L"&lt;", L"<" }, { L"&gt;", L">" },
        { L"&quot;", L"\"" }, { L"&#39;", L"'" }, { L"&apos;", L"'" },
        { L"&mdash;", L"—" }, { L"&ndash;", L"–" }, { L"&hellip;", L"…" },
    };
    for (auto& e : kEnt) {
        size_t pos = 0;
        while ((pos = s.find(e.from, pos)) != std::wstring::npos) {
            s.replace(pos, wcslen(e.from), e.to);
            pos += wcslen(e.to);
        }
    }
    return s;
}

// Markdown → простой текст: теги, ссылки и маркеры оформления убираются.
static std::wstring StripInline(std::wstring s) {
    std::wstring o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] == L'<') {
            size_t e = s.find(L'>', i + 1);
            if (e != std::wstring::npos) { i = e + 1; continue; }
        }
        o += s[i++];
    }
    s.swap(o);

    for (;;) {   // ![alt](url)
        size_t p = s.find(L"![");
        if (p == std::wstring::npos) break;
        size_t e = s.find(L')', p + 2);
        if (e == std::wstring::npos) break;
        s.erase(p, e - p + 1);
    }
    for (;;) {   // [текст](url) → текст
        size_t p = s.find(L'[');
        if (p == std::wstring::npos) break;
        size_t m = s.find(L"](", p + 1);
        if (m == std::wstring::npos) break;
        size_t e = s.find(L')', m + 2);
        if (e == std::wstring::npos) break;
        std::wstring label = s.substr(p + 1, m - p - 1);
        s.replace(p, e - p + 1, label);
    }
    for (;;) {   // **жирный**
        size_t p = s.find(L"**");
        if (p == std::wstring::npos) break;
        size_t e = s.find(L"**", p + 2);
        if (e == std::wstring::npos) break;
        s.erase(e, 2);
        s.erase(p, 2);
    }
    for (;;) {   // __жирный__
        size_t p = s.find(L"__");
        if (p == std::wstring::npos) break;
        size_t e = s.find(L"__", p + 2);
        if (e == std::wstring::npos) break;
        s.erase(e, 2);
        s.erase(p, 2);
    }
    std::wstring clean;
    clean.reserve(s.size());
    for (wchar_t c : s) {
        if (c == L'*' || c == L'`') continue;   // курсив и код
        clean += c;
    }
    return HtmlUnescape(std::move(clean));
}

static std::vector<DescBlock> MarkdownBlocks(const std::wstring& text) {
    std::vector<DescBlock> out;
    std::vector<std::wstring> lines;
    std::wstring cur;
    for (wchar_t c : text) {
        if (c == L'\r') continue;
        if (c == L'\n') { lines.push_back(cur); cur.clear(); }
        else cur += c;
    }
    lines.push_back(cur);

    bool code = false;
    std::wstring para;
    size_t total = 0;
    auto flush = [&]() {
        if (!para.empty()) { out.push_back({ para, 0 }); para.clear(); }
    };
    for (auto raw : lines) {
        if (total > 24000) break;
        std::wstring line = raw;
        while (!line.empty() && (line.back() == L' ' || line.back() == L'\t')) line.pop_back();
        size_t b = 0;
        while (b < line.size() && (line[b] == L' ' || line[b] == L'\t')) ++b;
        line = line.substr(b);

        if (line.rfind(L"```", 0) == 0) { flush(); code = !code; continue; }
        if (code) {
            if (!line.empty()) { out.push_back({ line, 3 }); total += line.size(); }
            continue;
        }
        if (line.empty()) { flush(); continue; }
        if (line.rfind(L"#", 0) == 0) {
            size_t lv = 0;
            while (lv < line.size() && line[lv] == L'#') ++lv;
            if (lv < line.size() && line[lv] == L' ') {
                flush();
                std::wstring t = StripInline(line.substr(lv + 1));
                if (!t.empty()) { out.push_back({ t, 1 }); total += t.size(); }
                continue;
            }
        }
        if (line == L"---" || line == L"***" || line == L"___") { flush(); continue; }
        if (line.front() == L'|' && line.back() == L'|') {
            if (line.find(L"---") != std::wstring::npos) continue;
            std::wstring joined, cell;
            for (size_t i = 1; i + 1 < line.size(); ++i) {
                if (line[i] != L'|') { cell += line[i]; continue; }
                std::wstring part = StripInline(cell);
                if (!part.empty()) {
                    if (!joined.empty()) joined += L" · ";
                    joined += part;
                }
                cell.clear();
            }
            std::wstring part = StripInline(cell);
            if (!part.empty()) { if (!joined.empty()) joined += L" · "; joined += part; }
            if (!joined.empty()) { flush(); out.push_back({ joined, 0 }); total += joined.size(); }
            continue;
        }
        if (line.size() > 1 && (line[0] == L'-' || line[0] == L'*' || line[0] == L'+') &&
            line[1] == L' ') {
            flush();
            std::wstring t = StripInline(line.substr(2));
            if (!t.empty()) { out.push_back({ L"•  " + t, 2 }); total += t.size(); }
            continue;
        }
        if (line[0] == L'>') {
            flush();
            std::wstring t = StripInline(line.substr(1));
            if (!t.empty()) { out.push_back({ t, 4 }); total += t.size(); }
            continue;
        }
        size_t d = 0;
        while (d < line.size() && iswdigit(line[d])) ++d;
        if (d > 0 && d + 1 < line.size() && line[d] == L'.' && line[d + 1] == L' ') {
            flush();
            std::wstring t = StripInline(line);
            if (!t.empty()) { out.push_back({ t, 2 }); total += t.size(); }
            continue;
        }
        std::wstring add = StripInline(line);
        if (add.empty()) continue;
        if (!para.empty()) para += L' ';
        para += add;
        total += add.size();
    }
    flush();
    return out;
}

// Версия проекта под выбранную сборку (fallback — самая свежая).
static nlohmann::json PickProjectVersion(const nlohmann::json& arr, const Profile& p,
                                         bool has, int tab, bool fallback) {
    if (!arr.is_array() || arr.empty()) return nlohmann::json();
    for (auto& v : arr) {
        if (has && !p.mc.empty() && !JsonHas(v, "game_versions", U8(p.mc))) continue;
        if (has && tab == 0 && !p.loader.empty() && !JsonHas(v, "loaders", U8(p.loader))) continue;
        return v;
    }
    if (fallback) return arr[0];
    return nlohmann::json();
}

// Рекурсивно собирает обязательные (и опциональные) зависимости версии.
static void CollectDeps(const Profile& prof, bool has, int tab,
                        const nlohmann::json& version, int depth,
                        const std::wstring& indent,
                        std::set<std::string>& seen,
                        std::vector<DepRow>& out, int& count) {
    if (!version.is_object() || depth > 2 || count >= 18) return;
    if (!version.contains("dependencies") || !version["dependencies"].is_array()) return;

    struct DepReq { std::string pid; bool optional; };
    std::vector<DepReq> reqs;
    for (auto& d : version["dependencies"]) {
        if (!d.is_object()) continue;
        std::string pid = d.value("project_id", "");
        std::string type = d.value("dependency_type", "");
        if (pid.empty()) continue;
        if (type == "required") reqs.push_back({ pid, false });
        else if (type == "optional") reqs.push_back({ pid, true });
    }
    for (size_t i = 0; i < reqs.size(); ++i) {
        if (count >= 18) return;
        bool last = (i + 1 == reqs.size());
        std::wstring branch = indent + (last ? L"└─ " : L"├─ ");
        std::wstring child = indent + (last ? L"   " : L"│  ");
        const DepReq& r = reqs[i];
        bool again = seen.count(r.pid) > 0;

        DepRow row;
        row.prefix = branch;
        row.name = W(r.pid);
        row.optional = r.optional;
        if (!again) { seen.insert(r.pid); ++count; }

        HttpResult pr = HttpRequest(L"GET", L"https://api.modrinth.com/v2/project/" + W(r.pid));
        if (pr.status == 200) {
            try {
                auto j = nlohmann::json::parse(pr.body);
                std::string t = j.value("title", "");
                if (!t.empty()) row.name = W(t);
            } catch (...) {}
        }

        nlohmann::json pv;
        if (!again) {
            HttpResult vr = HttpRequest(L"GET", L"https://api.modrinth.com/v2/project/" +
                                               W(r.pid) + L"/version");
            if (vr.status == 200) {
                try {
                    auto j = nlohmann::json::parse(vr.body);
                    pv = PickProjectVersion(j, prof, has, tab, false);
                } catch (...) {}
            }
        }

        if (again) row.note = L"уже выше";
        else if (r.optional) row.note = L"опционально";
        else if (!pv.is_object()) { row.note = L"нет версии под сборку"; row.missing = true; }

        out.push_back(row);
        if (!again && !r.optional && pv.is_object())
            CollectDeps(prof, has, tab, pv, depth + 1, child, seen, out, count);
    }
}

// Открывает страницу проекта: описание, скриншоты и дерево зависимостей.
static void OpenModDetail(App& a, std::shared_ptr<Item> it) {
    if (!it) return;
    auto& d = a.detail;
    ++d.gen;
    d.open = true;
    d.busy = true;
    d.item = it;
    d.title = it->name;
    d.summary = it->desc;
    d.author.clear();
    d.version.clear();
    d.license.clear();
    d.depNote.clear();
    d.err.clear();
    d.cats.clear();
    d.blocks.clear();
    d.shots.clear();
    d.deps.clear();
    d.shotRects.clear();
    d.scrollY = 0.f;
    d.scrollMax = 0.f;
    d.shotOpen = -1;
    d.downloads = it->downloads;
    d.followers = 0;
    int gen = d.gen;
    HWND hwnd = a.hwnd;
    int tab = it->type;
    std::wstring slug = it->slug;
    Profile prof = a.profile;
    bool has = a.hasProfile;
    Repaint(hwnd);

    std::thread([hwnd, gen, tab, slug, prof, has]() {
        auto data = std::make_shared<DetailData>();
        {
            HttpResult r = HttpRequest(L"GET", L"https://api.modrinth.com/v2/project/" + slug);
            if (r.status != 200) {
                data->err = L"Modrinth: не удалось загрузить проект (" +
                            std::to_wstring(r.status) + L")";
            } else {
                try {
                    auto j = nlohmann::json::parse(r.body);
                    data->title = W(j.value("title", ""));
                    data->summary = W(j.value("description", ""));
                    data->body = W(j.value("body", ""));
                    data->downloads = j.value("downloads", (long long)0);
                    data->followers = j.value("followers", (long long)0);
                    if (j.contains("license") && j["license"].is_object()) {
                        std::string ln = j["license"].value("id", "");
                        if (ln.empty()) ln = j["license"].value("name", "");
                        data->license = W(ln);
                    }
                    for (auto& c : j.value("categories", nlohmann::json::array()))
                        if (c.is_string()) data->cats.push_back(W(c.get<std::string>()));
                    for (auto& g : j.value("gallery", nlohmann::json::array())) {
                        if (!g.is_object()) continue;
                        std::string u = g.value("url", "");
                        if (u.empty()) continue;
                        ShotInfo s;
                        s.url = W(u);
                        s.title = W(g.value("title", ""));
                        data->shots.push_back(std::move(s));
                        if (data->shots.size() >= 6) break;
                    }
                    std::string id = j.value("id", "");
                    if (id.empty()) id = U8(slug);
                    std::string team = j.value("team", "");
                    if (!team.empty()) {
                        HttpResult tr = HttpRequest(L"GET", L"https://api.modrinth.com/v2/team/" +
                                                            W(team) + L"/members");
                        if (tr.status == 200) {
                            try {
                                auto m = nlohmann::json::parse(tr.body);
                                std::string owner;
                                for (auto& mm : m) {
                                    if (!mm.is_object()) continue;
                                    std::string role = mm.value("role", "");
                                    std::string un = (mm.contains("user") && mm["user"].is_object())
                                                   ? mm["user"].value("username", "") : "";
                                    if (un.empty()) continue;
                                    if (role == "Owner") { owner = un; break; }
                                    if (owner.empty()) owner = un;
                                }
                                data->author = W(owner);
                            } catch (...) {}
                        }
                    }

                    HttpResult vr = HttpRequest(L"GET", L"https://api.modrinth.com/v2/project/" +
                                                       W(id) + L"/version");
                    nlohmann::json arr;
                    if (vr.status == 200) {
                        try { arr = nlohmann::json::parse(vr.body); } catch (...) {}
                    }
                    bool useProf = has && tab == 0;
                    nlohmann::json pv = PickProjectVersion(arr, prof, useProf, tab, false);
                    if (!pv.is_object()) {
                        data->depNote = (arr.is_array() && !arr.empty())
                            ? L"нет версии под эту сборку" : L"нет версий";
                    } else if (tab != 0) {
                        data->depNote = L"зависимости есть только у модов";
                    } else {
                        data->version = W(pv.value("version_number", ""));
                        data->depNote = useProf
                            ? L"для " + LoaderName(prof.loader) + L" " + prof.mc
                            : L"без фильтра по сборке";
                        std::set<std::string> seen;
                        seen.insert(id);
                        int count = 0;
                        CollectDeps(prof, useProf, tab, pv, 0, L"", seen, data->deps, count);
                        if (data->deps.empty()) data->depNote += L" · обязательных нет";
                    }
                } catch (...) {
                    data->err = L"Modrinth: ответ не разобран";
                }
            }
            for (auto& s : data->shots) {
                std::string bytes = GetBytes(s.url);
                if (bytes.empty()) continue;
                ImageData img;
                if (DecodeImage(bytes, img)) s.px = std::move(img);
            }
        }

        PostUI(hwnd, [hwnd, data, gen]() {
            App* a = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (!a || !a->detail.open || a->detail.gen != gen) return;
            auto& d = a->detail;
            d.busy = false;
            d.err = data->err;
            if (!data->title.empty()) d.title = data->title;
            if (!data->summary.empty()) d.summary = data->summary;
            if (!data->author.empty()) d.author = data->author;
            if (!data->version.empty()) d.version = data->version;
            if (!data->license.empty()) d.license = data->license;
            if (!data->depNote.empty()) d.depNote = data->depNote;
            d.downloads = data->downloads;
            d.followers = data->followers;
            d.cats = data->cats;
            d.deps = std::move(data->deps);
            d.blocks = MarkdownBlocks(data->body);
            d.shots = std::move(data->shots);
            D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            if (a->rt) {
                for (auto& s : d.shots) {
                    if (s.px.w <= 0 || s.px.h <= 0 || s.px.px.empty()) continue;
                    a->rt->CreateBitmap(D2D1::SizeU((UINT32)s.px.w, (UINT32)s.px.h),
                                        s.px.px.data(), (UINT32)s.px.w * 4, props, s.bmp.put());
                }
            }
            d.scrollY = 0.f;
            Repaint(hwnd);
        });
    }).detach();
}

// ── создание сборок: скачивание версии и установка ядра ──────────────────────

static bool EnsureVersionJson(const std::wstring& vid, std::wstring& err) {
    fs::path vjson = McDir() / L"versions" / vid / (vid + L".json");
    std::error_code ec;
    if (fs::exists(vjson, ec)) return true;
    HttpResult m = HttpRequest(L"GET", L"https://launchermeta.mojang.com/mc/game/"
                                        L"version_manifest_v2.json");
    if (m.status != 200) { err = L"нет манифеста версий Mojang"; return false; }
    std::string url;
    try {
        auto mj = nlohmann::json::parse(m.body);
        for (auto& v : mj["versions"])
            if (v.value("id", "") == U8(vid)) { url = v.value("url", ""); break; }
    } catch (...) {}
    if (url.empty()) { err = L"версия " + vid + L" не найдена у Mojang"; return false; }
    fs::create_directories(vjson.parent_path(), ec);
    return HttpDownload(W(url), vjson, {}, err);
}

// Собирает задачи установки версии (client, библиотеки, ресурсы).
static bool CollectVersionTasks(const std::wstring& vid, std::vector<DownloadTask>& tasks,
                                nlohmann::json& vj, fs::path& rootJar, std::wstring& err) {
    fs::path mc = McDir();
    fs::path versions = mc / L"versions";
    std::set<std::wstring> seen;
    std::wstring rootId;
    vj = ResolveVersionJson(versions, vid, rootId, seen);
    if (vj.is_null() || vj.empty()) { err = L"не читается описание версии " + vid; return false; }
    if (rootId.empty()) rootId = vid;
    rootJar = versions / rootId / (rootId + L".jar");
    std::error_code ec;

    if (!fs::exists(rootJar, ec) && vj.contains("downloads") && vj["downloads"].is_object()) {
        const auto& dl = vj["downloads"];
        if (dl.contains("client") && dl["client"].is_object()) {
            std::string url = dl["client"].value("url", std::string());
            if (!url.empty()) tasks.push_back({ W(url), rootJar });
        }
    }
    if (vj.contains("libraries") && vj["libraries"].is_array()) {
        for (auto& lib : vj["libraries"]) {
            if (!lib.is_object()) continue;
            if (!RulesOk(lib.value("rules", nlohmann::json()))) continue;
            for (auto& f : LibFiles(lib))
                if (!f.url.empty()) tasks.push_back(f);
        }
    }
    if (vj.contains("assetIndex") && vj["assetIndex"].is_object()) {
        const auto& ai = vj["assetIndex"];
        std::string aid = ai.value("id", std::string());
        if (!aid.empty()) {
            fs::path idx = mc / L"assets" / L"indexes" / (W(aid) + L".json");
            if (!fs::exists(idx, ec)) {
                std::wstring url = W(ai.value("url", std::string()));
                std::wstring e;
                if (url.empty() || !HttpDownload(url, idx, {}, e)) {
                    err = L"индекс ресурсов: " + e;
                    return false;
                }
            }
            nlohmann::json index;
            if (ReadJsonFile(idx, index) && index.contains("objects") &&
                index["objects"].is_object()) {
                for (auto it = index["objects"].begin(); it != index["objects"].end(); ++it) {
                    std::string h = it.value().value("hash", std::string());
                    if (h.size() < 2) continue;
                    std::wstring hs = W(h);
                    tasks.push_back({ L"https://resources.download.minecraft.net/" +
                                          W(h.substr(0, 2)) + L"/" + hs,
                                      mc / L"assets" / L"objects" / W(h.substr(0, 2)) / hs });
                }
            }
        }
    }
    return true;
}

static bool EnsureVersionFiles(const std::wstring& vid, HWND hwnd,
                               const std::shared_ptr<LaunchState>& st, std::wstring& err) {
    std::wstring jerr;
    if (!EnsureVersionJson(vid, jerr)) {
        std::error_code ec;
        if (!fs::exists(McDir() / L"versions" / vid / (vid + L".json"), ec)) {
            err = jerr;
            return false;
        }
    }
    std::vector<DownloadTask> tasks;
    nlohmann::json vj;
    fs::path rootJar;
    if (!CollectVersionTasks(vid, tasks, vj, rootJar, err)) return false;
    {
        std::lock_guard<std::mutex> lk(st->mu);
        st->msg = L"Загрузка файлов " + vid + L"…";
    }
    Repaint(hwnd);
    if (!DownloadAll(hwnd, st, tasks, L"Загрузка файлов", err)) {
        if (err.empty()) err = L"файлы игры не скачались";
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(st->mu);
        st->msg = L"Распаковка нативов…";
    }
    Repaint(hwnd);
    ExtractNatives(vj, McDir() / L"versions" / vid / L"natives");
    std::error_code ec;
    std::ofstream mark(McDir() / L"versions" / vid / kInstallMarker, std::ios::binary);
    if (mark) mark << "ok";
    return true;
}

// Последняя версия ядра для указанной версии игры.
static std::wstring LatestLoaderVersion(const std::wstring& loader, const std::wstring& mc,
                                        std::wstring& err) {
    if (loader == L"fabric" || loader == L"quilt") {
        std::wstring base = loader == L"quilt" ? L"https://meta.quiltmc.org/v3"
                                               : L"https://meta.fabricmc.net/v2";
        HttpResult r = HttpRequest(L"GET", base + L"/versions/loader/" + W(UrlEncode(U8(mc))));
        if (r.status != 200) { err = L"нет списка " + LoaderName(loader) + L" для " + mc; return {}; }
        try {
            auto j = nlohmann::json::parse(r.body);
            if (j.is_array() && !j.empty()) {
                for (auto& o : j) {
                    if (!o.contains("loader") || !o["loader"].is_object()) continue;
                    std::wstring v = W(o["loader"].value("version", std::string()));
                    if (v.empty()) continue;
                    if (v.find(L'-') == std::wstring::npos) return v;   // стабильная
                }
                auto& o = j[0];
                if (o.contains("loader") && o["loader"].is_object())
                    return W(o["loader"].value("version", std::string()));
            }
        } catch (...) {}
        err = L"не удалось выбрать версию " + LoaderName(loader);
        return {};
    }
    if (loader == L"forge") {
        HttpResult r = HttpRequest(L"GET", L"https://files.minecraftforge.net/net/minecraftforge/"
                                          L"forge/promotions_slim.json");
        if (r.status != 200) { err = L"нет списка Forge"; return {}; }
        try {
            auto j = nlohmann::json::parse(r.body);
            if (j.contains("promos") && j["promos"].is_object()) {
                std::string key = U8(mc) + "-recommended";
                if (j["promos"].contains(key)) return W(j["promos"].value(key, std::string()));
                key = U8(mc) + "-latest";
                if (j["promos"].contains(key)) return W(j["promos"].value(key, std::string()));
            }
        } catch (...) {}
        err = L"нет Forge для " + mc;
        return {};
    }
    if (loader == L"neoforge") {
        HttpResult r = HttpRequest(L"GET", L"https://maven.neoforged.net/api/maven/versions/"
                                          L"releases/net/neoforged/neoforge");
        if (r.status != 200) { err = L"нет списка NeoForge"; return {}; }
        // Minecraft 1.21.1 → 21.1.*, 1.20.1 → 47.1.*, 1.20.6 → 20.6.*
        std::wstring prefix;
        if (mc == L"1.20.1") prefix = L"47.1.";
        else if (mc.rfind(L"1.", 0) == 0) {
            std::wstring rest = mc.substr(2);
            prefix = (rest.find(L'.') == std::wstring::npos ? rest + L".0." : rest + L".");
        }
        try {
            auto j = nlohmann::json::parse(r.body);
            std::wstring best;
            if (j.contains("versions") && j["versions"].is_array() && !prefix.empty()) {
                for (auto& v : j["versions"]) {
                    if (!v.is_string()) continue;
                    std::wstring s = W(v.get<std::string>());
                    if (s.rfind(prefix, 0) == 0) best = s;   // список по возрастанию
                }
            }
            if (!best.empty()) return best;
        } catch (...) {}
        err = L"нет NeoForge для " + mc;
        return {};
    }
    err = L"неизвестное ядро";
    return {};
}

static bool InstallLoaderNow(const std::wstring& loader, const std::wstring& mc, HWND hwnd,
                             const std::shared_ptr<LaunchState>& st, std::wstring& vid,
                             std::wstring& lver, std::wstring& err) {
    auto stage = [&](const std::wstring& m) {
        {
            std::lock_guard<std::mutex> lk(st->mu);
            st->msg = m;
            st->log.push_back(m);
        }
        Repaint(hwnd);
    };

    // база нужна всегда: client.jar, библиотеки и выбор Java
    stage(L"Скачивание Minecraft " + mc + L"…");
    if (!EnsureVersionFiles(mc, hwnd, st, err)) return false;

    if (loader == L"fabric" || loader == L"quilt") {
        stage(L"Поиск версии " + LoaderName(loader) + L"…");
        lver = LatestLoaderVersion(loader, mc, err);
        if (lver.empty()) return false;
        std::wstring base = loader == L"quilt" ? L"https://meta.quiltmc.org/v3"
                                               : L"https://meta.fabricmc.net/v2";
        HttpResult r = HttpRequest(L"GET", base + L"/versions/loader/" + W(UrlEncode(U8(mc))) +
                                              L"/" + W(UrlEncode(U8(lver))) + L"/profile/json");
        if (r.status != 200) {
            err = L"профиль " + LoaderName(loader) + L" не получен";
            return false;
        }
        vid = (loader == L"fabric" ? L"fabric-loader-" : L"quilt-loader-") + lver + L"-" + mc;
        fs::path p = McDir() / L"versions" / vid / (vid + L".json");
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        if (!f) { err = L"не записывается профиль ядра"; return false; }
        f << r.body;
        f.close();
        stage(L"Загрузка библиотек " + LoaderName(loader) + L"…");
        return EnsureVersionFiles(vid, hwnd, st, err);
    }

    // Forge и NeoForge ставятся официальным установщиком
    std::wstring url;
    if (loader == L"forge") {
        stage(L"Поиск версии Forge…");
        lver = LatestLoaderVersion(loader, mc, err);
        if (lver.empty()) return false;
        vid = mc + L"-forge-" + lver;
        url = L"https://maven.minecraftforge.net/net/minecraftforge/forge/" + mc + L"-" + lver +
              L"/forge-" + mc + L"-" + lver + L"-installer.jar";
    } else {
        stage(L"Поиск версии NeoForge…");
        lver = LatestLoaderVersion(loader, mc, err);
        if (lver.empty()) return false;
        vid = L"neoforge-" + lver;
        url = L"https://maven.neoforged.net/releases/net/neoforged/neoforge/" + lver +
              L"/neoforge-" + lver + L"-installer.jar";
    }

    std::error_code ec;
    fs::create_directories(NexusDir() / L"installers", ec);
    fs::path jar = NexusDir() / L"installers" / (vid + L"-installer.jar");
    if (!fs::exists(jar, ec)) {
        stage(L"Скачивание установщика " + LoaderName(loader) + L"…");
        std::wstring e;
        if (!HttpDownload(url, jar, {}, e)) {
            err = L"установщик не скачался: " + e;
            return false;
        }
    }

    std::set<std::wstring> before;
    fs::path vdir = McDir() / L"versions";
    if (fs::exists(vdir, ec))
        for (auto& e : fs::directory_iterator(vdir, ec))
            if (e.is_directory(ec)) before.insert(e.path().filename().wstring());

    std::set<std::wstring> seen;
    std::wstring rootId;
    nlohmann::json baseJson = ResolveVersionJson(vdir, mc, rootId, seen);
    std::wstring java = EnsureJava(baseJson, McDir(), L"", hwnd, st);
    if (java.empty()) {
        err = L"для установки ядра нужна Java — скачайте её в настройках";
        return false;
    }

    stage(L"Установка " + LoaderName(loader) + L" " + lver + L"…");
    // установщику Forge/NeoForge нужен launcher_profiles.json в целевой папке
    fs::path lp = McDir() / L"launcher_profiles.json";
    if (!fs::exists(lp, ec)) {
        std::ofstream lpf(lp, std::ios::binary);
        if (lpf) lpf << "{\"profiles\":{},\"settings\":{},\"version\":3}";
    }
    std::wstring args = L"-jar \"" + jar.wstring() + L"\" --installClient \"" +
                        McDir().wstring() + L"\"";
    std::string out = RunCapture(java, args, 900000);

    fs::path target = vdir / vid / (vid + L".json");
    if (!fs::exists(target, ec)) {
        if (fs::exists(vdir, ec)) {
            for (auto& e : fs::directory_iterator(vdir, ec)) {
                std::wstring n = e.path().filename().wstring();
                if (before.count(n)) continue;
                if (n.find(L"forge") != std::wstring::npos) { vid = n; target = vdir / n / (n + L".json"); }
            }
        }
    }
    if (!fs::exists(target, ec)) {
        std::wstring tail = W(out.size() > 300 ? out.substr(out.size() - 300) : out);
        err = L"установщик не создал версию: " + tail;
        return false;
    }
    stage(L"Загрузка файлов " + vid + L"…");
    return EnsureVersionFiles(vid, hwnd, st, err);
}

static void BuildWorker(HWND hwnd, std::shared_ptr<LaunchState> st, Profile prof, int loaderIdx) {
    static const wchar_t* keys[5] = { L"vanilla", L"fabric", L"quilt", L"forge", L"neoforge" };
    std::wstring loader = keys[std::clamp(loaderIdx, 0, 4)];
    std::wstring vid = prof.mc, lver, err;
    bool ok = true;

    {
        std::lock_guard<std::mutex> lk(st->mu);
        st->log.push_back(L"Сборка: " + prof.name + L" (" + LoaderName(loader) + L" " + prof.mc + L")");
    }
    if (loader == L"vanilla") {
        {
            std::lock_guard<std::mutex> lk(st->mu);
            st->msg = L"Скачивание Minecraft " + prof.mc + L"…";
        }
        Repaint(hwnd);
        ok = EnsureVersionFiles(prof.mc, hwnd, st, err);
    } else {
        ok = InstallLoaderNow(loader, prof.mc, hwnd, st, vid, lver, err);
    }

    st->pct = ok ? 100 : -1;
    {
        std::lock_guard<std::mutex> lk(st->mu);
        st->msg = ok ? L"Сборка готова" : L"Ошибка: " + err;
        st->log.push_back(ok ? L"Готово: " + vid : L"Ошибка: " + err);
    }
    st->active = false;

    PostUI(hwnd, [hwnd, ok, vid, lver, err, prof] {
        App* a = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (!a) return;
        int idx = -1;
        for (size_t i = 0; i < a->profiles.size(); ++i)
            if (a->profiles[i].dir == prof.dir) { idx = (int)i; break; }
        if (idx >= 0) {
            if (ok) {
                a->profiles[(size_t)idx].version = vid;
                if (!lver.empty()) a->profiles[(size_t)idx].loaderVer = lver;
                if (a->profileIdx == idx) a->profile = a->profiles[(size_t)idx];
            } else {
                // сборка не создалась — убираем её из списка
                a->profiles.erase(a->profiles.begin() + idx);
                if (a->profileIdx >= (int)a->profiles.size())
                    a->profileIdx = (int)a->profiles.size() - 1;
                a->hasProfile = a->profileIdx >= 0 && !a->profiles.empty();
                if (a->hasProfile) {
                    a->profile = a->profiles[(size_t)a->profileIdx];
                    RefreshMods(*a);
                } else {
                    a->profile = Profile{};
                }
            }
            SaveProfiles(*a);
        }
        if (ok) {
            a->status = L"Сборка «" + prof.name + L"» создана, версия скачана";
            a->statusCol = C_GREEN;
        } else {
            a->status = L"Не удалось создать «" + prof.name + L"»: " + err;
            a->statusCol = C_RED;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
    });
}

static std::wstring SafeFolderName(const std::wstring& in) {
    std::wstring out;
    for (wchar_t c : in) {
        if (c == L'<' || c == L'>' || c == L':' || c == L'"' || c == L'/' || c == L'\\' ||
            c == L'|' || c == L'?' || c == L'*')
            out += L'_';
        else
            out += c;
    }
    size_t a = out.find_first_not_of(L" \t\r\n");
    size_t b = out.find_last_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"build";
    out = out.substr(a, b - a + 1);
    return out.empty() ? L"build" : out;
}

static void FillLatestMc(App& a) {
    HWND hwnd = a.hwnd;
    std::thread([hwnd] {
        HttpResult r = HttpRequest(L"GET", L"https://launchermeta.mojang.com/mc/game/"
                                            L"version_manifest_v2.json");
        std::wstring v;
        if (r.status == 200) {
            try {
                auto j = nlohmann::json::parse(r.body);
                if (j.contains("latest") && j["latest"].is_object())
                    v = W(j["latest"].value("release", std::string()));
            } catch (...) {}
        }
        if (v.empty()) return;
        PostUI(hwnd, [hwnd, v] {
            App* a = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (!a || !a->buildUi.open || a->buildUi.busy) return;
            if (a->buildUi.mc.empty()) {
                a->buildUi.mc = v;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        });
    }).detach();
}

static void OpenBuildModal(App& a) {
    if (a.launch && a.launch->active.load()) {
        a.status = L"Дождитесь завершения подготовки";
        a.statusCol = C_YELLOW;
        InvalidateRect(a.hwnd, nullptr, FALSE);
        return;
    }
    a.buildUi = App::BuildUi{};
    a.buildUi.open = true;
    a.buildUi.focus = 1;
    if (a.hasProfile && !a.profile.mc.empty()) a.buildUi.mc = a.profile.mc;
    FillLatestMc(a);
    InvalidateRect(a.hwnd, nullptr, FALSE);
}

static void CreateBuild(App& a) {
    if (a.buildUi.busy) return;
    auto trim = [](std::wstring s) {
        size_t b = s.find_last_not_of(L" \t\r\n");
        s.erase(b == std::wstring::npos ? 0 : b + 1);
        size_t f = s.find_first_not_of(L" \t\r\n");
        if (f == std::wstring::npos) return std::wstring();
        return s.substr(f);
    };
    std::wstring name = trim(a.buildUi.name);
    std::wstring mc = trim(a.buildUi.mc);
    if (name.empty()) {
        a.buildUi.status = L"Введите название сборки";
        a.buildUi.focus = 1;
        InvalidateRect(a.hwnd, nullptr, FALSE);
        return;
    }
    if (mc.empty()) {
        a.buildUi.status = L"Укажите версию Minecraft";
        a.buildUi.focus = 2;
        InvalidateRect(a.hwnd, nullptr, FALSE);
        return;
    }

    std::error_code ec;
    fs::path base = McDir() / L"profiles";
    fs::path dir = base / SafeFolderName(name);
    int suffix = 2;
    while (fs::exists(dir / L"mods", ec)) dir = base / (SafeFolderName(name) + L"-" + std::to_wstring(suffix++));
    fs::create_directories(dir / L"mods", ec);
    if (!fs::exists(dir, ec)) {
        a.buildUi.status = L"Не удалось создать папку сборки";
        InvalidateRect(a.hwnd, nullptr, FALSE);
        return;
    }

    static const wchar_t* keys[5] = { L"vanilla", L"fabric", L"quilt", L"forge", L"neoforge" };
    int li = std::clamp(a.buildUi.loader, 0, 4);

    Profile pr;
    pr.name = name;
    pr.dir = dir.wstring();
    pr.loader = keys[li];
    pr.mc = mc;
    pr.version = pr.loader == L"vanilla" ? mc : L"";
    pr.valid = true;

    a.profiles.push_back(pr);
    a.profileIdx = (int)a.profiles.size() - 1;
    a.profile = pr;
    a.hasProfile = true;
    SaveProfiles(a);
    RefreshMods(a);

    auto st = std::make_shared<LaunchState>();
    st->active = true;
    st->pct = 0;
    st->msg = L"Подготовка «" + name + L"»…";
    st->log.push_back(L"Сборка: " + name);
    a.launch = st;

    a.buildUi.open = false;
    a.buildUi.busy = false;
    a.status = L"Создаю «" + name + L"» — скачиваю " +
               (li ? LoaderName(keys[li]) + L" " : L"") + mc + L"…";
    a.statusCol = C_ACCENT;
    std::thread(BuildWorker, a.hwnd, st, pr, li).detach();
    InvalidateRect(a.hwnd, nullptr, FALSE);
}

static const wchar_t* JavaFileFilter() {
    switch (g_lang.load(std::memory_order_relaxed)) {
    case LANG_EN: return L"java.exe\0java.exe\0All files\0*.*\0";
    case LANG_FR: return L"java.exe\0java.exe\0Tous les fichiers\0*.*\0";
    case LANG_DE: return L"java.exe\0java.exe\0Alle Dateien\0*.*\0";
    case LANG_ZH: return L"java.exe\0java.exe\0所有文件\0*.*\0";
    case LANG_JA: return L"java.exe\0java.exe\0すべてのファイル\0*.*\0";
    default:      return L"java.exe\0java.exe\0Все файлы\0*.*\0";
    }
}

static const wchar_t* SkinFileFilter() {
    switch (g_lang.load(std::memory_order_relaxed)) {
    case LANG_EN: return L"PNG image\0*.png\0All files\0*.*\0";
    case LANG_FR: return L"Image PNG\0*.png\0Tous les fichiers\0*.*\0";
    case LANG_DE: return L"PNG-Bild\0*.png\0Alle Dateien\0*.*\0";
    case LANG_ZH: return L"PNG 图片\0*.png\0所有文件\0*.*\0";
    case LANG_JA: return L"PNG 画像\0*.png\0すべてのファイル\0*.*\0";
    default:      return L"Картинка PNG\0*.png\0Все файлы\0*.*\0";
    }
}

static void PickJavaExe(App& a) {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = a.hwnd;
    ofn.lpstrFilter = JavaFileFilter();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    std::wstring title = TrText(L"Выберите java.exe");
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;

    fs::path p(file);
    if (_wcsicmp(p.filename().c_str(), L"java.exe") != 0 &&
        _wcsicmp(p.filename().c_str(), L"javaw.exe") != 0) {
        a.status = L"Выберите java.exe (или javaw.exe)";
        a.statusCol = C_RED;
        InvalidateRect(a.hwnd, nullptr, FALSE);
        return;
    }
    a.javaPathCfg = p.wstring();
    SaveSettings(a);
    a.lastJava = a.javaPathCfg;
    a.status = L"Java выбрана: " + a.javaPathCfg;
    a.statusCol = C_GREEN;
    InvalidateRect(a.hwnd, nullptr, FALSE);
}

static void DownloadJavaFor(App& a, const wchar_t* component) {
    if (a.javaSt && a.javaSt->active.load()) {
        a.status = L"Java уже скачивается — дождитесь окончания";
        a.statusCol = C_YELLOW;
        InvalidateRect(a.hwnd, nullptr, FALSE);
        return;
    }
    auto st = std::make_shared<LaunchState>();
    st->active = true;
    st->pct = 0;
    st->msg = L"Скачивание Java…";
    a.javaSt = st;
    a.status = L"Скачивание Java (" + std::wstring(component) + L")…";
    a.statusCol = C_ACCENT;

    HWND hwnd = a.hwnd;
    std::wstring comp = component;
    std::thread([hwnd, st, comp] {
        fs::path mc = McDir();
        std::wstring exe = InstallJavaRuntime(comp, hwnd, st, mc);
        st->active = false;
        st->pct = exe.empty() ? -1 : 100;
        {
            std::lock_guard<std::mutex> lk(st->mu);
            st->msg = exe.empty() ? L"Не удалось скачать Java" : L"Java готова";
        }
        PostUI(hwnd, [hwnd, exe] {
            App* a = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (!a) return;
            if (!exe.empty()) {
                a->javaPathCfg = exe;
                a->lastJava = exe;
                SaveSettings(*a);
                a->status = L"Java скачана и выбрана";
                a->statusCol = C_GREEN;
            } else {
                a->status = L"Не удалось скачать Java";
                a->statusCol = C_RED;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        });
    }).detach();
    InvalidateRect(a.hwnd, nullptr, FALSE);
}

static bool BuildLaunchCommand(const LaunchJob& job, const nlohmann::json& vj,
                               const fs::path& rootJar, const std::wstring& java,
                               const fs::path& nativesDir, std::wstring& cmdline,
                               std::wstring& err) {
    fs::path libs = job.mc / L"libraries";
    std::vector<std::wstring> cp;
    std::set<std::wstring> seen;
    auto addCp = [&](const std::wstring& p) {
        if (!p.empty() && seen.insert(p).second) cp.push_back(p);
    };
    addCp(rootJar.wstring());

    if (vj.contains("libraries") && vj["libraries"].is_array()) {
        for (auto& lib : vj["libraries"]) {
            if (!lib.is_object()) continue;
            if (!RulesOk(lib.value("rules", nlohmann::json()))) continue;
            for (auto& f : LibFiles(lib)) {
                std::error_code ec;
                if (fs::exists(f.dest, ec)) addCp(f.dest.wstring());
            }
        }
    }

    std::error_code ec;

    std::wstring cpstr;
    for (size_t i = 0; i < cp.size(); ++i) {
        if (i) cpstr += L";";
        cpstr += cp[i];
    }

    std::wstring assetId = L"legacy";
    if (vj.contains("assetIndex") && vj["assetIndex"].is_object())
        assetId = W(vj["assetIndex"].value("id", "legacy"));

    SubstCtx ctx = {
        { L"natives_directory", nativesDir.wstring() },
        { L"launcher_name", L"Midnight launcher" },
        { L"launcher_version", L"3.0" },
        { L"classpath", cpstr },
        { L"classpath_separator", L";" },
        { L"library_directory", libs.wstring() },
        { L"version_name", job.versionId },
        { L"game_directory", job.gameDir.wstring() },
        { L"assets_root", (job.mc / L"assets").wstring() },
        { L"assets_index_name", assetId },
        { L"auth_player_name", job.authName.empty() ? job.username : job.authName },
        { L"auth_uuid", job.authUuid.empty() ? L"0" : job.authUuid },
        { L"auth_access_token", job.authToken.empty() ? L"0" : job.authToken },
        { L"auth_session", job.authToken.empty() ? L"0" : job.authToken },
        { L"user_type", job.userType.empty() ? L"legacy" : job.userType },
        { L"version_type", L"release" },
        { L"user_properties", L"{}" },
        { L"clientid", L"" },
        { L"auth_xuid", L"" },
        { L"resolution_width", L"854" },
        { L"resolution_height", L"480" },
        { L"quickPlayPath", L"" },
    };

    std::vector<std::wstring> jvm, game;
    auto addEntry = [&](std::vector<std::wstring>& dst, const nlohmann::json& e) {
        if (e.is_string()) {
            dst.push_back(Subst(W(e.get<std::string>()), ctx));
            return;
        }
        if (!e.is_object()) return;
        if (!RulesOk(e.value("rules", nlohmann::json()))) return;
        nlohmann::json v = e.value("value", nlohmann::json());
        if (v.is_string()) {
            dst.push_back(Subst(W(v.get<std::string>()), ctx));
        } else if (v.is_array()) {
            for (auto& x : v)
                if (x.is_string()) dst.push_back(Subst(W(x.get<std::string>()), ctx));
        }
    };

    if (vj.contains("arguments") && vj["arguments"].is_object()) {
        const auto& args = vj["arguments"];
        if (args.contains("jvm") && args["jvm"].is_array())
            for (auto& e : args["jvm"]) addEntry(jvm, e);
        if (args.contains("game") && args["game"].is_array())
            for (auto& e : args["game"]) addEntry(game, e);
    }
    if (jvm.empty() && game.empty() && vj.contains("minecraftArguments")) {
        std::wstring legacy = W(vj["minecraftArguments"].get<std::string>());
        for (auto& t : SplitArgs(legacy)) game.push_back(Subst(t, ctx));
    }

    std::wstring jarName = rootJar.filename().wstring();
    for (auto& arg : jvm) {
        if (arg.rfind(L"-DignoreList=", 0) == 0 && arg.find(jarName) == std::wstring::npos)
            arg += L"," + jarName;
    }

    std::wstring mainClass = W(vj.value("mainClass", "net.minecraft.client.main.Main"));
    if (mainClass.empty()) mainClass = L"net.minecraft.client.main.Main";

    std::vector<std::wstring> all;
    all.push_back(java);
    all.push_back(L"-Xms" + job.ramMin);
    all.push_back(L"-Xmx" + job.ramMax);
    all.push_back(L"-Dminecraft.client.jar=" + rootJar.wstring());
    for (auto& a : jvm) all.push_back(a);
    if (jvm.empty()) {
        all.push_back(L"-Djava.library.path=" + nativesDir.wstring());
        all.push_back(L"-cp");
        all.push_back(cpstr);
    }
    for (auto& a : SplitArgs(job.javaArgs)) all.push_back(a);
    if (!job.elyInjector.empty())
        all.push_back(L"-javaagent:" + job.elyInjector + L"=" + std::wstring(kElyInjectorMeta));
    all.push_back(mainClass);

    if (game.empty()) {
        const wchar_t* fallback[] = { L"--username", nullptr, L"--version", nullptr, L"--gameDir",
                                      nullptr, L"--assetsDir", nullptr, L"--assetIndex", nullptr,
                                      L"--uuid", nullptr, L"--accessToken", nullptr, L"--userType",
                                      nullptr, L"--versionType", L"release" };
        std::wstring vals[] = { job.authName.empty() ? job.username : job.authName, job.versionId,
                                job.gameDir.wstring(), (job.mc / L"assets").wstring(), assetId,
                                job.authUuid.empty() ? L"0" : job.authUuid,
                                job.authToken.empty() ? L"0" : job.authToken,
                                job.userType.empty() ? L"legacy" : job.userType };
        int vi = 0;
        for (const wchar_t* t : fallback) all.push_back(t ? std::wstring(t) : vals[vi++]);
    } else {
        for (auto& a : game) all.push_back(a);
    }
    for (auto& a : SplitArgs(job.gameArgs)) all.push_back(a);

    cmdline.clear();
    for (size_t i = 0; i < all.size(); ++i) {
        if (i) cmdline += L" ";
        cmdline += QuoteArg(all[i]);
    }
    if (cmdline.size() > 30000) {
        err = L"слишком длинная команда запуска";
        return false;
    }
    return true;
}

static void LaunchWorker(LaunchJob job) {
    auto set = [&](int pct, const std::wstring& msg) {
        job.st->pct = pct;
        {
            std::lock_guard<std::mutex> lk(job.st->mu);
            job.st->msg = msg;
        }
        Repaint(job.hwnd);
    };
    auto log = [&](const std::wstring& line) {
        {
            std::lock_guard<std::mutex> lk(job.st->mu);
            job.st->log.push_back(line);
            if (job.st->log.size() > 400) job.st->log.pop_front();
        }
        Repaint(job.hwnd);
    };
    auto fail = [&](const std::wstring& msg) {
        log(L"Ошибка: " + msg);
        set(-1, L"Ошибка: " + msg);
        job.st->active = false;
        job.st->running = false;
        PostUI(job.hwnd, [hwnd = job.hwnd, msg] {
            App* a = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (!a) return;
            a->status = L"Ошибка: " + msg;
            a->statusCol = C_RED;
        });
    };

    fs::path versions = job.mc / L"versions";
    std::wstring vid = job.versionId;
    fs::path vjson = versions / vid / (vid + L".json");
    std::error_code ec;

    if (!fs::exists(vjson, ec)) {
        set(0, L"Получение версии " + vid + L"…");
        HttpResult m = HttpRequest(L"GET", L"https://launchermeta.mojang.com/mc/game/"
                                            L"version_manifest_v2.json");
        std::string url;
        try {
            auto mj = nlohmann::json::parse(m.body);
            for (auto& v : mj["versions"])
                if (v.value("id", "") == U8(vid)) { url = v.value("url", ""); break; }
        } catch (...) {}
        if (url.empty()) return fail(L"версия " + vid + L" не найдена в манифесте Mojang");
        std::wstring e;
        if (!HttpDownload(W(url), vjson, {}, e))
            return fail(L"описание версии не скачалось: " + e);
    }

    std::set<std::wstring> seen;
    std::wstring rootId;
    nlohmann::json vj = ResolveVersionJson(versions, vid, rootId, seen);
    if (vj.is_null() || vj.empty()) return fail(L"не читается описание версии " + vid);
    if (rootId.empty()) rootId = vid;
    fs::path rootJar = versions / rootId / (rootId + L".jar");

    log(L"Версия: " + vid + L" (jar: " + rootId + L".jar)");
    set(0, L"Проверка файлов игры…");

    std::vector<DownloadTask> tasks;
    if (!fs::exists(rootJar, ec) && vj.contains("downloads") && vj["downloads"].is_object()) {
        const auto& dl = vj["downloads"];
        if (dl.contains("client") && dl["client"].is_object()) {
            std::string url = dl["client"].value("url", std::string());
            if (!url.empty()) tasks.push_back({ W(url), rootJar });
        }
    }

    if (vj.contains("libraries") && vj["libraries"].is_array()) {
        for (auto& lib : vj["libraries"]) {
            if (!lib.is_object()) continue;
            if (!RulesOk(lib.value("rules", nlohmann::json()))) continue;
            for (auto& f : LibFiles(lib))
                if (!f.url.empty()) tasks.push_back(f);
        }
    }

    if (vj.contains("assetIndex") && vj["assetIndex"].is_object()) {
        const auto& ai = vj["assetIndex"];
        std::string aid = ai.value("id", std::string());
        if (!aid.empty()) {
            fs::path idx = job.mc / L"assets" / L"indexes" / (W(aid) + L".json");
            if (!fs::exists(idx, ec)) {
                std::wstring url = W(ai.value("url", std::string()));
                std::wstring e;
                set(0, L"Загрузка индекса ресурсов…");
                if (url.empty() || !HttpDownload(url, idx, {}, e))
                    return fail(L"индекс ресурсов не скачался: " + e);
            }
            nlohmann::json index;
            if (ReadJsonFile(idx, index) && index.contains("objects") &&
                index["objects"].is_object()) {
                for (auto it = index["objects"].begin(); it != index["objects"].end(); ++it) {
                    std::string h = it.value().value("hash", std::string());
                    if (h.size() < 2) continue;
                    std::wstring hs = W(h);
                    tasks.push_back({ L"https://resources.download.minecraft.net/" + W(h.substr(0, 2)) +
                                          L"/" + hs,
                                      job.mc / L"assets" / L"objects" / W(h.substr(0, 2)) / hs });
                }
            }
        }
    }

    std::wstring err;
    if (!DownloadAll(job.hwnd, job.st, tasks, L"Загрузка файлов", err))
        return fail(err.empty() ? L"файлы игры не скачались" : err);
    set(0, L"Распаковка нативов…");
    ExtractNatives(vj, versions / vid / L"natives");

    if (job.st->stop.load()) return fail(L"отменено");

    set(0, L"Поиск Java…");
    std::wstring java = EnsureJava(vj, job.mc, job.javaCfg, job.hwnd, job.st);
    if (java.empty()) return fail(L"Java не найдена — укажите путь в настройках");
    int major = JavaMajorOf(java);
    log(L"Java: " + java + (major ? L" (версия " + std::to_wstring(major) + L")" : L""));
    PostUI(job.hwnd, [hwnd = job.hwnd, java] {
        App* a = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (a) a->lastJava = java;
    });

    if (job.st->stop.load()) return fail(L"отменено");

    if (job.ely) {
        set(0, L"Подготовка авторизации Ely.by…");
        std::wstring injErr;
        if (!EnsureElyInjectorFile([&](int p) { job.st->pct = p; }, injErr))
            return fail(injErr);
        job.elyInjector = ElyInjectorPath().wstring();
        log(L"authlib-injector: " + job.elyInjector);
    }

    std::wstring cmdline;
    if (!BuildLaunchCommand(job, vj, rootJar, java, versions / vid / L"natives", cmdline, err))
        return fail(err);

    set(-1, L"Запуск игры…");
    log(L"Запуск: " + job.prof.name);

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return fail(L"не удалось создать pipe");
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{ sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = nullptr;

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
    buf.push_back(0);
    fs::create_directories(job.gameDir, ec);
    BOOL ok = CreateProcessW(java.c_str(), buf.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr,
                             job.gameDir.c_str(), &si, &pi);
    DWORD lastErr = GetLastError();
    CloseHandle(wr);
    if (!ok) {
        CloseHandle(rd);
        return fail(L"игра не запустилась: " + ErrText(lastErr));
    }

    {
        std::lock_guard<std::mutex> lk(job.st->mu);
        job.st->proc = pi.hProcess;
    }
    job.st->running = true;
    job.st->pct = 100;
    {
        std::lock_guard<std::mutex> lk(job.st->mu);
        job.st->msg = L"Игра запущена";
    }
    log(L"Игра запущена (PID " + std::to_wstring(pi.dwProcessId) + L")");
    PostUI(job.hwnd, [hwnd = job.hwnd, name = job.prof.name] {
        App* a = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (!a) return;
        a->status = L"«" + name + L"» запущена";
        a->statusCol = C_GREEN;
    });
    CloseHandle(pi.hThread);

    std::string pending;
    char rbuf[8192];
    DWORD n = 0;
    while (ReadFile(rd, rbuf, sizeof(rbuf), &n, nullptr) && n > 0) {
        pending.append(rbuf, n);
        size_t p;
        while ((p = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, p);
            pending.erase(0, p + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) log(DecodeText(line));
        }
        if (pending.size() > 20000) pending.clear();
    }
    if (!pending.empty()) log(DecodeText(pending));
    CloseHandle(rd);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    {
        std::lock_guard<std::mutex> lk(job.st->mu);
        job.st->proc = nullptr;
    }
    CloseHandle(pi.hProcess);

    bool stopped = job.st->stop.load();
    job.st->running = false;
    job.st->active = false;
    job.st->pct = -1;
    std::wstring msg = stopped ? L"Запуск остановлен"
                               : (code == 0 ? L"Игра закрыта"
                                            : L"Игра завершилась (код " + std::to_wstring(code) + L")");
    log(msg);
    {
        std::lock_guard<std::mutex> lk(job.st->mu);
        job.st->msg = msg;
    }
    Repaint(job.hwnd);
    PostUI(job.hwnd, [hwnd = job.hwnd, msg, stopped, code] {
        App* a = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (!a) return;
        a->status = msg;
        a->statusCol = (stopped || code == 0) ? C_DIM : C_RED;
        RefreshMods(*a);
    });
}

static void StopGame(App& a);

static std::wstring ProfileVersionId(const Profile& p, bool& ok) {
    std::wstring mcV = p.mc.empty() ? p.version : p.mc;
    std::wstring v = p.version.empty() ? mcV : p.version;
    if (p.loader.empty() || p.loader == L"vanilla") return v;

    std::error_code ec;
    auto exists = [&](const std::wstring& id) {
        return !id.empty() && fs::exists(McDir() / L"versions" / id / (id + L".json"), ec);
    };

    if (v != mcV && exists(v)) return v;

    std::vector<std::wstring> cand;
    std::wstring lv = p.loaderVer.empty() ? L"" : p.loaderVer;
    if (p.loader == L"fabric") cand.push_back(L"fabric-loader-" + lv + L"-" + mcV);
    else if (p.loader == L"quilt") cand.push_back(L"quilt-loader-" + lv + L"-" + mcV);
    else if (p.loader == L"forge") cand.push_back(mcV + L"-forge-" + lv);
    else if (p.loader == L"neoforge") cand.push_back(L"neoforge-" + lv);
    for (auto& c : cand)
        if (exists(c)) return c;

    ok = false;
    return v;
}

static void StartLaunch(App& a) {
    if (!a.hasProfile) {
        a.status = L"Сначала выберите сборку";
        a.statusCol = C_YELLOW;
        return;
    }
    if (!a.launch) a.launch = std::make_shared<LaunchState>();
    if (a.launch->active.load()) {
        if (a.launch->running.load()) StopGame(a);
        else {
            a.status = L"Подготовка уже идёт";
            a.statusCol = C_YELLOW;
        }
        return;
    }

    bool ok = true;
    std::wstring vid = ProfileVersionId(a.profile, ok);
    if (!ok) {
        std::wstring core = LoaderName(a.profile.loader);
        a.status = L"Ядро " + core + L" не установлено — сначала установите его в сборке";
        a.statusCol = C_RED;
        return;
    }

    auto st = std::make_shared<LaunchState>();
    st->active = true;
    st->pct = 0;
    st->msg = L"Подготовка…";
    st->log.push_back(L"Сборка: " + a.profile.name);
    a.launch = st;

    LaunchJob job;
    job.prof = a.profile;
    job.mc = McDir();
    job.gameDir = a.profile.dir;
    job.versionId = vid;
    job.ramMin = a.ramMin;
    job.ramMax = a.ramMax;
    job.javaCfg = a.javaPathCfg;
    job.javaArgs = a.javaArgs;
    job.gameArgs = a.gameArgs;
    job.username = a.username;
    if (AuthOnline(a)) {
        job.authName = AuthName(a);
        job.authUuid = AuthUuid(a);
        job.authToken = AuthToken(a);
        job.userType = a.authKind == L"ely" ? L"mojang" : L"msa";
        job.ely = a.authKind == L"ely";
        st->log.push_back(std::wstring(L"Аккаунт: ") + job.authName + L" (" +
                          (a.authKind == L"ely" ? L"Ely.by" : L"Microsoft") + L", онлайн)");
    } else {
        job.authName = a.username;
        job.authUuid = L"0";
        job.authToken = L"0";
        job.userType = L"legacy";
        st->log.push_back(L"Аккаунт: офлайн (" + a.username + L")");
    }
    job.hwnd = a.hwnd;
    job.st = st;

    a.status = L"Подготовка «" + a.profile.name + L"»…";
    a.statusCol = C_ACCENT;
    std::thread(LaunchWorker, std::move(job)).detach();
    InvalidateRect(a.hwnd, nullptr, FALSE);
}

static void StopGame(App& a) {
    if (!a.launch) return;
    a.launch->stop = true;
    HANDLE h = nullptr;
    {
        std::lock_guard<std::mutex> lk(a.launch->mu);
        h = a.launch->proc;
        if (h) TerminateProcess(h, 1);
    }
    a.status = a.launch->running.load() ? L"Остановка игры…" : L"Отмена…";
    a.statusCol = C_YELLOW;
}

// ── иконки ────────────────────────────────────────────────────────────────────
static void IconWorker(App* a) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (;;) {
        std::pair<std::shared_ptr<Item>, std::wstring> job;
        {
            std::unique_lock<std::mutex> lk(a->iconMu);
            a->iconCv.wait(lk, [&] { return a->iconQuit || !a->iconQ.empty(); });
            if (a->iconQuit) break;
            job = std::move(a->iconQ.front());
            a->iconQ.pop_front();
        }
        if (job.second.empty()) continue;
        HttpResult r = HttpRequest(L"GET", job.second);
        if (r.status != 200) continue;
        ImageData img;
        if (!DecodeImage(r.body, img)) continue;

        PostUI(a->hwnd, [a, item = job.first, img = std::move(img)] {
            if (!a->rt) return;
            Com<ID2D1Bitmap> bmp;
            D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            if (SUCCEEDED(a->rt->CreateBitmap(D2D1::SizeU((UINT32)img.w, (UINT32)img.h),
                                              img.px.data(), (UINT32)img.w * 4, props, bmp.put()))) {
                item->icon = std::move(bmp);
                item->iconState = 2;
                InvalidateRect(a->hwnd, nullptr, FALSE);
            }
        });
    }
    CoUninitialize();
}

static void EnqueueIconUrl(App& a, std::shared_ptr<Item> it, const std::wstring& url) {
    {
        std::lock_guard<std::mutex> lk(a.iconMu);
        a.iconQ.emplace_back(std::move(it), url);
    }
    a.iconCv.notify_one();
}

// ── загрузка каталога ─────────────────────────────────────────────────────────
static void LoadCatalog(App& a) {
    a.loading = true;
    int gen = ++a.catGen;
    a.items.clear();
    a.scrollY = 0.f;
    a.scrollMax = 0.f;
    a.hoverRow = -1;
    a.status = L"Загрузка каталога…";
    a.statusCol = C_DIM;
    InvalidateRect(a.hwnd, nullptr, FALSE);

    HWND hwnd = a.hwnd;
    int tab = a.catTab;
    bool only = a.onlyProfile;
    std::wstring q = a.query;
    Profile prof = a.profile;
    bool has = a.hasProfile;

    std::thread([hwnd, tab, only, q, prof, has, gen] {
        std::vector<std::shared_ptr<Item>> out;
        std::wstring err;
        out = FetchModrinth(tab, (only && has) ? &prof : nullptr, q, err);

        PostUI(hwnd, [hwnd, out, err, q, gen] {
            App* a = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (!a) return;
            if (gen != a->catGen.load()) return;
            a->items = out;
            a->loading = false;
            if (!err.empty()) {
                a->status = err;
                a->statusCol = C_RED;
            } else {
                a->status = (q.empty() ? std::wstring(L"Популярное") : L"Поиск: " + q) +
                            L" · Modrinth · " + std::to_wstring(out.size());
                a->statusCol = out.empty() ? C_DIM : C_GREEN;
            }
            a->scrollY = 0.f;
            InvalidateRect(hwnd, nullptr, FALSE);
            for (auto& it : out) {
                if (it->iconUrl.empty()) continue;
                {
                    std::lock_guard<std::mutex> lk(a->iconMu);
                    a->iconQ.emplace_back(it, it->iconUrl);
                }
                a->iconCv.notify_one();
            }
        });
    }).detach();
}

// ── утилиты рисования ─────────────────────────────────────────────────────────
static void FillRound(App& a, float x, float y, float w, float h, float r, ID2D1Brush* b) {
    a.rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), r, r), b);
}

static void StrokeRound(App& a, float x, float y, float w, float h, float r, ID2D1Brush* b,
                        float wd = 1.f) {
    a.rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), r, r), b, wd);
}

static void TextRaw(App& a, const std::wstring& s, IDWriteTextFormat* f, ID2D1Brush* b,
                    float x, float y, float w, float h) {
    a.rt->DrawText(s.c_str(), (UINT32)s.size(), f, D2D1::RectF(x, y, x + w, y + h), b);
}

static void Text(App& a, const std::wstring& s, IDWriteTextFormat* f, ID2D1Brush* b,
                 float x, float y, float w, float h) {
    int l = g_lang.load(std::memory_order_relaxed);
    if (l == LANG_RU || !TrHasCyrillic(s)) {
        TextRaw(a, s, f, b, x, y, w, h);
        return;
    }
    if (const wchar_t* e = TrExact(s, l)) {
        TextRaw(a, e, f, b, x, y, w, h);
        return;
    }
    TextRaw(a, TrText(s), f, b, x, y, w, h);
}

static void TextWrap(App& a, const std::wstring& s, IDWriteTextFormat* f, ID2D1Brush* b,
                     float x, float y, float w, float h) {
    std::wstring t;
    int l = g_lang.load(std::memory_order_relaxed);
    if (l == LANG_RU || !TrHasCyrillic(s)) t = s;
    else if (const wchar_t* e = TrExact(s, l)) t = e;
    else t = TrText(s);

    Com<IDWriteTextLayout> lay;
    if (FAILED(a.dw->CreateTextLayout(t.c_str(), (UINT32)t.size(), f, w, h, lay.put()))) {
        TextRaw(a, t, f, b, x, y, w, h);
        return;
    }
    lay->SetWordWrapping((l == LANG_ZH || l == LANG_JA)
                             ? DWRITE_WORD_WRAPPING_CHARACTER
                             : DWRITE_WORD_WRAPPING_EMERGENCY_BREAK);
    a.rt->DrawTextLayout(D2D1::Point2F(x, y), lay.get(), b);
}

static float MeasureTextRaw(App& a, const std::wstring& s, IDWriteTextFormat* f) {
    if (s.empty()) return 0.f;
    Com<IDWriteTextLayout> lay;
    if (FAILED(a.dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), f, 4000.f, 100.f, lay.put())))
        return 0.f;
    DWRITE_TEXT_METRICS m{};
    lay->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

static float MeasureText(App& a, const std::wstring& s, IDWriteTextFormat* f) {
    if (g_lang.load(std::memory_order_relaxed) == LANG_RU) return MeasureTextRaw(a, s, f);
    return MeasureTextRaw(a, TrText(s), f);
}

// Высота текста после переноса по ширине w.
static float TextHeight(App& a, const std::wstring& s, IDWriteTextFormat* f, float w) {
    if (g_lang.load(std::memory_order_relaxed) != LANG_RU) {
        std::wstring t = TrText(s);
        if (t.empty()) return 0.f;
        Com<IDWriteTextLayout> lay;
        if (FAILED(a.dw->CreateTextLayout(t.c_str(), (UINT32)t.size(), f, w, 100000.f, lay.put())))
            return 14.f;
        DWRITE_TEXT_METRICS m{};
        lay->GetMetrics(&m);
        return m.height;
    }
    if (s.empty()) return 0.f;
    Com<IDWriteTextLayout> lay;
    if (FAILED(a.dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), f, w, 100000.f, lay.put())))
        return 14.f;
    DWRITE_TEXT_METRICS m{};
    lay->GetMetrics(&m);
    return m.height;
}

// Обрезает строку по ширине с многоточием (с учётом перевода).
static std::wstring Ellipsize(App& a, std::wstring s, IDWriteTextFormat* f, float w) {
    if (g_lang.load(std::memory_order_relaxed) != LANG_RU) s = TrText(s);
    if (w <= 0.f || MeasureTextRaw(a, s, f) <= w) return s;
    while (!s.empty() && MeasureTextRaw(a, s + L"…", f) > w) s.pop_back();
    return s + L"…";
}

static float Ease(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return 1.f - std::pow(1.f - t, 3.f);
}

static const wchar_t* PlaceholderFor(int tab) {
    switch (tab) {
    case 0: return L"Название мода…";
    case 1: return L"Название ресурспака…";
    case 2: return L"Название шейдера…";
    default: return L"Название датапака…";
    }
}

// ── текст ─────────────────────────────────────────────────────────────────────
static void MakeFormat(App& a, Com<IDWriteTextFormat>& f, float size, DWRITE_FONT_WEIGHT weight,
                       DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
                       DWRITE_WORD_WRAPPING wrap = DWRITE_WORD_WRAPPING_NO_WRAP,
                       DWRITE_PARAGRAPH_ALIGNMENT par = DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
                       const wchar_t* family = nullptr) {
    int l = g_lang.load(std::memory_order_relaxed);
    const wchar_t* fam = nullptr;
    IDWriteFontCollection* coll = nullptr;
    const wchar_t* candidates[5] = {};
    int nc = 0;
    if (family) {
        fam = family;
        candidates[nc++] = family;
        candidates[nc++] = L"Segoe UI";
    } else if (l == LANG_ZH) {
        candidates[nc++] = L"Microsoft YaHei UI";
        candidates[nc++] = L"Microsoft YaHei";
        candidates[nc++] = L"SimSun";
        candidates[nc++] = L"Segoe UI";
    } else if (l == LANG_JA) {
        candidates[nc++] = L"Yu Gothic UI";
        candidates[nc++] = L"Yu Gothic";
        candidates[nc++] = L"Meiryo UI";
        candidates[nc++] = L"MS Gothic";
        candidates[nc++] = L"Segoe UI";
    } else {
        fam = a.fontFamily.empty() ? L"Segoe UI" : a.fontFamily.c_str();
        coll = a.fontColl.get();
        candidates[nc++] = fam;
        candidates[nc++] = L"Segoe UI";
    }
    HRESULT hr = E_FAIL;
    for (int i = 0; i < nc; ++i) {
        hr = a.dw->CreateTextFormat(candidates[i], (i == 0 && !family) ? coll : nullptr,
                                    weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                    size, LangLocale(l), f.put());
        if (SUCCEEDED(hr)) break;
    }
    if (FAILED(hr)) return;
    f->SetTextAlignment(align);
    f->SetParagraphAlignment(par);
    f->SetWordWrapping(wrap);
}

static const wchar_t* EffectiveFontName(App& a) {
    int l = g_lang.load(std::memory_order_relaxed);
    if (l == LANG_ZH) return L"Microsoft YaHei UI";
    if (l == LANG_JA) return L"Yu Gothic UI";
    return a.fontFamily.empty() ? L"Segoe UI" : a.fontFamily.c_str();
}

static void CreateTextFormats(App& a) {
    MakeFormat(a, a.fTitle,    30.f, DWRITE_FONT_WEIGHT_BOLD);
    MakeFormat(a, a.fH2,       16.f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    MakeFormat(a, a.fBody,     12.5f, DWRITE_FONT_WEIGHT_NORMAL);
    MakeFormat(a, a.fBodyWrap, 12.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
               DWRITE_WORD_WRAPPING_WRAP);
    MakeFormat(a, a.fSmall,    11.f, DWRITE_FONT_WEIGHT_NORMAL);
    MakeFormat(a, a.fTiny,     9.5f, DWRITE_FONT_WEIGHT_NORMAL);
    MakeFormat(a, a.fLogo,     17.f, DWRITE_FONT_WEIGHT_BOLD);
    MakeFormat(a, a.fLogoSub,   9.f, DWRITE_FONT_WEIGHT_NORMAL);
    MakeFormat(a, a.fBtn,      12.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER,
               DWRITE_WORD_WRAPPING_NO_WRAP, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    MakeFormat(a, a.fAvatar,   14.f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER,
               DWRITE_WORD_WRAPPING_NO_WRAP, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    MakeFormat(a, a.fLogoMark, 19.f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER,
               DWRITE_WORD_WRAPPING_NO_WRAP, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    MakeFormat(a, a.fDesc,     11.f, DWRITE_FONT_WEIGHT_NORMAL);
    MakeFormat(a, a.fTinyC,    9.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER,
               DWRITE_WORD_WRAPPING_NO_WRAP, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    MakeFormat(a, a.fRow,      13.f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    MakeFormat(a, a.fMono,     9.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
               DWRITE_WORD_WRAPPING_NO_WRAP, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, L"Consolas");
    MakeFormat(a, a.fCode,    30.f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER,
               DWRITE_WORD_WRAPPING_NO_WRAP, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, L"Consolas");
    MakeFormat(a, a.fModal,   22.f, DWRITE_FONT_WEIGHT_BOLD);
    MakeFormat(a, a.fModalSub, 11.f, DWRITE_FONT_WEIGHT_NORMAL);
    MakeFormat(a, a.fField,   11.f, DWRITE_FONT_WEIGHT_NORMAL);

    if (a.fDesc && a.dw) {
        if (SUCCEEDED(a.dw->CreateEllipsisTrimmingSign(a.fDesc.get(), a.ellipsis.put()))) {
            DWRITE_TRIMMING tr{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
            a.fDesc->SetTrimming(&tr, a.ellipsis.get());
            if (a.fTitle) a.fTitle->SetTrimming(&tr, a.ellipsis.get());
            if (a.fH2) a.fH2->SetTrimming(&tr, a.ellipsis.get());
            if (a.fRow) a.fRow->SetTrimming(&tr, a.ellipsis.get());
            if (a.fMono) a.fMono->SetTrimming(&tr, a.ellipsis.get());
            if (a.fField) a.fField->SetTrimming(&tr, a.ellipsis.get());
        }
    }
}

// ── шрифт интерфейса: свой из resources\fonts, иначе системный ────────────────
static void LoadUiFonts(App& a) {
    std::vector<fs::path> files;
    std::error_code ec;
    fs::path fd = ResDir(L"fonts");
    if (fs::is_directory(fd, ec)) {
        for (auto& e : fs::directory_iterator(fd, ec)) {
            if (!e.is_regular_file(ec)) continue;
            std::wstring ext = e.path().extension().wstring();
            for (auto& ch : ext) ch = (wchar_t)towlower(ch);
            if (ext != L".ttf" && ext != L".otf") continue;
            std::wstring nm = e.path().filename().wstring();
            if (nm.find(L"Medium") != std::wstring::npos ||
                nm.find(L"SemiBold") != std::wstring::npos ||
                nm.find(L"Light") != std::wstring::npos ||
                nm.find(L"Italic") != std::wstring::npos) continue;   // оставляем Regular и Bold
            files.push_back(e.path());
        }
        std::sort(files.begin(), files.end());
    }
    for (auto& f : files) AddFontResourceExW(f.c_str(), FR_PRIVATE, nullptr);

    a.fontFamily.clear();
    a.fontColl.reset();
    if (!files.empty()) {
        Com<IDWriteFactory3> f3;
        if (SUCCEEDED(a.dw->QueryInterface(__uuidof(IDWriteFactory3), (void**)f3.put()))) {
            Com<IDWriteFontSetBuilder> builder;
            Com<IDWriteFontSetBuilder1> b1;
            if (SUCCEEDED(f3->CreateFontSetBuilder(builder.put())) &&
                SUCCEEDED(builder->QueryInterface(__uuidof(IDWriteFontSetBuilder1),
                                                  (void**)b1.put()))) {
                for (auto& f : files) {
                    Com<IDWriteFontFile> ff;
                    if (SUCCEEDED(a.dw->CreateFontFileReference(f.c_str(), nullptr, ff.put())))
                        b1->AddFontFile(ff.get());
                }
                Com<IDWriteFontSet> set;
                if (SUCCEEDED(b1->CreateFontSet(set.put()))) {
                    Com<IDWriteFontCollection1> coll;
                    if (SUCCEEDED(f3->CreateFontCollectionFromFontSet(set.get(), coll.put()))) {
                        BOOL has = FALSE;
                        UINT32 idx = 0;
                        if (SUCCEEDED(coll->FindFamilyName(L"Inter", &idx, &has)) && has) {
                            a.fontFamily = L"Inter";
                            a.fontColl = std::move(coll);
                        }
                    }
                }
            }
        }
    }
    if (a.fontFamily.empty()) {
        Com<IDWriteFontCollection> sysc;
        if (SUCCEEDED(a.dw->GetSystemFontCollection(sysc.put(), FALSE))) {
            const wchar_t* stack[] = { L"Inter", L"Bahnschrift", L"Century Gothic", L"Corbel",
                                       L"Candara", L"Calibri", L"Trebuchet MS", L"Verdana",
                                       L"Segoe UI" };
            for (auto fam : stack) {
                BOOL has = FALSE;
                UINT32 idx = 0;
                if (SUCCEEDED(sysc->FindFamilyName(fam, &idx, &has)) && has) {
                    a.fontFamily = fam;
                    break;
                }
            }
        }
        if (a.fontFamily.empty()) a.fontFamily = L"Segoe UI";
    }
    CreateTextFormats(a);
}

// ── ресурсы устройства ────────────────────────────────────────────────────────
static void DiscardDeviceResources(App& a) {
    a.brSide.reset(); a.brCard.reset(); a.brCardHi.reset(); a.brBorder.reset();
    a.brInput.reset(); a.brText.reset(); a.brDim.reset(); a.brAccent.reset();
    a.brAccentHi.reset(); a.brAccentSoft.reset(); a.brGreen.reset(); a.brPink.reset();
    a.brRed.reset(); a.brWhite.reset(); a.brGrad.reset(); a.brYellow.reset();
    a.brOverlay.reset();
    for (auto& b : a.brSw) b.reset();
    a.layer.reset();
    a.logoBmp.reset();
    a.logoGeo.reset();
    a.logoLayer.reset();
    for (auto& s : a.detail.shots) s.bmp.reset();
    a.rt.reset();
}

// ── скин: битмапы предпросмотра и головы (зависят от устройства) ──────────────
static void RebuildSkinBitmaps(App& a) {
    a.skinBmp.reset();
    a.headBmp.reset();
    if (!a.rt) return;
    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (a.skinPrevW > 0 && a.skinPrevH > 0 && !a.skinPrevPx.empty())
        a.rt->CreateBitmap(D2D1::SizeU((UINT32)a.skinPrevW, (UINT32)a.skinPrevH),
                           a.skinPrevPx.data(), (UINT32)a.skinPrevW * 4, props, a.skinBmp.put());
    if (a.skinHeadW > 0 && a.skinHeadH > 0 && !a.skinHeadPx.empty())
        a.rt->CreateBitmap(D2D1::SizeU((UINT32)a.skinHeadW, (UINT32)a.skinHeadH),
                           a.skinHeadPx.data(), (UINT32)a.skinHeadW * 4, props, a.headBmp.put());
}

// Пересобирает превью и голову из a.skinPng.
static bool ComputeSkinBitmaps(App& a) {
    a.skinPrevPx.clear();
    a.skinHeadPx.clear();
    a.skinPrevW = a.skinPrevH = a.skinHeadW = a.skinHeadH = 0;
    SkinPx sp;
    bool ok = false;
    if (!a.skinPng.empty() && DecodeSkinPng(a.skinPng, sp) && SkinValid(sp)) {
        ok = RenderSkin(sp, a.skinVariant == 1, 8, a.skinPrevPx, a.skinPrevW, a.skinPrevH);
        RenderSkinHead(sp, 38, a.skinHeadPx, a.skinHeadW, a.skinHeadH);
    }
    RebuildSkinBitmaps(a);
    return ok;
}

// Логотип в сайдбаре: midnight.png (тот же рисунок, что у иконки exe).
static void LoadLogo(App& a) {
    a.logoBmp.reset();
    if (!a.rt) return;
    std::error_code ec;
    fs::path p = ResPath(L"midnight.png");
    if (!fs::exists(p, ec)) return;
    std::ifstream f(p, std::ios::binary);
    if (!f) return;
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    ImageData img;
    if (!DecodeImage(bytes, img) || img.px.empty()) return;
    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    a.rt->CreateBitmap(D2D1::SizeU((UINT32)img.w, (UINT32)img.h), img.px.data(),
                       (UINT32)img.w * 4, props, a.logoBmp.put());
}

static void CreateDeviceResources(App& a) {
    if (a.rt) return;
    RECT rc{};
    GetClientRect(a.hwnd, &rc);
    float dpi = (float)GetDpiForWindow(a.hwnd);
    if (dpi <= 0.f) dpi = 96.f;
    a.dip = dpi / 96.f;

    UINT32 w = (UINT32)std::max(1.f, (rc.right - rc.left) / a.dip);
    UINT32 h = (UINT32)std::max(1.f, (rc.bottom - rc.top) / a.dip);

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN), dpi, dpi);

    if (FAILED(a.d2d->CreateHwndRenderTarget(
            props, D2D1::HwndRenderTargetProperties(a.hwnd, D2D1::SizeU(w, h)), a.rt.put())))
        return;

    a.rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    a.rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

    auto mk = [&](Com<ID2D1SolidColorBrush>& b, const D2D1_COLOR_F& c) {
        a.rt->CreateSolidColorBrush(c, b.put());
    };
    mk(a.brSide, C_SIDE);
    mk(a.brCard, C_CARD);
    mk(a.brCardHi, C_CARD_HI);
    mk(a.brBorder, C_BORDER);
    mk(a.brInput, C_INPUT);
    mk(a.brText, C_TEXT);
    mk(a.brDim, C_DIM);
    mk(a.brAccent, C_ACCENT);
    mk(a.brAccentHi, C_ACCENT_HI);
    mk(a.brAccentSoft, C_ACCENT_SOFT);
    mk(a.brGreen, C_GREEN);
    mk(a.brYellow, C_YELLOW);
    mk(a.brPink, C_PINK);
    mk(a.brRed, C_RED);
    mk(a.brWhite, C_WHITE);
    mk(a.brOverlay, D2D1::ColorF(0x000000, 0.62f));
    for (int i = 0; i < 6; ++i) {
        D2D1_COLOR_F c;
        if (!HexOk(kAccentPresets[i], c)) c = D2D1::ColorF(0x888888);
        a.rt->CreateSolidColorBrush(c, a.brSw[i].put());
    }

    D2D1_GRADIENT_STOP stops[2] = {
        { 0.f, C_ACCENT },
        { 1.f, C_ACCENT2 },
    };
    Com<ID2D1GradientStopCollection> coll;
    a.rt->CreateGradientStopCollection(stops, 2, coll.put());
    if (coll) {
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gp = D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(0.f, 0.f), D2D1::Point2F(44.f, 44.f));
        a.rt->CreateLinearGradientBrush(gp, coll.get(), a.brGrad.put());
    }

    RebuildSkinBitmaps(a);
    LoadLogo(a);
}

// Применяет тему и цвета: пересоздаёт палитру, кисти и перерисовывает окно.
static void ApplyThemeNow(App& a) {
    ApplyPalette(a.theme, a.accent, a.accent2);
    BOOL dark = (a.theme != L"light");
    DwmSetWindowAttribute(a.hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    DiscardDeviceResources(a);
    CreateDeviceResources(a);
    if (a.settingsScrollY > a.settingsScrollMax) a.settingsScrollY = a.settingsScrollMax;
    a.status = L"Оформление обновлено";
    a.statusCol = C_GREEN;
    InvalidateRect(a.hwnd, nullptr, FALSE);
}

// ── иконки навигации ──────────────────────────────────────────────────────────
static void DrawNavIcon(App& a, int i, float x, float y, float s, ID2D1Brush* b) {
    switch (i) {
    case 0: {
        a.rt->DrawLine(D2D1::Point2F(x + s * 0.08f, y + s * 0.46f),
                       D2D1::Point2F(x + s * 0.50f, y + s * 0.08f), b, 1.7f);
        a.rt->DrawLine(D2D1::Point2F(x + s * 0.50f, y + s * 0.08f),
                       D2D1::Point2F(x + s * 0.92f, y + s * 0.46f), b, 1.7f);
        FillRound(a, x + s * 0.22f, y + s * 0.42f, s * 0.56f, s * 0.52f, 2.f, b);
        break;
    }
    case 1: {
        float q = s * 0.42f, g = s * 0.10f;
        FillRound(a, x + s * 0.06f, y + s * 0.06f, q, q, 2.f, b);
        FillRound(a, x + s * 0.06f + q + g, y + s * 0.06f, q, q, 2.f, b);
        FillRound(a, x + s * 0.06f, y + s * 0.06f + q + g, q, q, 2.f, b);
        FillRound(a, x + s * 0.06f + q + g, y + s * 0.06f + q + g, q, q, 2.f, b);
        break;
    }
    case 2: {
        for (int k = 0; k < 3; ++k) {
            float yy = y + s * (0.18f + 0.28f * k);
            a.rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + s * 0.14f, yy), 1.7f, 1.7f), b);
            a.rt->DrawLine(D2D1::Point2F(x + s * 0.30f, yy), D2D1::Point2F(x + s * 0.92f, yy), b, 1.7f);
        }
        break;
    }
    default: {
        for (int k = 0; k < 3; ++k) {
            float yy = y + s * (0.18f + 0.30f * k);
            a.rt->DrawLine(D2D1::Point2F(x + s * 0.06f, yy), D2D1::Point2F(x + s * 0.94f, yy), b, 1.5f);
            float kx = x + s * (0.30f + 0.20f * (k % 2));
            a.rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx, yy), 2.6f, 2.6f), b);
        }
        break;
    }
    }
}

// ── строки каталога ───────────────────────────────────────────────────────────
static ID2D1Brush* ProgressBrush(App& a, int pr, bool installed) {
    if (installed) return a.brCardHi.get();
    if (pr >= 0) return a.brAccentHi.get();
    return a.brAccent.get();
}

// ── страницы ──────────────────────────────────────────────────────────────────
static void DrawHome(App& a, float x, float y, float w) {
    float H = a.rt->GetSize().height;
    Text(a, L"Главная", a.fTitle.get(), a.brText.get(), x, y, w, 42);
    Text(a, L"Сборки и запуск игры", a.fSmall.get(), a.brDim.get(), x, y + 44, w, 18);

    float topY = y + 92;
    float listW = 286.f;
    float gap = 16.f;
    float rx = x + listW + gap;
    float rw = w - listW - gap;
    float bottom = H - 30.f;

    // ── список сборок ──
    float listH = std::max(120.f, bottom - topY - 8.f);
    FillRound(a, x, topY, listW, listH, 14, a.brCard.get());
    Text(a, L"СБОРКИ", a.fTiny.get(), a.brDim.get(), x + 16, topY + 14, 90, 14);
    bool nbHot = (a.hoverWidget == 32);
    FillRound(a, x + listW - 148, topY + 7, 132, 27, 8,
              nbHot ? a.brAccent.get() : a.brCardHi.get());
    Text(a, L"+  Новая сборка", a.fTinyC.get(), nbHot ? a.brWhite.get() : a.brText.get(),
         x + listW - 148, topY + 7, 132, 27);

    if (a.profiles.empty()) {
        Text(a, L"Профилей нет", a.fH2.get(), a.brText.get(), x + 16, topY + 44, listW - 32, 24);
        Text(a, L"Нажмите «+ Новая сборка» — версия и ядро скачаются сами.",
             a.fDesc.get(), a.brDim.get(), x + 16, topY + 76, listW - 32, 40);
    }

    float ry = topY + 38;
    int maxRows = (int)((listH - 50) / 70);
    for (int i = 0; i < (int)a.profiles.size() && i < maxRows; ++i) {
        const Profile& p = a.profiles[(size_t)i];
        bool sel = (i == a.profileIdx);
        bool hov = (a.hoverWidget == 40 + i);
        if (sel) {
            FillRound(a, x + 10, ry, listW - 20, 62, 10, a.brAccentSoft.get());
            StrokeRound(a, x + 10, ry, listW - 20, 62, 10, a.brAccent.get());
        } else {
            FillRound(a, x + 10, ry, listW - 20, 62, 10, hov ? a.brCardHi.get() : a.brCard.get());
            StrokeRound(a, x + 10, ry, listW - 20, 62, 10, a.brBorder.get());
        }
        TextRaw(a, p.name, a.fRow.get(), sel ? a.brText.get() : a.brText.get(),
                x + 22, ry + 9, listW - 106, 20);
        std::wstring sub = LoaderName(p.loader);
        std::wstring mcv = p.mc.empty() ? p.version : p.mc;
        if (!mcv.empty()) sub += L" " + mcv;
        Text(a, sub, a.fTiny.get(), a.brDim.get(), x + 22, ry + 34, listW - 106, 14);

        bool delHot = (a.hoverWidget == 70 + i);
        if (delHot) FillRound(a, x + listW - 46, ry + 6, 28, 28, 8, a.brCardHi.get());
        Text(a, L"✕", a.fTinyC.get(), delHot ? a.brRed.get() : a.brDim.get(),
             x + listW - 46, ry + 6, 28, 28);

        bool running = (sel && a.launch && a.launch->running.load());
        if (running) a.rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + listW - 60, ry + 16), 4.f, 4.f),
                                       a.brGreen.get());
        ry += 70;
    }
    if ((int)a.profiles.size() > maxRows)
        Text(a, L"…и ещё " + std::to_wstring(a.profiles.size() - maxRows),
             a.fTiny.get(), a.brDim.get(), x + 16, bottom - 26, listW - 32, 14);

    // ── карточка запуска ──
    std::wstring msg = L"Готов к запуску";
    int pct = -1;
    bool running = false, active = false;
    if (a.launch) {
        std::lock_guard<std::mutex> lk(a.launch->mu);
        msg = a.launch->msg;
        pct = a.launch->pct.load();
        running = a.launch->running.load();
        active = a.launch->active.load();
    }

    FillRound(a, rx, topY, rw, 196, 16, a.brCard.get());
    FillRound(a, rx + 20, topY + 18, 3, 18, 1.5f, a.brAccent.get());
    Text(a, L"ТЕКУЩАЯ СБОРКА", a.fTiny.get(), a.brDim.get(), rx + 32, topY + 16, rw - 60, 14);
    if (a.hasProfile)
        TextRaw(a, a.profile.name, a.fTitle.get(), a.brText.get(),
                rx + 20, topY + 34, rw - 40, 42);
    else
        Text(a, L"Сборки нет", a.fTitle.get(), a.brText.get(),
             rx + 20, topY + 34, rw - 40, 42);

    std::wstring meta;
    if (a.hasProfile) {
        meta = LoaderName(a.profile.loader);
        std::wstring mcv = a.profile.mc.empty() ? a.profile.version : a.profile.mc;
        if (!mcv.empty()) meta += L" " + mcv;
        if (!a.profile.loaderVer.empty() && a.profile.loader != L"vanilla")
            meta += L" · " + a.profile.loaderVer;
        meta += L" · " + std::to_wstring(a.modsList.size()) + L" " +
                Plural(a.modsList.size(), L"мод", L"мода", L"модов");
    } else {
        meta = L"Профили не найдены в %APPDATA%\\.minecraft\\nexus";
    }
    Text(a, meta, a.fBody.get(), a.brDim.get(), rx + 20, topY + 82, rw - 40, 20);

    std::wstring javaLine;
    if (!a.lastJava.empty()) {
        fs::path jp(a.lastJava);
        fs::path comp = jp.parent_path().parent_path().parent_path();
        std::wstring label = jp.parent_path().parent_path().filename().wstring();
        if (comp.filename().wstring().rfind(L"java-runtime", 0) == 0)
            label = comp.filename().wstring();
        int jm = JavaMajorOf(a.lastJava);
        javaLine = L"Java " + (jm ? std::to_wstring(jm) : std::wstring(L"?")) +
                   (label.empty() ? L"" : L" · " + label);
    } else {
        javaLine = L"Java определится при первом запуске";
    }
    Text(a, javaLine, a.fTiny.get(), a.brDim.get(), rx + 20, topY + 104, rw - 200, 16);

    if (a.hasProfile) {
        bool ovHot = (a.hoverWidget == 33);
        FillRound(a, rx + rw - 158, topY + 88, 138, 30, 8,
                  ovHot ? a.brCardHi.get() : a.brInput.get());
        StrokeRound(a, rx + rw - 158, topY + 88, 138, 30, 8,
                    ovHot ? a.brAccent.get() : a.brBorder.get());
        Text(a, L"Обзор сборки", a.fTinyC.get(), ovHot ? a.brText.get() : a.brDim.get(),
             rx + rw - 158, topY + 88, 138, 30);
    }

    ID2D1Brush* msgBrush = a.brDim.get();
    if (msg.rfind(L"Ошибка", 0) == 0) msgBrush = a.brRed.get();
    else if (running) msgBrush = a.brGreen.get();
    else if (active) msgBrush = a.brAccentHi.get();
    Text(a, msg, a.fSmall.get(), msgBrush, rx + 20, topY + 126, rw - 230, 18);

    float barX = rx + 20, barY = topY + 156, barW = std::max(80.f, rw - 56 - 210);
    FillRound(a, barX, barY, barW, 6, 3, a.brInput.get());
    if (pct >= 0) {
        float filled = barW * std::clamp(pct / 100.f, 0.f, 1.f);
        if (filled > 0.5f) FillRound(a, barX, barY, filled, 6, 3, running ? a.brGreen.get() : a.brAccent.get());
    }

    bool hot = (a.hoverWidget == 30);
    ID2D1Brush* btn = !a.hasProfile ? a.brCardHi.get()
                      : running       ? (hot ? a.brRed.get() : a.brCardHi.get())
                      : active        ? a.brCardHi.get()
                      : (hot ? a.brAccentHi.get() : a.brAccent.get());
    FillRound(a, rx + rw - 200, topY + 126, 180, 48, 12, btn);
    std::wstring btnText = running ? L"■  ОСТАНОВИТЬ" : (active ? L"ОТМЕНА" : L"▶  ИГРАТЬ");
    ID2D1Brush* btnFg = (!a.hasProfile || active && !running) ? a.brDim.get() : a.brWhite.get();
    Text(a, btnText, a.fBtn.get(), btnFg, rx + rw - 200, topY + 126, 180, 48);

    // ── лог ──
    float logY = topY + 210;
    float logH = std::max(80.f, bottom - logY - 8.f);
    FillRound(a, rx, logY, rw, logH, 14, a.brCard.get());
    Text(a, L"Лог запуска", a.fH2.get(), a.brText.get(), rx + 16, logY + 12, 200, 22);

    bool clrHot = (a.hoverWidget == 31);
    Text(a, L"Очистить", a.fTiny.get(), clrHot ? a.brText.get() : a.brDim.get(),
         rx + rw - 92, logY + 16, 76, 16);
    a.rt->DrawLine(D2D1::Point2F(rx + 16, logY + 40), D2D1::Point2F(rx + rw - 16, logY + 40),
                   a.brBorder.get(), 1.f);

    std::deque<std::wstring> log;
    if (a.launch) {
        std::lock_guard<std::mutex> lk(a.launch->mu);
        log = a.launch->log;
    }
    int maxLines = std::max(1, (int)((logH - 52) / 14));
    int skip = std::max(0, (int)log.size() - maxLines);
    float ly = logY + 46;
    int idx = 0;
    for (auto& line : log) {
        if (idx++ < skip) continue;
        ID2D1Brush* col = a.brDim.get();
        if (line.rfind(L"Ошибка", 0) == 0 || line.find(L"ERROR") != std::wstring::npos ||
            line.find(L"FATAL") != std::wstring::npos)
            col = a.brRed.get();
        else if (line.rfind(L"Сборка:", 0) == 0 || line.rfind(L"Игра запущена", 0) == 0 ||
                 line.rfind(L"Версия:", 0) == 0)
            col = a.brText.get();
        Text(a, line, a.fMono.get(), col, rx + 16, ly, rw - 32, 14);
        ly += 14;
        if (ly > logY + logH - 10) break;
    }
    if (log.empty()) {
        TextWrap(a, L"Здесь появятся сообщения о подготовке файлов, Java и запуске игры.",
                 a.fBody.get(), a.brDim.get(), rx + 16, logY + 48, rw - 32, 44);
    }
}

static void DrawMods(App& a, float x, float y, float w) {
    float H = a.rt->GetSize().height;
    Text(a, L"Моды", a.fTitle.get(), a.brText.get(), x, y, w, 42);
    std::wstring sub = a.hasProfile
        ? a.profile.name + L" · " + std::to_wstring(a.modsList.size()) + L" " +
              Plural(a.modsList.size(), L"файл", L"файла", L"файлов") + L" в mods"
        : L"Сборка не найдена";
    Text(a, sub, a.fSmall.get(), a.brDim.get(), x, y + 44, w, 18);

    if (!a.hasProfile || a.modsList.empty()) {
        float cy = y + 86;
        FillRound(a, x, cy, w, 120, 14, a.brCard.get());
        Text(a, a.hasProfile ? L"Модов пока нет" : L"Сборка не найдена",
             a.fH2.get(), a.brText.get(), x + 20, cy + 20, w - 40, 24);
        Text(a, a.hasProfile
                    ? L"Открой «Каталог», выбери мод и нажми «Установить»."
                    : L"Создай сборку на главной странице.",
             a.fBodyWrap.get(), a.brDim.get(), x + 20, cy + 52, w - 40, 40);
        return;
    }

    // ── заголовок раздела (не прокручивается) ──
    Text(a, L"Дерево зависимостей", a.fH2.get(), a.brText.get(), x, y + 82, w - 116, 22);
    bool hot = (a.hoverWidget == 60) && !a.modsTree.busy;
    Text(a, a.modsTree.busy ? L"Обновляю…" : L"Обновить", a.fTiny.get(),
         hot ? a.brText.get() : a.brDim.get(), x + w - 96, y + 84, 96, 20);

    std::wstring hint = a.modsTree.busy
        ? L"Определяю моды по SHA-1 и загружаю связи…"
        : a.modsTree.note;
    bool hintWarn = hint.rfind(L"Modrinth", 0) == 0 || hint.rfind(L"Не удалось", 0) == 0;
    if (hint.empty()) hint = L"По данным Modrinth · показаны обязательные зависимости";
    Text(a, hint, a.fTiny.get(), hintWarn ? a.brYellow.get() : a.brDim.get(),
         x, y + 106, w - 8, 16);

    float top = y + 128.f;
    float bottom = H - 30.f;
    if (bottom - top < 40.f) return;

    float treeH = a.modsTree.rows.empty() ? 24.f : (float)a.modsTree.rows.size() * 26.f;
    float contentH = treeH + 16.f + 26.f + (float)a.modsList.size() * 54.f + 4.f;
    a.modsScrollMax = std::max(0.f, contentH - (bottom - top));
    if (a.modsScrollY < 0.f) a.modsScrollY = 0.f;
    if (a.modsScrollY > a.modsScrollMax) a.modsScrollY = a.modsScrollMax;

    a.rt->PushAxisAlignedClip(D2D1::RectF(x, top, x + w, bottom),
                              D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    float cy = top - a.modsScrollY;

    // ── дерево ──
    if (a.modsTree.rows.empty()) {
        Text(a, a.modsTree.busy ? L"Загрузка…" : L"Нет данных о зависимостях.",
             a.fSmall.get(), a.brDim.get(), x, cy + 2, w - 16, 20);
        cy += 24.f;
    } else {
        for (auto& r : a.modsTree.rows) {
            if (cy > bottom) break;
            if (cy + 26.f >= top) {
                if (r.root) {
                    Text(a, r.name, a.fRow.get(), r.unknown ? a.brDim.get() : a.brText.get(),
                         x, cy + 2, w - 210.f, 20);
                    if (!r.note.empty())
                        Text(a, r.note, a.fTiny.get(),
                             r.unknown ? a.brDim.get() : a.brYellow.get(),
                             x + w - 200.f, cy + 5.f, 200.f, 16);
                } else {
                    float pw = MeasureText(a, r.prefix, a.fMono.get());
                    Text(a, r.prefix, a.fMono.get(), a.brDim.get(), x, cy + 3, pw + 6.f, 18);
                    Text(a, r.name, a.fBody.get(),
                         r.missing ? a.brYellow.get() : a.brText.get(),
                         x + pw, cy + 2, w - pw - 190.f, 20);
                    if (!r.note.empty())
                        Text(a, r.note, a.fTiny.get(),
                             r.missing ? a.brYellow.get() : a.brDim.get(),
                             x + w - 180.f, cy + 5.f, 180.f, 16);
                }
            }
            cy += 26.f;
        }
    }
    cy += 16.f;

    // ── файлы ──
    Text(a, L"Файлы (" + std::to_wstring(a.modsList.size()) + L")",
         a.fH2.get(), a.brText.get(), x, cy, w, 22);
    cy += 26.f;
    for (auto& m : a.modsList) {
        if (cy + 46.f >= top && cy <= bottom) {
            FillRound(a, x, cy, w, 46, 10, a.brCard.get());
            std::wstring nm = m.name;
            if (nm.size() > 4 && nm.substr(nm.size() - 4) == L".jar") nm.resize(nm.size() - 4);
            Text(a, nm, a.fBody.get(), a.brText.get(), x + 14, cy + 4, w - 190, 20);
            Text(a, FmtSize(m.size), a.fTiny.get(), a.brDim.get(), x + 14, cy + 25, 160, 14);
        }
        cy += 54.f;
    }

    a.rt->PopAxisAlignedClip();

    if (a.modsScrollMax > 0.5f) {
        float trackH = bottom - top;
        float thumbH = std::max(28.f, trackH * trackH / (trackH + a.modsScrollMax));
        float pos = (trackH - thumbH) * std::clamp(a.modsScrollY / a.modsScrollMax, 0.f, 1.f);
        FillRound(a, x + w + 10.f, top + pos, 4.f, thumbH, 2.f, a.brBorder.get());
    }
}

static void DrawCatalog(App& a, float x, float y, float w) {
    float H = a.rt->GetSize().height;

    Text(a, L"Каталог", a.fTitle.get(), a.brText.get(), x, y, w, 42);
    Text(a, L"Моды, ресурспаки, шейдеры и датапаки из Modrinth",
         a.fSmall.get(), a.brDim.get(), x, y + 44, w, 18);

    // вкладки типов
    const wchar_t* tabs[4] = { L"Моды", L"Ресурспаки", L"Шейдеры", L"Датапаки" };
    const float tw[4] = { 78.f, 124.f, 100.f, 108.f };
    float tx = x, ty = y + CAT_TABS_Y;
    for (int i = 0; i < 4; ++i) {
        bool on = (a.catTab == i);
        FillRound(a, tx, ty, tw[i], 34, 10, on ? a.brAccent.get() : a.brCard.get());
        if (!on) StrokeRound(a, tx, ty, tw[i], 34, 10, a.brBorder.get());
        Text(a, tabs[i], a.fBtn.get(), on ? a.brWhite.get() : a.brText.get(), tx, ty, tw[i], 34);
        tx += tw[i] + 8;
    }

    // поиск
    float sy = y + CAT_SEARCH_Y;
    float bw = w;
    FillRound(a, x, sy, bw, 42, 12, a.brInput.get());
    StrokeRound(a, x, sy, bw, 42, 12, a.searchFocus ? a.brAccent.get() : a.brBorder.get(),
                a.searchFocus ? 1.6f : 1.f);
    if (a.query.empty()) {
        Text(a, PlaceholderFor(a.catTab), a.fBody.get(), a.brDim.get(), x + 14, sy + 12, bw - 28, 20);
    } else {
        TextRaw(a, a.query, a.fBody.get(), a.brText.get(), x + 14, sy + 12, bw - 40, 20);
        if (a.searchFocus) {
            float cxp = x + 14 + std::min(MeasureTextRaw(a, a.query, a.fBody.get()), bw - 42.f);
            a.rt->FillRectangle(D2D1::RectF(cxp + 1, sy + 11, cxp + 2.4f, sy + 31), a.brAccent.get());
        }
    }

    // фильтры
    float fy = y + CAT_FILTER_Y;
    FillRound(a, x, fy, 150, 34, 10, a.onlyProfile ? a.brAccentSoft.get() : a.brCard.get());
    StrokeRound(a, x, fy, 150, 34, 10, a.onlyProfile ? a.brAccent.get() : a.brBorder.get());
    Text(a, (a.onlyProfile ? L"✓  " : L"     ") + std::wstring(L"Как в сборке"),
         a.fTiny.get(), a.onlyProfile ? a.brText.get() : a.brDim.get(), x + 10, fy + 10, 136, 16);

    std::wstring prof = a.hasProfile
        ? LoaderName(a.profile.loader) + L" " + a.profile.mc
        : L"фильтры выключены";
    Text(a, prof, a.fTiny.get(), a.brDim.get(), x + 162, fy + 10, 240, 16);

    FillRound(a, x + w - 140, fy, 140, 34, 10, a.brCard.get());
    StrokeRound(a, x + w - 140, fy, 140, 34, 10, a.brBorder.get());
    Text(a, L"Популярное", a.fTinyC.get(), a.brText.get(), x + w - 140, fy, 140, 34);

    // список
    float top = y + CAT_LIST_Y;
    float viewH = H - top - 34.f;
    float total = a.items.empty() ? 0.f : a.items.size() * (ROW_H + ROW_GAP) - ROW_GAP;
    a.scrollMax = std::max(0.f, total - viewH);
    if (a.scrollY > a.scrollMax) a.scrollY = a.scrollMax;

    a.rt->PushAxisAlignedClip(D2D1::RectF(x, top, x + w, H - 34.f),
                              D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (a.loading && a.items.empty()) {
        for (int i = 0; i < 4; ++i) {
            float ry = top + i * (ROW_H + ROW_GAP);
            FillRound(a, x, ry, w, ROW_H, 12, a.brCard.get());
            FillRound(a, x + 14, ry + 16, 56, 56, 10, a.brCardHi.get());
            FillRound(a, x + 84, ry + 18, 240, 14, 6, a.brCardHi.get());
            FillRound(a, x + 84, ry + 42, std::min(430.f, w - 300), 10, 5, a.brBorder.get());
            FillRound(a, x + 84, ry + 60, std::min(330.f, w - 330), 10, 5, a.brBorder.get());
            FillRound(a, x + w - 126, ry + 16, 108, 30, 8, a.brCardHi.get());
        }
    } else if (!a.loading && a.items.empty()) {
        FillRound(a, x, top, w, 150, 14, a.brCard.get());
        Text(a, L"Ничего не найдено", a.fH2.get(), a.brText.get(), x + 20, top + 22, w - 40, 24);
        Text(a, L"Попробуй другой запрос или выключи «Как в сборке».",
             a.fBodyWrap.get(), a.brDim.get(), x + 20, top + 56, w - 40, 40);
    } else {
        for (size_t i = 0; i < a.items.size(); ++i) {
            float ry = top + (float)i * (ROW_H + ROW_GAP) - a.scrollY;
            if (ry + ROW_H < top - 4 || ry > H - 34) continue;
            auto& it = *a.items[i];
            bool hov = (a.hoverRow == (int)i);

            FillRound(a, x, ry, w, ROW_H, 12, hov ? a.brCardHi.get() : a.brCard.get());
            if (hov) StrokeRound(a, x, ry, w, ROW_H, 12, a.brBorder.get());

            if (it.icon) {
                D2D1_RECT_F dst = D2D1::RectF(x + 14, ry + 16, x + 70, ry + 72);
                a.rt->DrawBitmap(it.icon.get(), dst, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            } else {
                FillRound(a, x + 14, ry + 16, 56, 56, 10, a.brCardHi.get());
                std::wstring letter = it.name.empty() ? L"?" : it.name.substr(0, 1);
                Text(a, letter, a.fLogoMark.get(), a.brDim.get(), x + 14, ry + 16, 56, 56);
            }

            TextRaw(a, it.name, a.fH2.get(), a.brText.get(), x + 84, ry + 12, w - 84 - 150, 22);
            TextRaw(a, it.desc, a.fDesc.get(), a.brDim.get(), x + 84, ry + 35, w - 84 - 150, 16);

            std::wstring meta = FmtCount(it.downloads) + L" скачиваний";
            if (it.installed) meta += L" · установлен";
            Text(a, meta, a.fTiny.get(), it.installed ? a.brGreen.get() : a.brDim.get(),
                 x + 84, ry + 62, w - 84 - 150, 14);

            int pr = it.progress.load();
            bool inst = it.installed.load();
            std::wstring btn = inst ? L"✓ Готово" : (pr >= 0 ? std::to_wstring(pr) + L"%"
                                                     : (pr == -2 ? L"…" : L"Установить"));
            FillRound(a, x + w - 126, ry + 16, 108, 30, 8, ProgressBrush(a, pr, inst));
            Text(a, btn, a.fBtn.get(), inst ? a.brDim.get() : a.brWhite.get(),
                 x + w - 126, ry + 16, 108, 30);
            Text(a, L"подробнее", a.fTiny.get(), a.brDim.get(),
                 x + w - 126, ry + 54, 108, 14);
        }
    }

    a.rt->PopAxisAlignedClip();
}

// ── страница проекта (каталог) ────────────────────────────────────────────────
static void DrawDetail(App& a, float x, float y, float w) {
    auto& d = a.detail;
    float H = a.rt->GetSize().height;
    float bottom = H - 30.f;

    // назад
    bool backHot = a.hoverWidget == 500;
    FillRound(a, x, y, 96, 30, 9, backHot ? a.brCardHi.get() : a.brCard.get());
    StrokeRound(a, x, y, 96, 30, 9, a.brBorder.get());
    Text(a, L"← Назад", a.fBtn.get(), backHot ? a.brText.get() : a.brDim.get(), x, y, 96, 30);

    // кнопки
    const MRect inst{ x + w - 320.f, y + 44.f, 140.f, 36.f };
    const MRect web{ x + w - 170.f, y + 44.f, 170.f, 36.f };
    bool iHot = a.hoverWidget == 502, wHot = a.hoverWidget == 501;
    FillRound(a, inst.x, inst.y, inst.w, inst.h, 10, iHot ? a.brAccentHi.get() : a.brAccent.get());
    Text(a, d.item && d.item->installed.load() ? L"✓ Готово" : L"Установить", a.fBtn.get(),
         a.brWhite.get(), inst.x, inst.y, inst.w, inst.h);
    FillRound(a, web.x, web.y, web.w, web.h, 10, wHot ? a.brCardHi.get() : a.brCard.get());
    StrokeRound(a, web.x, web.y, web.w, web.h, 10, wHot ? a.brAccent.get() : a.brBorder.get());
    Text(a, L"Открыть на Modrinth",
         a.fBtn.get(), wHot ? a.brText.get() : a.brDim.get(), web.x, web.y, web.w, web.h);

    // шапка
    float ix = x, iy = y + 44.f, isz = 72.f;
    FillRound(a, ix, iy, isz, isz, 14, a.brCard.get());
    if (d.item && d.item->icon) {
        a.rt->DrawBitmap(d.item->icon.get(),
                         D2D1::RectF(ix + 4, iy + 4, ix + isz - 4, iy + isz - 4),
                         1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
        std::wstring letter = d.title.empty() ? L"?" : d.title.substr(0, 1);
        Text(a, letter, a.fLogoMark.get(), a.brDim.get(), ix, iy, isz, isz);
    }

    float tx = ix + isz + 16.f;
    float tw = w - isz - 16.f - 330.f;
    Text(a, Ellipsize(a, d.title, a.fModal.get(), tw), a.fModal.get(), a.brText.get(),
         tx, iy - 6.f, tw, 28);
    Text(a, Ellipsize(a, d.summary, a.fSmall.get(), tw), a.fSmall.get(), a.brDim.get(),
         tx, iy + 24.f, tw, 16);

    std::wstring meta;
    if (!d.author.empty()) meta = L"от " + d.author;
    if (d.downloads > 0) {
        if (!meta.empty()) meta += L"  ·  ";
        meta += FmtCount(d.downloads) + L" скачиваний";
    }
    if (d.followers > 0) {
        if (!meta.empty()) meta += L"  ·  ";
        meta += FmtCount(d.followers) + L" подписчиков";
    }
    if (!d.license.empty()) {
        if (!meta.empty()) meta += L"  ·  ";
        meta += d.license;
    }
    if (!meta.empty()) Text(a, Ellipsize(a, meta, a.fTiny.get(), tw),
                            a.fTiny.get(), a.brDim.get(), tx, iy + 44.f, tw, 14);

    std::wstring tags;
    for (size_t i = 0; i < d.cats.size() && i < 6; ++i) {
        if (!tags.empty()) tags += L" · ";
        tags += d.cats[i];
    }
    if (!d.version.empty()) {
        if (!tags.empty()) tags += L"   ·   ";
        tags += L"версия " + d.version;
    }
    if (!tags.empty()) Text(a, Ellipsize(a, tags, a.fTiny.get(), tw),
                            a.fTiny.get(), a.brAccent.get(), tx, iy + 62.f, tw, 14);

    // прокручиваемое содержимое
    float top = y + 128.f;
    a.rt->PushAxisAlignedClip(D2D1::RectF(x, top, x + w, bottom),
                              D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    float cy = top - d.scrollY;

    if (d.busy) {
        Text(a, L"Загрузка описания и зависимостей…", a.fBodyWrap.get(), a.brDim.get(),
             x, cy, w, 20);
        cy += 40.f;
    } else {
        if (!d.err.empty()) {
            Text(a, d.err, a.fBodyWrap.get(), a.brYellow.get(), x, cy, w, 20);
            cy += 26.f;
        }

        // скриншоты
        Text(a, L"Скриншоты", a.fH2.get(), a.brText.get(), x, cy, w, 22);
        cy += 28.f;
        d.shotRects.assign(d.shots.size(), MRect{});
        if (d.shots.empty()) {
            Text(a, L"Скриншотов нет", a.fSmall.get(), a.brDim.get(), x, cy, w, 18);
            cy += 24.f;
        } else {
            float gap = 12.f;
            float sw = (w - gap * 2.f) / 3.f;
            float sh = sw * 9.f / 16.f;
            for (size_t i = 0; i < d.shots.size(); ++i) {
                int col = (int)(i % 3), row = (int)(i / 3);
                float sx = x + col * (sw + gap);
                float sy = cy + row * (sh + 30.f);
                if (!d.shots[i].bmp && d.shots[i].px.w > 0 && a.rt) {
                    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                          D2D1_ALPHA_MODE_PREMULTIPLIED));
                    a.rt->CreateBitmap(D2D1::SizeU((UINT32)d.shots[i].px.w,
                                                   (UINT32)d.shots[i].px.h),
                                       d.shots[i].px.px.data(), (UINT32)d.shots[i].px.w * 4,
                                       props, d.shots[i].bmp.put());
                }
                if (sy + sh + 26 < top || sy > bottom) continue;
                d.shotRects[i] = MRect{ sx, sy, sw, sh };
                FillRound(a, sx, sy, sw, sh, 10, a.brCard.get());
                if (d.shots[i].bmp) {
                    a.rt->DrawBitmap(d.shots[i].bmp.get(),
                                     D2D1::RectF(sx + 2, sy + 2, sx + sw - 2, sy + sh - 2),
                                     1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                } else {
                    Text(a, L"загрузка…", a.fTiny.get(), a.brDim.get(), sx, sy, sw, sh);
                }
                if (!d.shots[i].title.empty())
                    Text(a, d.shots[i].title, a.fTiny.get(), a.brDim.get(),
                         sx, sy + sh + 4.f, sw, 14);
            }
            int rows = (int)((d.shots.size() + 2) / 3);
            cy += rows * (sh + 30.f) + 6.f;
        }

        // зависимости
        Text(a, L"Зависимости", a.fH2.get(), a.brText.get(), x, cy, w, 22);
        cy += 26.f;
        if (!d.depNote.empty()) {
            Text(a, d.depNote, a.fTiny.get(), a.brDim.get(), x, cy, w, 16);
            cy += 20.f;
        }
        for (auto& r : d.deps) {
            if (cy > top - 30.f && cy < bottom) {
                float pw = MeasureText(a, r.prefix, a.fMono.get());
                Text(a, r.prefix, a.fMono.get(), a.brDim.get(), x, cy, pw + 6.f, 18);
                Text(a, r.name, a.fBody.get(), r.missing ? a.brYellow.get() : a.brText.get(),
                     x + pw, cy, w - pw - 190.f, 18);
                if (!r.note.empty())
                    Text(a, r.note, a.fTiny.get(),
                         r.missing ? a.brYellow.get() : a.brDim.get(),
                         x + w - 180.f, cy + 2.f, 180.f, 16);
            }
            cy += 22.f;
        }
        if (d.deps.empty() && d.depNote.empty()) {
            Text(a, L"Обязательных зависимостей нет", a.fSmall.get(), a.brDim.get(), x, cy, w, 18);
            cy += 22.f;
        }
        cy += 12.f;

        // описание
        Text(a, L"Описание", a.fH2.get(), a.brText.get(), x, cy, w, 22);
        cy += 28.f;
        if (d.blocks.empty()) {
            Text(a, L"Описание не указано", a.fSmall.get(), a.brDim.get(), x, cy, w, 18);
            cy += 22.f;
        }
        for (auto& b : d.blocks) {
            IDWriteTextFormat* f = b.kind == 1 ? a.fH2.get()
                                 : b.kind == 3 ? a.fMono.get() : a.fBodyWrap.get();
            float ind = (b.kind == 2 || b.kind == 3 || b.kind == 4) ? 12.f : 0.f;
            cy += (b.kind == 1 ? 12.f : 4.f);
            float hgt = TextHeight(a, b.text, f, w - ind);
            if (cy + hgt > top && cy < bottom)
                Text(a, b.text, f, b.kind == 1 ? a.brText.get() : a.brDim.get(),
                     x + ind, cy, w - ind, hgt + 2.f);
            cy += hgt + 2.f;
        }
        cy += 26.f;
    }

    d.scrollMax = std::max(0.f, (cy + d.scrollY) - bottom);
    a.rt->PopAxisAlignedClip();

    if (d.scrollMax > 0.f) {
        float trackH = bottom - top;
        float thumbH = std::max(28.f, trackH * trackH / (trackH + d.scrollMax));
        float pos = (trackH - thumbH) * std::clamp(d.scrollY / d.scrollMax, 0.f, 1.f);
        FillRound(a, x + w + 10.f, top + pos, 4.f, thumbH, 2.f, a.brBorder.get());
    }
}

// Просмотр скриншота во весь экран.
static void DrawShotLightbox(App& a) {
    int i = a.detail.shotOpen;
    if (i < 0 || i >= (int)a.detail.shots.size()) return;
    auto& s = a.detail.shots[i];
    if (!s.bmp && s.px.w > 0 && a.rt) {
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        a.rt->CreateBitmap(D2D1::SizeU((UINT32)s.px.w, (UINT32)s.px.h),
                           s.px.px.data(), (UINT32)s.px.w * 4, props, s.bmp.put());
    }
    float W = a.rt->GetSize().width, H = a.rt->GetSize().height;
    a.rt->FillRectangle(D2D1::RectF(0, 0, W, H), a.brOverlay.get());
    if (!s.bmp || s.px.w <= 0 || s.px.h <= 0) return;
    float ax = SIDE_W + 30.f, ay = 56.f;
    float availW = W - ax - 36.f, availH = H - ay - 96.f;
    float sc = std::min(availW / (float)s.px.w, availH / (float)s.px.h);
    float dw = s.px.w * sc, dh = s.px.h * sc;
    float dx = ax + (availW - dw) / 2.f;
    float dy = ay + (availH - dh) / 2.f;
    a.rt->DrawBitmap(s.bmp.get(), D2D1::RectF(dx, dy, dx + dw, dy + dh), 1.f,
                     D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    if (!s.title.empty())
        Text(a, s.title, a.fSmall.get(), a.brText.get(), dx, dy + dh + 10.f, dw, 18);
    Text(a, L"Щелчок или Esc — закрыть", a.fTiny.get(), a.brDim.get(),
         dx, dy + dh + (s.title.empty() ? 10.f : 30.f), dw, 14);
}

// ── страница «Настройки» ──────────────────────────────────────────────────────
struct SettingsLayout {
    float cw = 0.f;
    float memTop = 0, memH = 0, javaTop = 0, javaH = 0;
    float runTop = 0, runH = 0, langTop = 0, langH = 0, uiTop = 0, uiH = 0;
    MRect ramMin, ramMax, javaPath, javaCheck, javaBrowse, javaDl[4], user, javaArgs, gameArgs;
    MRect langBtn[6];
    MRect themeDark, themeLight;
    MRect accentSw[6], accentEntry, accentApply;
    MRect accent2Sw[6], accent2Entry, accent2Apply;
    MRect save;
    float contentH = 0.f;
};

static SettingsLayout SettingsLayoutFor(App& a, float x, float y, float w) {
    SettingsLayout L;
    float cw = std::min(w, 720.f);
    L.cw = cw;
    const float pad = 20.f;
    float inner = cw - 2 * pad;
    float halfW = (inner - 12.f) / 2.f;
    float top = y + 86.f - a.settingsScrollY;

    L.memTop = top;   L.memH = 126.f;
    L.ramMin = MRect{ x + pad, L.memTop + 72.f, halfW, 38.f };
    L.ramMax = MRect{ x + pad + halfW + 12.f, L.memTop + 72.f, halfW, 38.f };

    L.javaTop = L.memTop + L.memH + 14.f;   L.javaH = 274.f;
    L.javaPath  = MRect{ x + pad, L.javaTop + 72.f, inner, 38.f };
    L.javaBrowse = MRect{ x + pad, L.javaTop + 122.f, 132.f, 38.f };
    L.javaCheck = MRect{ x + pad + 140.f, L.javaTop + 122.f, 176.f, 38.f };
    for (int i = 0; i < 4; ++i)
        L.javaDl[i] = MRect{ x + pad + i * 84.f, L.javaTop + 194.f, 76.f, 32.f };

    L.runTop = L.javaTop + L.javaH + 14.f;   L.runH = 254.f;
    L.user     = MRect{ x + pad, L.runTop + 72.f, inner, 38.f };
    L.javaArgs = MRect{ x + pad, L.runTop + 136.f, inner, 38.f };
    L.gameArgs = MRect{ x + pad, L.runTop + 200.f, inner, 38.f };

    L.langTop = L.runTop + L.runH + 14.f;   L.langH = 92.f;
    float langW = (inner - 5.f * 8.f) / 6.f;
    for (int i = 0; i < 6; ++i)
        L.langBtn[i] = MRect{ x + pad + i * (langW + 8.f), L.langTop + 44.f, langW, 34.f };

    L.uiTop = L.langTop + L.langH + 14.f;   L.uiH = 280.f;
    L.themeDark  = MRect{ x + pad, L.uiTop + 74.f, halfW, 36.f };
    L.themeLight = MRect{ x + pad + halfW + 12.f, L.uiTop + 74.f, halfW, 36.f };

    const float swW = 34.f, swGap = 8.f, entryW = 110.f, applyW = 74.f;
    float tail = entryW + 10.f + applyW;
    float rowLeft = x + pad;
    float rowRight = x + pad + inner;

    float ay = L.uiTop + 142.f;
    for (int i = 0; i < 6; ++i)
        L.accentSw[i] = MRect{ rowLeft + i * (swW + swGap), ay, swW, 32.f };
    L.accentEntry = MRect{ rowRight - tail, ay, entryW, 32.f };
    L.accentApply = MRect{ rowRight - applyW, ay, applyW, 32.f };

    float by = L.uiTop + 208.f;
    for (int i = 0; i < 6; ++i)
        L.accent2Sw[i] = MRect{ rowLeft + i * (swW + swGap), by, swW, 32.f };
    L.accent2Entry = MRect{ rowRight - tail, by, entryW, 32.f };
    L.accent2Apply = MRect{ rowRight - applyW, by, applyW, 32.f };

    L.save = MRect{ x, L.uiTop + L.uiH + 14.f, cw, 48.f };
    L.contentH = L.memH + L.javaH + L.runH + L.langH + L.uiH + L.save.h + 14.f * 5.f;
    return L;
}

static void DrawField(App& a, const MRect& r, const std::wstring& val, const wchar_t* ph,
                      bool focused) {
    FillRound(a, r.x, r.y, r.w, r.h, 10, a.brInput.get());
    StrokeRound(a, r.x, r.y, r.w, r.h, 10, focused ? a.brAccent.get() : a.brBorder.get(),
                focused ? 1.6f : 1.f);
    float ty = r.y + (r.h - 18.f) / 2.f;
    if (val.empty()) {
        Text(a, ph, a.fField.get(), a.brDim.get(), r.x + 12, ty, r.w - 24, 18);
    } else {
        TextRaw(a, val, a.fField.get(), a.brText.get(), r.x + 12, ty, r.w - 24, 18);
        if (focused) {
            float cx = r.x + 12 + std::min(MeasureTextRaw(a, val, a.fField.get()), r.w - 26.f);
            a.rt->FillRectangle(D2D1::RectF(cx + 1.f, ty, cx + 2.4f, ty + 18.f), a.brAccent.get());
        }
    }
}

static void DrawSeg(App& a, const MRect& r, const wchar_t* t, bool on, bool hot) {
    FillRound(a, r.x, r.y, r.w, r.h, 10,
              on ? a.brAccent.get() : (hot ? a.brCardHi.get() : a.brInput.get()));
    if (!on) StrokeRound(a, r.x, r.y, r.w, r.h, 10, hot ? a.brAccent.get() : a.brBorder.get());
    Text(a, t, a.fBtn.get(), on ? a.brWhite.get() : a.brText.get(), r.x, r.y, r.w, r.h);
}

static void DrawMiniBtn(App& a, const MRect& r, const wchar_t* t, bool hot) {
    FillRound(a, r.x, r.y, r.w, r.h, 8, hot ? a.brBorder.get() : a.brCardHi.get());
    StrokeRound(a, r.x, r.y, r.w, r.h, 8, a.brBorder.get());
    Text(a, t, a.fBtn.get(), a.brText.get(), r.x, r.y, r.w, r.h);
}

static void DrawSwatch(App& a, const MRect& r, const wchar_t* hex, const std::wstring& cur,
                       bool hot) {
    int idx = -1;
    for (int i = 0; i < 6; ++i)
        if (wcscmp(hex, kAccentPresets[i]) == 0) { idx = i; break; }
    FillRound(a, r.x, r.y, r.w, r.h, 8,
              idx >= 0 ? a.brSw[idx].get() : a.brCardHi.get());
    bool sel = (_wcsicmp(cur.c_str(), hex) == 0);
    if (sel) StrokeRound(a, r.x - 2, r.y - 2, r.w + 4, r.h + 4, 10, a.brText.get(), 2.f);
    else if (hot) StrokeRound(a, r.x, r.y, r.w, r.h, 8, a.brText.get());
}

// ── скины: страница «Смена скина» ─────────────────────────────────────────────
static bool NearCol(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b);

static ID2D1Brush* SkinStatusBrush(App& a) {
    if (NearCol(a.skinStatusCol, C_RED)) return a.brRed.get();
    if (NearCol(a.skinStatusCol, C_GREEN)) return a.brGreen.get();
    if (NearCol(a.skinStatusCol, C_YELLOW)) return a.brYellow.get();
    if (NearCol(a.skinStatusCol, C_ACCENT)) return a.brAccent.get();
    return a.brDim.get();
}

struct SkinLayout {
    MRect back, classic, slim, pick, nick, take, apply, reset, capeNone;
    MRect capes[8];
    int capeCount = 0;
    float leftX = 0, leftW = 0, boxX = 0, boxY = 0, boxW = 0, boxH = 0;
    float prevX = 0, prevY = 0, rightX = 0, rightW = 0, top = 0;
};

static SkinLayout SkinLayoutFor(App& a, float x, float y, float w) {
    SkinLayout L;
    L.top = y + 74.f;
    L.back = MRect{ x, y + 14.f, 92.f, 32.f };
    L.leftX = x;
    L.leftW = 200.f;
    L.rightX = x + 216.f;
    L.rightW = w - 216.f;
    float rx = L.rightX, rw = L.rightW, top = L.top;

    float segW = (rw - 10.f) / 2.f;
    L.classic = MRect{ rx, top + 22.f, segW, 36.f };
    L.slim = MRect{ rx + segW + 10.f, top + 22.f, segW, 36.f };

    L.pick = MRect{ rx + 16.f, top + 130.f, rw - 32.f, 34.f };

    float nickW = rw - 32.f - 96.f;
    L.nick = MRect{ rx + 16.f, top + 244.f, nickW, 34.f };
    L.take = MRect{ rx + 16.f + nickW + 8.f, top + 244.f, 88.f, 34.f };

    L.apply = MRect{ rx, top + 302.f, rw, 46.f };
    L.reset = MRect{ rx, top + 356.f, rw, 30.f };
    L.capeNone = MRect{ rx + 16.f, top + 466.f, 108.f, 30.f };

    L.capeCount = (int)std::min<size_t>(8, a.capes.size());
    if (L.capeCount > 0) {
        const float gap = 6.f, startX = rx + 16.f + 114.f;
        float avail = (rx + rw) - 16.f - startX;
        float bw = (avail - gap * (L.capeCount - 1)) / L.capeCount;
        bw = std::clamp(bw, 56.f, 120.f);
        for (int i = 0; i < L.capeCount; ++i)
            L.capes[i] = MRect{ startX + i * (bw + gap), top + 466.f, bw, 30.f };
    }

    L.boxX = x + 18.f;
    L.boxY = top + 34.f;
    L.boxW = 164.f;
    L.boxH = 268.f;
    L.prevX = L.boxX + (L.boxW - (float)std::max(1, a.skinPrevW)) / 2.f;
    L.prevY = L.boxY + (L.boxH - (float)std::max(1, a.skinPrevH)) / 2.f;
    return L;
}

static void CenterText(App& a, const std::wstring& s, IDWriteTextFormat* f, ID2D1Brush* b,
                       float cx, float y, float h = 18.f) {
    float tw = MeasureText(a, s, f);
    Text(a, s, f, b, cx - tw / 2.f, y, tw + 2.f, h);
}

static void DrawSkin(App& a, float x, float y, float w) {
    SkinLayout L = SkinLayoutFor(a, x, y, w);
    float top = L.top, rx = L.rightX, rw = L.rightW;

    // заголовок
    FillRound(a, x, y, w, 62.f, 14.f, a.brCard.get());
    StrokeRound(a, x, y, w, 62.f, 14.f, a.brBorder.get());
    bool bh = a.hoverWidget == 400;
    FillRound(a, L.back.x, L.back.y, L.back.w, L.back.h, 9.f,
              bh ? a.brAccent.get() : a.brInput.get());
    StrokeRound(a, L.back.x, L.back.y, L.back.w, L.back.h, 9.f,
                bh ? a.brAccent.get() : a.brBorder.get());
    Text(a, L"← Назад", a.fBtn.get(), bh ? a.brWhite.get() : a.brText.get(),
         L.back.x, L.back.y, L.back.w, L.back.h);
    Text(a, L"Смена скина", a.fH2.get(), a.brText.get(), x + 116.f, y + 8.f, w - 140.f, 24.f);
    Text(a, L"аккаунт: " + AuthName(a) +
             (a.authKind == L"ely" ? L" · Ely.by" : L" · Microsoft"),
         a.fTiny.get(), a.brAccent.get(), x + 116.f, y + 36.f, w - 140.f, 16.f);

    // предпросмотр
    FillRound(a, L.leftX, top, L.leftW, 320.f, 16.f, a.brCard.get());
    StrokeRound(a, L.leftX, top, L.leftW, 320.f, 16.f, a.brBorder.get());
    Text(a, L"Предпросмотр", a.fTiny.get(), a.brDim.get(), L.leftX + 14.f, top + 12.f,
         L.leftW - 28.f, 16.f);
    FillRound(a, L.boxX, L.boxY, L.boxW, L.boxH, 12.f, a.brInput.get());
    if (a.skinBmp && a.skinPrevW > 0 && a.skinPrevH > 0) {
        a.rt->DrawBitmap(a.skinBmp.get(),
                         D2D1::RectF(L.prevX, L.prevY, L.prevX + (float)a.skinPrevW,
                                     L.prevY + (float)a.skinPrevH));
    } else {
        CenterText(a, a.skinBusy ? L"загрузка…" : L"стандартный", a.fTiny.get(), a.brDim.get(),
                   L.boxX + L.boxW / 2.f, L.boxY + L.boxH / 2.f - 16.f);
        CenterText(a, a.skinBusy ? L"" : L"скин", a.fTiny.get(), a.brDim.get(),
                   L.boxX + L.boxW / 2.f, L.boxY + L.boxH / 2.f);
    }
    Text(a, a.skinLabel, a.fTiny.get(), a.brDim.get(), L.leftX + 12.f, top + 306.f,
         L.leftW - 24.f, 14.f);

    // модель
    Text(a, L"Модель скина", a.fTiny.get(), a.brDim.get(), rx + 2.f, top + 2.f, 220.f, 16.f);
    DrawSeg(a, L.classic, L"Классический (Steve)", a.skinVariant == 0, a.hoverWidget == 401);
    DrawSeg(a, L.slim, L"Тонкий (Alex)", a.skinVariant == 1, a.hoverWidget == 402);

    // свой файл
    FillRound(a, rx, top + 70.f, rw, 110.f, 12.f, a.brCard.get());
    StrokeRound(a, rx, top + 70.f, rw, 110.f, 12.f, a.brBorder.get());
    Text(a, L"Свой файл", a.fH2.get(), a.brText.get(), rx + 16.f, top + 80.f, rw - 32.f, 22.f);
    Text(a, L"PNG 64×64, подойдёт и старый 64×32", a.fTiny.get(), a.brDim.get(), rx + 16.f,
         top + 104.f, rw - 32.f, 14.f);
    DrawMiniBtn(a, L.pick, L"Выбрать файл…", a.hoverWidget == 403);

    // скин другого игрока
    FillRound(a, rx, top + 190.f, rw, 100.f, 12.f, a.brCard.get());
    StrokeRound(a, rx, top + 190.f, rw, 100.f, 12.f, a.brBorder.get());
    Text(a, L"Скин другого игрока", a.fH2.get(), a.brText.get(), rx + 16.f, top + 200.f,
         rw - 32.f, 22.f);
    Text(a, L"Скачать чужой скин по нику и поставить себе", a.fTiny.get(), a.brDim.get(),
         rx + 16.f, top + 222.f, rw - 32.f, 14.f);
    DrawField(a, L.nick, a.skinNick, L"Ник игрока", a.skinFocus == 1);
    DrawMiniBtn(a, L.take, L"Взять", a.hoverWidget == 405);

    // действия
    bool ah = a.hoverWidget == 406;
    FillRound(a, L.apply.x, L.apply.y, L.apply.w, L.apply.h, 12.f,
              ah ? a.brAccentHi.get() : a.brAccent.get());
    Text(a, a.skinBusy ? L"Подождите…" : L"Поставить скин", a.fBtn.get(), a.brWhite.get(),
         L.apply.x, L.apply.y, L.apply.w, L.apply.h);
    bool rh = a.hoverWidget == 407;
    FillRound(a, L.reset.x, L.reset.y, L.reset.w, L.reset.h, 9.f,
              rh ? a.brCardHi.get() : a.brInput.get());
    StrokeRound(a, L.reset.x, L.reset.y, L.reset.w, L.reset.h, 9.f, a.brBorder.get());
    Text(a, L"Сбросить на стандартный", a.fBtn.get(), a.brDim.get(),
         L.reset.x, L.reset.y, L.reset.w, L.reset.h);

    if (!a.skinStatus.empty())
        Text(a, a.skinStatus, a.fBodyWrap.get(), SkinStatusBrush(a), rx, top + 392.f, rw, 34.f);

    // плащи (как в Python: без плащей карточка не показывается)
    if (a.authKind != L"ely" && !a.capes.empty()) {
        FillRound(a, rx, top + 428.f, rw, 84.f, 12.f, a.brCard.get());
        StrokeRound(a, rx, top + 428.f, rw, 84.f, 12.f, a.brBorder.get());
        Text(a, L"Плащ", a.fH2.get(), a.brText.get(), rx + 16.f, top + 436.f, 120.f, 22.f);
        DrawMiniBtn(a, L.capeNone, L"Не носить", a.hoverWidget == 429);
        for (int i = 0; i < L.capeCount; ++i) {
            const CapeInfo& c = a.capes[i];
            const MRect& r = L.capes[i];
            bool on = c.active, hot = a.hoverWidget == 430 + i;
            FillRound(a, r.x, r.y, r.w, r.h, 8.f,
                      on ? a.brAccent.get() : (hot ? a.brBorder.get() : a.brCardHi.get()));
            if (!on) StrokeRound(a, r.x, r.y, r.w, r.h, 8.f, a.brBorder.get());
            Text(a, (on ? L"✓ " : L"") + c.alias, a.fBtn.get(),
                 on ? a.brWhite.get() : a.brText.get(), r.x, r.y, r.w, r.h);
        }
    }
}

// ── скины: действия ───────────────────────────────────────────────────────────
static void SkinStatus(App& a, const std::wstring& s, D2D1_COLOR_F c) {
    a.skinStatus = s;
    a.skinStatusCol = c;
}

static void SkinReload(App& a, const std::wstring& doneMsg = std::wstring()) {
    if (a.skinBusy) return;
    a.skinBusy = true;
    HWND hwnd = a.hwnd;
    bool ely = (a.authKind == L"ely");
    std::wstring tok = AuthToken(a), name = AuthName(a);
    if (doneMsg.empty())
        SkinStatus(a, ely ? L"Читаю профиль Ely.by…" : L"Читаю профиль Minecraft…", C_DIM);
    Repaint(hwnd);

    std::thread([hwnd, ely, tok, name, doneMsg]() {
        SkinProfile r = FetchSkinProfile(ely, tok, name);
        PostUI(hwnd, [hwnd, r, doneMsg]() {
            App* a = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (!a || !a->skinOpen) return;
            a->skinBusy = false;
            if (!r.error.empty()) {
                SkinStatus(*a, r.error, C_RED);
            } else {
                a->capes = r.capes;
                a->skinPng = r.png;
                a->skinLabel = r.label;
                if (!r.png.empty()) {
                    SkinPx sp;
                    if (DecodeSkinPng(r.png, sp) && SkinValid(sp))
                        a->skinVariant = r.slim ? 1 : 0;
                }
                ComputeSkinBitmaps(*a);
                SkinStatus(*a, doneMsg, doneMsg.empty() ? C_DIM : C_GREEN);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        });
    }).detach();
}

static void OpenSkinPage(App& a) {
    if (!AuthOnline(a)) {
        a.status = L"Сначала войдите в аккаунт";
        a.statusCol = C_YELLOW;
        Repaint(a.hwnd);
        return;
    }
    a.skinOpen = true;
    a.skinFocus = 0;
    a.skinNick.clear();
    a.capes.clear();
    a.skinPng.clear();
    a.skinLabel.clear();
    a.skinBusy = false;
    SkinStatus(a, L"", C_DIM);
    ComputeSkinBitmaps(a);
    Repaint(a.hwnd);
    SkinReload(a);
}

// Голова текущего аккаунта в карточке сайдбара: активный скин тянется в фоне.
static void SyncHead(App& a) {
    if (!AuthOnline(a)) {
        if (!a.headKey.empty()) {
            a.headKey.clear();
            a.skinPng.clear();
            a.skinLabel.clear();
            ComputeSkinBitmaps(a);
            Repaint(a.hwnd);
        }
        return;
    }
    std::wstring key = a.authKind + L"|" + AuthName(a);
    if (a.headKey == key) return;
    a.headKey = key;
    HWND hwnd = a.hwnd;
    bool ely = (a.authKind == L"ely");
    std::wstring tok = AuthToken(a), name = AuthName(a);
    std::thread([hwnd, ely, tok, name, key]() {
        SkinProfile r = FetchSkinProfile(ely, tok, name);
        // токен мог истечь — берём скин по нику (публичные API Mojang/Ely.by)
        if (r.png.empty() && !ely) {
            std::string png; bool slim = false; std::wstring real, err;
            if (SkinByName(name, png, slim, real, err)) {
                r.png = png;
                r.slim = slim;
                r.error.clear();
            }
        }
        PostUI(hwnd, [hwnd, r, key]() {
            App* a = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (!a || a->headKey != key) return;
            if (!r.error.empty() || r.png.empty()) return;   // останется буква
            if (a->skinOpen) return;                         // страница скина сама загрузит
            a->skinPng = r.png;
            a->skinLabel = r.label;
            SkinPx sp;
            if (DecodeSkinPng(r.png, sp) && SkinValid(sp))
                a->skinVariant = r.slim ? 1 : 0;
            ComputeSkinBitmaps(*a);
            Repaint(hwnd);
        });
    }).detach();
}

static void SkinPickFile(App& a) {
    if (a.skinBusy) return;
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = a.hwnd;
    ofn.lpstrFilter = SkinFileFilter();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    std::wstring title = TrText(L"Выберите скин (PNG 64×64)");
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;

    fs::path p(file);
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        SkinStatus(a, L"файл не читается", C_RED);
        Repaint(a.hwnd);
        return;
    }
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    SkinPx sp;
    if (data.empty() || !DecodeSkinPng(data, sp) || !SkinValid(sp)) {
        SkinStatus(a, L"нужен PNG 64×64 (или старый 64×32)", C_RED);
        Repaint(a.hwnd);
        return;
    }
    a.skinPng = data;
    a.skinVariant = SkinIsSlim(sp) ? 1 : 0;
    a.skinLabel = L"файл: " + p.filename().wstring();
    ComputeSkinBitmaps(a);
    SkinStatus(a, std::wstring(L"модель — ") +
                      (a.skinVariant == 1 ? L"тонкая (Alex)" : L"классическая (Steve)") +
                      L", поправьте при необходимости",
               C_DIM);
    Repaint(a.hwnd);
}

static void SkinTakeByName(App& a) {
    std::wstring nick = a.skinNick;
    while (!nick.empty() && (nick.front() == L' ' || nick.front() == L'\t')) nick.erase(nick.begin());
    while (!nick.empty() && (nick.back() == L' ' || nick.back() == L'\t')) nick.pop_back();
    if (nick.empty()) {
        SkinStatus(a, L"введите ник игрока", C_YELLOW);
        Repaint(a.hwnd);
        return;
    }
    if (a.skinBusy) return;
    a.skinBusy = true;
    HWND hwnd = a.hwnd;
    SkinStatus(a, L"ищу скин игрока " + nick + L"…", C_DIM);
    Repaint(hwnd);

    std::thread([hwnd, nick]() {
        std::string png;
        bool slim = false;
        std::wstring real, err;
        bool ok = SkinByName(nick, png, slim, real, err);
        PostUI(hwnd, [hwnd, ok, png, slim, real, err, nick]() {
            App* a = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (!a || !a->skinOpen) return;
            a->skinBusy = false;
            if (!ok) {
                SkinStatus(*a, err, C_YELLOW);
            } else {
                SkinPx sp;
                if (DecodeSkinPng(png, sp) && SkinValid(sp)) {
                    a->skinPng = png;
                    a->skinVariant = slim ? 1 : 0;
                    std::wstring who = real.empty() ? nick : real;
                    a->skinLabel = L"скин игрока " + who;
                    ComputeSkinBitmaps(*a);
                    SkinStatus(*a, L"взял скин " + who + L" — нажмите «Поставить скин»", C_DIM);
                } else {
                    SkinStatus(*a, L"файл скина не читается", C_RED);
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        });
    }).detach();
}

static void SkinApply(App& a) {
    if (a.skinBusy) return;
    if (a.skinPng.empty()) {
        SkinStatus(a, L"сначала выберите файл или возьмите скин по нику", C_YELLOW);
        Repaint(a.hwnd);
        return;
    }
    a.skinBusy = true;
    HWND hwnd = a.hwnd;
    bool ely = (a.authKind == L"ely");
    std::wstring tok = AuthToken(a), uid = AuthUuid(a);
    std::string png = a.skinPng;
    bool slim = (a.skinVariant == 1);
    SkinStatus(a, L"ставлю скин…", C_DIM);
    Repaint(hwnd);

    std::thread([hwnd, ely, tok, uid, png, slim]() {
        std::wstring err = UploadSkinApi(ely, tok, uid, png, slim);
        PostUI(hwnd, [hwnd, err]() {
            App* a = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (!a || !a->skinOpen) return;
            a->skinBusy = false;
            if (!err.empty()) {
                SkinStatus(*a, err, C_RED);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
            SkinStatus(*a, L"скин поставлен ✓", C_GREEN);
            InvalidateRect(hwnd, nullptr, FALSE);
            SkinReload(*a, L"скин поставлен ✓");
        });
    }).detach();
}

static void SkinReset(App& a) {
    if (a.skinBusy) return;
    std::wstring msg = TrText(L"Сбросить скин на стандартный?");
    std::wstring title = TrText(L"Смена скина");
    if (MessageBoxW(a.hwnd, msg.c_str(), title.c_str(),
                    MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    a.skinBusy = true;
    HWND hwnd = a.hwnd;
    bool ely = (a.authKind == L"ely");
    std::wstring tok = AuthToken(a), uid = AuthUuid(a);
    SkinStatus(a, L"сбрасываю скин…", C_DIM);
    Repaint(hwnd);

    std::thread([hwnd, ely, tok, uid]() {
        std::wstring err = DeleteSkinApi(ely, tok, uid);
        PostUI(hwnd, [hwnd, err]() {
            App* a = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (!a || !a->skinOpen) return;
            a->skinBusy = false;
            if (!err.empty()) {
                SkinStatus(*a, err, C_RED);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
            SkinStatus(*a, L"скин сброшен", C_GREEN);
            InvalidateRect(hwnd, nullptr, FALSE);
            SkinReload(*a, L"скин сброшен");
        });
    }).detach();
}

static void CapeApply(App& a, const std::wstring& capeId) {
    if (a.skinBusy || a.authKind == L"ely") return;
    a.skinBusy = true;
    HWND hwnd = a.hwnd;
    std::wstring tok = AuthToken(a);
    SkinStatus(a, capeId.empty() ? L"снимаю плащ…" : L"надеваю плащ…", C_DIM);
    Repaint(hwnd);

    std::thread([hwnd, tok, capeId]() {
        std::wstring err = CapeApi(tok, capeId);
        PostUI(hwnd, [hwnd, err]() {
            App* a = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (!a || !a->skinOpen) return;
            a->skinBusy = false;
            if (!err.empty()) {
                SkinStatus(*a, err, C_RED);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
            SkinStatus(*a, L"плащ обновлён", C_GREEN);
            InvalidateRect(hwnd, nullptr, FALSE);
            SkinReload(*a, L"плащ обновлён");
        });
    }).detach();
}

static void DrawSettings(App& a, float x, float y, float w) {
    float H = a.rt->GetSize().height;
    Text(a, L"Настройки", a.fTitle.get(), a.brText.get(), x, y, w, 42);
    Text(a, L"Тема, цвета, Java и параметры запуска", a.fSmall.get(), a.brDim.get(),
         x, y + 44, w, 18);

    SettingsLayout L = SettingsLayoutFor(a, x, y, w);
    float viewTop = y + 82.f, viewBottom = H - 30.f;
    a.settingsScrollMax = std::max(0.f, L.contentH - (viewBottom - viewTop));
    if (a.settingsScrollY > a.settingsScrollMax) {
        a.settingsScrollY = a.settingsScrollMax;
        L = SettingsLayoutFor(a, x, y, w);
    }

    a.rt->PushAxisAlignedClip(D2D1::RectF(x, viewTop, x + w, viewBottom),
                              D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    auto card = [&](float top, float h, const wchar_t* title) {
        FillRound(a, x, top, L.cw, h, 14, a.brCard.get());
        StrokeRound(a, x, top, L.cw, h, 14, a.brBorder.get());
        FillRound(a, x + 20, top + 16, 3, 16, 1.5f, a.brAccent.get());
        Text(a, title, a.fH2.get(), a.brText.get(), x + 32, top + 13, L.cw - 60, 22);
    };
    auto label = [&](const MRect& r, const wchar_t* t) {
        Text(a, t, a.fTiny.get(), a.brDim.get(), r.x + 2, r.y - 17, r.w, 14);
    };

    // Память
    card(L.memTop, L.memH, L"Память");
    label(L.ramMin, L"Минимум (МБ)");
    DrawField(a, L.ramMin, a.ramMin, L"512M", a.setFocus == 300);
    label(L.ramMax, L"Максимум (МБ)");
    DrawField(a, L.ramMax, a.ramMax, L"4G", a.setFocus == 301);

    // Java
    card(L.javaTop, L.javaH, L"Java");
    label(L.javaPath, L"Путь к java.exe (пусто — автоопределение)");
    DrawField(a, L.javaPath, a.javaPathCfg, L"C:\\Java\\bin\\java.exe", a.setFocus == 302);

    bool jb = a.hoverWidget == 308;
    FillRound(a, L.javaBrowse.x, L.javaBrowse.y, L.javaBrowse.w, L.javaBrowse.h, 10,
              jb ? a.brBorder.get() : a.brCardHi.get());
    StrokeRound(a, L.javaBrowse.x, L.javaBrowse.y, L.javaBrowse.w, L.javaBrowse.h, 10,
                a.brBorder.get());
    Text(a, L"Обзор…", a.fBtn.get(), a.brText.get(), L.javaBrowse.x, L.javaBrowse.y,
         L.javaBrowse.w, L.javaBrowse.h);

    bool jc = a.hoverWidget == 303;
    FillRound(a, L.javaCheck.x, L.javaCheck.y, L.javaCheck.w, L.javaCheck.h, 10,
              jc ? a.brBorder.get() : a.brCardHi.get());
    StrokeRound(a, L.javaCheck.x, L.javaCheck.y, L.javaCheck.w, L.javaCheck.h, 10, a.brBorder.get());
    Text(a, a.javaChecking ? L"Проверяю…" : L"Проверить Java", a.fBtn.get(), a.brText.get(),
         L.javaCheck.x, L.javaCheck.y, L.javaCheck.w, L.javaCheck.h);

    Text(a, L"Скачать Java (Mojang) — рантайм встанет в .minecraft\\runtime:",
         a.fTiny.get(), a.brDim.get(), x + 20, L.javaTop + 174, L.cw - 40, 14);
    static const wchar_t* javaLabels[4] = { L"Java 8", L"Java 17", L"Java 21", L"Java 25" };
    for (int i = 0; i < 4; ++i) {
        bool hot = a.hoverWidget == 350 + i;
        FillRound(a, L.javaDl[i].x, L.javaDl[i].y, L.javaDl[i].w, L.javaDl[i].h, 8,
                  hot ? a.brAccent.get() : a.brCardHi.get());
        if (!hot) StrokeRound(a, L.javaDl[i].x, L.javaDl[i].y, L.javaDl[i].w, L.javaDl[i].h, 8,
                              a.brBorder.get());
        Text(a, javaLabels[i], a.fTinyC.get(), hot ? a.brWhite.get() : a.brText.get(),
             L.javaDl[i].x, L.javaDl[i].y, L.javaDl[i].w, L.javaDl[i].h);
    }

    std::wstring jmsg = L"Своя Java не выбрана — используется автоопределение";
    int jpct = -1;
    if (a.javaSt) {
        std::lock_guard<std::mutex> lk(a.javaSt->mu);
        jmsg = a.javaSt->msg;
        jpct = a.javaSt->pct.load();
    } else if (!a.javaPathCfg.empty()) {
        jmsg = L"Выбрана своя Java: " + a.javaPathCfg;
    }
    ID2D1Brush* jb2 = a.javaSt && a.javaSt->active.load() ? a.brAccent.get() : a.brDim.get();
    Text(a, jmsg, a.fTiny.get(), jb2, x + 20, L.javaTop + 236, L.cw - 40, 16);
    if (jpct >= 0) {
        float barW = L.cw - 40.f;
        FillRound(a, x + 20, L.javaTop + 256, barW, 6, 3, a.brInput.get());
        float filled = barW * std::clamp(jpct / 100.f, 0.f, 1.f);
        if (filled > 0.5f) FillRound(a, x + 20, L.javaTop + 256, filled, 6, 3, a.brAccent.get());
    }

    // Запуск и аккаунт
    card(L.runTop, L.runH, L"Запуск и аккаунт");
    label(L.user, L"Имя игрока (оффлайн-режим)");
    DrawField(a, L.user, a.username, L"Player", a.setFocus == 304);
    label(L.javaArgs, L"Доп. аргументы Java");
    DrawField(a, L.javaArgs, a.javaArgs, L"-XX:+UseG1GC …", a.setFocus == 305);
    label(L.gameArgs, L"Доп. аргументы игры");
    DrawField(a, L.gameArgs, a.gameArgs, L"--fullscreen", a.setFocus == 306);

    // Язык
    card(L.langTop, L.langH, L"Язык");
    for (int i = 0; i < 6; ++i)
        DrawSeg(a, L.langBtn[i], LangName(i), g_lang.load(std::memory_order_relaxed) == i,
                a.hoverWidget == 360 + i);

    // Интерфейс
    card(L.uiTop, L.uiH, L"Интерфейс");
    Text(a, L"Тема оформления", a.fTiny.get(), a.brDim.get(), x + 20, L.uiTop + 54, L.cw - 40, 14);
    DrawSeg(a, L.themeDark, L"Тёмная", a.theme != L"light", a.hoverWidget == 310);
    DrawSeg(a, L.themeLight, L"Светлая", a.theme == L"light", a.hoverWidget == 311);

    Text(a, L"Основной цвет", a.fTiny.get(), a.brDim.get(), x + 20, L.uiTop + 122, 200, 14);
    for (int i = 0; i < 6; ++i)
        DrawSwatch(a, L.accentSw[i], kAccentPresets[i], a.accent, a.hoverWidget == 320 + i);
    DrawField(a, L.accentEntry, a.accentEdit, L"#RRGGBB", a.setFocus == 326);
    DrawMiniBtn(a, L.accentApply, L"ок", a.hoverWidget == 327);

    Text(a, L"Второстепенный цвет", a.fTiny.get(), a.brDim.get(), x + 20, L.uiTop + 188, 200, 14);
    for (int i = 0; i < 6; ++i)
        DrawSwatch(a, L.accent2Sw[i], kAccent2Presets[i], a.accent2, a.hoverWidget == 330 + i);
    DrawField(a, L.accent2Entry, a.accent2Edit, L"#RRGGBB", a.setFocus == 336);
    DrawMiniBtn(a, L.accent2Apply, L"ок", a.hoverWidget == 337);

    Text(a, std::wstring(L"Шрифт интерфейса: ") + EffectiveFontName(a), a.fTiny.get(),
         a.brDim.get(), x + 20, L.uiTop + 248, L.cw - 40, 14);

    // Сохранить
    bool sv = a.hoverWidget == 340;
    FillRound(a, L.save.x, L.save.y, L.save.w, L.save.h, 12,
              sv ? a.brAccentHi.get() : a.brAccent.get());
    Text(a, L"Сохранить настройки", a.fBtn.get(), a.brWhite.get(),
         L.save.x, L.save.y, L.save.w, L.save.h);

    a.rt->PopAxisAlignedClip();

    if (a.settingsScrollMax > 0.5f) {
        float trackY = viewTop, trackH = viewBottom - viewTop;
        float thumbH = std::max(30.f, trackH * (trackH / L.contentH));
        float thumbY = trackY + (trackH - thumbH) *
                       (a.settingsScrollMax > 0.f ? a.settingsScrollY / a.settingsScrollMax : 0.f);
        FillRound(a, x + L.cw + 6, thumbY, 4, thumbH, 2, a.brBorder.get());
    }
}

static void DrawPage(App& a, int page, float x, float y, float w) {
    switch (page) {
    case 0: DrawHome(a, x, y, w); break;
    case 1: DrawMods(a, x, y, w); break;
    case 2: DrawCatalog(a, x, y, w); break;
    default: DrawSettings(a, x, y, w); break;
    }
}

// ── сайдбар ───────────────────────────────────────────────────────────────────
static void DrawSidebar(App& a) {
    float H = a.rt->GetSize().height;
    a.rt->FillRectangle(D2D1::RectF(0, 0, SIDE_W, H), a.brSide.get());
    a.rt->DrawLine(D2D1::Point2F(SIDE_W - 0.5f, 0), D2D1::Point2F(SIDE_W - 0.5f, H),
                   a.brBorder.get(), 1.f);

    // логотип: иконка приложения (midnight.png) в скруглённой плитке,
    // если файла нет — прежний градиент с буквой «M»
    if (a.logoBmp) {
        D2D1_RECT_F lr = D2D1::RectF(22, 22, 62, 62);
        if (!a.logoGeo && a.d2d)
            a.d2d->CreateRoundedRectangleGeometry(D2D1::RoundedRect(lr, 12.f, 12.f),
                                                  a.logoGeo.put());
        if (!a.logoLayer) a.rt->CreateLayer(nullptr, a.logoLayer.put());
        if (a.logoGeo && a.logoLayer) {
            a.rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), a.logoGeo.get(),
                                                  D2D1_ANTIALIAS_MODE_PER_PRIMITIVE),
                            a.logoLayer.get());
            a.rt->DrawBitmap(a.logoBmp.get(), lr, 1.f,
                             D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            a.rt->PopLayer();
        } else {
            a.rt->DrawBitmap(a.logoBmp.get(), lr, 1.f,
                             D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
        StrokeRound(a, 22, 22, 40, 40, 12, a.brBorder.get());
    } else {
        FillRound(a, 22, 22, 40, 40, 12, a.brGrad.get());
        Text(a, L"M", a.fLogoMark.get(), a.brWhite.get(), 22, 22, 40, 40);
    }
    Text(a, L"MIDNIGHT", a.fLogo.get(), a.brText.get(), 72, 26, 140, 22);
    Text(a, L"LAUNCHER", a.fLogoSub.get(), a.brDim.get(), 73, 48, 140, 14);

    Text(a, L"МЕНЮ", a.fTiny.get(), a.brDim.get(), 24, 98, 100, 14);

    for (int i = 0; i < 4; ++i) {
        float y = NAV_Y0 + i * NAV_STEP;
        bool active = (a.page == i);
        bool hover = (a.hoverNav == i);
        if (active) {
            FillRound(a, SIDE_PAD, y, SIDE_W - 2 * SIDE_PAD, NAV_H, 10, a.brCardHi.get());
            FillRound(a, SIDE_PAD, y + 10, 3, NAV_H - 20, 1.5f, a.brAccent.get());
        } else if (hover) {
            FillRound(a, SIDE_PAD, y, SIDE_W - 2 * SIDE_PAD, NAV_H, 10, a.brCard.get());
        }
        ID2D1Brush* ic = active ? a.brAccent.get() : (hover ? a.brText.get() : a.brDim.get());
        DrawNavIcon(a, i, SIDE_PAD + 16, y + 10, 20, ic);
        Text(a, kNav[i], a.fBody.get(), (active || hover) ? a.brText.get() : a.brDim.get(),
             SIDE_PAD + 46, y + 11, 150, 18);
    }

    float cw = SIDE_W - 2 * SIDE_PAD;
    float cy = H - 14 - 118;
    bool online = AuthOnline(a);
    std::wstring name = online ? AuthName(a) : a.username;
    if (name.empty()) name = L"Player";
    bool cardHot = online && a.hoverWidget == 50;
    FillRound(a, SIDE_PAD, cy, cw, 118, 14, cardHot ? a.brCardHi.get() : a.brCard.get());
    StrokeRound(a, SIDE_PAD, cy, cw, 118, 14,
                online ? a.brAccentSoft.get() : a.brBorder.get());

    a.rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(SIDE_PAD + 34, cy + 34), 18.f, 18.f),
                      a.brGrad.get());
    if (a.headBmp && a.skinHeadW > 0) {
        a.rt->DrawBitmap(a.headBmp.get(),
                         D2D1::RectF(SIDE_PAD + 16, cy + 16, SIDE_PAD + 52, cy + 52));
    } else {
        std::wstring av = name.empty() ? L"?" : name.substr(0, 1);
        CharUpperBuffW(av.data(), 1);
        Text(a, av, a.fAvatar.get(), a.brWhite.get(), SIDE_PAD + 16, cy + 24, 36, 20);
    }
    TextRaw(a, name, a.fBody.get(), a.brText.get(), SIDE_PAD + 60, cy + 16, 128, 18);
    a.rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(SIDE_PAD + 63, cy + 44), 3.5f, 3.5f),
                      online ? a.brGreen.get() : a.brYellow.get());
    std::wstring mode = !online ? L"Офлайн-режим"
                        : a.authKind == L"ely" ? L"Ely.by · онлайн"
                                               : L"Microsoft · онлайн";
    Text(a, mode, a.fTiny.get(), online ? a.brGreen.get() : a.brDim.get(),
         SIDE_PAD + 71, cy + 36, 122, 14);

    bool btnHot = a.hoverWidget == 51;
    FillRound(a, SIDE_PAD + 12, cy + 60, cw - 24, 26, 8,
              btnHot ? a.brAccent.get() : a.brCardHi.get());
    Text(a, online ? L"Аккаунт" : L"Войти в аккаунт", a.fBtn.get(),
         btnHot ? a.brWhite.get() : a.brText.get(), SIDE_PAD + 12, cy + 60, cw - 24, 26);
    bool skinHot = a.hoverWidget == 52;
    FillRound(a, SIDE_PAD + 12, cy + 90, cw - 24, 24, 8,
              skinHot ? a.brCardHi.get() : a.brCard.get());
    StrokeRound(a, SIDE_PAD + 12, cy + 90, cw - 24, 24, 8, a.brBorder.get());
    Text(a, L"Сменить скин", a.fBtn.get(), skinHot ? a.brText.get() : a.brDim.get(),
         SIDE_PAD + 12, cy + 90, cw - 24, 24);
}

// ── окна входа ────────────────────────────────────────────────────────────────
struct ModalLayout {
    float pw = 470, ph = 400, px = 0, py = 0;
    MRect panel, c1, c2, p1, p2, cancel;
    MRect open, msCancel;
    MRect user, pass, submit, elyCancel;
    MRect rows[2], add, logout;
};

static int SavedAccounts(const App& a) {
    return (a.ms.online ? 1 : 0) + (a.ely.online ? 1 : 0);
}

static ModalLayout ModalLayoutFor(App& a) {
    ModalLayout m;
    float W = a.rt ? a.rt->GetSize().width : 1180.f;
    float H = a.rt ? a.rt->GetSize().height : 740.f;
    int modal = a.authUi.modal;
    int n = std::max(1, SavedAccounts(a));
    if (modal == 1) { m.pw = 470; m.ph = 400; }
    else if (modal == 2) { m.pw = 478; m.ph = 392; }
    else if (modal == 3) { m.pw = 478; m.ph = 462; }
    else { m.pw = 470; m.ph = 240.f + n * 72.f; }

    m.px = (W - m.pw) / 2.f;
    m.py = (H - m.ph) / 2.f;
    m.panel = { m.px, m.py, m.pw, m.ph };
    float px = m.px, py = m.py, pw = m.pw;

    if (modal == 1) {
        m.c1 = { px + 24, py + 92, pw - 48, 112 };
        m.c2 = { px + 24, py + 212, pw - 48, 112 };
        m.p1 = { px + pw - 24 - 16 - 190, py + 92 + 68, 190, 32 };
        m.p2 = { px + pw - 24 - 16 - 190, py + 212 + 68, 190, 32 };
        m.cancel = { px + pw / 2 - 60, py + 348, 120, 36 };
    } else if (modal == 2) {
        m.open = { px + (pw - 260) / 2, py + 172, 260, 38 };
        m.msCancel = { px + pw / 2 - 80, py + 326, 160, 38 };
    } else if (modal == 3) {
        m.user = { px + 28, py + 116, pw - 56, 42 };
        m.pass = { px + 28, py + 192, pw - 56, 42 };
        m.submit = { px + 28, py + 310, pw - 56, 44 };
        m.elyCancel = { px + pw / 2 - 60, py + 364, 120, 32 };
    } else {
        for (int i = 0; i < 2; ++i)
            m.rows[i] = { px + 24, py + 92 + i * 72.f, pw - 48, 64 };
        m.add = { px + 24, py + 92 + n * 72.f + 8, pw - 48, 40 };
        m.logout = { px + pw / 2 - 80, m.add.y + 50, 160, 30 };
    }
    return m;
}

static void DrawBtn(App& a, const MRect& r, const std::wstring& text, bool primary, bool hot,
                    bool disabled = false) {
    ID2D1Brush* bg;
    if (disabled) bg = a.brCardHi.get();
    else if (primary) bg = hot ? a.brAccentHi.get() : a.brAccent.get();
    else bg = hot ? a.brCardHi.get() : a.brInput.get();
    FillRound(a, r.x, r.y, r.w, r.h, 10, bg);
    if (!primary && !disabled) StrokeRound(a, r.x, r.y, r.w, r.h, 10, a.brBorder.get());
    ID2D1Brush* fg = disabled ? a.brDim.get() : primary ? a.brWhite.get() : a.brText.get();
    Text(a, text, a.fBtn.get(), fg, r.x, r.y, r.w, r.h);
}

static void DrawAuthBar(App& a, float x, float y, float w, float pct) {
    FillRound(a, x, y, w, 6, 3, a.brInput.get());
    float f = std::clamp(pct, 0.f, 1.f) * w;
    if (f > 0.5f) FillRound(a, x, y, f, 6, 3, a.brAccent.get());
}

static void DrawAuthModal(App& a) {
    int m = a.authUi.modal;
    if (!m) return;
    float W = a.rt->GetSize().width, H = a.rt->GetSize().height;
    a.rt->FillRectangle(D2D1::RectF(0, 0, W, H), a.brOverlay.get());

    ModalLayout L = ModalLayoutFor(a);
    FillRound(a, L.px, L.py, L.pw, L.ph, 16, a.brSide.get());
    StrokeRound(a, L.px, L.py, L.pw, L.ph, 16, a.brBorder.get());
    float px = L.px, py = L.py, pw = L.pw;

    auto hover = [&](int id) { return a.hoverWidget == id; };

    if (m == 1) {
        Text(a, L"Вход в аккаунт", a.fModal.get(), a.brText.get(), px + 26, py + 22, pw - 52, 32);
        Text(a, L"Выберите, как войти в игру", a.fModalSub.get(), a.brDim.get(),
             px + 26, py + 58, pw - 52, 18);

        FillRound(a, L.c1.x, L.c1.y, L.c1.w, L.c1.h, 14, a.brCard.get());
        StrokeRound(a, L.c1.x, L.c1.y, L.c1.w, L.c1.h, 14, a.brBorder.get());
        Text(a, L"Microsoft", a.fH2.get(), a.brText.get(), L.c1.x + 16, L.c1.y + 12, 220, 22);
        Text(a, L"Лицензионный аккаунт Minecraft — вход по коду на сайте Microsoft.",
             a.fDesc.get(), a.brDim.get(), L.c1.x + 16, L.c1.y + 38, L.c1.w - 32, 18);
        DrawBtn(a, L.p1, L"Войти через Microsoft", true, hover(601));

        FillRound(a, L.c2.x, L.c2.y, L.c2.w, L.c2.h, 14, a.brCard.get());
        StrokeRound(a, L.c2.x, L.c2.y, L.c2.w, L.c2.h, 14, a.brBorder.get());
        Text(a, L"Ely.by", a.fH2.get(), a.brText.get(), L.c2.x + 16, L.c2.y + 12, 220, 22);
        Text(a, L"Аккаунт для неофициальных серверов: свой ник, скин и плащ.",
             a.fDesc.get(), a.brDim.get(), L.c2.x + 16, L.c2.y + 38, L.c2.w - 32, 18);
        DrawBtn(a, L.p2, L"Войти через Ely.by", false, hover(602));

        Text(a, L"Отмена", a.fBtn.get(), hover(603) ? a.brText.get() : a.brDim.get(),
             L.cancel.x, L.cancel.y, L.cancel.w, L.cancel.h);
    } else if (m == 2) {
        Text(a, L"Вход через Microsoft", a.fModal.get(), a.brText.get(), px + 26, py + 22,
             pw - 52, 32);
        Text(a, L"Откройте сайт и введите код ниже", a.fModalSub.get(), a.brDim.get(),
             px + 26, py + 58, pw - 52, 18);

        FillRound(a, px + 24, py + 88, pw - 48, 160, 14, a.brCard.get());
        StrokeRound(a, px + 24, py + 88, pw - 48, 160, 14, a.brBorder.get());
        Text(a, a.authUi.code.empty() ? L"•••• ••••" : a.authUi.code, a.fCode.get(),
             a.brAccent.get(), px + 24, py + 96, pw - 48, 46);
        Text(a, a.authUi.url, a.fBody.get(), a.brDim.get(), px + 24, py + 144, pw - 48, 20);
        DrawBtn(a, L.open, L"Открыть microsoft.com/link", false, hover(610));

        ID2D1Brush* sc = a.authUi.error ? a.brRed.get() : a.brDim.get();
        Text(a, a.authUi.status, a.fSmall.get(), sc, px + 28, py + 258, pw - 56, 36);
        DrawAuthBar(a, px + 28, py + 302, pw - 56, a.authUi.pct);

        Text(a, L"Отмена", a.fBtn.get(), hover(611) ? a.brText.get() : a.brDim.get(),
             L.msCancel.x, L.msCancel.y, L.msCancel.w, L.msCancel.h);
    } else if (m == 3) {
        Text(a, L"Вход через Ely.by", a.fModal.get(), a.brText.get(), px + 26, py + 22,
             pw - 52, 32);
        Text(a, L"Тот же ник и пароль, что на ely.by", a.fModalSub.get(), a.brDim.get(),
             px + 26, py + 58, pw - 52, 18);

        Text(a, L"Ник или e-mail", a.fTiny.get(), a.brDim.get(), px + 28, py + 96, pw - 56, 14);
        FillRound(a, L.user.x, L.user.y, L.user.w, L.user.h, 10, a.brInput.get());
        StrokeRound(a, L.user.x, L.user.y, L.user.w, L.user.h, 10,
                    a.authUi.focusField == 1 ? a.brAccent.get() : a.brBorder.get());
        bool userEmpty = a.authUi.user.empty();
        if (userEmpty)
            Text(a, L"например: Steve", a.fBody.get(), a.brDim.get(),
                 L.user.x + 14, L.user.y + 11, L.user.w - 28, 20);
        else
            TextRaw(a, a.authUi.user, a.fBody.get(), a.brText.get(),
                    L.user.x + 14, L.user.y + 11, L.user.w - 28, 20);

        Text(a, L"Пароль", a.fTiny.get(), a.brDim.get(), px + 28, py + 170, pw - 56, 14);
        FillRound(a, L.pass.x, L.pass.y, L.pass.w, L.pass.h, 10, a.brInput.get());
        StrokeRound(a, L.pass.x, L.pass.y, L.pass.w, L.pass.h, 10,
                    a.authUi.focusField == 2 ? a.brAccent.get() : a.brBorder.get());
        bool passEmpty = a.authUi.pass.empty();
        if (passEmpty)
            Text(a, L"пароль или пароль:код", a.fBody.get(), a.brDim.get(),
                 L.pass.x + 14, L.pass.y + 11, L.pass.w - 28, 20);
        else
            TextRaw(a, std::wstring(a.authUi.pass.size(), L'•'), a.fBody.get(), a.brText.get(),
                    L.pass.x + 14, L.pass.y + 11, L.pass.w - 28, 20);

        ID2D1Brush* sc = a.authUi.error ? a.brRed.get() : a.brDim.get();
        Text(a, a.authUi.status.empty() ? L"Пароль не сохраняется — только токен сессии"
                                        : a.authUi.status,
             a.fSmall.get(), sc, px + 28, py + 246, pw - 56, 34);
        DrawAuthBar(a, px + 28, py + 290, pw - 56, a.authUi.pct);
        DrawBtn(a, L.submit, a.authUi.busy ? L"Вход…" : L"Войти", true, hover(622),
                a.authUi.busy);
        Text(a, L"Отмена", a.fBtn.get(), hover(623) ? a.brText.get() : a.brDim.get(),
             L.elyCancel.x, L.elyCancel.y, L.elyCancel.w, L.elyCancel.h);
    } else {
        Text(a, L"Аккаунты", a.fModal.get(), a.brText.get(), px + 26, py + 22, pw - 52, 32);
        Text(a, L"Переключение не требует пароля заново", a.fModalSub.get(), a.brDim.get(),
             px + 26, py + 58, pw - 52, 18);

        int idx = 0;
        auto row = [&](bool online, const std::wstring& provider, const std::wstring& user,
                       bool active, int id) {
            if (!online) return;
            MRect r = L.rows[idx];
            FillRound(a, r.x, r.y, r.w, r.h, 12,
                      active ? a.brAccentSoft.get()
                             : (hover(id) ? a.brCardHi.get() : a.brCard.get()));
            StrokeRound(a, r.x, r.y, r.w, r.h, 12,
                        active ? a.brAccent.get() : a.brBorder.get());
            a.rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(r.x + 32, r.y + 32), 17.f, 17.f),
                              provider == L"Microsoft" ? a.brAccent.get() : a.brPink.get());
            std::wstring initial = user.empty() ? L"?" : user.substr(0, 1);
            CharUpperBuffW(initial.data(), 1);
            Text(a, initial, a.fAvatar.get(), a.brWhite.get(), r.x + 15, r.y + 22, 34, 20);
            Text(a, user, a.fBody.get(), a.brText.get(), r.x + 58, r.y + 13, r.w - 170, 18);
            Text(a, provider, a.fTiny.get(), a.brDim.get(), r.x + 58, r.y + 34, r.w - 170, 14);
            Text(a, active ? L"Активный" : L"Войти", a.fBtn.get(),
                 active ? a.brDim.get() : a.brAccentHi.get(),
                 r.x + r.w - 112, r.y + 18, 96, 28);
            ++idx;
        };
        row(a.ms.online, L"Microsoft", a.ms.username, a.authKind == L"ms", 630);
        row(a.ely.online, L"Ely.by", a.ely.username, a.authKind == L"ely", 631);
        if (!a.ms.online && !a.ely.online)
            Text(a, L"Сохранённых аккаунтов нет", a.fSmall.get(), a.brDim.get(),
                 px + 28, py + 108, pw - 56, 20);

        DrawBtn(a, L.add, L"+  Добавить аккаунт", true, hover(632));
        Text(a, L"Выйти из аккаунта", a.fBtn.get(), hover(633) ? a.brRed.get() : a.brDim.get(),
             L.logout.x, L.logout.y, L.logout.w, L.logout.h);
    }
}

// ── окно «Новая сборка» ───────────────────────────────────────────────────────
struct BuildModalLayout {
    float px = 0, py = 0, pw = 520.f, ph = 430.f;
    MRect panel, nameR, mcR, chips[5], create, cancel;
};

static BuildModalLayout BuildModalLayoutFor(App& a) {
    BuildModalLayout L;
    float W = a.rt ? a.rt->GetSize().width : 1180.f;
    float H = a.rt ? a.rt->GetSize().height : 740.f;
    L.px = (W - L.pw) / 2.f;
    L.py = (H - L.ph) / 2.f;
    L.panel = { L.px, L.py, L.pw, L.ph };
    const float pad = 24.f;
    float inner = L.pw - 2 * pad;
    L.nameR = { L.px + pad, L.py + 108.f, inner, 42.f };
    L.mcR = { L.px + pad, L.py + 186.f, inner, 42.f };
    float cw = (inner - 4 * 8.f) / 5.f;
    for (int i = 0; i < 5; ++i)
        L.chips[i] = { L.px + pad + i * (cw + 8.f), L.py + 264.f, cw, 34.f };
    L.create = { L.px + pad, L.py + 366.f, inner - 136.f, 44.f };
    L.cancel = { L.px + L.pw - pad - 120.f, L.py + 368.f, 120.f, 40.f };
    return L;
}

static void DrawBuildModal(App& a) {
    if (!a.buildUi.open) return;
    float W = a.rt->GetSize().width, H = a.rt->GetSize().height;
    a.rt->FillRectangle(D2D1::RectF(0, 0, W, H), a.brOverlay.get());

    BuildModalLayout L = BuildModalLayoutFor(a);
    FillRound(a, L.px, L.py, L.pw, L.ph, 16, a.brSide.get());
    StrokeRound(a, L.px, L.py, L.pw, L.ph, 16, a.brBorder.get());
    auto hover = [&](int id) { return a.hoverWidget == id; };

    Text(a, L"Новая сборка", a.fModal.get(), a.brText.get(), L.px + 26, L.py + 20, 300, 32);
    Text(a, L"Версия Minecraft скачается сразу после создания",
         a.fModalSub.get(), a.brDim.get(), L.px + 26, L.py + 56, L.pw - 52, 18);
    Text(a, L"Название", a.fTiny.get(), a.brDim.get(), L.px + 26, L.py + 88, 200, 14);
    DrawField(a, L.nameR, a.buildUi.name, L"Например: Выживание", a.buildUi.focus == 1);
    Text(a, L"Версия Minecraft", a.fTiny.get(), a.brDim.get(), L.px + 26, L.py + 166, 200, 14);
    DrawField(a, L.mcR, a.buildUi.mc, L"1.21.1", a.buildUi.focus == 2);
    Text(a, L"Ядро", a.fTiny.get(), a.brDim.get(), L.px + 26, L.py + 244, 200, 14);
    static const wchar_t* loads[5] = { L"Vanilla", L"Fabric", L"Quilt", L"Forge", L"NeoForge" };
    for (int i = 0; i < 5; ++i)
        DrawSeg(a, L.chips[i], loads[i], a.buildUi.loader == i, hover(710 + i));

    std::wstring st = a.buildUi.status.empty()
        ? L"Папка сборки и файлы игры скачаются автоматически."
        : a.buildUi.status;
    ID2D1Brush* sc = a.brDim.get();
    if (st.rfind(L"Не удалось", 0) == 0 || st.rfind(L"Ошибка", 0) == 0) sc = a.brRed.get();
    else if (st.rfind(L"Введите", 0) == 0 || st.rfind(L"Укажите", 0) == 0) sc = a.brYellow.get();
    Text(a, st, a.fBodyWrap.get(), sc, L.px + 26, L.py + 306, L.pw - 52, 36);

    if (a.buildUi.busy) DrawAuthBar(a, L.px + 26, L.py + 350, L.pw - 52, 0.6f);
    DrawBtn(a, L.create, a.buildUi.busy ? L"Создаю…" : L"Создать сборку", true, hover(720),
            a.buildUi.busy);
    DrawBtn(a, L.cancel, L"Отмена", false, hover(721), a.buildUi.busy);
}

// ── окно «Куда установить» ────────────────────────────────────────────────────
struct InstallModalLayout {
    float px = 0, py = 0, pw = 560.f, ph = 300.f;
    MRect panel, rows[6], cancel;
    int count = 0;
};

static InstallModalLayout InstallModalLayoutFor(App& a) {
    InstallModalLayout L;
    int n = std::clamp((int)a.installUi.cand.size(), 0, 6);
    L.count = n;
    L.ph = 196.f + (n ? n * 60.f : 76.f);
    float W = a.rt ? a.rt->GetSize().width : 1180.f;
    float H = a.rt ? a.rt->GetSize().height : 740.f;
    L.px = (W - L.pw) / 2.f;
    L.py = (H - L.ph) / 2.f;
    L.panel = { L.px, L.py, L.pw, L.ph };
    for (int i = 0; i < n; ++i)
        L.rows[i] = { L.px + 24.f, L.py + 112.f + i * 60.f, L.pw - 48.f, 52.f };
    L.cancel = { L.px + L.pw / 2.f - 70.f, L.py + L.ph - 52.f, 140.f, 36.f };
    return L;
}

static void DrawInstallModal(App& a) {
    if (!a.installUi.open || !a.installUi.item) return;
    float W = a.rt->GetSize().width, H = a.rt->GetSize().height;
    a.rt->FillRectangle(D2D1::RectF(0, 0, W, H), a.brOverlay.get());

    InstallModalLayout L = InstallModalLayoutFor(a);
    FillRound(a, L.px, L.py, L.pw, L.ph, 16, a.brSide.get());
    StrokeRound(a, L.px, L.py, L.pw, L.ph, 16, a.brBorder.get());
    auto hover = [&](int id) { return a.hoverWidget == id; };

    static const wchar_t* types[4] = { L"мод", L"ресурспак", L"шейдер", L"датапак" };
    int tab = std::clamp(a.installUi.item->type, 0, 3);

    Text(a, L"Куда установить?", a.fModal.get(), a.brText.get(), L.px + 26, L.py + 20, 340, 32);
    Text(a, a.installUi.item->name + L"   ·   " + types[tab], a.fModalSub.get(), a.brDim.get(),
         L.px + 26, L.py + 56, L.pw - 52, 18);

    if (a.installUi.busy) {
        Text(a, a.installUi.status, a.fSmall.get(), a.brDim.get(), L.px + 26, L.py + 116,
             L.pw - 52, 40);
    } else if (a.installUi.cand.empty()) {
        Text(a, L"Нет подходящих сборок", a.fH2.get(), a.brYellow.get(),
             L.px + 26, L.py + 112, L.pw - 52, 24);
        Text(a, tab == 0
                    ? L"Мод не поддерживает ни одну из ваших сборок: нужны та же версия "
                      L"Minecraft и подходящее ядро (Fabric, Forge, NeoForge или Quilt)."
                    : L"Создайте сборку на главной странице.",
             a.fBodyWrap.get(), a.brDim.get(), L.px + 26, L.py + 146, L.pw - 52, 50);
        if (!a.installUi.status.empty())
            Text(a, a.installUi.status, a.fTiny.get(), a.brYellow.get(),
                 L.px + 26, L.py + 196, L.pw - 52, 16);
    } else {
        for (int i = 0; i < L.count; ++i) {
            int idx = a.installUi.cand[(size_t)i];
            if (idx < 0 || (size_t)idx >= a.profiles.size()) continue;
            const Profile& p = a.profiles[(size_t)idx];
            MRect r = L.rows[i];
            bool hot = hover(730 + i);
            FillRound(a, r.x, r.y, r.w, r.h, 12, hot ? a.brCardHi.get() : a.brCard.get());
            StrokeRound(a, r.x, r.y, r.w, r.h, 12, hot ? a.brAccent.get() : a.brBorder.get());
            Text(a, p.name, a.fRow.get(), a.brText.get(), r.x + 16, r.y + 8, r.w - 200, 20);
            std::wstring meta = LoaderName(p.loader) + L" " + p.mc;
            if (i < (int)a.installUi.labels.size() && !a.installUi.labels[(size_t)i].empty())
                meta += L"   ·   " + a.installUi.labels[(size_t)i];
            Text(a, meta, a.fTiny.get(), a.brDim.get(), r.x + 16, r.y + 30, r.w - 150, 14);
            Text(a, L"Установить", a.fBtn.get(), hot ? a.brAccentHi.get() : a.brAccent.get(),
                 r.x + r.w - 128, r.y + 12, 112, 28);
        }
        if (!a.installUi.status.empty())
            Text(a, a.installUi.status, a.fTiny.get(), a.brYellow.get(),
                 L.px + 26, L.cancel.y - 34, L.pw - 52, 16);
    }

    DrawBtn(a, L.cancel, L"Отмена", false, hover(729), false);
}

// ── подтверждение удаления сборки ─────────────────────────────────────────────
struct DeleteModalLayout {
    float px = 0, py = 0, pw = 440.f, ph = 222.f;
    MRect panel, del, cancel;
};

static DeleteModalLayout DeleteModalLayoutFor(App& a) {
    DeleteModalLayout L;
    float W = a.rt ? a.rt->GetSize().width : 1180.f;
    float H = a.rt ? a.rt->GetSize().height : 740.f;
    L.px = (W - L.pw) / 2.f;
    L.py = (H - L.ph) / 2.f;
    L.panel = { L.px, L.py, L.pw, L.ph };
    L.cancel = { L.px + L.pw - 26.f - 120.f, L.py + L.ph - 56.f, 120.f, 38.f };
    L.del = { L.cancel.x - 10.f - 130.f, L.cancel.y, 130.f, 38.f };
    return L;
}

static void DrawDeleteModal(App& a) {
    if (!a.delUi.open) return;
    float W = a.rt->GetSize().width, H = a.rt->GetSize().height;
    a.rt->FillRectangle(D2D1::RectF(0, 0, W, H), a.brOverlay.get());

    DeleteModalLayout L = DeleteModalLayoutFor(a);
    FillRound(a, L.px, L.py, L.pw, L.ph, 16, a.brSide.get());
    StrokeRound(a, L.px, L.py, L.pw, L.ph, 16, a.brBorder.get());
    auto hover = [&](int id) { return a.hoverWidget == id; };

    std::wstring name;
    int idx = a.delUi.idx;
    if (idx >= 0 && idx < (int)a.profiles.size()) name = a.profiles[(size_t)idx].name;

    Text(a, L"Удалить сборку?", a.fModal.get(), a.brText.get(),
         L.px + 26, L.py + 20, L.pw - 52, 32);
    Text(a, L"«" + name + L"»", a.fH2.get(), a.brText.get(),
         L.px + 26, L.py + 60, L.pw - 52, 22);
    Text(a, L"Папка сборки со всеми модами, мирами и настройками будет удалена "
            L"без возможности восстановления.",
         a.fBodyWrap.get(), a.brDim.get(), L.px + 26, L.py + 94, L.pw - 52, 44);
    Text(a, L"Сборка также исчезнет из списка.", a.fTiny.get(), a.brDim.get(),
         L.px + 26, L.py + 144, L.pw - 52, 16);

    bool dh = hover(760);
    FillRound(a, L.del.x, L.del.y, L.del.w, L.del.h, 10,
              dh ? a.brRed.get() : a.brCardHi.get());
    Text(a, L"Удалить", a.fBtn.get(), dh ? a.brWhite.get() : a.brRed.get(),
         L.del.x, L.del.y, L.del.w, L.del.h);
    DrawBtn(a, L.cancel, L"Отмена", false, hover(761), false);
}

// ── обзор сборки: содержимое папки и список модов ────────────────────────────
static unsigned long long DirBytes(const fs::path& dir, unsigned long long* files = nullptr) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return 0;
    unsigned long long total = 0, count = 0;
    for (fs::recursive_directory_iterator
             it(dir, fs::directory_options::skip_permission_denied, ec),
         end;
         it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        std::error_code fe;
        if (!it->is_regular_file(fe) || fe) continue;
        unsigned long long sz = (unsigned long long)it->file_size(fe);
        if (fe) continue;
        total += sz;
        ++count;
    }
    if (files) *files += count;
    return total;
}

static void OpenOverview(App& a) {
    if (!a.hasProfile) {
        a.status = L"Сначала выберите сборку";
        a.statusCol = C_YELLOW;
        InvalidateRect(a.hwnd, nullptr, FALSE);
        return;
    }
    RefreshMods(a);

    OverviewData o;
    o.open = true;
    o.idx = a.profileIdx;
    o.name = a.profile.name;
    o.loader = a.profile.loader;
    o.mc = a.profile.mc;
    o.loaderVer = a.profile.loaderVer;
    o.dir = a.profile.dir;
    o.mods = a.modsList;
    for (auto& m : o.mods) o.modsSize += m.size;

    std::error_code ec;
    fs::path dir(o.dir);
    o.total = DirBytes(dir, &o.files);

    fs::path saves = dir / L"saves";
    if (fs::is_directory(saves, ec)) {
        for (auto& e : fs::directory_iterator(saves, ec)) {
            std::error_code de;
            if (e.is_directory(de) && !de) ++o.worlds;
        }
    }
    if (fs::is_directory(dir, ec)) {
        for (auto& e : fs::directory_iterator(dir, ec)) {
            std::error_code de;
            if (!e.is_directory(de) || de) continue;
            unsigned long long sz = DirBytes(e.path());
            if (sz) o.folders.push_back({ e.path().filename().wstring(), sz });
        }
    }
    std::sort(o.folders.begin(), o.folders.end(),
              [](const auto& x, const auto& y) { return x.second > y.second; });
    for (auto& f : o.folders)
        if (f.first == L"saves") { o.savesSize = f.second; break; }

    a.overview = std::move(o);
    a.status = L"Обзор сборки «" + a.overview.name + L"»";
    a.statusCol = C_DIM;
    InvalidateRect(a.hwnd, nullptr, FALSE);
}

struct OverviewLayout {
    float px = 0, py = 0, pw = 820.f, ph = 620.f;
    MRect panel, close, folder, mods, done;
    MRect tiles[4];
    float listX = 0, listW = 0, listTop = 0, listBottom = 0;
};

static OverviewLayout OverviewLayoutFor(App& a) {
    OverviewLayout L;
    float W = a.rt ? a.rt->GetSize().width : 1180.f;
    float H = a.rt ? a.rt->GetSize().height : 740.f;
    L.pw = std::min(820.f, std::max(560.f, W - 60.f));
    L.ph = std::clamp(H - 40.f, 480.f, 620.f);
    L.px = (W - L.pw) / 2.f;
    L.py = (H - L.ph) / 2.f;
    L.panel = { L.px, L.py, L.pw, L.ph };
    const float pad = 26.f;
    L.close = { L.px + L.pw - 54.f, L.py + 16.f, 32.f, 32.f };
    float inner = L.pw - 2 * pad;
    float tw = (inner - 3 * 10.f) / 4.f;
    for (int i = 0; i < 4; ++i)
        L.tiles[i] = { L.px + pad + i * (tw + 10.f), L.py + 106.f, tw, 64.f };
    L.folder = { L.px + L.pw - pad - 118.f, L.py + 182.f, 118.f, 24.f };
    L.listX = L.px + pad;
    L.listW = inner - 8.f;
    L.listTop = L.py + 286.f;
    L.listBottom = L.py + L.ph - 66.f;
    L.done = { L.px + L.pw - pad - 110.f, L.py + L.ph - 48.f, 110.f, 34.f };
    L.mods = { L.done.x - 10.f - 150.f, L.done.y, 150.f, 34.f };
    return L;
}

static void DrawOverviewModal(App& a) {
    if (!a.overview.open) return;
    OverviewData& o = a.overview;
    float W = a.rt->GetSize().width, H = a.rt->GetSize().height;
    a.rt->FillRectangle(D2D1::RectF(0, 0, W, H), a.brOverlay.get());

    OverviewLayout L = OverviewLayoutFor(a);
    FillRound(a, L.px, L.py, L.pw, L.ph, 16, a.brSide.get());
    StrokeRound(a, L.px, L.py, L.pw, L.ph, 16, a.brBorder.get());
    auto hover = [&](int id) { return a.hoverWidget == id; };
    const float pad = 26.f;
    float inner = L.pw - 2 * pad;

    Text(a, L"Обзор сборки", a.fModal.get(), a.brText.get(), L.px + pad, L.py + 18, 300, 32);
    Text(a, Ellipsize(a, o.name, a.fH2.get(), inner - 60), a.fH2.get(), a.brText.get(),
         L.px + pad, L.py + 54, inner - 60, 22);

    std::wstring meta = LoaderName(o.loader);
    std::wstring mcv = o.mc.empty() ? o.loaderVer : o.mc;
    if (!mcv.empty()) meta += L" " + mcv;
    if (!o.mc.empty() && !o.loaderVer.empty() && o.loader != L"vanilla")
        meta += L" · " + o.loaderVer;
    meta += L" · " + std::to_wstring(o.mods.size()) + L" " +
            Plural(o.mods.size(), L"мод", L"мода", L"модов");
    Text(a, Ellipsize(a, meta, a.fSmall.get(), inner - 60), a.fSmall.get(), a.brDim.get(),
         L.px + pad, L.py + 80, inner - 60, 18);

    bool closeHot = hover(770);
    if (closeHot) FillRound(a, L.close.x, L.close.y, L.close.w, L.close.h, 8, a.brCardHi.get());
    Text(a, L"✕", a.fTinyC.get(), closeHot ? a.brRed.get() : a.brDim.get(),
         L.close.x, L.close.y, L.close.w, L.close.h);

    unsigned long long rest = o.total > o.modsSize + o.savesSize
        ? o.total - o.modsSize - o.savesSize : 0;
    struct TileText { const wchar_t* cap; std::wstring val, sub; };
    TileText tiles[4] = {
        { L"ВСЕГО", FmtSize(o.total),
          std::to_wstring(o.files) + L" " +
              Plural(o.files, L"файл", L"файла", L"файлов") },
        { L"МОДЫ", FmtSize(o.modsSize),
          std::to_wstring(o.mods.size()) + L" " +
              Plural(o.mods.size(), L"мод", L"мода", L"модов") },
        { L"МИРЫ", FmtSize(o.savesSize),
          std::to_wstring(o.worlds) + L" " +
              Plural((unsigned long long)o.worlds, L"мир", L"мира", L"миров") },
        { L"ПРОЧЕЕ", FmtSize(rest), L"конфиги и логи" },
    };
    for (int i = 0; i < 4; ++i) {
        const MRect& r = L.tiles[i];
        FillRound(a, r.x, r.y, r.w, r.h, 12, a.brCard.get());
        StrokeRound(a, r.x, r.y, r.w, r.h, 12, a.brBorder.get());
        Text(a, tiles[i].cap, a.fTiny.get(), a.brDim.get(), r.x + 14, r.y + 9, r.w - 28, 14);
        Text(a, Ellipsize(a, tiles[i].val, a.fModal.get(), r.w - 28), a.fModal.get(),
             a.brText.get(), r.x + 14, r.y + 24, r.w - 28, 28);
        Text(a, tiles[i].sub, a.fTiny.get(), a.brDim.get(), r.x + 14, r.y + 49, r.w - 28, 13);
    }

    Text(a, L"ПАПКА СБОРКИ", a.fTiny.get(), a.brDim.get(), L.px + pad, L.py + 184, 180, 14);
    bool folderHot = hover(771);
    if (folderHot) FillRound(a, L.folder.x, L.folder.y, L.folder.w, L.folder.h, 6, a.brCardHi.get());
    Text(a, L"Открыть папку", a.fTinyC.get(), folderHot ? a.brAccentHi.get() : a.brDim.get(),
         L.folder.x, L.folder.y, L.folder.w, L.folder.h);
    Text(a, Ellipsize(a, o.dir, a.fTiny.get(), inner - 132), a.fTiny.get(), a.brText.get(),
         L.px + pad, L.py + 202, inner - 132, 14);

    std::wstring chips;
    for (size_t i = 0; i < o.folders.size() && i < 6; ++i) {
        if (!chips.empty()) chips += L"  ·  ";
        chips += o.folders[i].first + L" " + FmtSize(o.folders[i].second);
    }
    if (o.folders.size() > 6)
        chips += L"  ·  ещё " + std::to_wstring(o.folders.size() - 6);
    chips = chips.empty() ? std::wstring(L"Папка пуста") : L"Папки: " + chips;
    Text(a, Ellipsize(a, chips, a.fTiny.get(), inner),
         a.fTiny.get(), a.brDim.get(), L.px + pad, L.py + 222, inner, 14);

    a.rt->DrawLine(D2D1::Point2F(L.px + pad, L.py + 246),
                   D2D1::Point2F(L.px + L.pw - pad, L.py + 246), a.brBorder.get(), 1.f);

    Text(a, L"Моды (" + std::to_wstring(o.mods.size()) + L")",
         a.fH2.get(), a.brText.get(), L.px + pad, L.py + 256, 260, 22);
    std::wstring msz = FmtSize(o.modsSize);
    float mw = MeasureText(a, msz, a.fTiny.get());
    Text(a, msz, a.fTiny.get(), a.brDim.get(), L.px + L.pw - pad - mw,
         L.py + 262, mw + 2, 14);

    float viewH = std::max(0.f, L.listBottom - L.listTop);
    o.scrollMax = std::max(0.f, (float)o.mods.size() * 46.f - viewH);
    o.scrollY = std::clamp(o.scrollY, 0.f, o.scrollMax);

    a.rt->PushAxisAlignedClip(
        D2D1::RectF(L.listX, L.listTop, L.listX + L.listW, L.listBottom),
        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (o.mods.empty()) {
        Text(a, L"В этой сборке нет модов", a.fSmall.get(), a.brDim.get(),
             L.listX + 2, L.listTop + 8, L.listW - 4, 20);
    } else {
        float my = L.listTop - o.scrollY;
        for (auto& m : o.mods) {
            if (my + 40.f >= L.listTop && my <= L.listBottom) {
                FillRound(a, L.listX, my, L.listW, 40.f, 8, a.brCard.get());
                std::wstring nm = m.name;
                if (nm.size() > 4 && nm.substr(nm.size() - 4) == L".jar")
                    nm.resize(nm.size() - 4);
                Text(a, Ellipsize(a, nm, a.fBody.get(), L.listW - 130.f),
                     a.fBody.get(), a.brText.get(), L.listX + 14.f, my + 10.f,
                     L.listW - 130.f, 20.f);
                std::wstring sz = FmtSize(m.size);
                float sw = MeasureText(a, sz, a.fTiny.get());
                Text(a, sz, a.fTiny.get(), a.brDim.get(),
                     L.listX + L.listW - 14.f - sw, my + 13.f, sw + 2.f, 14.f);
            }
            my += 46.f;
        }
    }
    a.rt->PopAxisAlignedClip();

    if (o.scrollMax > 0.5f) {
        float thumbH = std::max(28.f, viewH * viewH / (viewH + o.scrollMax));
        float pos = (viewH - thumbH) * std::clamp(o.scrollY / o.scrollMax, 0.f, 1.f);
        FillRound(a, L.listX + L.listW + 4.f, L.listTop + pos, 4.f, thumbH, 2.f,
                  a.brBorder.get());
    }

    Text(a, L"Дерево зависимостей — в разделе «Моды»", a.fTiny.get(), a.brDim.get(),
         L.px + pad, L.py + L.ph - 40.f, 360.f, 16.f);
    DrawBtn(a, L.mods, L"Открыть «Моды»", false, hover(773), false);
    DrawBtn(a, L.done, L"Закрыть", true, hover(770), false);
}

static void OpenAuthModal(App& a, int modal) {
    if (a.authJob) {
        a.authJob->cancel = true;
        a.authJob.reset();
    }
    ++a.authGen;
    a.authUi = AuthUi{};
    a.authUi.modal = modal;
    if (modal == 3) a.authUi.focusField = 1;
    InvalidateRect(a.hwnd, nullptr, FALSE);
}

static void StartMsLogin(App& a) {
    OpenAuthModal(a, 2);
    auto job = std::make_shared<AuthJob>();
    a.authJob = job;
    a.authUi.busy = true;
    a.authUi.status = L"Запрос кода у Microsoft…";
    std::thread(MsLoginWorker, a.hwnd, a.authGen, job).detach();
}

static void SubmitElyLogin(App& a) {
    if (a.authUi.busy) return;
    std::wstring user = a.authUi.user;
    user.erase(0, user.find_first_not_of(L" \t\r\n"));
    user.erase(user.find_last_not_of(L" \t\r\n") + 1);
    if (user.empty()) {
        a.authUi.error = true;
        a.authUi.status = L"Введите ник или e-mail";
        a.authUi.focusField = 1;
        return;
    }
    if (a.authUi.pass.empty()) {
        a.authUi.error = true;
        a.authUi.status = L"Введите пароль";
        a.authUi.focusField = 2;
        return;
    }
    if (a.ely.clientToken.empty()) a.ely.clientToken = RandomHex(32);
    auto job = std::make_shared<AuthJob>();
    a.authJob = job;
    a.authUi.busy = true;
    a.authUi.error = false;
    a.authUi.focusField = 0;
    std::thread(ElyLoginWorker, a.hwnd, a.authGen, job, user, a.authUi.pass,
                a.ely.clientToken).detach();
}

static void SwitchAccount(App& a, const std::wstring& kind) {
    if ((kind == L"ms" && !a.ms.online) || (kind == L"ely" && !a.ely.online)) return;
    a.authKind = kind;
    RememberKind(a);
    a.status = L"Активный аккаунт: " + AuthName(a);
    a.statusCol = C_GREEN;
    CloseAuthModal(a);
}

static void LogoutActive(App& a) {
    bool wasEly = a.authKind == L"ely";
    if (wasEly) {
        std::wstring tok = a.ely.mcToken, ct = a.ely.clientToken;
        if (!tok.empty()) {
            std::thread([tok, ct] {
                nlohmann::json p;
                p["accessToken"] = U8(tok);
                p["clientToken"] = U8(ct);
                HttpRequest(L"POST", std::wstring(kElyAuth) + L"/auth/invalidate", L"", p.dump(),
                            L"application/json");
            }).detach();
        }
        a.ely = ElyAuth{};
        std::error_code ec;
        fs::remove(ElyPath(), ec);
    } else {
        a.ms = MsAuth{};
        std::error_code ec;
        fs::remove(AuthPath(), ec);
    }
    bool other = wasEly ? a.ms.online : a.ely.online;
    a.authKind = other ? (wasEly ? L"ms" : L"ely") : L"ms";
    RememberKind(a);
    if (other) {
        a.status = L"Активный аккаунт: " + AuthName(a);
        a.statusCol = C_GREEN;
    } else {
        a.status = L"Вы вышли из аккаунта";
        a.statusCol = C_DIM;
    }
    CloseAuthModal(a);
}

// Тихий вход при старте: продлевает сохранённые сессии в фоне.
static void StartAuthRefresh(App& a) {
    auto job = std::make_shared<AuthJob>();
    int gen = a.authGen;
    if (a.ms.online && !a.ms.msRefresh.empty())
        std::thread(MsRefreshWorker, a.hwnd, gen, job, a.ms.msRefresh).detach();
    if (a.ely.online)
        std::thread(ElyRefreshWorker, a.hwnd, gen, job, a.ely.mcToken, a.ely.clientToken)
            .detach();
}

static bool NearCol(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b) {
    return std::fabs(a.r - b.r) < 0.06f && std::fabs(a.g - b.g) < 0.06f &&
           std::fabs(a.b - b.b) < 0.06f;
}

static ID2D1Brush* StatusBrush(App& a) {
    if (NearCol(a.statusCol, C_RED)) return a.brRed.get();
    if (NearCol(a.statusCol, C_GREEN)) return a.brGreen.get();
    if (NearCol(a.statusCol, C_YELLOW)) return a.brYellow.get();
    if (NearCol(a.statusCol, C_ACCENT)) return a.brAccent.get();
    return a.brDim.get();
}

// ── кадр ──────────────────────────────────────────────────────────────────────
static void DrawContent(App& a) {
    float W = a.rt->GetSize().width;
    float H = a.rt->GetSize().height;
    float x0 = SIDE_W + 30.f;
    float w = W - x0 - 30.f;

    float t = Ease(a.animT);
    float dir = (a.page >= a.fromPage) ? 1.f : -1.f;
    float dx = a.animating ? (1.f - t) * 38.f * dir : 0.f;
    float opacity = a.animating ? std::clamp(0.15f + 0.85f * t, 0.f, 1.f) : 1.f;

    a.rt->PushAxisAlignedClip(D2D1::RectF(SIDE_W + 1, 0, W, H - 30.f),
                              D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    bool useLayer = a.animating;
    if (useLayer) {
        if (!a.layer) a.rt->CreateLayer(nullptr, a.layer.put());
        if (a.layer) {
            a.rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr,
                                                  D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                  D2D1::IdentityMatrix(), opacity),
                            a.layer.get());
        } else {
            useLayer = false;
        }
    }
    if (a.detail.open) DrawDetail(a, x0 + dx, PAGE_Y, w);
    else if (a.skinOpen) DrawSkin(a, x0 + dx, PAGE_Y, w);
    else DrawPage(a, a.page, x0 + dx, PAGE_Y, w);
    if (useLayer) a.rt->PopLayer();
    a.rt->PopAxisAlignedClip();

    // статус-строка
    a.rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x0 + 4, H - 17), 3.5f, 3.5f), StatusBrush(a));
    Text(a, a.status, a.fTiny.get(), StatusBrush(a), x0 + 16, H - 26, w - 16, 16);
}

static void Paint(App& a) {
    if (!a.rt) return;
    SyncHead(a);
    a.rt->BeginDraw();
    a.rt->SetTransform(D2D1::Matrix3x2F::Identity());
    a.rt->Clear(C_BG);
    DrawSidebar(a);
    DrawContent(a);
    DrawShotLightbox(a);
    DrawAuthModal(a);
    DrawBuildModal(a);
    DrawInstallModal(a);
    DrawDeleteModal(a);
    DrawOverviewModal(a);
    if (a.rt->EndDraw() == D2DERR_RECREATE_TARGET) DiscardDeviceResources(a);
}

// ── логика ────────────────────────────────────────────────────────────────────
static void GoPage(App& a, int p) {
    if (p == a.page) return;
    a.fromPage = a.page;
    a.page = p;
    a.animT = 0.f;
    a.animating = true;
    a.searchFocus = false;
    a.setFocus = 0;
    SetTimer(a.hwnd, 1, 16, nullptr);
    InvalidateRect(a.hwnd, nullptr, FALSE);
}

struct HitInfo {
    int id = 0;
    int row = -1;
};

static HitInfo HitTest(App& a, float x, float y) {
    HitInfo h;
    if (!a.rt) return h;

    if (a.overview.open) {
        OverviewLayout L = OverviewLayoutFor(a);
        if (!L.panel.hit(x, y)) { h.id = 772; return h; }
        if (L.close.hit(x, y)) h.id = 770;
        else if (L.folder.hit(x, y)) h.id = 771;
        else if (L.mods.hit(x, y)) h.id = 773;
        else if (L.done.hit(x, y)) h.id = 770;
        return h;
    }

    if (a.delUi.open) {
        DeleteModalLayout L = DeleteModalLayoutFor(a);
        if (!L.panel.hit(x, y)) { h.id = 762; return h; }
        if (L.del.hit(x, y)) h.id = 760;
        else if (L.cancel.hit(x, y)) h.id = 761;
        return h;
    }

    if (a.buildUi.open) {
        BuildModalLayout L = BuildModalLayoutFor(a);
        if (!L.panel.hit(x, y)) { if (!a.buildUi.busy) h.id = 722; return h; }
        if (a.buildUi.busy) return h;
        if (L.nameR.hit(x, y)) { h.id = 700; return h; }
        if (L.mcR.hit(x, y)) { h.id = 701; return h; }
        for (int i = 0; i < 5; ++i)
            if (L.chips[i].hit(x, y)) { h.id = 710 + i; return h; }
        if (L.create.hit(x, y)) { h.id = 720; return h; }
        if (L.cancel.hit(x, y)) { h.id = 721; return h; }
        return h;
    }

    if (a.installUi.open) {
        InstallModalLayout L = InstallModalLayoutFor(a);
        if (!L.panel.hit(x, y)) { h.id = 728; return h; }
        if (L.cancel.hit(x, y)) { h.id = 729; return h; }
        if (!a.installUi.busy) {
            for (int i = 0; i < L.count; ++i)
                if (L.rows[i].hit(x, y)) { h.id = 730 + i; return h; }
        }
        return h;
    }

    if (a.authUi.modal) {
        ModalLayout L = ModalLayoutFor(a);
        if (!L.panel.hit(x, y)) { h.id = 600; return h; }
        int m = a.authUi.modal;
        if (m == 1) {
            if (L.p1.hit(x, y)) h.id = 601;
            else if (L.p2.hit(x, y)) h.id = 602;
            else if (L.cancel.hit(x, y)) h.id = 603;
        } else if (m == 2) {
            if (L.open.hit(x, y)) h.id = 610;
            else if (L.msCancel.hit(x, y)) h.id = 611;
        } else if (m == 3) {
            if (L.user.hit(x, y)) h.id = 620;
            else if (L.pass.hit(x, y)) h.id = 621;
            else if (L.submit.hit(x, y)) h.id = 622;
            else if (L.elyCancel.hit(x, y)) h.id = 623;
        } else {
            int idx = 0;
            bool saved[2] = { a.ms.online, a.ely.online };
            for (int i = 0; i < 2; ++i) {
                if (!saved[i]) continue;
                if (L.rows[idx].hit(x, y)) { h.id = 630 + i; return h; }
                ++idx;
            }
            if (L.add.hit(x, y)) h.id = 632;
            else if (L.logout.hit(x, y)) h.id = 633;
        }
        return h;
    }

    if (x < SIDE_W) {
        for (int i = 0; i < 4; ++i) {
            float y0 = NAV_Y0 + i * NAV_STEP;
            if (y >= y0 && y <= y0 + NAV_H) { h.id = 1 + i; return h; }
        }
        float H = a.rt->GetSize().height;
        float cy = H - 14 - 118;
        if (x >= SIDE_PAD && x <= SIDE_W - SIDE_PAD) {
            if (y >= cy && y <= cy + 58) { h.id = 50; return h; }
            if (y >= cy + 60 && y <= cy + 86) { h.id = 51; return h; }
            if (y >= cy + 90 && y <= cy + 114) { h.id = 52; return h; }
        }
        return h;
    }

    if (a.detail.open && a.detail.shotOpen >= 0) { h.id = 520; return h; }
    if (a.detail.open) {
        float px = SIDE_W + 30.f;
        float py = PAGE_Y;
        float w = a.rt->GetSize().width - px - 30.f;
        if (y >= py && y <= py + 30.f && x >= px && x <= px + 96.f) { h.id = 500; return h; }
        float btnY = py + 44.f;
        if (y >= btnY && y <= btnY + 36.f) {
            if (x >= px + w - 320.f && x <= px + w - 180.f) { h.id = 502; return h; }
            if (x >= px + w - 170.f && x <= px + w) { h.id = 501; return h; }
        }
        for (size_t i = 0; i < a.detail.shotRects.size(); ++i)
            if (a.detail.shotRects[i].w > 0.f && a.detail.shotRects[i].hit(x, y)) {
                h.id = 510 + (int)i;
                return h;
            }
        return h;
    }

    if (a.skinOpen) {
        float sx = SIDE_W + 30.f, sw = a.rt->GetSize().width - sx - 30.f;
        SkinLayout L = SkinLayoutFor(a, sx, PAGE_Y, sw);
        if (L.back.hit(x, y)) { h.id = 400; return h; }
        if (L.classic.hit(x, y)) { h.id = 401; return h; }
        if (L.slim.hit(x, y)) { h.id = 402; return h; }
        if (L.pick.hit(x, y)) { h.id = 403; return h; }
        if (L.nick.hit(x, y)) { h.id = 404; return h; }
        if (L.take.hit(x, y)) { h.id = 405; return h; }
        if (L.apply.hit(x, y)) { h.id = 406; return h; }
        if (L.reset.hit(x, y)) { h.id = 407; return h; }
        if (a.authKind != L"ely") {
            if (L.capeNone.hit(x, y)) { h.id = 429; return h; }
            for (int i = 0; i < L.capeCount; ++i)
                if (L.capes[i].hit(x, y)) { h.id = 430 + i; return h; }
        }
        return h;
    }

    float px = SIDE_W + 30.f;
    float py = PAGE_Y;
    float w = a.rt->GetSize().width - px - 30.f;

    if (a.page == 0) {
        float topY = py + 92;
        float listW = 286.f;
        float rx = px + listW + 16;
        float rw = w - listW - 16;
        float bottom = a.rt->GetSize().height - 30.f;

        float ry = topY + 38;
        int maxRows = (int)(std::max(0.f, bottom - topY - 8.f - 50.f) / 70.f);
        if (x >= px + listW - 148 && x <= px + listW - 16 && y >= topY + 7 && y <= topY + 34) {
            h.id = 32;
            return h;
        }
        for (int i = 0; i < (int)a.profiles.size() && i < maxRows; ++i) {
            if (x >= px + listW - 46 && x <= px + listW - 18 && y >= ry + 4 && y <= ry + 36) {
                h.id = 70 + i;
                return h;
            }
            if (x >= px + 10 && x <= px + listW - 10 && y >= ry && y <= ry + 62) {
                h.id = 40 + i;
                return h;
            }
            ry += 70;
        }
        if (a.hasProfile && x >= rx + rw - 158 && x <= rx + rw - 20 &&
            y >= topY + 88 && y <= topY + 118) {
            h.id = 33;
            return h;
        }
        if (x >= rx + rw - 200 && x <= rx + rw - 20 && y >= topY + 126 && y <= topY + 174) {
            h.id = 30;
            return h;
        }
        float logY = topY + 210;
        if (x >= rx + rw - 104 && x <= rx + rw - 16 && y >= logY + 8 && y <= logY + 36) {
            h.id = 31;
            return h;
        }
        return h;
    }
    if (a.page == 3) {
        if (y < PAGE_Y + 80.f) return h;
        float H = a.rt->GetSize().height;
        SettingsLayout L = SettingsLayoutFor(a, px, py, w);
        auto hit = [&](const MRect& r) {
            return x >= r.x && x <= r.x + r.w &&
                   y >= std::max(r.y, PAGE_Y + 80.f) && y <= std::min(r.y + r.h, H - 30.f);
        };
        if (hit(L.ramMin)) { h.id = 300; return h; }
        if (hit(L.ramMax)) { h.id = 301; return h; }
        if (hit(L.javaPath)) { h.id = 302; return h; }
        if (hit(L.javaCheck)) { h.id = 303; return h; }
        if (hit(L.javaBrowse)) { h.id = 308; return h; }
        for (int i = 0; i < 4; ++i) if (hit(L.javaDl[i])) { h.id = 350 + i; return h; }
        if (hit(L.user)) { h.id = 304; return h; }
        if (hit(L.javaArgs)) { h.id = 305; return h; }
        if (hit(L.gameArgs)) { h.id = 306; return h; }

        for (int i = 0; i < 6; ++i) if (hit(L.langBtn[i])) { h.id = 360 + i; return h; }
        if (hit(L.themeDark)) { h.id = 310; return h; }
        if (hit(L.themeLight)) { h.id = 311; return h; }
        for (int i = 0; i < 6; ++i) if (hit(L.accentSw[i])) { h.id = 320 + i; return h; }
        if (hit(L.accentEntry)) { h.id = 326; return h; }
        if (hit(L.accentApply)) { h.id = 327; return h; }
        for (int i = 0; i < 6; ++i) if (hit(L.accent2Sw[i])) { h.id = 330 + i; return h; }
        if (hit(L.accent2Entry)) { h.id = 336; return h; }
        if (hit(L.accent2Apply)) { h.id = 337; return h; }
        if (hit(L.save)) { h.id = 340; return h; }
        return h;
    }
    if (a.page == 1) {
        if (a.hasProfile && !a.modsList.empty() &&
            x >= px + w - 104 && x <= px + w && y >= py + 80 && y <= py + 112) {
            h.id = 60;
            return h;
        }
        return h;
    }
    if (a.page != 2) return h;

    const float tw[4] = { 78.f, 124.f, 100.f, 108.f };
    float tx = px;
    for (int i = 0; i < 4; ++i) {
        if (x >= tx && x <= tx + tw[i] && y >= py + CAT_TABS_Y && y <= py + CAT_TABS_Y + 34) {
            h.id = 10 + i;
            return h;
        }
        tx += tw[i] + 8;
    }

    float bw = w;
    if (y >= py + CAT_SEARCH_Y && y <= py + CAT_SEARCH_Y + 42) {
        if (x >= px && x <= px + bw) { h.id = 20; return h; }
    }

    if (y >= py + CAT_FILTER_Y && y <= py + CAT_FILTER_Y + 34) {
        if (x >= px && x <= px + 150) { h.id = 23; return h; }
        if (x >= px + w - 140 && x <= px + w) { h.id = 24; return h; }
    }

    float top = py + CAT_LIST_Y;
    if (y >= top) {
        float local = y - top + a.scrollY;
        int row = (int)(local / (ROW_H + ROW_GAP));
        float inRow = local - row * (ROW_H + ROW_GAP);
        if (row >= 0 && (size_t)row < a.items.size() && inRow >= 0 && inRow <= ROW_H) {
            h.row = row;
            if (inRow >= 14 && inRow <= 48 && x >= px + w - 126 && x <= px + w - 18) {
                h.id = 200 + row;
            } else {
                h.id = 100 + row;
            }
        }
    }
    return h;
}

static std::wstring GetClipboardText() {
    std::wstring r;
    if (!OpenClipboard(nullptr)) return r;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        auto* p = (const wchar_t*)GlobalLock(h);
        if (p) {
            r = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    r.erase(std::remove(r.begin(), r.end(), L'\r'), r.end());
    r.erase(std::remove(r.begin(), r.end(), L'\n'), r.end());
    return r;
}

// ── действия страницы настроек ────────────────────────────────────────────────
static std::wstring* FocusField(App& a) {
    switch (a.setFocus) {
    case 300: return &a.ramMin;
    case 301: return &a.ramMax;
    case 302: return &a.javaPathCfg;
    case 304: return &a.username;
    case 305: return &a.javaArgs;
    case 306: return &a.gameArgs;
    case 326: return &a.accentEdit;
    case 336: return &a.accent2Edit;
    default: return nullptr;
    }
}

static int NextSetFocus(int cur, bool back) {
    static const int order[] = { 300, 301, 302, 304, 305, 306, 326, 336 };
    const int n = 8;
    int i = 0;
    for (int k = 0; k < n; ++k) if (order[k] == cur) { i = k; break; }
    i = (i + (back ? n - 1 : 1)) % n;
    return order[i];
}

static void SetThemeCfg(App& a, bool light) {
    std::wstring t = light ? L"light" : L"dark";
    if (a.theme == t) return;
    a.theme = t;
    SaveSettings(a);
    ApplyThemeNow(a);
}

static void ApplyAccentEdit(App& a, bool second) {
    const std::wstring& v = second ? a.accent2Edit : a.accentEdit;
    D2D1_COLOR_F c;
    if (!HexOk(v, c)) {
        a.status = L"Цвет в виде #RRGGBB, например #7C5CFF";
        a.statusCol = C_YELLOW;
        Repaint(a.hwnd);
        return;
    }
    std::wstring hex = HexStr(c);
    if (second) a.accent2 = hex; else a.accent = hex;
    a.accentEdit = a.accent;
    a.accent2Edit = a.accent2;
    SaveSettings(a);
    ApplyThemeNow(a);
}

static void PickColor(App& a, bool second, const wchar_t* hex) {
    D2D1_COLOR_F c;
    if (!HexOk(hex, c)) return;
    if (second) { a.accent2 = HexStr(c); a.accent2Edit = a.accent2; }
    else { a.accent = HexStr(c); a.accentEdit = a.accent; }
    SaveSettings(a);
    ApplyThemeNow(a);
}

static void SaveSettingsFromUi(App& a) {
    SaveSettings(a);
    a.setFocus = 0;
    a.status = L"Настройки сохранены";
    a.statusCol = C_GREEN;
    InvalidateRect(a.hwnd, nullptr, FALSE);
}

static void SetLanguage(App& a, int l) {
    if (l < 0 || l > 5) return;
    if (g_lang.load(std::memory_order_relaxed) == l) return;
    a.lang = LangCode(l);
    g_lang.store(l, std::memory_order_relaxed);
    CreateTextFormats(a);
    SaveSettings(a);
    if (a.hwnd) InvalidateRect(a.hwnd, nullptr, FALSE);
}

// id 1 — иконка, встроенная в exe (src/app.rc); иначе — файл рядом с exe
static HICON LoadAppIcon(HINSTANCE hi, int cx, int cy) {
    HICON h = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    if (h) return h;
    std::error_code ec;
    fs::path ico = ResPath(L"midnight.ico");
    if (!fs::exists(ico, ec)) return nullptr;
    return (HICON)LoadImageW(nullptr, ico.c_str(), IMAGE_ICON, cx, cy, LR_LOADFROMFILE);
}

static void SetWindowIcon(HWND hwnd) {
    HINSTANCE hi = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    HICON big = LoadAppIcon(hi, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    HICON small = LoadAppIcon(hi, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    if (big) SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)big);
    if (small) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small);
}

// ── окно ──────────────────────────────────────────────────────────────────────
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* a = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        auto* app = new App();
        app->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)app);
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, app->d2d.put());
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(app->dw.put()));
        if (app->dw) LoadUiFonts(*app);
        LoadSettings(*app);
        if (app->dw) CreateTextFormats(*app);
        LoadProfiles(*app);
        LoadAuth(*app);
        RefreshMods(*app);
        app->iconThread = std::thread(IconWorker, app);
        LoadCatalog(*app);
        StartAuthRefresh(*app);
        SetWindowIcon(hwnd);
        return 0;
    }

    case WM_APP + 1: {
        auto* f = (std::function<void()>*)lp;
        if (f) { (*f)(); delete f; }
        return 0;
    }

    case WM_APP + 2:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_SIZE: {
        if (a && a->rt) {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            a->rt->Resize(D2D1::SizeU(
                (UINT32)std::max(1.f, (rc.right - rc.left) / a->dip),
                (UINT32)std::max(1.f, (rc.bottom - rc.top) / a->dip)));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        if (a) {
            CreateDeviceResources(*a);
            Paint(*a);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!a) break;
        float x = GET_X_LPARAM(lp) / a->dip;
        float y = GET_Y_LPARAM(lp) / a->dip;
        int nav = -1;
        int row = -1;
        int widget = 0;
        if (a->overview.open || a->authUi.modal || a->buildUi.open ||
            a->installUi.open || a->delUi.open) {
            HitInfo h = HitTest(*a, x, y);
            widget = h.id;
        } else if (x < SIDE_W) {
            for (int i = 0; i < 4; ++i) {
                float y0 = NAV_Y0 + i * NAV_STEP;
                if (y >= y0 && y <= y0 + NAV_H) nav = i;
            }
        } else if (a->detail.open) {
            HitInfo h = HitTest(*a, x, y);
            widget = h.id;
        } else if (a->skinOpen) {
            HitInfo h = HitTest(*a, x, y);
            widget = h.id;
        } else if (a->page == 2) {
            HitInfo h = HitTest(*a, x, y);
            if (h.id >= 100) row = h.row;
            widget = h.id >= 10 && h.id < 100 ? h.id : 0;
        } else if (a->page == 3) {
            HitInfo h = HitTest(*a, x, y);
            widget = h.id >= 300 ? h.id : 0;
        } else if (a->page == 0) {
            HitInfo h = HitTest(*a, x, y);
            widget = h.id;
        } else if (a->page == 1) {
            HitInfo h = HitTest(*a, x, y);
            widget = h.id;
        }

        if (nav != a->hoverNav || row != a->hoverRow || widget != a->hoverWidget) {
            a->hoverNav = nav;
            a->hoverRow = row;
            a->hoverWidget = widget;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        bool hand = nav >= 0 || row >= 0 ||
                    (a->page == 2 && widget) ||
                    (a->page == 3 && widget) ||
                    (a->page == 0 && widget) ||
                    (a->page == 1 && widget) ||
                    (a->detail.open && widget) ||
                    (a->skinOpen && widget) ||
                    (a->authUi.modal && widget >= 601) ||
                    (a->overview.open && widget) ||
                    (a->buildUi.open && widget) ||
                    (a->installUi.open && widget) ||
                    (a->delUi.open && widget);
        SetCursor(LoadCursorW(nullptr, hand ? IDC_HAND : IDC_ARROW));
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (a && (a->hoverNav != -1 || a->hoverRow != -1 || a->hoverWidget != 0)) {
            a->hoverNav = -1;
            a->hoverRow = -1;
            a->hoverWidget = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONUP: {
        if (!a) break;
        float x = GET_X_LPARAM(lp) / a->dip;
        float y = GET_Y_LPARAM(lp) / a->dip;
        HitInfo h = HitTest(*a, x, y);

        if (a->overview.open) {
            if (h.id == 770 || h.id == 772) {
                a->overview.open = false;
            } else if (h.id == 771) {
                if (!a->overview.dir.empty())
                    ShellExecuteW(nullptr, L"open", a->overview.dir.c_str(), nullptr, nullptr,
                                  SW_SHOWNORMAL);
            } else if (h.id == 773) {
                a->overview.open = false;
                GoPage(*a, 1);
                RefreshMods(*a);
                a->modsScrollY = 0.f;
                StartModsTree(*a);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (a->delUi.open) {
            if (h.id == 760) {
                DeleteProfile(*a, a->delUi.idx);
            } else if (h.id == 761 || h.id == 762) {
                a->delUi.open = false;
                a->delUi.idx = -1;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (a->authUi.modal) {
            switch (h.id) {
            case 600:
            case 603:
            case 611:
            case 623: CloseAuthModal(*a); break;
            case 601: StartMsLogin(*a); break;
            case 602: OpenAuthModal(*a, 3); break;
            case 610:
                ShellExecuteW(nullptr, L"open", kMsVerify, nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case 620: a->authUi.focusField = 1; break;
            case 621: a->authUi.focusField = 2; break;
            case 622: SubmitElyLogin(*a); break;
            case 630: a->skinOpen = false; SwitchAccount(*a, L"ms"); break;
            case 631: a->skinOpen = false; SwitchAccount(*a, L"ely"); break;
            case 632: OpenAuthModal(*a, 1); break;
            case 633: a->skinOpen = false; LogoutActive(*a); break;
            default: break;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (a->buildUi.open) {
            if (h.id == 722 || h.id == 721) {
                if (!a->buildUi.busy) {
                    a->buildUi.open = false;
                    a->buildUi.focus = 0;
                }
            } else if (!a->buildUi.busy) {
                if (h.id == 700) a->buildUi.focus = 1;
                else if (h.id == 701) a->buildUi.focus = 2;
                else if (h.id == 720) CreateBuild(*a);
                else if (h.id >= 710 && h.id <= 714) {
                    a->buildUi.loader = h.id - 710;
                    a->buildUi.status.clear();
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (a->installUi.open) {
            if (h.id == 728 || h.id == 729) {
                a->installUi.open = false;
                a->installUi.item.reset();
                ++a->installUi.gen;
            } else if (!a->installUi.busy && h.id >= 730) {
                int i = h.id - 730;
                if (i >= 0 && i < (int)a->installUi.cand.size()) {
                    auto it = a->installUi.item;
                    int idx = a->installUi.cand[(size_t)i];
                    if (it) GoInstall(*a, it, idx);
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (h.id == 50 || h.id == 51) {
            a->skinOpen = false;
            a->skinFocus = 0;
            OpenAuthModal(*a, AuthOnline(*a) ? 4 : 1);
            return 0;
        }
        if (h.id == 52) {
            a->detail.open = false;
            a->detail.shotOpen = -1;
            OpenSkinPage(*a);
            return 0;
        }

        if (h.id >= 1 && h.id <= 4) {
            a->skinOpen = false;
            a->skinFocus = 0;
            a->detail.open = false;
            a->detail.shotOpen = -1;
            GoPage(*a, h.id - 1);
            if (h.id - 1 == 1) {
                RefreshMods(*a);
                a->modsScrollY = 0.f;
                StartModsTree(*a);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (a->detail.open && h.id != 50 && h.id != 51 && h.id != 52) {
            if (a->detail.shotOpen >= 0) {
                a->detail.shotOpen = -1;
            } else if (h.id == 500) {
                a->detail.open = false;
            } else if (h.id == 501) {
                if (a->detail.item && !a->detail.item->pageUrl.empty())
                    ShellExecuteW(nullptr, L"open", a->detail.item->pageUrl.c_str(),
                                  nullptr, nullptr, SW_SHOWNORMAL);
            } else if (h.id == 502) {
                if (a->detail.item) OpenInstallPicker(*a, a->detail.item);
            } else if (h.id >= 510) {
                int i = h.id - 510;
                if (i >= 0 && i < (int)a->detail.shots.size() && a->detail.shots[(size_t)i].bmp)
                    a->detail.shotOpen = i;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (a->skinOpen) {
            if (h.id == 400) {
                a->skinOpen = false;
                a->skinFocus = 0;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (h.id == 401) {
                a->skinVariant = 0;
                ComputeSkinBitmaps(*a);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (h.id == 402) {
                a->skinVariant = 1;
                ComputeSkinBitmaps(*a);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            a->skinFocus = (h.id == 404) ? 1 : 0;
            switch (h.id) {
            case 403: SkinPickFile(*a); break;
            case 405: SkinTakeByName(*a); break;
            case 406: SkinApply(*a); break;
            case 407: SkinReset(*a); break;
            case 429: CapeApply(*a, std::wstring()); break;
            default:
                if (h.id >= 430 && h.id - 430 < (int)a->capes.size())
                    CapeApply(*a, a->capes[h.id - 430].id);
                break;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (h.id >= 10 && h.id <= 13) {
            a->catTab = h.id - 10;
            a->query.clear();
            a->searchFocus = false;
            LoadCatalog(*a);
            return 0;
        }
        if (a->page == 3) {
            if (h.id >= 300 && h.id <= 306) {
                a->setFocus = h.id;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (h.id == 326 || h.id == 336) {
                a->setFocus = h.id;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (h.id >= 320 && h.id <= 325) {
                a->setFocus = 326;
                PickColor(*a, false, kAccentPresets[h.id - 320]);
                return 0;
            }
            if (h.id >= 330 && h.id <= 335) {
                a->setFocus = 336;
                PickColor(*a, true, kAccent2Presets[h.id - 330]);
                return 0;
            }
            a->setFocus = 0;
            switch (h.id) {
            case 303: CheckJavaNow(*a); break;
            case 308: PickJavaExe(*a); break;
            case 350: DownloadJavaFor(*a, L"jre-legacy"); break;
            case 351: DownloadJavaFor(*a, L"java-runtime-gamma"); break;
            case 352: DownloadJavaFor(*a, L"java-runtime-delta"); break;
            case 353: DownloadJavaFor(*a, L"java-runtime-epsilon"); break;
            case 310: SetThemeCfg(*a, false); break;
            case 311: SetThemeCfg(*a, true); break;
            case 327: ApplyAccentEdit(*a, false); break;
            case 337: ApplyAccentEdit(*a, true); break;
            case 340: SaveSettingsFromUi(*a); break;
            case 360: SetLanguage(*a, LANG_RU); break;
            case 361: SetLanguage(*a, LANG_EN); break;
            case 362: SetLanguage(*a, LANG_FR); break;
            case 363: SetLanguage(*a, LANG_DE); break;
            case 364: SetLanguage(*a, LANG_ZH); break;
            case 365: SetLanguage(*a, LANG_JA); break;
            default: break;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        switch (h.id) {
        case 20: a->searchFocus = true; break;
        case 23: a->onlyProfile = !a->onlyProfile; LoadCatalog(*a); break;
        case 24: a->query.clear(); LoadCatalog(*a); break;
        case 32: OpenBuildModal(*a); break;
        case 33: OpenOverview(*a); break;
        case 60: StartModsTree(*a, true); break;
        case 30:
            if (a->launch && a->launch->active.load()) StopGame(*a);
            else StartLaunch(*a);
            break;
        case 31:
            if (a->launch) {
                std::lock_guard<std::mutex> lk(a->launch->mu);
                a->launch->log.clear();
            }
            break;
        default:
            if (h.id >= 70 && h.id < 70 + (int)a->profiles.size()) {
                a->delUi.open = true;
                a->delUi.idx = h.id - 70;
            } else if (h.id >= 40 && h.id < 40 + (int)a->profiles.size()) {
                SelectProfile(*a, h.id - 40);
            } else if (h.id >= 200 && h.row >= 0 && (size_t)h.row < a->items.size()) {
                OpenInstallPicker(*a, a->items[h.row]);
            } else if (h.id >= 100 && h.row >= 0 && (size_t)h.row < a->items.size()) {
                OpenModDetail(*a, a->items[h.row]);
            } else {
                a->searchFocus = false;
            }
            break;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        if (a && a->overview.open) {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            a->overview.scrollY = std::clamp(
                a->overview.scrollY - delta / 120.f * 70.f, 0.f, a->overview.scrollMax);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (!a || a->authUi.modal || a->buildUi.open || a->installUi.open || a->delUi.open) break;
        if (a->detail.open) {
            if (a->detail.shotOpen < 0) {
                int delta = GET_WHEEL_DELTA_WPARAM(wp);
                a->detail.scrollY = std::clamp(a->detail.scrollY - delta / 120.f * 70.f,
                                               0.f, a->detail.scrollMax);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (a->page == 1) {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            a->modsScrollY = std::clamp(a->modsScrollY - delta / 120.f * 70.f,
                                        0.f, a->modsScrollMax);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (a->page == 3) {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            a->settingsScrollY = std::clamp(a->settingsScrollY - delta / 120.f * 70.f,
                                            0.f, a->settingsScrollMax);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (a->page != 2) break;
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        if (pt.y < (PAGE_Y + CAT_LIST_Y) * a->dip) return 0;
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        a->scrollY = std::clamp(a->scrollY - delta / 120.f * 60.f, 0.f, a->scrollMax);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_KEYDOWN: {
        if (a && a->overview.open) {
            if (wp == VK_ESCAPE) {
                a->overview.open = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (a && a->delUi.open) {
            if (wp == VK_ESCAPE) {
                a->delUi.open = false;
                a->delUi.idx = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (a && a->buildUi.open) {
            if (a->buildUi.busy) return 0;
            if (wp == VK_ESCAPE) {
                a->buildUi.open = false;
                a->buildUi.focus = 0;
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (wp == VK_TAB) {
                a->buildUi.focus = a->buildUi.focus == 2 ? 1 : 2;
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (wp == VK_BACK) {
                std::wstring& t = a->buildUi.focus == 2 ? a->buildUi.mc : a->buildUi.name;
                if (!t.empty()) t.pop_back();
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (wp == VK_RETURN) {
                CreateBuild(*a);
            } else if (wp == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                std::wstring& t = a->buildUi.focus == 2 ? a->buildUi.mc : a->buildUi.name;
                t += GetClipboardText();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (a && a->installUi.open) {
            if (wp == VK_ESCAPE) {
                a->installUi.open = false;
                a->installUi.item.reset();
                ++a->installUi.gen;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (a && a->authUi.modal) {
            if (wp == VK_ESCAPE) {
                CloseAuthModal(*a);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (a->authUi.modal == 3) {
                if (wp == VK_TAB) {
                    a->authUi.focusField = a->authUi.focusField == 1 ? 2 : 1;
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (wp == VK_BACK) {
                    std::wstring& t = a->authUi.focusField == 2 ? a->authUi.pass : a->authUi.user;
                    if (!t.empty()) t.pop_back();
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (wp == VK_RETURN) {
                    SubmitElyLogin(*a);
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (wp == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                    std::wstring t = GetClipboardText();
                    if (!t.empty() && a->authUi.focusField) {
                        (a->authUi.focusField == 2 ? a->authUi.pass : a->authUi.user) += t;
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                }
            }
            return 0;
        }
        if (a && a->detail.open) {
            if (wp == VK_ESCAPE) {
                if (a->detail.shotOpen >= 0) a->detail.shotOpen = -1;
                else a->detail.open = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (a && a->skinOpen && a->skinFocus) {
            if (wp == VK_ESCAPE) {
                a->skinFocus = 0;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wp == VK_BACK) {
                if (!a->skinNick.empty()) a->skinNick.pop_back();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wp == VK_RETURN) {
                SkinTakeByName(*a);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wp == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                a->skinNick += GetClipboardText();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            return 0;
        }
        if (a && a->page == 3 && a->setFocus) {
            if (wp == VK_ESCAPE) {
                a->setFocus = 0;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wp == VK_TAB) {
                a->setFocus = NextSetFocus(a->setFocus, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wp == VK_BACK) {
                std::wstring* t = FocusField(*a);
                if (t && !t->empty()) t->pop_back();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wp == VK_RETURN) {
                if (a->setFocus == 326) ApplyAccentEdit(*a, false);
                else if (a->setFocus == 336) ApplyAccentEdit(*a, true);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wp == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                std::wstring* t = FocusField(*a);
                if (t) *t += GetClipboardText();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            return 0;
        }
        if (!a || a->page != 2 || !a->searchFocus) break;
        if (wp == VK_BACK) {
            if (!a->query.empty()) a->query.pop_back();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (wp == VK_RETURN) { LoadCatalog(*a); return 0; }
        if (wp == VK_ESCAPE) {
            a->searchFocus = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (wp == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            std::wstring t = GetClipboardText();
            if (!t.empty()) a->query += t;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    }

    case WM_CHAR: {
        if (a && a->delUi.open) return 0;
        if (a && a->buildUi.open) {
            if (!a->buildUi.busy && a->buildUi.focus && wp >= 32 && wp != 127) {
                std::wstring& t = a->buildUi.focus == 2 ? a->buildUi.mc : a->buildUi.name;
                if (t.size() < 120) t += (wchar_t)wp;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (a && a->installUi.open) return 0;
        if (a && a->authUi.modal) {
            if (a->authUi.modal == 3 && a->authUi.focusField && wp >= 32 && wp != 127) {
                std::wstring& t = a->authUi.focusField == 2 ? a->authUi.pass : a->authUi.user;
                if (t.size() < 200) t += (wchar_t)wp;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (a && a->skinOpen && a->skinFocus && wp >= 32 && wp != 127) {
            if (a->skinNick.size() < 40) a->skinNick += (wchar_t)wp;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (a && a->page == 3 && a->setFocus && wp >= 32 && wp != 127) {
            std::wstring* t = FocusField(*a);
            if (t && t->size() < 512) *t += (wchar_t)wp;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (a && a->page == 2 && a->searchFocus && wp >= 32 && wp != 127) {
            a->query += (wchar_t)wp;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    }

    case WM_TIMER:
        if (a && wp == 1) {
            a->animT = std::min(1.f, a->animT + 0.065f);
            if (a->animT >= 1.f) {
                a->animating = false;
                KillTimer(hwnd, 1);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_DPICHANGED: {
        RECT* r = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        if (a) {
            a->dip = GetDpiForWindow(hwnd) / 96.f;
            if (a->rt) a->rt->SetDpi(96.f * a->dip, 96.f * a->dip);
        }
        return 0;
    }

    case WM_GETMINMAXINFO: {
        auto* mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize.x = 1000;
        mm->ptMinTrackSize.y = 620;
        return 0;
    }

    case WM_DESTROY:
        if (a) {
            {
                std::lock_guard<std::mutex> lk(a->iconMu);
                a->iconQuit = true;
            }
            a->iconCv.notify_all();
            if (a->iconThread.joinable()) a->iconThread.join();
            DiscardDeviceResources(*a);
            // Фоновые потоки запуска могут ещё работать: не удаляем App, процесс всё равно
            // завершается сразу после выхода из цикла сообщений.
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetCurrentProcessExplicitAppUserModelID(L"MidnightLauncher");

    HICON wicon = LoadAppIcon(hInst, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    HICON wiconSm = LoadAppIcon(hInst, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hIcon = wicon;
    wc.hIconSm = wiconSm ? wiconSm : wicon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"MidnightCppWnd";
    RegisterClassExW(&wc);

    RECT r{ 0, 0, 1180, 740 };
    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int wx = wa.left + ((wa.right - wa.left) - ww) / 2;
    int wy = wa.top + ((wa.bottom - wa.top) - wh) / 2;
    HWND hwnd = CreateWindowExW(0, L"MidnightCppWnd", L"Midnight launcher", WS_OVERLAPPEDWINDOW,
                                wx, wy, ww, wh, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 0;

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return 0;
}

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "rife_core.h"
#include "rife_app_api.h"
#include "app_manifest.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")

#define GRID_W 40
#define GRID_H 30
#define MAX_SHORTCUTS 32
#define MAX_PLUGIN_WINDOWS 16

typedef struct {
    char name[64];
    char path[MAX_PATH];
    float x;
    float y;
    uint32_t color_top;
    uint32_t color_bot;
    HICON custom_icon;
} DesktopShortcut;

typedef struct {
    int x0;
    int x1;
    float tx;
} HorizLookup;

typedef struct {
    const RifePluginApp* plugin;
    void* inst;
    bool is_open;
    bool is_maximized;
    float anim;

    float x, y, w, h;
    float restore_x, restore_y, restore_w, restore_h;
    bool inited;
} ActiveWindow;

typedef struct {
    HWND hwnd;
    HDC hdc_mem;
    HBITMAP hbm_mem;
    HBITMAP hbm_old;
    uint32_t* pixels;
    HFONT hfont_panel_title;
    HFONT hfont_display;
    HFONT hfont_title;
    HFONT hfont_body;
    HFONT hfont_bold;
    HFONT hfont_sm;
    HFONT hfont_caption;
    int win_width;
    int win_height;
    HorizLookup* hlook;
    float dock_anim;
    float drawer_anim;
    bool drawer_open;
    DesktopShortcut shortcuts[MAX_SHORTCUTS];
    size_t shortcut_count;
    float grid_r[GRID_H][GRID_W];
    float grid_g[GRID_H][GRID_W];
    float grid_b[GRID_H][GRID_W];
    float aura_time;
    float breath_t;
    DesktopModeType current_mode;
    bool is_fullscreen;
    RECT prev_rect;
    FontScaleType current_font_scale;
    ActiveWindow windows[MAX_PLUGIN_WINDOWS];

    bool cloud_expanded;
    float cloud_anim;
    float absorption_ripple_t;

    int active_win_idx;
    int drag_mode;
    int resize_dir;
    int hover_resize_dir;
    float drag_start_mx, drag_start_my;
    float drag_start_wx, drag_start_wy, drag_start_ww, drag_start_wh;
    bool snap_preview;
} Win32Platform;

void rife_render_immediate(RifeCore* core);

static inline float rife_clampf(float v, float min_v, float max_v) {
    if (v < min_v) return min_v;
    if (v > max_v) return max_v;
    return v;
}

static inline float rife_lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline float rife_fluid_decay(float current, float target, float lambda, float dt) {
    if (dt <= 0.0f) return current;
    float t = 1.0f - expf(-lambda * dt);
    return current + (target - current) * t;
}

static inline float rife_smootherstep(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static inline float rife_fluid_elastic_step(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    float s = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    float overshoot = 0.055f * sinf(3.14159265f * t) * (t * t) * (1.0f - t);
    return s + overshoot;
}

static void rife_draw_text_u8(HDC hdc, int x, int y, const char* utf8_str) {
    if (!utf8_str || !utf8_str[0]) return;
    wchar_t wbuf[256];
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, wbuf, 256);
    if (wlen > 0) {
        TextOutW(hdc, x, y, wbuf, wlen - 1);
    }
}

static void set_system_taskbar_visible(bool visible) {
    HWND taskbar = FindWindowA("Shell_TrayWnd", NULL);
    if (taskbar) {
        ShowWindow(taskbar, visible ? SW_SHOW : SW_HIDE);
    }
}

static void update_system_fonts(Win32Platform* plat, FontScaleType scale) {
    if (!plat) return;
    float factor = 1.0f;
    if (scale == FONT_SCALE_125) factor = 1.25f;
    else if (scale == FONT_SCALE_150) factor = 1.50f;

    if (plat->hfont_panel_title) DeleteObject(plat->hfont_panel_title);
    if (plat->hfont_display) DeleteObject(plat->hfont_display);
    if (plat->hfont_title) DeleteObject(plat->hfont_title);
    if (plat->hfont_body) DeleteObject(plat->hfont_body);
    if (plat->hfont_bold) DeleteObject(plat->hfont_bold);
    if (plat->hfont_sm) DeleteObject(plat->hfont_sm);
    if (plat->hfont_caption) DeleteObject(plat->hfont_caption);

    // 查询 Windows 系统默认 UI 字体 (System Default UI Font)
    wchar_t font_face[LF_FACESIZE] = L"Microsoft YaHei UI";
    NONCLIENTMETRICSW ncm;
    memset(&ncm, 0, sizeof(NONCLIENTMETRICSW));
    ncm.cbSize = sizeof(NONCLIENTMETRICSW);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICSW), &ncm, 0)) {
        if (ncm.lfMessageFont.lfFaceName[0] != L'\0') {
            wcsncpy_s(font_face, LF_FACESIZE, ncm.lfMessageFont.lfFaceName, _TRUNCATE);
        }
    }

    // 使用负值获得纯粹字符 EM 像素高度 (Pixel EM Height)，杜绝正值导致字符被额外挤压模糊
    int s_display = -(int)(21 * factor + 0.5f);
    int s_panel   = -(int)(18 * factor + 0.5f);
    int s_title   = -(int)(15 * factor + 0.5f);
    int s_body    = -(int)(13 * factor + 0.5f);
    int s_bold    = -(int)(13 * factor + 0.5f);
    int s_sm      = -(int)(12 * factor + 0.5f);
    int s_cap     = -(int)(11 * factor + 0.5f);

    plat->hfont_display = CreateFontW(s_display, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font_face);
    plat->hfont_panel_title = CreateFontW(s_panel, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font_face);
    plat->hfont_title = CreateFontW(s_title, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font_face);
    plat->hfont_body = CreateFontW(s_body, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font_face);
    plat->hfont_bold = CreateFontW(s_bold, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font_face);
    plat->hfont_sm = CreateFontW(s_sm, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font_face);
    plat->hfont_caption = CreateFontW(s_cap, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font_face);

    plat->current_font_scale = scale;
}

static void toggle_immersion_fullscreen(Win32Platform* plat) {
    if (!plat || !plat->hwnd) return;
    if (plat->is_fullscreen) {
        plat->is_fullscreen = false;
        set_system_taskbar_visible(true);
        SetWindowLongA(plat->hwnd, GWL_STYLE, WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_POPUP);
        int w = plat->prev_rect.right - plat->prev_rect.left;
        int h = plat->prev_rect.bottom - plat->prev_rect.top;
        if (w < 400 || h < 300) { w = 680; h = 480; }
        SetWindowPos(plat->hwnd, HWND_NOTOPMOST, plat->prev_rect.left, plat->prev_rect.top, w, h,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    else {
        plat->is_fullscreen = true;
        GetWindowRect(plat->hwnd, &plat->prev_rect);
        set_system_taskbar_visible(false);
        int sx = GetSystemMetrics(SM_CXSCREEN);
        int sy = GetSystemMetrics(SM_CYSCREEN);
        SetWindowLongA(plat->hwnd, GWL_STYLE, WS_POPUP);
        SetWindowPos(plat->hwnd, HWND_TOPMOST, 0, 0, sx, sy, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
}

static HWND g_workerw_cache = NULL;
static BOOL CALLBACK EnumWindowsWorkerWProc(HWND hwnd, LPARAM lParam) {
    (void)lParam;
    HWND p = FindWindowExA(hwnd, NULL, "SHELLDLL_DefView", NULL);
    if (p != NULL) {
        g_workerw_cache = FindWindowExA(NULL, hwnd, "WorkerW", NULL);
    }
    return TRUE;
}

HWND get_wallpaper_workerw(void) {
    HWND progman = FindWindowA("Progman", NULL);
    SendMessageTimeoutA(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, NULL);
    g_workerw_cache = NULL;
    EnumWindows(EnumWindowsWorkerWProc, 0);
    return g_workerw_cache;
}

void apply_desktop_mode(Win32Platform* plat, DesktopModeType mode) {
    if (plat->current_mode == mode) return;
    plat->current_mode = mode;

    if (mode == DESKTOP_MODE_WALLPAPER) {
        HWND workerw = get_wallpaper_workerw();
        if (workerw) {
            SetParent(plat->hwnd, workerw);
            RECT rc;
            GetWindowRect(workerw, &rc);
            SetWindowPos(plat->hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
    }
    else {
        SetParent(plat->hwnd, NULL);
        SetWindowLongA(plat->hwnd, GWL_STYLE, WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_POPUP);
        SetWindowPos(plat->hwnd, NULL, 100, 100, 680, 480, SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
}

HICON rife_load_custom_icon(const char* icon_path) {
    if (!icon_path || !icon_path[0]) return NULL;
    HICON h = (HICON)LoadImageA(NULL, icon_path, IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
    if (h) return h;
    SHFILEINFOA sfi = { 0 };
    DWORD_PTR res = SHGetFileInfoA(icon_path, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON);
    if (res && sfi.hIcon) return sfi.hIcon;
    return NULL;
}

void update_horiz_lookup(Win32Platform* plat, int width) {
    if (!plat || !plat->hlook || width <= 0) return;
    int w = (width > 7680) ? 7680 : width;
    float inv_w = (w > 1) ? (1.0f / (float)(w - 1)) : 0.0f;
    float gw_scale = (float)(GRID_W - 1);
    for (int x = 0; x < w; x++) {
        float gx = (float)x * inv_w * gw_scale;
        int x0 = (int)gx;
        int x1 = (x0 < GRID_W - 1) ? x0 + 1 : x0;
        plat->hlook[x].x0 = x0;
        plat->hlook[x].x1 = x1;
        plat->hlook[x].tx = gx - (float)x0;
    }
}

void desktop_add_shortcut(Win32Platform* plat, const char* file_path, bool grid_align) {
    if (!plat || plat->shortcut_count >= MAX_SHORTCUTS || !file_path) return;
    for (size_t i = 0; i < plat->shortcut_count; i++) {
        if (_stricmp(plat->shortcuts[i].path, file_path) == 0) return;
    }
    DesktopShortcut* sc = &plat->shortcuts[plat->shortcut_count];
    snprintf(sc->path, sizeof(sc->path), "%s", file_path);

    const char* filename = strrchr(file_path, '\\');
    filename = filename ? filename + 1 : file_path;
    snprintf(sc->name, sizeof(sc->name), "%s", filename);

    size_t idx = plat->shortcut_count;
    int col = grid_align ? (int)(idx % 6) : (int)(idx % 8);
    int row = grid_align ? (int)(idx / 6) : (int)(idx / 8);
    sc->x = 24.0f + (float)col * 92.0f;
    sc->y = 52.0f + (float)row * 96.0f;
    sc->custom_icon = rife_load_custom_icon(file_path);

    uint32_t gradients[][2] = {
        {0x0284C7FF, 0x0369A1FF},
        {0x059669FF, 0x047857FF},
        {0x7C3AEDFF, 0x6D28D9FF},
        {0xD97706FF, 0xB45309FF},
        {0xDB2777FF, 0xBE185DFF},
        {0x475569FF, 0x334155FF}
    };
    sc->color_top = gradients[idx % 6][0];
    sc->color_bot = gradients[idx % 6][1];
    plat->shortcut_count++;
}

void rife_render_gemini_light_field(RifeCore* core, Win32Platform* plat, const RifeSystemConfig* cfg) {
    if (!plat || !plat->pixels) return;
    float ww = (float)plat->win_width;
    float wh = (float)plat->win_height;
    float mx = core->input.mouse_x;
    float my = core->input.mouse_y;
    float t = plat->aura_time;

    float n1_x = ww * 0.18f + sinf(t * 0.70f) * (ww * 0.14f) + cosf(t * 1.35f) * (ww * 0.035f);
    float n1_y = wh * 0.22f + cosf(t * 0.85f) * (wh * 0.12f) + sinf(t * 1.60f) * (wh * 0.025f);
    float n2_x = ww * 0.52f + cosf(t * 0.80f) * (ww * 0.17f) + sinf(t * 1.45f) * (ww * 0.035f);
    float n2_y = wh * 0.18f + sinf(t * 1.05f) * (wh * 0.10f) + cosf(t * 1.75f) * (wh * 0.025f);
    float n3_x = ww * 0.82f + sinf(t * 0.58f) * (ww * 0.13f) + cosf(t * 1.20f) * (ww * 0.035f);
    float n3_y = wh * 0.28f + cosf(t * 0.72f) * (wh * 0.14f) + sinf(t * 1.50f) * (wh * 0.025f);
    float n4_x = ww * 0.35f + cosf(t * 0.90f) * (ww * 0.18f) + sinf(t * 1.65f) * (ww * 0.035f);
    float n4_y = wh * 0.56f + sinf(t * 0.75f) * (wh * 0.15f) + cosf(t * 1.30f) * (wh * 0.025f);
    float n5_x = ww * 0.72f + sinf(t * 1.00f) * (ww * 0.15f) + cosf(t * 1.85f) * (ww * 0.035f);
    float n5_y = wh * 0.62f + cosf(t * 0.62f) * (wh * 0.14f) + sinf(t * 1.40f) * (wh * 0.025f);

    const float r1_sq = 300.0f * 300.0f, inv_r1 = 1.0f / 300.0f;
    const float r2_sq = 310.0f * 310.0f, inv_r2 = 1.0f / 310.0f;
    const float r3_sq = 280.0f * 280.0f, inv_r3 = 1.0f / 280.0f;
    const float r4_sq = 270.0f * 270.0f, inv_r4 = 1.0f / 270.0f;
    const float r5_sq = 260.0f * 260.0f, inv_r5 = 1.0f / 260.0f;
    const float rm_sq = 230.0f * 230.0f, inv_rm = 1.0f / 230.0f;

    AuraPaletteType pal = cfg ? cfg->palette : PALETTE_GEMINI;
    bool disturbance = cfg ? cfg->cursor_disturbance : true;

    for (int y = 0; y < GRID_H; y++) {
        float py = ((float)y / (float)(GRID_H - 1)) * wh;
        for (int x = 0; x < GRID_W; x++) {
            float px = ((float)x / (float)(GRID_W - 1)) * ww;
            float r, g, b;

            if (pal == PALETTE_OBSIDIAN) {
                r = 15.0f; g = 23.0f; b = 42.0f;
            }
            else if (pal == PALETTE_SUNSET) {
                r = 255.0f; g = 241.0f; b = 242.0f;
            }
            else if (pal == PALETTE_CYBER) {
                r = 240.0f; g = 253.0f; b = 250.0f;
            }
            else {
                r = 242.0f; g = 245.0f; b = 251.0f;
            }

            float d1_sq = (px - n1_x) * (px - n1_x) + (py - n1_y) * (py - n1_y);
            if (d1_sq < r1_sq) {
                float w = 1.0f - sqrtf(d1_sq) * inv_r1;
                w = w * w * (3.0f - 2.0f * w) * 0.80f;
                if (pal == PALETTE_OBSIDIAN) { r += (30.0f - r) * w; g += (58.0f - g) * w; b += (138.0f - b) * w; }
                else if (pal == PALETTE_SUNSET) { r += (244.0f - r) * w; g += (63.0f - g) * w; b += (94.0f - b) * w; }
                else if (pal == PALETTE_CYBER) { r += (16.0f - r) * w; g += (185.0f - g) * w; b += (129.0f - b) * w; }
                else { r += (66.0f - r) * w; g += (133.0f - g) * w; b += (244.0f - b) * w; }
            }

            float d2_sq = (px - n2_x) * (px - n2_x) + (py - n2_y) * (py - n2_y);
            if (d2_sq < r2_sq) {
                float w = 1.0f - sqrtf(d2_sq) * inv_r2;
                w = w * w * (3.0f - 2.0f * w) * 0.82f;
                if (pal == PALETTE_OBSIDIAN) { r += (88.0f - r) * w; g += (28.0f - g) * w; b += (135.0f - b) * w; }
                else if (pal == PALETTE_SUNSET) { r += (249.0f - r) * w; g += (115.0f - g) * w; b += (22.0f - b) * w; }
                else if (pal == PALETTE_CYBER) { r += (6.0f - r) * w; g += (182.0f - g) * w; b += (212.0f - b) * w; }
                else { r += (155.0f - r) * w; g += (114.0f - g) * w; b += (207.0f - b) * w; }
            }

            float d3_sq = (px - n3_x) * (px - n3_x) + (py - n3_y) * (py - n3_y);
            if (d3_sq < r3_sq) {
                float w = 1.0f - sqrtf(d3_sq) * inv_r3;
                w = w * w * (3.0f - 2.0f * w) * 0.76f;
                if (pal == PALETTE_OBSIDIAN) { r += (190.0f - r) * w; g += (24.0f - g) * w; b += (93.0f - b) * w; }
                else if (pal == PALETTE_SUNSET) { r += (234.0f - r) * w; g += (179.0f - g) * w; b += (8.0f - b) * w; }
                else if (pal == PALETTE_CYBER) { r += (59.0f - r) * w; g += (130.0f - g) * w; b += (246.0f - b) * w; }
                else { r += (236.0f - r) * w; g += (72.0f - g) * w; b += (153.0f - b) * w; }
            }

            float d4_sq = (px - n4_x) * (px - n4_x) + (py - n4_y) * (py - n4_y);
            if (d4_sq < r4_sq) {
                float w = 1.0f - sqrtf(d4_sq) * inv_r4;
                w = w * w * (3.0f - 2.0f * w) * 0.70f;
                if (pal == PALETTE_OBSIDIAN) { r += (217.0f - r) * w; g += (119.0f - g) * w; b += (6.0f - b) * w; }
                else if (pal == PALETTE_SUNSET) { r += (168.0f - r) * w; g += (85.0f - g) * w; b += (247.0f - b) * w; }
                else if (pal == PALETTE_CYBER) { r += (99.0f - r) * w; g += (102.0f - g) * w; b += (241.0f - b) * w; }
                else { r += (251.0f - r) * w; g += (146.0f - g) * w; b += (60.0f - b) * w; }
            }

            float d5_sq = (px - n5_x) * (px - n5_x) + (py - n5_y) * (py - n5_y);
            if (d5_sq < r5_sq) {
                float w = 1.0f - sqrtf(d5_sq) * inv_r5;
                w = w * w * (3.0f - 2.0f * w) * 0.65f;
                if (pal == PALETTE_OBSIDIAN) { r += (6.0f - r) * w; g += (95.0f - g) * w; b += (70.0f - b) * w; }
                else if (pal == PALETTE_SUNSET) { r += (236.0f - r) * w; g += (72.0f - g) * w; b += (153.0f - b) * w; }
                else if (pal == PALETTE_CYBER) { r += (52.0f - r) * w; g += (211.0f - g) * w; b += (153.0f - b) * w; }
                else { r += (182.0f - r) * w; g += (212.0f - g) * w; }
            }

            if (disturbance) {
                float dm_sq = (px - mx) * (px - mx) + (py - my) * (py - my);
                if (dm_sq < rm_sq) {
                    float w = 1.0f - sqrtf(dm_sq) * inv_rm;
                    w = w * w * (3.0f - 2.0f * w) * 0.55f;
                    if (pal == PALETTE_OBSIDIAN) { r += (51.0f - r) * w; g += (65.0f - g) * w; b += (85.0f - b) * w; }
                    else { r += (147.0f - r) * w; g += (197.0f - g) * w; b += (253.0f - b) * w; }
                }
            }

            plat->grid_r[y][x] = rife_clampf(r, 0.0f, 255.0f);
            plat->grid_g[y][x] = rife_clampf(g, 0.0f, 255.0f);
            plat->grid_b[y][x] = rife_clampf(b, 0.0f, 255.0f);
        }
    }

    int width = (plat->win_width > 7680) ? 7680 : plat->win_width;
    int height = plat->win_height;
    float inv_h = (height > 1) ? (1.0f / (float)(height - 1)) : 0.0f;
    float gh_scale = (float)(GRID_H - 1);

    float scan_r[GRID_W];
    float scan_g[GRID_W];
    float scan_b[GRID_W];

    for (int y = 0; y < height; y++) {
        float gy = (float)y * inv_h * gh_scale;
        int y0 = (int)gy;
        int y1 = (y0 < GRID_H - 1) ? y0 + 1 : y0;
        float ty = gy - (float)y0;

        const float* r0_row = plat->grid_r[y0];
        const float* r1_row = plat->grid_r[y1];
        const float* g0_row = plat->grid_g[y0];
        const float* g1_row = plat->grid_g[y1];
        const float* b0_row = plat->grid_b[y0];
        const float* b1_row = plat->grid_b[y1];

        for (int gx = 0; gx < GRID_W; gx++) {
            scan_r[gx] = r0_row[gx] + (r1_row[gx] - r0_row[gx]) * ty;
            scan_g[gx] = g0_row[gx] + (g1_row[gx] - g0_row[gx]) * ty;
            scan_b[gx] = b0_row[gx] + (b1_row[gx] - b0_row[gx]) * ty;
        }

        uint32_t* line = &plat->pixels[y * plat->win_width];
        for (int x = 0; x < width; x++) {
            int x0 = plat->hlook[x].x0;
            int x1 = plat->hlook[x].x1;
            float tx = plat->hlook[x].tx;

            uint32_t ir = (uint32_t)(scan_r[x0] + (scan_r[x1] - scan_r[x0]) * tx);
            uint32_t ig = (uint32_t)(scan_g[x0] + (scan_g[x1] - scan_g[x0]) * tx);
            uint32_t ib = (uint32_t)(scan_b[x0] + (scan_b[x1] - scan_b[x0]) * tx);
            line[x] = (ir << 16) | (ig << 8) | ib;
        }
    }
}

void rife_draw_subpixel_liquid_glass(Win32Platform* plat, float gx, float gy, float gw, float gh, float radius, float glass_alpha, bool highlight, bool specular_rim, uint32_t tint_rgb) {
    if (!plat || !plat->pixels) return;
    int x0 = (int)floorf(gx);
    int y0 = (int)floorf(gy);
    int x1 = (int)ceilf(gx + gw);
    int y1 = (int)ceilf(gy + gh);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > plat->win_width) x1 = plat->win_width;
    if (y1 > plat->win_height) y1 = plat->win_height;

    float alpha = highlight ? (glass_alpha + 0.08f) : glass_alpha;
    alpha = rife_clampf(alpha, 0.0f, 0.96f);
    int width = plat->win_width;
    float tr = (float)((tint_rgb >> 16) & 0xFF);
    float tg = (float)((tint_rgb >> 8) & 0xFF);
    float tb = (float)(tint_rgb & 0xFF);

    float half_w = gw * 0.5f;
    float half_h = gh * 0.5f;
    float center_x = gx + half_w;
    float center_y = gy + half_h;
    float r_clamped = radius;
    if (r_clamped > half_w) r_clamped = half_w;
    if (r_clamped > half_h) r_clamped = half_h;
    float inner_w = half_w - r_clamped;
    float inner_h = half_h - r_clamped;

    for (int y = y0; y < y1; y++) {
        float py = (float)y + 0.5f;
        uint32_t* line = &plat->pixels[y * width];
        float qy = fabsf(py - center_y) - inner_h;
        float vert = (py - gy) / gh;
        float specular = (specular_rim && py - gy < 3.2f) ? (1.0f - (py - gy) * 0.28f) * 42.0f : 0.0f;
        float blend_alpha = alpha * (0.85f - vert * 0.15f);

        for (int x = x0; x < x1; x++) {
            float px = (float)x + 0.5f;
            float qx = fabsf(px - center_x) - inner_w;
            float dist;

            if (qx <= 0.0f && qy <= 0.0f) {
                float in_val = (qx > qy) ? qx : qy;
                dist = in_val - r_clamped;
            }
            else if (qx > 0.0f && qy <= 0.0f) {
                dist = qx - r_clamped;
            }
            else if (qx <= 0.0f && qy > 0.0f) {
                dist = qy - r_clamped;
            }
            else {
                dist = sqrtf(qx * qx + qy * qy) - r_clamped;
            }

            if (dist > 1.0f) continue;

            uint32_t orig = line[x];
            float ob = (float)(orig & 0xFF);
            float og = (float)((orig >> 8) & 0xFF);
            float or_ = (float)((orig >> 16) & 0xFF);

            float r = rife_lerpf(or_, tr, blend_alpha) + specular;
            float g = rife_lerpf(og, tg, blend_alpha) + specular;
            float b = rife_lerpf(ob, tb, blend_alpha) + specular;

            if (dist <= -1.6f) {
                line[x] = ((uint32_t)rife_clampf(r, 0.0f, 255.0f) << 16) |
                    ((uint32_t)rife_clampf(g, 0.0f, 255.0f) << 8) |
                    (uint32_t)rife_clampf(b, 0.0f, 255.0f);
                continue;
            }

            if (specular_rim) {
                float stroke_factor = rife_clampf(1.0f - fabsf(dist + 0.6f), 0.0f, 1.0f);
                if (stroke_factor > 0.0f) {
                    float rim = (vert < 0.22f) ? 1.25f : ((vert < 0.55f) ? 0.70f : 0.35f);
                    float s = stroke_factor * rim;
                    r = rife_lerpf(r, 255.0f, s * 1.05f);
                    g = rife_lerpf(g, 255.0f, s);
                    b = rife_lerpf(b, 255.0f, s * 0.95f);
                }
            }

            float coverage = rife_clampf(0.5f - dist, 0.0f, 1.0f);
            uint32_t final_r = (uint32_t)rife_clampf(rife_lerpf(or_, r, coverage), 0.0f, 255.0f);
            uint32_t final_g = (uint32_t)rife_clampf(rife_lerpf(og, g, coverage), 0.0f, 255.0f);
            uint32_t final_b = (uint32_t)rife_clampf(rife_lerpf(ob, b, coverage), 0.0f, 255.0f);
            line[x] = (final_r << 16) | (final_g << 8) | final_b;
        }
    }
}

void rife_draw_subpixel_circle(Win32Platform* plat, float cx, float cy, float radius, uint32_t fill_color, uint32_t border_color) {
    if (!plat || !plat->pixels) return;
    int x0 = (int)floorf(cx - radius - 1.0f);
    int y0 = (int)floorf(cy - radius - 1.0f);
    int x1 = (int)ceilf(cx + radius + 1.0f);
    int y1 = (int)ceilf(cy + radius + 1.0f);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > plat->win_width) x1 = plat->win_width;
    if (y1 > plat->win_height) y1 = plat->win_height;

    float fr = (float)((fill_color >> 16) & 0xFF);
    float fg = (float)((fill_color >> 8) & 0xFF);
    float fb = (float)(fill_color & 0xFF);
    float br = (float)((border_color >> 16) & 0xFF);
    float bg = (float)((border_color >> 8) & 0xFF);
    float bb = (float)(border_color & 0xFF);
    int width = plat->win_width;

    for (int y = y0; y < y1; y++) {
        float py = (float)y + 0.5f;
        uint32_t* line = &plat->pixels[y * width];
        for (int x = x0; x < x1; x++) {
            float px = (float)x + 0.5f;
            float d_sq = (px - cx) * (px - cx) + (py - cy) * (py - cy);
            if (d_sq > (radius + 1.0f) * (radius + 1.0f)) continue;

            float d = sqrtf(d_sq) - radius;
            float coverage = rife_clampf(0.5f - d, 0.0f, 1.0f);
            if (coverage <= 0.0f) continue;

            uint32_t orig = line[x];
            float ob = (float)(orig & 0xFF);
            float og = (float)((orig >> 8) & 0xFF);
            float or_ = (float)((orig >> 16) & 0xFF);

            float stroke_factor = rife_clampf(1.0f - fabsf(d + 0.5f), 0.0f, 1.0f);
            float r = rife_lerpf(fr, br, stroke_factor);
            float g = rife_lerpf(fg, bg, stroke_factor);
            float b = rife_lerpf(fb, bb, stroke_factor);

            uint32_t final_r = (uint32_t)rife_clampf(rife_lerpf(or_, r, coverage), 0.0f, 255.0f);
            uint32_t final_g = (uint32_t)rife_clampf(rife_lerpf(og, g, coverage), 0.0f, 255.0f);
            uint32_t final_b = (uint32_t)rife_clampf(rife_lerpf(ob, b, coverage), 0.0f, 255.0f);
            line[x] = (final_r << 16) | (final_g << 8) | final_b;
        }
    }
}

void rife_draw_procedural_icon_direct(HDC hdc, float x, float y, float size, uint32_t col_top, uint32_t col_bot, const char* glyph, HICON custom_icon, bool force_native) {
    if (force_native && custom_icon) {
        DrawIconEx(hdc, (int)(x + (size - 32.0f) * 0.5f), (int)(y + (size - 32.0f) * 0.5f), custom_icon, 32, 32, 0, NULL, DI_NORMAL);
        return;
    }

    int r = (int)(size * 0.24f);
    SetDCPenColor(hdc, RGB(255, 255, 255));
    SetDCBrushColor(hdc, RGB((col_bot >> 24) & 0xFF, (col_bot >> 16) & 0xFF, (col_bot >> 8) & 0xFF));
    RoundRect(hdc, (int)x, (int)y, (int)(x + size), (int)(y + size), r, r);

    SetDCPenColor(hdc, RGB((col_top >> 24) & 0xFF, (col_top >> 16) & 0xFF, (col_top >> 8) & 0xFF));
    SetDCBrushColor(hdc, RGB((col_top >> 24) & 0xFF, (col_top >> 16) & 0xFF, (col_top >> 8) & 0xFF));
    RoundRect(hdc, (int)(x + 1.0f), (int)(y + 1.0f), (int)(x + size - 1.0f), (int)(y + size * 0.48f), r - 2, r - 2);

    // 顶部 1px 晶莹高光线 (Pebble Sheen)
    SetDCPenColor(hdc, RGB(255, 255, 255));
    MoveToEx(hdc, (int)(x + (float)r * 0.7f), (int)(y + 1.0f), NULL);
    LineTo(hdc, (int)(x + size - (float)r * 0.7f), (int)(y + 1.0f));

    if (custom_icon) {
        DrawIconEx(hdc, (int)(x + (size - 24.0f) * 0.5f), (int)(y + (size - 24.0f) * 0.5f), custom_icon, 24, 24, 0, NULL, DI_NORMAL);
    }
    else if (glyph && glyph[0]) {
        SetTextColor(hdc, RGB(255, 255, 255));
        int tx = (int)(x + size * 0.32f);
        int ty = (int)(y + size * 0.22f);
        if (strlen(glyph) > 1) tx = (int)(x + size * 0.20f);
        rife_draw_text_u8(hdc, tx, ty, glyph);
    }
}

static void rife_draw_resize_arrow_hint(HDC hdc, float mx, float my, int dir) {
    if (dir <= 0) return;
    float bx = mx + 12.0f;
    float by = my + 12.0f;
    float bw = 24.0f;
    float bh = 24.0f;

    SetDCPenColor(hdc, RGB(226, 232, 240));
    SetDCBrushColor(hdc, RGB(15, 23, 42));
    RoundRect(hdc, (int)bx, (int)by, (int)(bx + bw), (int)(by + bh), 8, 8);

    HPEN pen = (HPEN)GetStockObject(DC_PEN);
    SetDCPenColor(hdc, RGB(56, 189, 248));
    HPEN old_pen = (HPEN)SelectObject(hdc, pen);

    float cx = bx + 12.0f;
    float cy = by + 12.0f;

    if (((dir & 1) && (dir & 4)) || ((dir & 2) && (dir & 8))) {
        MoveToEx(hdc, (int)(cx - 5), (int)(cy - 5), NULL); LineTo(hdc, (int)(cx + 5), (int)(cy + 5));
        MoveToEx(hdc, (int)(cx - 5), (int)(cy - 2), NULL); LineTo(hdc, (int)(cx - 5), (int)(cy - 5)); LineTo(hdc, (int)(cx - 2), (int)(cy - 5));
        MoveToEx(hdc, (int)(cx + 5), (int)(cy + 2), NULL); LineTo(hdc, (int)(cx + 5), (int)(cy + 5)); LineTo(hdc, (int)(cx + 2), (int)(cy + 5));
    }
    else if (((dir & 2) && (dir & 4)) || ((dir & 1) && (dir & 8))) {
        MoveToEx(hdc, (int)(cx + 5), (int)(cy - 5), NULL); LineTo(hdc, (int)(cx - 5), (int)(cy + 5));
        MoveToEx(hdc, (int)(cx + 5), (int)(cy - 2), NULL); LineTo(hdc, (int)(cx + 5), (int)(cy - 5)); LineTo(hdc, (int)(cx + 2), (int)(cy - 5));
        MoveToEx(hdc, (int)(cx - 5), (int)(cy + 2), NULL); LineTo(hdc, (int)(cx - 5), (int)(cy + 5)); LineTo(hdc, (int)(cx - 2), (int)(cy + 5));
    }
    else if (dir & 3) {
        MoveToEx(hdc, (int)(cx - 6), (int)cy, NULL); LineTo(hdc, (int)(cx + 6), (int)cy);
        MoveToEx(hdc, (int)(cx - 3), (int)(cy - 3), NULL); LineTo(hdc, (int)(cx - 6), (int)cy); LineTo(hdc, (int)(cx - 3), (int)(cy + 3));
        MoveToEx(hdc, (int)(cx + 3), (int)(cy - 3), NULL); LineTo(hdc, (int)(cx + 6), (int)cy); LineTo(hdc, (int)(cx + 3), (int)(cy + 3));
    }
    else if (dir & 12) {
        MoveToEx(hdc, (int)cx, (int)(cy - 6), NULL); LineTo(hdc, (int)cx, (int)(cy + 6));
        MoveToEx(hdc, (int)(cx - 3), (int)(cy - 3), NULL); LineTo(hdc, (int)cx, (int)(cy - 6)); LineTo(hdc, (int)(cx + 3), (int)(cy - 3));
        MoveToEx(hdc, (int)(cx - 3), (int)(cy + 3), NULL); LineTo(hdc, (int)cx, (int)(cy + 6)); LineTo(hdc, (int)(cx + 3), (int)(cy + 3));
    }

    SelectObject(hdc, old_pen);
}

void rife_render_flush(RifeCore* core) {
    Win32Platform* plat = (Win32Platform*)core->platform_data;
    if (!plat || !plat->hdc_mem || !plat->pixels) return;

    RifeSystemConfig* cfg = rife_get_system_config();

    if (cfg->font_scale != plat->current_font_scale) {
        update_system_fonts(plat, cfg->font_scale);
    }

    bool specular_rim = cfg->specular_rim;
    float glass_alpha = cfg->glass_alpha;
    bool force_native_ico = (cfg->icon_style == ICON_STYLE_NATIVE_ICO);
    bool is_zh = (cfg->language == LANG_ZH_CN);
    bool is_obsidian = (cfg->palette == PALETTE_OBSIDIAN);

    apply_desktop_mode(plat, cfg->desktop_mode);

    HDC hdc_win = GetDC(plat->hwnd);
    rife_render_gemini_light_field(core, plat, cfg);

    float ww = (float)plat->win_width;
    float wh = (float)plat->win_height;
    float mx = core->input.mouse_x;
    float my = core->input.mouse_y;

    if (plat->snap_preview) {
        rife_draw_subpixel_liquid_glass(plat, 8.0f, 8.0f, ww - 16.0f, wh - 16.0f, 16.0f, 0.40f, true, true, 0xFFFFFF);
    }

    float col_size = 22.0f;
    float orig_x = (ww - col_size) * 0.5f;
    float orig_y = 10.0f;

    // 1. ColorOS 风格流体云：未展开抽屉时在顶部常驻
    if (plat->drawer_anim < 0.99f) {
        float exp_w = ww * 0.50f;
        if (exp_w < 300.0f) exp_w = 300.0f;
        float exp_h = 42.0f;

        float cloud_w = rife_lerpf(col_size, exp_w, plat->cloud_anim);
        float cloud_h = rife_lerpf(col_size, exp_h, plat->cloud_anim);
        float cloud_x = (ww - cloud_w) * 0.5f;
        float cloud_y = 10.0f;
        float cloud_r = cloud_h * 0.5f;
        bool cloud_hvr = (mx >= cloud_x && mx <= cloud_x + cloud_w && my >= cloud_y && my <= cloud_y + cloud_h);

        float c_alpha = 0.82f;
        if (is_obsidian || cfg->cloud_color == CLOUD_COLOR_OBSIDIAN) c_alpha = 0.92f;
        else if (cfg->cloud_color == CLOUD_COLOR_AZURE) c_alpha = 0.88f;

        // 根据光场流体色彩设置联动流体云材质底色 (Paletted Glass Substrate)
        uint32_t cloud_tint = 0xFFFFFF;
        if (is_obsidian || cfg->cloud_color == CLOUD_COLOR_OBSIDIAN) {
            cloud_tint = 0x161122; // 深度曜石暗晶黑紫 (Deep Smoked Obsidian Dark Glass)
        }
        else if (cfg->palette == PALETTE_SUNSET || cfg->cloud_color == CLOUD_COLOR_VIOLET) {
            cloud_tint = 0xFFEADB; // 日落暖橙金
        }
        else if (cfg->palette == PALETTE_CYBER || cfg->cloud_color == CLOUD_COLOR_AZURE) {
            cloud_tint = 0xD9EFFF; // 极客霓虹冰蓝
        }
        else {
            cloud_tint = 0xDEFAF8; // 双子星极光晶白青
        }
        rife_draw_subpixel_liquid_glass(plat, cloud_x, cloud_y, cloud_w, cloud_h, cloud_r, c_alpha, cloud_hvr || plat->cloud_expanded, specular_rim, cloud_tint);

        // 仿生非对称呼吸灯 (与光场色彩预设同步联动)
        if (plat->cloud_anim < 0.25f) {
            float cx = cloud_x + cloud_w * 0.5f;
            float cy = cloud_y + cloud_h * 0.5f;
            float norm_breath = (expf(sinf(plat->breath_t)) - 0.367879f) / 2.350402f;
            float m_dist = sqrtf((mx - cx) * (mx - cx) + (my - cy) * (my - cy));
            float sense_boost = (m_dist < 80.0f) ? (1.0f - m_dist / 80.0f) * 0.16f : 0.0f;
            float breath = rife_clampf(norm_breath + sense_boost, 0.0f, 1.0f);

            uint32_t fill_col, border_col;
            float target_r, target_g, target_b;

            BreathColorType bc = cfg->breath_color;
            if (cfg->palette == PALETTE_OBSIDIAN) bc = BREATH_COLOR_VIOLET;
            else if (cfg->palette == PALETTE_SUNSET) bc = BREATH_COLOR_AMBER;
            else if (cfg->palette == PALETTE_CYBER) bc = BREATH_COLOR_AZURE;
            else if (cfg->palette == PALETTE_GEMINI && cfg->breath_color == BREATH_COLOR_EMERALD) bc = BREATH_COLOR_CYAN;

            switch (bc) {
            case BREATH_COLOR_CYAN:
                fill_col = 0x06B6D4; border_col = 0x67E8F9;
                target_r = 6.0f; target_g = 182.0f; target_b = 212.0f; break;
            case BREATH_COLOR_AZURE:
                fill_col = 0x3B82F6; border_col = 0x93C5FD;
                target_r = 59.0f; target_g = 130.0f; target_b = 246.0f; break;
            case BREATH_COLOR_AMBER:
                fill_col = 0xF59E0B; border_col = 0xFDE68A;
                target_r = 245.0f; target_g = 158.0f; target_b = 11.0f; break;
            case BREATH_COLOR_VIOLET:
                fill_col = 0xA855F7; border_col = 0xE9D5FF;
                target_r = 168.0f; target_g = 85.0f; target_b = 247.0f; break;
            case BREATH_COLOR_EMERALD:
            default:
                fill_col = 0x22C55E; border_col = 0x86EFAC;
                target_r = 34.0f; target_g = 245.0f; target_b = 142.0f; break;
            }

            float halo_r = 12.0f;
            int h_x0 = (int)(cx - halo_r); if (h_x0 < 0) h_x0 = 0;
            int h_x1 = (int)(cx + halo_r + 1.0f); if (h_x1 > plat->win_width) h_x1 = plat->win_width;
            int h_y0 = (int)(cy - halo_r); if (h_y0 < 0) h_y0 = 0;
            int h_y1 = (int)(cy + halo_r + 1.0f); if (h_y1 > plat->win_height) h_y1 = plat->win_height;

            float glow_intensity = 0.18f + 0.72f * breath;
            float inv_halo_sq = 1.0f / (halo_r * halo_r);
            for (int gy = h_y0; gy < h_y1; gy++) {
                uint32_t* line = &plat->pixels[gy * plat->win_width];
                float py = (float)gy + 0.5f;
                float dy = py - cy;
                float dy_sq = dy * dy;
                for (int gx = h_x0; gx < h_x1; gx++) {
                    float px = (float)gx + 0.5f;
                    float dx = px - cx;
                    float d_sq = dx * dx + dy_sq;
                    if (d_sq < halo_r * halo_r) {
                        float factor = expf(-3.2f * d_sq * inv_halo_sq) * glow_intensity;
                        uint32_t pix = line[gx];
                        float b = (float)(pix & 0xFF);
                        float g = (float)((pix >> 8) & 0xFF);
                        float r = (float)((pix >> 16) & 0xFF);

                        r += (target_r - r) * factor * 0.40f;
                        g += (target_g - g) * factor * 0.88f;
                        b += (target_b - b) * factor * 0.45f;

                        line[gx] = ((uint32_t)rife_clampf(r, 0, 255) << 16) |
                            ((uint32_t)rife_clampf(g, 0, 255) << 8) |
                            (uint32_t)rife_clampf(b, 0, 255);
                    }
                }
            }

            float core_r = 3.4f + 6.2f * breath;
            rife_draw_subpixel_circle(plat, cx, cy, core_r, fill_col, border_col);
            if (breath > 0.28f) {
                float white_r = (breath - 0.28f) * 2.5f;
                rife_draw_subpixel_circle(plat, cx, cy - 0.4f, white_r, 0xFFFFFF, 0xFFFFFF);
            }

            // 微球吞噬光子扩散波 (Photonic Absorption Ripple Ring)
            if (plat->absorption_ripple_t > 0.01f) {
                float rip_progress = 1.0f - plat->absorption_ripple_t;
                float ring_r = 9.0f + rip_progress * 26.0f;
                float ring_w = 2.4f;
                float rip_alpha = plat->absorption_ripple_t * 0.75f;
                int rx0 = (int)floorf(cx - ring_r - ring_w - 1.0f);
                int ry0 = (int)floorf(cy - ring_r - ring_w - 1.0f);
                int rx1 = (int)ceilf(cx + ring_r + ring_w + 1.0f);
                int ry1 = (int)ceilf(cy + ring_r + ring_w + 1.0f);
                if (rx0 < 0) rx0 = 0;
                if (ry0 < 0) ry0 = 0;
                if (rx1 > plat->win_width) rx1 = plat->win_width;
                if (ry1 > plat->win_height) ry1 = plat->win_height;
                for (int ry = ry0; ry < ry1; ry++) {
                    float py = (float)ry + 0.5f;
                    float dy = py - cy;
                    uint32_t* rline = &plat->pixels[ry * plat->win_width];
                    for (int rx = rx0; rx < rx1; rx++) {
                        float px = (float)rx + 0.5f;
                        float dx = px - cx;
                        float dist = sqrtf(dx * dx + dy * dy);
                        float delta = fabsf(dist - ring_r);
                        if (delta < ring_w) {
                            float factor = (1.0f - delta / ring_w) * rip_alpha;
                            uint32_t pix = rline[rx];
                            float b = (float)(pix & 0xFF);
                            float g = (float)((pix >> 8) & 0xFF);
                            float r = (float)((pix >> 16) & 0xFF);
                            r += (target_r - r) * factor;
                            g += (target_g - g) * factor;
                            b += (target_b - b) * factor;
                            rline[rx] = ((uint32_t)rife_clampf(r, 0.0f, 255.0f) << 16) |
                                        ((uint32_t)rife_clampf(g, 0.0f, 255.0f) << 8) |
                                        (uint32_t)rife_clampf(b, 0.0f, 255.0f);
                        }
                    }
                }
            }
        }

        if (plat->cloud_anim > 0.35f) {
            float ly = cloud_y + cloud_h * 0.5f;
            rife_draw_subpixel_circle(plat, cloud_x + 24.0f, ly, 5.5f, 0xFF5F56, 0xE0443E);
            rife_draw_subpixel_circle(plat, cloud_x + 44.0f, ly, 5.5f, 0xFFBD2E, 0xDEA123);
            rife_draw_subpixel_circle(plat, cloud_x + 64.0f, ly, 5.5f, 0x27C93F, 0x1AAB29);
        }
    }

    // 2. 应用抽屉：从顶部流体云向下展开 (~1/3 屏幕面积)
    if (plat->drawer_anim > 0.01f) {
        float dw_w = ww * 0.60f;
        if (dw_w < 360.0f) dw_w = 360.0f;
        float dw_h = wh * 0.56f;
        if (dw_h < 240.0f) dw_h = 240.0f;
        float target_dw_x = (ww - dw_w) * 0.5f;
        float target_dw_y = (wh - dw_h) * 0.44f;

        float ease = rife_smootherstep(plat->drawer_anim);

        float cur_dw_x = rife_lerpf(orig_x, target_dw_x, ease);
        float cur_dw_y = rife_lerpf(orig_y, target_dw_y, ease);
        float cur_dw_w = rife_lerpf(col_size, dw_w, ease);
        float cur_dw_h = rife_lerpf(col_size, dw_h, ease);
        float cur_dw_r = rife_lerpf(col_size * 0.5f, 22.0f, ease);

        rife_draw_subpixel_liquid_glass(plat, cur_dw_x, cur_dw_y, cur_dw_w, cur_dw_h, cur_dw_r, is_obsidian ? 0.94f : 0.90f, false, specular_rim, is_obsidian ? 0x161122 : 0xFFFFFF);
    }

    // 3. 应用窗口：从顶部流体云双向缩放展开/缩回 (活动窗口后绘制置顶)
    for (int pass = 0; pass < 2; pass++) {
        for (size_t i = 0; i < g_installed_app_count; i++) {
            bool is_active = ((int)i == plat->active_win_idx);
            if ((pass == 0 && is_active) || (pass == 1 && !is_active)) continue;

            ActiveWindow* win = &plat->windows[i];
            if (!win->inst || win->anim < 0.01f) continue;

            float target_x = win->is_maximized ? 0.0f : win->x;
            float target_y = win->is_maximized ? 0.0f : win->y;
            float target_w = win->is_maximized ? ww : win->w;
            float target_h = win->is_maximized ? wh : win->h;
            float target_r = win->is_maximized ? 0.0f : 22.0f;

            float cur_x, cur_y, cur_w, cur_h, cur_r;
            if (win->anim >= 0.999f) {
                cur_x = target_x;
                cur_y = target_y;
                cur_w = target_w;
                cur_h = target_h;
                cur_r = target_r;
            } else {
                float ease = rife_smootherstep(win->anim);
                cur_x = floorf(rife_lerpf(orig_x, target_x, ease));
                cur_y = floorf(rife_lerpf(orig_y, target_y, ease));
                cur_w = floorf(rife_lerpf(col_size, target_w, ease));
                cur_h = floorf(rife_lerpf(col_size, target_h, ease));
                cur_r = rife_lerpf(col_size * 0.5f, target_r, ease);
            }

            rife_draw_subpixel_liquid_glass(plat, cur_x, cur_y, cur_w, cur_h, cur_r, is_obsidian ? 0.96f : 0.94f, false, specular_rim, is_obsidian ? 0x161122 : 0xFFFFFF);
        }
    }

    // 4. 桌面快捷方式
    for (size_t i = 0; i < plat->shortcut_count; i++) {
        DesktopShortcut* sc = &plat->shortcuts[i];
        bool hvr = (mx >= sc->x && mx <= sc->x + 76.0f && my >= sc->y && my <= sc->y + 80.0f);
        rife_draw_subpixel_liquid_glass(plat, sc->x, sc->y, 76.0f, 80.0f, 16.0f, is_obsidian ? 0.88f : glass_alpha, hvr, specular_rim, is_obsidian ? 0x161122 : 0xFFFFFF);
    }

    // 5. 底部纤细修长 Dock 栏 (固定 44px 高度)
    float dock_w = ww * (2.0f / 3.0f);
    if (dock_w < 280.0f) dock_w = 280.0f;
    float dock_x = (cfg->dock_align == DOCK_ALIGN_RIGHT) ? (ww - dock_w - 24.0f) : ((ww - dock_w) * 0.5f);
    float resting_y = wh - 52.0f;
    float active_y = wh - 58.0f;
    float dock_y = rife_lerpf(resting_y, active_y, plat->dock_anim);
    bool dock_hvr = (mx >= dock_x && mx <= dock_x + dock_w && my >= dock_y && my <= dock_y + 44.0f);

    rife_draw_subpixel_liquid_glass(plat, dock_x, dock_y, dock_w, 44.0f, 16.0f, is_obsidian ? 0.90f : glass_alpha, dock_hvr, specular_rim, is_obsidian ? 0x161122 : 0xFFFFFF);

    GdiFlush();
    SetBkMode(plat->hdc_mem, TRANSPARENT);
    SelectObject(plat->hdc_mem, GetStockObject(DC_PEN));
    SelectObject(plat->hdc_mem, GetStockObject(DC_BRUSH));

    // 6. 右下角水印
    SelectObject(plat->hdc_mem, plat->hfont_caption);
    SetTextColor(plat->hdc_mem, is_obsidian ? RGB(167, 139, 250) : RGB(148, 163, 184));
    rife_draw_text_u8(plat->hdc_mem, (int)(ww - 88.0f), (int)(wh - 14.0f), "Made by Renly");

    // 7. 桌面快捷方式文字
    SelectObject(plat->hdc_mem, plat->hfont_sm);
    for (size_t i = 0; i < plat->shortcut_count; i++) {
        DesktopShortcut* sc = &plat->shortcuts[i];
        rife_draw_procedural_icon_direct(plat->hdc_mem, sc->x + 20.0f, sc->y + 12.0f, 36.0f,
            sc->color_top, sc->color_bot, "D", sc->custom_icon, force_native_ico);

        char disp[16];
        snprintf(disp, sizeof(disp), "%s", sc->name);
        if (strlen(sc->name) > 8) {
            disp[7] = '.'; disp[8] = '.'; disp[9] = '.'; disp[10] = '\0';
        }
        SetTextColor(plat->hdc_mem, is_obsidian ? RGB(248, 250, 252) : RGB(15, 23, 42));
        rife_draw_text_u8(plat->hdc_mem, (int)(sc->x + 10.0f), (int)(sc->y + 54.0f), disp);
    }

    // 8. 抽屉内容渲染 (自顶部流体云展开时错落倾泻下落)
    if (plat->drawer_anim > 0.05f) {
        float dw_w = ww * 0.60f;
        if (dw_w < 360.0f) dw_w = 360.0f;
        float dw_h = wh * 0.56f;
        if (dw_h < 240.0f) dw_h = 240.0f;
        float cur_dw_x = (ww - dw_w) * 0.5f;
        float cur_dw_y = (wh - dw_h) * 0.44f;

        float orig_cx = orig_x + col_size * 0.5f;
        float orig_cy = orig_y + col_size * 0.5f;

        if (plat->drawer_anim > 0.18f) {
            float title_t = rife_clampf((plat->drawer_anim - 0.18f) / 0.82f, 0.0f, 1.0f);
            float title_ease = rife_smootherstep(title_t);
            float title_x = rife_lerpf(orig_cx - 40.0f, cur_dw_x + 24.0f, title_ease);
            float title_y = rife_lerpf(orig_cy, cur_dw_y + 18.0f, title_ease);
            SelectObject(plat->hdc_mem, plat->hfont_title);
            SetTextColor(plat->hdc_mem, is_obsidian ? RGB(248, 250, 252) : RGB(15, 23, 42));
            rife_draw_text_u8(plat->hdc_mem, (int)title_x, (int)title_y, is_zh ? "应用程序抽屉" : "Applications");
        }

        int cols = 3;
        float card_w = 110.0f;
        float card_h = 76.0f;
        float gap_x = (dw_w - 48.0f - (float)cols * card_w) / (float)(cols - 1);

        for (size_t i = 0; i < g_installed_app_count; i++) {
            const RifePluginApp* app = g_installed_apps[i];
            int col = (int)(i % cols);
            int row = (int)(i / cols);
            float target_ax = cur_dw_x + 24.0f + (float)col * (card_w + gap_x);
            float target_ay = cur_dw_y + 54.0f + (float)row * (card_h + 16.0f);

            // 错落多相位级联：每行和每列微延迟
            float phase_delay = (float)row * 0.08f + (float)col * 0.04f;
            float raw_card_t = (plat->drawer_anim - phase_delay) / (1.0f - phase_delay);
            float card_t = rife_clampf(raw_card_t, 0.0f, 1.0f);
            float card_ease = rife_fluid_elastic_step(card_t);

            if (card_ease <= 0.02f) continue;

            // 喷泉扇形抛物线微弧度
            float fountain_arc = sinf(3.14159265f * card_ease) * (1.0f - card_ease) * 18.0f;
            float col_offset = ((float)col - 1.0f) * fountain_arc;

            // 位置自微球中心向目标网格流展
            float cur_card_w = rife_lerpf(14.0f, card_w, card_ease);
            float cur_card_h = rife_lerpf(14.0f, card_h, card_ease);
            float cur_ax = rife_lerpf(orig_cx - cur_card_w * 0.5f, target_ax, card_ease) + col_offset;
            float cur_ay = rife_lerpf(orig_cy - cur_card_h * 0.5f, target_ay, card_ease);

            // 卡片暗晶柔接触阴影 (Soft AO Shadow)
            COLORREF shadow_col = is_obsidian ? RGB(10, 8, 18) : RGB(226, 232, 240);
            SetDCPenColor(plat->hdc_mem, shadow_col);
            SetDCBrushColor(plat->hdc_mem, shadow_col);
            RoundRect(plat->hdc_mem, (int)cur_ax, (int)(cur_ay + 2.0f), (int)(cur_ax + cur_card_w), (int)(cur_ay + cur_card_h + 2.0f), (int)(14.0f * card_ease), (int)(14.0f * card_ease));

            bool app_hvr = (mx >= cur_ax && mx <= cur_ax + cur_card_w && my >= cur_ay && my <= cur_ay + cur_card_h);
            COLORREF border_col = is_obsidian ? RGB(68, 56, 92) : RGB(241, 245, 249);
            COLORREF body_col = is_obsidian ? (app_hvr ? RGB(45, 38, 62) : RGB(28, 23, 40)) : (app_hvr ? RGB(255, 255, 255) : RGB(248, 250, 252));
            SetDCPenColor(plat->hdc_mem, border_col);
            SetDCBrushColor(plat->hdc_mem, body_col);
            RoundRect(plat->hdc_mem, (int)cur_ax, (int)cur_ay, (int)(cur_ax + cur_card_w), (int)(cur_ay + cur_card_h), (int)(14.0f * card_ease), (int)(14.0f * card_ease));

            // 图标与文字跟随卡片中心自然膨胀展现
            float icon_scale = rife_clampf((card_t - 0.20f) / 0.80f, 0.0f, 1.0f);
            if (icon_scale > 0.05f) {
                float icon_sz = 32.0f * icon_scale;
                float icon_x = cur_ax + (cur_card_w - icon_sz) * 0.5f;
                float icon_y = cur_ay + 8.0f * icon_scale;
                rife_draw_procedural_icon_direct(plat->hdc_mem, icon_x, icon_y, icon_sz,
                    app->color_top, app->color_bot, app->glyph, NULL, false);

                if (icon_scale > 0.45f) {
                    SelectObject(plat->hdc_mem, plat->hfont_sm);
                    SetTextColor(plat->hdc_mem, is_obsidian ? RGB(241, 245, 249) : RGB(15, 23, 42));
                    rife_draw_text_u8(plat->hdc_mem, (int)(cur_ax + 14.0f), (int)(cur_ay + 48.0f), is_zh ? app->name_zh : app->name_en);
                }
            }
        }
    }

    // 9. 底部纤细 Dock 内图标居中排布 (32px)
    int dock_pinned_count = 0;
    for (size_t i = 0; i < g_installed_app_count; i++) {
        if (g_installed_apps[i]->pin_to_dock) dock_pinned_count++;
    }
    int total_dock_items = dock_pinned_count + 1;
    float item_size = 32.0f;
    float item_gap = 16.0f;
    float total_items_w = (float)total_dock_items * item_size + (float)(total_dock_items - 1) * item_gap;
    float items_start_x = dock_x + (dock_w - total_items_w) * 0.5f;

    int dock_idx = 0;
    for (size_t i = 0; i < g_installed_app_count; i++) {
        const RifePluginApp* app = g_installed_apps[i];
        if (!app->pin_to_dock) continue;

        float btn_x = items_start_x + (float)dock_idx * (item_size + item_gap);
        float btn_y = dock_y + 6.0f;
        bool btn_hvr = (mx >= btn_x && mx <= btn_x + item_size && my >= btn_y - 3.0f && my <= btn_y + item_size + 3.0f);
        float off_y = btn_hvr ? -3.0f : 0.0f;

        rife_draw_procedural_icon_direct(plat->hdc_mem, btn_x, btn_y + off_y, item_size,
            app->color_top, app->color_bot, app->glyph, NULL, false);
        dock_idx++;
    }

    // 抽屉触发九宫格图标
    float all_btn_x = items_start_x + (float)dock_pinned_count * (item_size + item_gap);
    float all_btn_y = dock_y + 6.0f;
    bool all_hvr = (mx >= all_btn_x && mx <= all_btn_x + item_size && my >= all_btn_y - 3.0f && my <= all_btn_y + item_size + 3.0f);
    float all_off_y = all_hvr ? -3.0f : 0.0f;

    rife_draw_subpixel_liquid_glass(plat, all_btn_x, all_btn_y + all_off_y, item_size, item_size, 10.0f, is_obsidian ? 0.88f : 0.82f, all_hvr || plat->drawer_open, specular_rim, is_obsidian ? 0x161122 : 0xFFFFFF);

    float dot_ox = all_btn_x + 8.5f;
    float dot_oy = all_btn_y + all_off_y + 8.5f;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            COLORREF tile_col = (r == 1 && c == 1) ? RGB(168, 85, 247) : (is_obsidian ? RGB(226, 232, 240) : RGB(15, 23, 42));
            SetDCPenColor(plat->hdc_mem, tile_col);
            SetDCBrushColor(plat->hdc_mem, tile_col);
            RoundRect(plat->hdc_mem, (int)(dot_ox + (float)c * 5.5f), (int)(dot_oy + (float)r * 5.5f),
                (int)(dot_ox + (float)c * 5.5f + 3.0f), (int)(dot_oy + (float)r * 5.5f + 3.0f), 1, 1);
        }
    }

    // 10. 活动应用窗口内容与三色控制灯 (自流体云膨胀与湮灭吞噬，活动窗口后绘制置顶)
    for (int pass = 0; pass < 2; pass++) {
        for (size_t i = 0; i < g_installed_app_count; i++) {
            bool is_active = ((int)i == plat->active_win_idx);
            if ((pass == 0 && is_active) || (pass == 1 && !is_active)) continue;

            ActiveWindow* win = &plat->windows[i];
            if (!win->inst || win->anim < 0.02f) continue;

            float target_x = win->is_maximized ? 0.0f : win->x;
            float target_y = win->is_maximized ? 0.0f : win->y;
            float target_w = win->is_maximized ? ww : win->w;
            float target_h = win->is_maximized ? wh : win->h;

            float cur_x, cur_y, cur_w, cur_h;
            if (win->anim >= 0.999f) {
                cur_x = target_x;
                cur_y = target_y;
                cur_w = target_w;
                cur_h = target_h;
            } else {
                float ease = rife_smootherstep(win->anim);
                cur_x = floorf(rife_lerpf(orig_x, target_x, ease));
                cur_y = floorf(rife_lerpf(orig_y, target_y, ease));
                cur_w = floorf(rife_lerpf(col_size, target_w, ease));
                cur_h = floorf(rife_lerpf(col_size, target_h, ease));
            }

            // 控制灯自微球中心向两翼平滑展开
            float ease_lamp = (win->anim >= 0.999f) ? 1.0f : rife_smootherstep(win->anim);
            float light_scale = rife_clampf(ease_lamp * 1.6f, 0.0f, 1.0f);
            if (light_scale > 0.05f) {
                float l_sz = 12.0f * light_scale;
                float l_r = l_sz * 0.5f;
                float l_y = cur_y + 14.0f * light_scale;
                rife_draw_round_rect(core, cur_x + 16.0f * light_scale, l_y, l_sz, l_sz, l_r, 0xFF5F56FF, 0xE0443EFF);
                rife_draw_round_rect(core, cur_x + 34.0f * light_scale, l_y, l_sz, l_sz, l_r, 0xFFBD2EFF, 0xDEA123FF);
                rife_draw_round_rect(core, cur_x + 52.0f * light_scale, l_y, l_sz, l_sz, l_r, 0x27C93FFF, 0x1AAB29FF);
            }

            // 窗口标题：自流体云向右舒展
            if (win->anim > 0.25f && cur_w > 160.0f) {
                float title_alpha = rife_clampf((win->anim - 0.25f) / 0.75f, 0.0f, 1.0f);
                float title_off = (1.0f - title_alpha) * 12.0f;
                rife_draw_text_font(core, cur_x + 78.0f + title_off, cur_y + 10.0f, is_zh ? win->plugin->name_zh : win->plugin->name_en, is_obsidian ? 0xF8FAFCFF : 0x0F172AFF, 1);
            }

            // 插件界面内容：带动态流体裁剪框，自微球中心向四周铺展
            if (win->plugin->render && cur_h > 46.0f && cur_w > 120.0f) {
                float target_r = win->is_maximized ? 0.0f : 20.0f;
                // 窗口专用安全圆角视口裁剪：严格限制在窗口底板边缘圆角以内，内缩 1px 保护反光边缘，杜绝直角溢出
                rife_push_scissor_round(core, cur_x + 1.0f, cur_y + 36.0f, cur_w - 2.0f, cur_h - 37.0f, target_r);
                // 展开动画过程中以目标基准尺寸稳定排版渲染，结合视口硬件裁切，根除每帧重新排版计算导致的抖动 (Anti-jitter)
                float app_w = (win->anim >= 0.999f) ? (cur_w - 2.0f) : (target_w - 2.0f);
                float app_h = (win->anim >= 0.999f) ? (cur_h - 37.0f) : (target_h - 37.0f);
                win->plugin->render(win->inst, core, cur_x + 1.0f, cur_y + 36.0f, app_w, app_h);
                rife_pop_scissor(core);
            }
        }
    }

    // 11. 提交渲染命令
    RenderCmd* curr = core->render_head;
    while (curr) {
        if (curr->type == CMD_TEXT) {
            if (curr->font_id == 1) SelectObject(plat->hdc_mem, plat->hfont_title);
            else if (curr->font_id == 2) SelectObject(plat->hdc_mem, plat->hfont_panel_title);
            else if (curr->font_id == 3) SelectObject(plat->hdc_mem, plat->hfont_sm);
            else if (curr->font_id == 4) SelectObject(plat->hdc_mem, plat->hfont_caption);
            else if (curr->font_id == 5) SelectObject(plat->hdc_mem, plat->hfont_bold);
            else if (curr->font_id == 6) SelectObject(plat->hdc_mem, plat->hfont_display);
            else SelectObject(plat->hdc_mem, plat->hfont_body);

            uint8_t r = (uint8_t)((curr->color >> 24) & 0xFF);
            uint8_t g = (uint8_t)((curr->color >> 16) & 0xFF);
            uint8_t b = (uint8_t)((curr->color >> 8) & 0xFF);
            SetTextColor(plat->hdc_mem, RGB(r, g, b));
            rife_draw_text_u8(plat->hdc_mem, (int)curr->x, (int)curr->y, curr->text);
        }
        else if (curr->type == CMD_ROUND_RECT) {
            uint8_t r = (uint8_t)((curr->color >> 24) & 0xFF);
            uint8_t g = (uint8_t)((curr->color >> 16) & 0xFF);
            uint8_t b = (uint8_t)((curr->color >> 8) & 0xFF);
            uint8_t br = (uint8_t)((curr->border_color >> 24) & 0xFF);
            uint8_t bg = (uint8_t)((curr->border_color >> 16) & 0xFF);
            uint8_t bb = (uint8_t)((curr->border_color >> 8) & 0xFF);
            SetDCPenColor(plat->hdc_mem, RGB(br, bg, bb));
            SetDCBrushColor(plat->hdc_mem, RGB(r, g, b));
            RoundRect(plat->hdc_mem, (int)curr->x, (int)curr->y, (int)(curr->x + curr->w), (int)(curr->y + curr->h), (int)curr->radius, (int)curr->radius);
        }
        else if (curr->type == CMD_RECT) {
            uint8_t r = (uint8_t)((curr->color >> 24) & 0xFF);
            uint8_t g = (uint8_t)((curr->color >> 16) & 0xFF);
            uint8_t b = (uint8_t)((curr->color >> 8) & 0xFF);
            SetDCPenColor(plat->hdc_mem, RGB(r, g, b));
            SetDCBrushColor(plat->hdc_mem, RGB(r, g, b));
            RECT rc = { (int)curr->x, (int)curr->y, (int)(curr->x + curr->w), (int)(curr->y + curr->h) };
            FillRect(plat->hdc_mem, &rc, (HBRUSH)GetStockObject(DC_BRUSH));
        }
        else if (curr->type == CMD_SCISSOR_PUSH) {
            SaveDC(plat->hdc_mem);
            HRGN rgn = NULL;
            if (curr->radius > 0.5f) {
                // 特殊圆角窗口视口裁剪：顶部在标题栏下方保持水平直线，底部两角严格圆角
                int rx0 = (int)curr->x;
                int ry0 = (int)(curr->y - 36.0f);
                int rx1 = (int)(curr->x + curr->w + 1.0f);
                int ry1 = (int)(curr->y + curr->h + 1.0f);
                int rd = (int)(curr->radius * 2.0f);
                HRGN rgn_win = CreateRoundRectRgn(rx0, ry0, rx1, ry1, rd, rd);
                HRGN rgn_box = CreateRectRgn(rx0, (int)curr->y, rx1, ry1);
                CombineRgn(rgn_box, rgn_box, rgn_win, RGN_AND);
                DeleteObject(rgn_win);
                rgn = rgn_box;
            } else {
                rgn = CreateRectRgn((int)curr->x, (int)curr->y, (int)(curr->x + curr->w + 1.0f), (int)(curr->y + curr->h + 1.0f));
            }
            HRGN existing = CreateRectRgn(0, 0, 0, 0);
            if (GetClipRgn(plat->hdc_mem, existing) == 1) {
                ExtSelectClipRgn(plat->hdc_mem, rgn, RGN_AND);
            } else {
                SelectClipRgn(plat->hdc_mem, rgn);
            }
            DeleteObject(existing);
            DeleteObject(rgn);
        }
        else if (curr->type == CMD_SCISSOR_POP) {
            RestoreDC(plat->hdc_mem, -1);
        }
        curr = curr->next;
    }

    if (plat->hover_resize_dir > 0) {
        rife_draw_resize_arrow_hint(plat->hdc_mem, mx, my, plat->hover_resize_dir);
    }

    BitBlt(hdc_win, 0, 0, plat->win_width, plat->win_height, plat->hdc_mem, 0, 0, SRCCOPY);
    ReleaseDC(plat->hwnd, hdc_win);

    core->render_head = NULL;
    core->render_tail = NULL;
    core->render_cmd_count = 0;
}

void rife_render_immediate(RifeCore* core) {
    if (!core) return;
    arena_reset(&core->frame_arena);
    core->render_head = NULL;
    core->render_tail = NULL;
    core->render_cmd_count = 0;
    rife_render_flush(core);
    core->needs_redraw = false;
}

void rife_platform_resize(RifeCore* core, int w, int h) {
    Win32Platform* plat = (Win32Platform*)core->platform_data;
    if (!plat || w <= 0 || h <= 0) return;
    plat->win_width = w;
    plat->win_height = h;
    update_horiz_lookup(plat, w);

    if (plat->hdc_mem) {
        SelectObject(plat->hdc_mem, plat->hbm_old);
        if (plat->hbm_mem) {
            DeleteObject(plat->hbm_mem);
            plat->hbm_mem = NULL;
        }
        HDC hdc_win = GetDC(plat->hwnd);
        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        plat->hbm_mem = CreateDIBSection(hdc_win, &bmi, DIB_RGB_COLORS, (void**)&plat->pixels, NULL, 0);
        if (plat->hbm_mem) {
            plat->hbm_old = (HBITMAP)SelectObject(plat->hdc_mem, plat->hbm_mem);
        }
        ReleaseDC(plat->hwnd, hdc_win);
    }
    if (plat->pixels) {
        rife_render_immediate(core);
    }
}

LRESULT CALLBACK rife_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    RifeCore* core = (RifeCore*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!core) return DefWindowProc(hwnd, msg, wparam, lparam);
    Win32Platform* plat = (Win32Platform*)core->platform_data;

    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_NCCALCSIZE: if (wparam == TRUE) return 0; break;
    case WM_NCACTIVATE: return TRUE;
    case WM_SYSCOMMAND: {
        if ((wparam & 0xFFF0) == SC_MAXIMIZE) {
            toggle_immersion_fullscreen(plat);
            return 0;
        }
        break;
    }
    case WM_KEYDOWN: {
        if (wparam == VK_ESCAPE) {
            if (plat->is_fullscreen) {
                toggle_immersion_fullscreen(plat);
            }
            plat->cloud_expanded = false;
            plat->drawer_open = false;
            rife_request_redraw(core);
            return 0;
        }
        break;
    }
    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wparam;
        UINT count = DragQueryFileA(hDrop, 0xFFFFFFFF, NULL, 0);
        RifeSystemConfig* cfg = rife_get_system_config();
        bool grid_align = cfg->shortcut_grid_align;
        for (UINT i = 0; i < count; i++) {
            char path[MAX_PATH];
            if (DragQueryFileA(hDrop, i, path, MAX_PATH)) {
                desktop_add_shortcut(plat, path, grid_align);
            }
        }
        DragFinish(hDrop);
        rife_request_redraw(core);
        return 0;
    }
    case WM_NCHITTEST: {
        if (plat->current_mode == DESKTOP_MODE_WALLPAPER || plat->is_fullscreen) return HTCLIENT;
        POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
        ScreenToClient(hwnd, &pt);
        RECT rc;
        GetClientRect(hwnd, &rc);

        int b = 8;
        bool l = pt.x < b;
        bool r = pt.x >= rc.right - b;
        bool t = pt.y < b;
        bool u = pt.y >= rc.bottom - b;
        if (t && l) return HTTOPLEFT;
        if (t && r) return HTTOPRIGHT;
        if (u && l) return HTBOTTOMLEFT;
        if (u && r) return HTBOTTOMRIGHT;
        if (l) return HTLEFT;
        if (r) return HTRIGHT;
        if (t) return HTTOP;
        if (u) return HTBOTTOM;

        float col_size = 22.0f;
        float exp_w = (float)plat->win_width * 0.50f;
        if (exp_w < 300.0f) exp_w = 300.0f;
        float cur_cloud_w = rife_lerpf(col_size, exp_w, plat->cloud_anim);
        float cur_cloud_h = rife_lerpf(col_size, 42.0f, plat->cloud_anim);
        float cur_cloud_x = ((float)plat->win_width - cur_cloud_w) * 0.5f;

        if (pt.y >= 6 && pt.y <= 10 + cur_cloud_h + 4 && pt.x >= cur_cloud_x - 4 && pt.x <= cur_cloud_x + cur_cloud_w + 4) {
            return HTCLIENT;
        }

        if (pt.y <= 30) {
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_SIZE: {
        int w = LOWORD(lparam);
        int h = HIWORD(lparam);
        rife_platform_resize(core, w, h);
        return 0;
    }
    case WM_CLOSE:
        set_system_taskbar_visible(true);
        core->running = false;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        set_system_taskbar_visible(true);
        PostQuitMessage(0);
        return 0;
    case WM_MOUSEMOVE:
        core->input.mouse_x = (float)LOWORD(lparam);
        core->input.mouse_y = (float)HIWORD(lparam);
        rife_request_redraw(core);
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pt;
        pt.x = (short)LOWORD(lparam);
        pt.y = (short)HIWORD(lparam);
        ScreenToClient(hwnd, &pt);
        core->input.mouse_x = (float)pt.x;
        core->input.mouse_y = (float)pt.y;
        short delta = GET_WHEEL_DELTA_WPARAM(wparam);
        core->input.scroll_delta += (float)delta / (float)WHEEL_DELTA;
        rife_request_redraw(core);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        core->input.mouse_down[0] = 1;
        SetCapture(hwnd);
        return 0;
    }
    case WM_LBUTTONUP: {
        core->input.mouse_down[0] = 0;

        if ((plat->drag_mode == 1 || plat->drag_mode == 2) && plat->active_win_idx >= 0) {
            ActiveWindow* win = &plat->windows[plat->active_win_idx];
            if (win->inst) {
                float my = core->input.mouse_y;
                float ww = (float)plat->win_width;
                float wh = (float)plat->win_height;
                bool to_top = (plat->drag_mode == 1 && my <= 22.0f);
                bool cover_desktop = (win->w >= ww * 0.80f && win->h >= wh * 0.80f && win->x <= 50.0f && win->y <= 50.0f);

                if (to_top || cover_desktop) {
                    if (!win->is_maximized) {
                        win->restore_x = win->x;
                        win->restore_y = win->y;
                        win->restore_w = win->w;
                        win->restore_h = win->h;
                        win->is_maximized = true;
                    }
                    if (!plat->is_fullscreen) {
                        toggle_immersion_fullscreen(plat);
                    }
                    rife_request_redraw(core);
                }
            }
        }

        plat->drag_mode = 0;
        plat->active_win_idx = -1;
        plat->snap_preview = false;
        ReleaseCapture();
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (plat && plat->hdc_mem) {
            BitBlt(hdc, 0, 0, plat->win_width, plat->win_height, plat->hdc_mem, 0, 0, SRCCOPY);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

bool rife_platform_init(RifeCore* core, Win32Platform* plat, const char* title, int width, int height) {
    plat->win_width = width;
    plat->win_height = height;
    plat->pixels = NULL;
    plat->dock_anim = 0.0f;
    plat->drawer_anim = 0.0f;
    plat->drawer_open = false;
    plat->shortcut_count = 0;
    plat->aura_time = 0.0f;
    plat->breath_t = 0.0f;
    plat->current_mode = DESKTOP_MODE_FLOATING;
    plat->is_fullscreen = false;
    plat->prev_rect.left = 100;
    plat->prev_rect.top = 100;
    plat->prev_rect.right = 100 + width;
    plat->prev_rect.bottom = 100 + height;

    plat->cloud_expanded = false;
    plat->cloud_anim = 0.0f;
    plat->absorption_ripple_t = 0.0f;
    plat->active_win_idx = -1;
    plat->drag_mode = 0;
    plat->resize_dir = 0;
    plat->hover_resize_dir = 0;
    plat->snap_preview = false;

    for (size_t i = 0; i < MAX_PLUGIN_WINDOWS; i++) {
        plat->windows[i].plugin = (i < g_installed_app_count) ? g_installed_apps[i] : NULL;
        plat->windows[i].inst = NULL;
        plat->windows[i].is_open = false;
        plat->windows[i].is_maximized = false;
        plat->windows[i].anim = 0.0f;
        plat->windows[i].inited = false;
    }

    plat->hlook = (HorizLookup*)arena_alloc(&core->persistent_arena, sizeof(HorizLookup) * 7680);
    if (!plat->hlook) return false;

    update_horiz_lookup(plat, width);

    core->platform_data = plat;
    WNDCLASSA wc = { 0 };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = rife_wnd_proc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "RifeDesktopHostClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (width > screen_w - 40) width = screen_w - 40;
    if (height > screen_h - 70) height = screen_h - 70;
    plat->win_width = width;
    plat->win_height = height;

    int init_x = (screen_w > width) ? (screen_w - width) / 2 : 20;
    int init_y = (screen_h > height) ? (screen_h - height) / 2 : 35;

    plat->hwnd = CreateWindowExA(WS_EX_APPWINDOW | WS_EX_ACCEPTFILES, wc.lpszClassName, title,
        WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_POPUP,
        init_x, init_y, width, height, NULL, NULL, wc.hInstance, NULL);
    if (!plat->hwnd) return false;

    DragAcceptFiles(plat->hwnd, TRUE);
    int corner_preference = 2;
    DwmSetWindowAttribute(plat->hwnd, 33, &corner_preference, sizeof(corner_preference));
    MARGINS margins = { 1, 1, 1, 1 };
    DwmExtendFrameIntoClientArea(plat->hwnd, &margins);
    SetWindowLongPtr(plat->hwnd, GWLP_USERDATA, (LONG_PTR)core);

    HDC hdc = GetDC(plat->hwnd);
    plat->hdc_mem = CreateCompatibleDC(hdc);
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    plat->hbm_mem = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void**)&plat->pixels, NULL, 0);
    if (plat->hbm_mem) {
        plat->hbm_old = (HBITMAP)SelectObject(plat->hdc_mem, plat->hbm_mem);
    }
    ReleaseDC(plat->hwnd, hdc);

    plat->hfont_panel_title = NULL;
    plat->hfont_display = NULL;
    plat->hfont_title = NULL;
    plat->hfont_body = NULL;
    plat->hfont_bold = NULL;
    plat->hfont_sm = NULL;
    plat->hfont_caption = NULL;
    update_system_fonts(plat, FONT_SCALE_125);

    SetWindowPos(plat->hwnd, HWND_TOP, init_x, init_y, width, height, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    ShowWindow(plat->hwnd, SW_SHOWNORMAL);
    UpdateWindow(plat->hwnd);
    SetForegroundWindow(plat->hwnd);
    return true;
}

void rife_platform_shutdown(Win32Platform* plat) {
    if (!plat) return;
    set_system_taskbar_visible(true);
    for (size_t i = 0; i < plat->shortcut_count; i++) {
        if (plat->shortcuts[i].custom_icon) DestroyIcon(plat->shortcuts[i].custom_icon);
    }

    for (size_t i = 0; i < g_installed_app_count; i++) {
        if (plat->windows[i].inst && plat->windows[i].plugin->destroy) {
            plat->windows[i].plugin->destroy(plat->windows[i].inst);
            plat->windows[i].inst = NULL;
        }
    }

    if (plat->hdc_mem) {
        SelectObject(plat->hdc_mem, plat->hbm_old);
        if (plat->hbm_mem) DeleteObject(plat->hbm_mem);
        DeleteDC(plat->hdc_mem);
    }
    if (plat->hfont_panel_title) DeleteObject(plat->hfont_panel_title);
    if (plat->hfont_display) DeleteObject(plat->hfont_display);
    if (plat->hfont_title) DeleteObject(plat->hfont_title);
    if (plat->hfont_body) DeleteObject(plat->hfont_body);
    if (plat->hfont_bold) DeleteObject(plat->hfont_bold);
    if (plat->hfont_sm) DeleteObject(plat->hfont_sm);
    if (plat->hfont_caption) DeleteObject(plat->hfont_caption);
}

bool desktop_launcher_init(RifeApp* self, RifeCore* core) {
    (void)self; (void)core; return true;
}

void desktop_launcher_update(RifeApp* self, RifeCore* core, const RifeInput* input, bool is_focused, uint64_t dt_ns) {
    (void)self;
    Win32Platform* plat = (Win32Platform*)core->platform_data;
    if (!plat) return;

    RifeSystemConfig* cfg = rife_get_system_config();

    float breath_period = 6.0f;
    if (cfg->breath_speed == BREATH_SPEED_SLOW) breath_period = 4.0f;
    else if (cfg->breath_speed == BREATH_SPEED_NORMAL) breath_period = 2.5f;

    float dt_sec = (float)(dt_ns / 1000000ULL) * 0.001f;
    plat->breath_t += dt_sec * (6.2831853f / breath_period);
    if (plat->breath_t > 62831.0f) plat->breath_t -= 62831.0f;

    float step = 0.0016f * (float)(dt_ns / 1000000ULL);
    plat->aura_time += step;
    if (plat->aura_time > 10000.0f) plat->aura_time -= 10000.0f;

    float ww = (float)plat->win_width;
    float wh = (float)plat->win_height;
    float mx = input->mouse_x;
    float my = input->mouse_y;

    // 1. 流体云展开动力学
    float target_cloud = plat->cloud_expanded ? 1.0f : 0.0f;
    plat->cloud_anim = rife_fluid_decay(plat->cloud_anim, target_cloud, 14.0f, dt_sec);
    if (fabsf(plat->cloud_anim - target_cloud) < 0.001f) plat->cloud_anim = target_cloud;

    // 微球吞噬光子扩散衰减
    if (plat->absorption_ripple_t > 0.0f) {
        plat->absorption_ripple_t -= dt_sec * 4.5f;
        if (plat->absorption_ripple_t < 0.0f) plat->absorption_ripple_t = 0.0f;
    }

    // 2. 抽屉自顶部流体云展开动画
    float target_drawer = plat->drawer_open ? 1.0f : 0.0f;
    plat->drawer_anim = rife_fluid_decay(plat->drawer_anim, target_drawer, 12.0f, dt_sec);
    if (fabsf(plat->drawer_anim - target_drawer) < 0.001f) plat->drawer_anim = target_drawer;

    // 3. 悬停手柄检测
    int hover_dir = 0;
    if (plat->drag_mode == 2) {
        hover_dir = plat->resize_dir;
    }
    else if (plat->drag_mode == 0) {
        for (int i = (int)g_installed_app_count - 1; i >= 0; i--) {
            ActiveWindow* win = &plat->windows[i];
            if (!win->inst || win->anim < 0.90f || win->is_maximized) continue;
            float cur_x = win->x;
            float cur_y = win->y;
            float cur_w = win->w;
            float cur_h = win->h;

            if (mx >= cur_x - 6.0f && mx <= cur_x + cur_w + 6.0f &&
                my >= cur_y - 6.0f && my <= cur_y + cur_h + 6.0f) {
                int dir = 0;
                if (mx >= cur_x - 6.0f && mx <= cur_x + 6.0f) dir |= 1;
                if (mx >= cur_x + cur_w - 6.0f && mx <= cur_x + cur_w + 6.0f) dir |= 2;
                if (my >= cur_y - 6.0f && my <= cur_y + 6.0f) dir |= 4;
                if (my >= cur_y + cur_h - 6.0f && my <= cur_y + cur_h + 6.0f) dir |= 8;
                if (dir != 0) {
                    hover_dir = dir;
                    break;
                }
            }
        }
    }
    plat->hover_resize_dir = hover_dir;

    // 4. 活动窗口拖动与拉伸
    if (input->mouse_down[0] && plat->drag_mode != 0 && plat->active_win_idx >= 0) {
        ActiveWindow* win = &plat->windows[plat->active_win_idx];
        if (win->inst && win->anim > 0.90f) {
            float dx = mx - plat->drag_start_mx;
            float dy = my - plat->drag_start_my;

            if (win->is_maximized && plat->drag_mode == 1 && dy > 10.0f) {
                win->is_maximized = false;
                win->w = (win->restore_w >= 360.0f) ? win->restore_w : win->plugin->default_w;
                win->h = (win->restore_h >= 240.0f) ? win->restore_h : win->plugin->default_h;
                win->x = mx - win->w * 0.5f;
                win->y = my - 16.0f;
                plat->drag_start_wx = win->x;
                plat->drag_start_wy = win->y;
                plat->drag_start_mx = mx;
                plat->drag_start_my = my;
            }
            else if (!win->is_maximized) {
                if (plat->drag_mode == 1) {
                    win->x = plat->drag_start_wx + dx;
                    win->y = plat->drag_start_wy + dy;

                    bool to_top = (my <= 22.0f);
                    bool cover_desktop = (win->w >= ww * 0.80f && win->h >= wh * 0.80f && win->x <= 50.0f && win->y <= 50.0f);
                    plat->snap_preview = (to_top || cover_desktop);
                }
                else if (plat->drag_mode == 2) {
                    if (plat->resize_dir & 1) {
                        float new_w = plat->drag_start_ww - dx;
                        if (new_w >= 360.0f) {
                            win->x = plat->drag_start_wx + dx;
                            win->w = new_w;
                        }
                    }
                    if (plat->resize_dir & 2) {
                        float new_w = plat->drag_start_ww + dx;
                        if (new_w >= 360.0f) win->w = new_w;
                    }
                    if (plat->resize_dir & 4) {
                        float new_h = plat->drag_start_wh - dy;
                        if (new_h >= 240.0f) {
                            win->y = plat->drag_start_wy + dy;
                            win->h = new_h;
                        }
                    }
                    if (plat->resize_dir & 8) {
                        float new_h = plat->drag_start_wh + dy;
                        if (new_h >= 240.0f) win->h = new_h;
                    }
                    bool cover_desktop = (win->w >= ww * 0.80f && win->h >= wh * 0.80f && win->x <= 50.0f && win->y <= 50.0f);
                    plat->snap_preview = cover_desktop;
                }
            }
            rife_request_redraw(core);
        }
    }

    // 5. 应用窗口折叠与物理析构
    for (size_t i = 0; i < g_installed_app_count; i++) {
        ActiveWindow* win = &plat->windows[i];
        if (!win->inst) continue;

        float target = win->is_open ? 1.0f : 0.0f;
        win->anim = rife_fluid_decay(win->anim, target, 22.0f, dt_sec);
        if (win->is_open && win->anim >= 0.985f) {
            win->anim = 1.0f;
        }

        if (!win->is_open && win->anim < 0.015f) {
            win->anim = 0.0f;
            win->plugin->destroy(win->inst);
            win->inst = NULL;
            if (plat->active_win_idx == (int)i) plat->active_win_idx = -1;
            plat->absorption_ripple_t = 1.0f; // 触发微球吞噬光子扩散波
            rife_request_redraw(core);
            continue;
        }

        float cur_x = win->is_maximized ? 0.0f : win->x;
        float cur_y = win->is_maximized ? 0.0f : win->y;
        float cur_w = win->is_maximized ? ww : win->w;
        float cur_h = win->is_maximized ? wh : win->h;

        if (input->mouse_pressed[0] && win->anim > 0.85f) {
            if (my >= cur_y + 6.0f && my <= cur_y + 30.0f) {
                if (mx >= cur_x + 10.0f && mx <= cur_x + 28.0f) {
                    win->is_open = false;
                    rife_request_redraw(core);
                    return;
                }
                if (mx >= cur_x + 29.0f && mx <= cur_x + 47.0f) {
                    win->is_open = false;
                    rife_request_redraw(core);
                    return;
                }
                if (mx >= cur_x + 48.0f && mx <= cur_x + 68.0f) {
                    if (!win->is_maximized) {
                        win->restore_x = win->x;
                        win->restore_y = win->y;
                        win->restore_w = win->w;
                        win->restore_h = win->h;
                    }
                    win->is_maximized = !win->is_maximized;
                    rife_request_redraw(core);
                    return;
                }
            }

            if (!win->is_maximized) {
                int dir = 0;
                if (mx >= cur_x - 5.0f && mx <= cur_x + 7.0f) dir |= 1;
                if (mx >= cur_x + cur_w - 7.0f && mx <= cur_x + cur_w + 5.0f) dir |= 2;
                if (my >= cur_y - 5.0f && my <= cur_y + 7.0f) dir |= 4;
                if (my >= cur_y + cur_h - 7.0f && my <= cur_y + cur_h + 5.0f) dir |= 8;

                if (dir != 0 && mx >= cur_x - 6.0f && mx <= cur_x + cur_w + 6.0f &&
                    my >= cur_y - 6.0f && my <= cur_y + cur_h + 6.0f) {
                    plat->active_win_idx = (int)i;
                    plat->drag_mode = 2;
                    plat->resize_dir = dir;
                    plat->drag_start_mx = mx;
                    plat->drag_start_my = my;
                    plat->drag_start_wx = win->x;
                    plat->drag_start_wy = win->y;
                    plat->drag_start_ww = win->w;
                    plat->drag_start_wh = win->h;
                    return;
                }
            }

            if (my >= cur_y && my <= cur_y + 36.0f && mx >= cur_x && mx <= cur_x + cur_w) {
                plat->active_win_idx = (int)i;
                plat->drag_mode = 1;
                plat->drag_start_mx = mx;
                plat->drag_start_my = my;
                plat->drag_start_wx = win->is_maximized ? 0.0f : win->x;
                plat->drag_start_wy = win->is_maximized ? 0.0f : win->y;
                return;
            }

            if (win->plugin->update && mx >= cur_x && mx <= cur_x + cur_w && my >= cur_y + 36.0f && my <= cur_y + cur_h) {
                plat->active_win_idx = (int)i;
                RifeInput client_input = *input;
                client_input.mouse_x = mx - cur_x;
                client_input.mouse_y = my - (cur_y + 36.0f);
                win->plugin->update(win->inst, core, &client_input, cur_w, cur_h - 36.0f);
                return;
            }
        }

        // 鼠标滚轮事件分发至应用窗口
        if (input->scroll_delta != 0.0f && win->anim > 0.85f && win->is_open) {
            if (win->plugin->update && mx >= cur_x && mx <= cur_x + cur_w && my >= cur_y + 36.0f && my <= cur_y + cur_h) {
                plat->active_win_idx = (int)i;
                RifeInput client_input = *input;
                client_input.mouse_x = mx - cur_x;
                client_input.mouse_y = my - (cur_y + 36.0f);
                win->plugin->update(win->inst, core, &client_input, cur_w, cur_h - 36.0f);
                rife_request_redraw(core);
            }
        }
    }

    // 6. 底部纤细 Dock 浮动与插值判定 (44px)
    float dock_w = ww * (2.0f / 3.0f);
    if (dock_w < 280.0f) dock_w = 280.0f;
    float dock_x = (cfg->dock_align == DOCK_ALIGN_RIGHT) ? (ww - dock_w - 24.0f) : ((ww - dock_w) * 0.5f);
    float resting_y = wh - 52.0f;
    float active_y = wh - 58.0f;
    float dock_y = rife_lerpf(resting_y, active_y, plat->dock_anim); // 已补齐定义！

    bool in_dock_zone = (my >= wh - 62.0f && mx >= dock_x && mx <= dock_x + dock_w);
    float target_dock = (in_dock_zone || cfg->dock_always_visible) ? 1.0f : 0.0f;
    plat->dock_anim = rife_fluid_decay(plat->dock_anim, target_dock, 15.0f, dt_sec);
    if (fabsf(plat->dock_anim - target_dock) < 0.001f) plat->dock_anim = target_dock;

    bool is_animating = (fabsf(plat->dock_anim - target_dock) > 0.001f) ||
        (fabsf(plat->drawer_anim - target_drawer) > 0.001f) ||
        (fabsf(plat->cloud_anim - target_cloud) > 0.001f) ||
        (plat->absorption_ripple_t > 0.001f) ||
        (plat->cloud_anim < 0.25f) ||
        (plat->hover_resize_dir > 0) ||
        cfg->aura_animated || plat->drag_mode != 0;
    for (size_t i = 0; i < g_installed_app_count; i++) {
        if (plat->windows[i].inst) {
            float target = plat->windows[i].is_open ? 1.0f : 0.0f;
            if (fabsf(plat->windows[i].anim - target) > 0.001f) {
                is_animating = true;
                break;
            }
        }
    }
    if (is_animating || input->mouse_pressed[0] || input->mouse_down[0] || input->scroll_delta != 0.0f) {
        rife_request_redraw(core);
    }

    // 7. 点击调度与分发
    if (is_focused && input->mouse_pressed[0]) {
        // A. 顶部流体云自身点击
        float col_size = 22.0f;
        float exp_w = ww * 0.50f;
        if (exp_w < 300.0f) exp_w = 300.0f;
        float cur_cloud_w = rife_lerpf(col_size, exp_w, plat->cloud_anim);
        float cur_cloud_h = rife_lerpf(col_size, 42.0f, plat->cloud_anim);
        float cur_cloud_x = (ww - cur_cloud_w) * 0.5f;
        float cur_cloud_y = 10.0f;

        if (mx >= cur_cloud_x && mx <= cur_cloud_x + cur_cloud_w && my >= cur_cloud_y && my <= cur_cloud_y + cur_cloud_h) {
            if (plat->cloud_expanded) {
                if (mx >= cur_cloud_x + 14.0f && mx <= cur_cloud_x + 34.0f) {
                    set_system_taskbar_visible(true);
                    core->running = false;
                    DestroyWindow(plat->hwnd);
                    return;
                }
                if (mx >= cur_cloud_x + 35.0f && mx <= cur_cloud_x + 54.0f) {
                    set_system_taskbar_visible(true);
                    ShowWindow(plat->hwnd, SW_MINIMIZE);
                    return;
                }
                if (mx >= cur_cloud_x + 55.0f && mx <= cur_cloud_x + 75.0f) {
                    toggle_immersion_fullscreen(plat);
                    return;
                }
                plat->cloud_expanded = false;
            }
            else {
                plat->cloud_expanded = true;
            }
            rife_request_redraw(core);
            return;
        }
        else {
            if (plat->cloud_expanded) {
                plat->cloud_expanded = false;
                rife_request_redraw(core);
            }
        }

        // B. 点击由流体云展开的应用程序抽屉
        if (plat->drawer_open && plat->drawer_anim > 0.35f) {
            float dw_w = ww * 0.60f;
            if (dw_w < 360.0f) dw_w = 360.0f;
            float dw_h = wh * 0.56f;
            if (dw_h < 240.0f) dw_h = 240.0f;
            float cur_dw_x = (ww - dw_w) * 0.5f;
            float cur_dw_y = (wh - dw_h) * 0.44f;

            int cols = 3;
            float card_w = 110.0f;
            float card_h = 76.0f;
            float gap_x = (dw_w - 48.0f - (float)cols * card_w) / (float)(cols - 1);

            for (size_t i = 0; i < g_installed_app_count; i++) {
                int col = (int)(i % cols);
                int row = (int)(i / cols);
                float ax = cur_dw_x + 24.0f + (float)col * (card_w + gap_x);
                float ay = cur_dw_y + 54.0f + (float)row * (card_h + 16.0f);

                if (mx >= ax && mx <= ax + card_w && my >= ay && my <= ay + card_h) {
                    ActiveWindow* win = &plat->windows[i];
                    if (win->is_open && win->anim > 0.5f) {
                        win->is_open = false;
                    } else {
                        for (size_t k = 0; k < g_installed_app_count; k++) {
                            if (k != i) plat->windows[k].is_open = false;
                        }
                        if (!win->inst) {
                            win->inst = win->plugin->create(core);
                        }
                        win->is_open = true;
                        win->anim = 0.0f;
                        if (!win->inited) {
                            win->w = win->plugin->default_w;
                            win->h = win->plugin->default_h;
                            if (win->w > ww - 48.0f) win->w = ww - 48.0f;
                            if (win->h > wh - 110.0f) win->h = wh - 110.0f;
                            if (win->w < 360.0f) win->w = 360.0f;
                            if (win->h < 260.0f) win->h = 260.0f;
                            win->x = (ww - win->w) * 0.5f;
                            if (win->x < 16.0f) win->x = 16.0f;
                            win->y = (wh - win->h) * 0.44f;
                            if (win->y < 46.0f) win->y = 46.0f;
                            win->restore_x = win->x;
                            win->restore_y = win->y;
                            win->restore_w = win->w;
                            win->restore_h = win->h;
                            win->inited = true;
                        }
                        plat->active_win_idx = (int)i;
                    }
                    plat->drawer_open = false;
                    rife_request_redraw(core);
                    return;
                }
            }

            if (mx < cur_dw_x || mx > cur_dw_x + dw_w || my < cur_dw_y || my > cur_dw_y + dw_h) {
                plat->drawer_open = false;
                return;
            }
            return;
        }

        // C. 活动应用窗口阻断桌面穿透
        for (size_t i = 0; i < g_installed_app_count; i++) {
            ActiveWindow* win = &plat->windows[i];
            if (!win->inst || win->anim < 0.05f) continue;
            float cur_x = win->is_maximized ? 0.0f : win->x;
            float cur_y = win->is_maximized ? 0.0f : win->y;
            float cur_w = win->is_maximized ? ww : win->w;
            float cur_h = win->is_maximized ? wh : win->h;
            if (mx >= cur_x && mx <= cur_x + cur_w && my >= cur_y && my <= cur_y + cur_h) {
                return;
            }
        }

        // D. 底部纤细 Dock 点击
        int dock_pinned_count = 0;
        for (size_t i = 0; i < g_installed_app_count; i++) {
            if (g_installed_apps[i]->pin_to_dock) dock_pinned_count++;
        }
        int total_dock_items = dock_pinned_count + 1;
        float item_size = 32.0f;
        float item_gap = 16.0f;
        float total_items_w = (float)total_dock_items * item_size + (float)(total_dock_items - 1) * item_gap;
        float items_start_x = dock_x + (dock_w - total_items_w) * 0.5f;

        int dock_idx = 0;
        for (size_t i = 0; i < g_installed_app_count; i++) {
            const RifePluginApp* app = g_installed_apps[i];
            if (!app->pin_to_dock) continue;

            float btn_x = items_start_x + (float)dock_idx * (item_size + item_gap);
            float btn_y = dock_y + 6.0f; // 正确引用已声明的 dock_y

            if (mx >= btn_x && mx <= btn_x + item_size && my >= btn_y - 3.0f && my <= btn_y + item_size + 3.0f) {
                ActiveWindow* win = &plat->windows[i];
                if (win->is_open && win->anim > 0.5f) {
                    win->is_open = false;
                } else {
                    for (size_t k = 0; k < g_installed_app_count; k++) {
                        if (k != i) plat->windows[k].is_open = false;
                    }
                    if (!win->inst) {
                        win->inst = win->plugin->create(core);
                    }
                    win->is_open = true;
                    if (!win->inited) {
                        win->w = win->plugin->default_w;
                        win->h = win->plugin->default_h;
                        if (win->w > ww - 48.0f) win->w = ww - 48.0f;
                        if (win->h > wh - 110.0f) win->h = wh - 110.0f;
                        if (win->w < 360.0f) win->w = 360.0f;
                        if (win->h < 260.0f) win->h = 260.0f;
                        win->x = (ww - win->w) * 0.5f;
                        if (win->x < 16.0f) win->x = 16.0f;
                        win->y = (wh - win->h) * 0.44f;
                        if (win->y < 46.0f) win->y = 46.0f;
                        win->restore_x = win->x;
                        win->restore_y = win->y;
                        win->restore_w = win->w;
                        win->restore_h = win->h;
                        win->inited = true;
                    }
                    plat->active_win_idx = (int)i;
                }
                rife_request_redraw(core);
                return;
            }
            dock_idx++;
        }

        // 九宫格按钮：触发顶部流体云展开为应用抽屉
        float all_btn_x = items_start_x + (float)dock_pinned_count * (item_size + item_gap);
        float all_btn_y = dock_y + 6.0f; // 正确引用已声明的 dock_y
        if (mx >= all_btn_x && mx <= all_btn_x + item_size && my >= all_btn_y - 3.0f && my <= all_btn_y + item_size + 3.0f) {
            plat->drawer_open = !plat->drawer_open;
            return;
        }

        // E. 桌面快捷方式点击
        for (size_t i = 0; i < plat->shortcut_count; i++) {
            DesktopShortcut* sc = &plat->shortcuts[i];
            if (mx >= sc->x && mx <= sc->x + 76.0f && my >= sc->y && my <= sc->y + 80.0f) {
                ShellExecuteA(NULL, "open", sc->path, NULL, NULL, SW_SHOWNORMAL);
                return;
            }
        }
    }
}

void desktop_launcher_render(RifeApp* self, RifeCore* core) {
    (void)self; (void)core;
}
void desktop_launcher_on_event(RifeApp* self, const RifeEvent* event) {
    (void)self; (void)event;
}
void desktop_launcher_shutdown(RifeApp* self, RifeCore* core) {
    (void)self; (void)core;
}

int main(void) {
    timeBeginPeriod(1);
    static RifeCore core;
    static Win32Platform plat;

    if (!rife_core_init(&core, 1024 * 1024, 256 * 1024, 60)) return 1;
    if (!rife_platform_init(&core, &plat, "RifeOS Workspace Host", 1240, 780)) {
        rife_core_shutdown(&core);
        return 1;
    }

    RifeApp launcher_app;
    launcher_app.app_id = 1000;
    launcher_app.is_visible = true;
    snprintf(launcher_app.name, sizeof(launcher_app.name), "%s", "DesktopHost");
    launcher_app.init = desktop_launcher_init;
    launcher_app.update = desktop_launcher_update;
    launcher_app.render = desktop_launcher_render;
    launcher_app.on_event = desktop_launcher_on_event;
    launcher_app.shutdown = desktop_launcher_shutdown;
    launcher_app.user_data = NULL;

    if (!rife_register_app(&core, launcher_app)) {
        rife_platform_shutdown(&plat);
        rife_core_shutdown(&core);
        return 1;
    }

    while (core.running) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        RifeSystemConfig* cfg = rife_get_system_config();
        if (IsIconic(plat.hwnd)) {
            Sleep(25);
            continue;
        }
        if (cfg->background_throttle && GetForegroundWindow() != plat.hwnd && plat.current_mode != DESKTOP_MODE_WALLPAPER) {
            Sleep(16);
        }

        rife_core_tick(&core);

        if (!core.needs_redraw && !cfg->aura_animated && plat.drag_mode == 0 && plat.cloud_anim > 0.3f && plat.hover_resize_dir == 0) {
            Sleep(8);
        }
    }

    rife_platform_shutdown(&plat);
    rife_core_shutdown(&core);
    timeEndPeriod(1);
    return 0;
}

#if defined(_WIN32)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nShowCmd;
    return main();
}
#endif
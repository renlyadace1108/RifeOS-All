#define _CRT_SECURE_NO_WARNINGS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <timeapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#include "rife_core.h"
#include "rife_app_api.h"
#include "app_manifest.h"
#include "app_calendar.h"
#include "app_clock.h"
#include "app_settings.h"

// -------------------------------------------------------------
// Rife 现代一体化单窗架构定义 (Modern Unified Single-Window Architecture)
// -------------------------------------------------------------

#define TITLEBAR_HEIGHT 44.0f
#define SIDEBAR_WIDTH   200.0f
#define MIN_WINDOW_W    980
#define MIN_WINDOW_H    640

typedef enum {
    PAGE_SCHEDULE = 0, // 📅 多维日程 (Rtodo)
    PAGE_CLOCK    = 1, // ⏱️ 极简时钟 (Rclock)
    PAGE_SETTINGS = 2  // ⚙️ 偏好设置 (Settings)
} AppPage;

typedef struct {
    HWND hwnd;
    HDC hdc_mem;
    HBITMAP hbm_mem;
    HBITMAP hbm_old;
    uint32_t* pixels;
    int win_width;
    int win_height;

    // 清晰字体句柄 (负值 EM 像素高度)
    HFONT hfont_display;
    HFONT hfont_panel_title;
    HFONT hfont_title;
    HFONT hfont_body;
    HFONT hfont_bold;
    HFONT hfont_sm;
    HFONT hfont_caption;
    FontScaleType current_font_scale;

    // 核心页面状态
    AppPage active_page;
    int hovered_nav_idx;      // -1 或 0..2
    int hovered_title_btn;    // -1, 0: min, 1: max, 2: close
    bool mouse_in_window;

    // 插件应用实例 (常驻双 Arena / 零堆搅动)
    void* calendar_inst;
    void* clock_inst;
    void* settings_inst;

    // 运行期输入状态
    RifeInput input;
} AppContext;

static AppContext* s_app = NULL;
static RifeCore*   s_core = NULL;

// -------------------------------------------------------------
// 物理级工作集深度紧凑 (OS Working Set Trim)
// -------------------------------------------------------------
static inline void rife_reclaim_physical_memory(void) {
    HeapCompact(GetProcessHeap(), 0);
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

// -------------------------------------------------------------
// 跨模块页面跳转 API
// -------------------------------------------------------------
void rife_open_app_by_id(const char* app_id) {
    if (!app_id || !s_app || !s_core) return;
    if (strcmp(app_id, "rtodo") == 0) s_app->active_page = PAGE_SCHEDULE;
    else if (strcmp(app_id, "clock") == 0) s_app->active_page = PAGE_CLOCK;
    else if (strcmp(app_id, "settings") == 0) s_app->active_page = PAGE_SETTINGS;
    rife_reclaim_physical_memory();
    rife_request_redraw(s_core);
}

// -------------------------------------------------------------
// 字体引擎管理 (Font Management)
// -------------------------------------------------------------
static void destroy_fonts(AppContext* app) {
    if (!app) return;
    if (app->hfont_display)     { DeleteObject(app->hfont_display);     app->hfont_display = NULL; }
    if (app->hfont_panel_title) { DeleteObject(app->hfont_panel_title); app->hfont_panel_title = NULL; }
    if (app->hfont_title)       { DeleteObject(app->hfont_title);       app->hfont_title = NULL; }
    if (app->hfont_body)        { DeleteObject(app->hfont_body);        app->hfont_body = NULL; }
    if (app->hfont_bold)        { DeleteObject(app->hfont_bold);        app->hfont_bold = NULL; }
    if (app->hfont_sm)          { DeleteObject(app->hfont_sm);          app->hfont_sm = NULL; }
    if (app->hfont_caption)     { DeleteObject(app->hfont_caption);     app->hfont_caption = NULL; }
}

static void update_system_fonts(AppContext* app, FontScaleType scale) {
    if (!app) return;
    destroy_fonts(app);

    float factor = 1.0f;
    if (scale == FONT_SCALE_125) factor = 1.20f;
    else if (scale == FONT_SCALE_150) factor = 1.40f;

    wchar_t font_face[LF_FACESIZE] = L"Microsoft YaHei UI";
    NONCLIENTMETRICSW ncm;
    memset(&ncm, 0, sizeof(NONCLIENTMETRICSW));
    ncm.cbSize = sizeof(NONCLIENTMETRICSW);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICSW), &ncm, 0)) {
        if (ncm.lfMessageFont.lfFaceName[0] != L'\0') {
            wcsncpy_s(font_face, LF_FACESIZE, ncm.lfMessageFont.lfFaceName, _TRUNCATE);
        }
    }

    int s_display = -(int)(21 * factor + 0.5f);
    int s_panel   = -(int)(17 * factor + 0.5f);
    int s_title   = -(int)(15 * factor + 0.5f);
    int s_body    = -(int)(13 * factor + 0.5f);
    int s_bold    = -(int)(13 * factor + 0.5f);
    int s_sm      = -(int)(12 * factor + 0.5f);
    int s_cap     = -(int)(11 * factor + 0.5f);

    app->hfont_display     = CreateFontW(s_display, 0, 0, 0, FW_LIGHT,    FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font_face);
    app->hfont_panel_title = CreateFontW(s_panel,   0, 0, 0, FW_NORMAL,   FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font_face);
    app->hfont_title       = CreateFontW(s_title,   0, 0, 0, FW_NORMAL,   FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font_face);
    app->hfont_body        = CreateFontW(s_body,    0, 0, 0, FW_LIGHT,    FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font_face);
    app->hfont_bold        = CreateFontW(s_bold,    0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font_face);
    app->hfont_sm          = CreateFontW(s_sm,      0, 0, 0, FW_LIGHT,    FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font_face);
    app->hfont_caption     = CreateFontW(s_cap,     0, 0, 0, FW_LIGHT,    FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font_face);

    app->current_font_scale = scale;
}

// -------------------------------------------------------------
// DIBSection 软件光栅化帧缓冲管理
// -------------------------------------------------------------
static void recreate_backbuffer(AppContext* app, int w, int h) {
    if (!app || w <= 0 || h <= 0) return;
    app->win_width = w;
    app->win_height = h;

    if (app->hdc_mem) {
        if (app->hbm_old) SelectObject(app->hdc_mem, app->hbm_old);
        if (app->hbm_mem) {
            DeleteObject(app->hbm_mem);
            app->hbm_mem = NULL;
        }
        HDC hdc_win = GetDC(app->hwnd);
        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h; // 自顶向下
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        app->hbm_mem = CreateDIBSection(hdc_win, &bmi, DIB_RGB_COLORS, (void**)&app->pixels, NULL, 0);
        ReleaseDC(app->hwnd, hdc_win);
        app->hbm_old = (HBITMAP)SelectObject(app->hdc_mem, app->hbm_mem);
    }
}

static inline float rife_clampf(float val, float min_val, float max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

static inline float rife_lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

// -------------------------------------------------------------
// 底层亚像素连续超椭圆距离场 (Squircle Box-SDF) 光栅化
// -------------------------------------------------------------
static void rife_blend_round_rect_pixels(AppContext* app, float rx, float ry, float rw, float rh, float radius, uint32_t color, uint32_t border_color) {
    if (!app || !app->pixels || rw <= 0.0f || rh <= 0.0f) return;
    int x0 = (int)floorf(rx - 1.5f);
    int y0 = (int)floorf(ry - 1.5f);
    int x1 = (int)ceilf(rx + rw + 1.5f);
    int y1 = (int)ceilf(ry + rh + 1.5f);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > app->win_width) x1 = app->win_width;
    if (y1 > app->win_height) y1 = app->win_height;

    RECT clip_rc;
    if (GetClipBox(app->hdc_mem, &clip_rc) != NULLREGION && clip_rc.right > clip_rc.left && clip_rc.bottom > clip_rc.top) {
        if (x0 < clip_rc.left) x0 = clip_rc.left;
        if (y0 < clip_rc.top) y0 = clip_rc.top;
        if (x1 > clip_rc.right) x1 = clip_rc.right;
        if (y1 > clip_rc.bottom) y1 = clip_rc.bottom;
    }
    if (x0 >= x1 || y0 >= y1) return;

    float fr = (float)((color >> 24) & 0xFF);
    float fg = (float)((color >> 16) & 0xFF);
    float fb = (float)((color >> 8) & 0xFF);
    float fa = (float)(color & 0xFF) / 255.0f;

    float br = (float)((border_color >> 24) & 0xFF);
    float bg = (float)((border_color >> 16) & 0xFF);
    float bb = (float)((border_color >> 8) & 0xFF);
    float ba = (float)(border_color & 0xFF) / 255.0f;

    float half_w = rw * 0.5f;
    float half_h = rh * 0.5f;
    float cx = rx + half_w;
    float cy = ry + half_h;
    float r_clamped = radius;
    if (r_clamped > half_w) r_clamped = half_w;
    if (r_clamped > half_h) r_clamped = half_h;
    float inner_w = half_w - r_clamped;
    float inner_h = half_h - r_clamped;
    int width = app->win_width;

    for (int y = y0; y < y1; y++) {
        float py = (float)y + 0.5f;
        float qy = fabsf(py - cy) - inner_h;
        uint32_t* line = &app->pixels[y * width];
        for (int x = x0; x < x1; x++) {
            float px = (float)x + 0.5f;
            float qx = fabsf(px - cx) - inner_w;
            float dist;
            if (qx <= 0.0f && qy <= 0.0f) {
                float in_val = (qx > qy) ? qx : qy;
                dist = in_val - r_clamped;
            } else if (qx > 0.0f && qy <= 0.0f) {
                dist = qx - r_clamped;
            } else if (qx <= 0.0f && qy > 0.0f) {
                dist = qy - r_clamped;
            } else {
                dist = sqrtf(qx * qx + qy * qy) - r_clamped;
            }
            if (dist > 1.0f) continue;

            if (dist <= -1.5f && ba <= 0.001f) {
                if (fa >= 0.999f) {
                    line[x] = ((uint32_t)fr << 16) | ((uint32_t)fg << 8) | (uint32_t)fb;
                    continue;
                }
            }

            uint32_t orig = line[x];
            float ob = (float)(orig & 0xFF);
            float og = (float)((orig >> 8) & 0xFF);
            float or_ = (float)((orig >> 16) & 0xFF);

            float fill_cov = rife_clampf(0.5f - dist, 0.0f, 1.0f);
            float eff_fa = fa * fill_cov;
            float cur_r = rife_lerpf(or_, fr, eff_fa);
            float cur_g = rife_lerpf(og, fg, eff_fa);
            float cur_b = rife_lerpf(ob, fb, eff_fa);

            if (ba > 0.001f) {
                float border_dist = fabsf(dist + 0.5f) - 0.5f;
                float border_cov = rife_clampf(0.5f - border_dist, 0.0f, 1.0f);
                float eff_ba = ba * border_cov;
                cur_r = rife_lerpf(cur_r, br, eff_ba);
                cur_g = rife_lerpf(cur_g, bg, eff_ba);
                cur_b = rife_lerpf(cur_b, bb, eff_ba);
            }

            uint32_t final_r = (uint32_t)rife_clampf(cur_r, 0.0f, 255.0f);
            uint32_t final_g = (uint32_t)rife_clampf(cur_g, 0.0f, 255.0f);
            uint32_t final_b = (uint32_t)rife_clampf(cur_b, 0.0f, 255.0f);
            line[x] = (final_r << 16) | (final_g << 8) | final_b;
        }
    }
}

// -------------------------------------------------------------
// 环形扇面、圆与线段亚像素混合
// -------------------------------------------------------------
static void rife_blend_circle_pixels(AppContext* app, float cx, float cy, float radius, uint32_t color, uint32_t border_color) {
    if (!app || !app->pixels || radius <= 0.0f) return;
    int x0 = (int)floorf(cx - radius - 1.5f);
    int y0 = (int)floorf(cy - radius - 1.5f);
    int x1 = (int)ceilf(cx + radius + 1.5f);
    int y1 = (int)ceilf(cy + radius + 1.5f);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > app->win_width) x1 = app->win_width;
    if (y1 > app->win_height) y1 = app->win_height;

    RECT clip_rc;
    if (GetClipBox(app->hdc_mem, &clip_rc) != NULLREGION && clip_rc.right > clip_rc.left && clip_rc.bottom > clip_rc.top) {
        if (x0 < clip_rc.left) x0 = clip_rc.left;
        if (y0 < clip_rc.top) y0 = clip_rc.top;
        if (x1 > clip_rc.right) x1 = clip_rc.right;
        if (y1 > clip_rc.bottom) y1 = clip_rc.bottom;
    }
    if (x0 >= x1 || y0 >= y1) return;

    float fr = (float)((color >> 24) & 0xFF);
    float fg = (float)((color >> 16) & 0xFF);
    float fb = (float)((color >> 8) & 0xFF);
    float fa = (float)(color & 0xFF) / 255.0f;

    float br = (float)((border_color >> 24) & 0xFF);
    float bg = (float)((border_color >> 16) & 0xFF);
    float bb = (float)((border_color >> 8) & 0xFF);
    float ba = (float)(border_color & 0xFF) / 255.0f;

    int width = app->win_width;
    float r_sq_max = (radius + 1.5f) * (radius + 1.5f);

    for (int y = y0; y < y1; y++) {
        float py = (float)y + 0.5f;
        uint32_t* line = &app->pixels[y * width];
        for (int x = x0; x < x1; x++) {
            float px = (float)x + 0.5f;
            float d_sq = (px - cx) * (px - cx) + (py - cy) * (py - cy);
            if (d_sq > r_sq_max) continue;

            float d = sqrtf(d_sq) - radius;
            float cov = rife_clampf(0.5f - d, 0.0f, 1.0f);
            if (cov <= 0.0f) continue;

            uint32_t orig = line[x];
            float ob = (float)(orig & 0xFF);
            float og = (float)((orig >> 8) & 0xFF);
            float or_ = (float)((orig >> 16) & 0xFF);

            float eff_fa = fa * cov;
            float cur_r = rife_lerpf(or_, fr, eff_fa);
            float cur_g = rife_lerpf(og, fg, eff_fa);
            float cur_b = rife_lerpf(ob, fb, eff_fa);

            if (ba > 0.001f) {
                float border_dist = fabsf(d + 0.5f) - 0.5f;
                float border_cov = rife_clampf(0.5f - border_dist, 0.0f, 1.0f);
                float eff_ba = ba * border_cov;
                cur_r = rife_lerpf(cur_r, br, eff_ba);
                cur_g = rife_lerpf(cur_g, bg, eff_ba);
                cur_b = rife_lerpf(cur_b, bb, eff_ba);
            }

            line[x] = ((uint32_t)rife_clampf(cur_r, 0.0f, 255.0f) << 16) |
                      ((uint32_t)rife_clampf(cur_g, 0.0f, 255.0f) << 8)  |
                       (uint32_t)rife_clampf(cur_b, 0.0f, 255.0f);
        }
    }
}

static void rife_draw_line_capsule_pixels(AppContext* app, float x1, float y1, float x2, float y2, float thickness, uint32_t color) {
    if (!app || !app->pixels || thickness <= 0.0f) return;
    float radius = thickness * 0.5f;
    float min_x = (x1 < x2 ? x1 : x2) - radius - 1.5f;
    float max_x = (x1 > x2 ? x1 : x2) + radius + 1.5f;
    float min_y = (y1 < y2 ? y1 : y2) - radius - 1.5f;
    float max_y = (y1 > y2 ? y1 : y2) + radius + 1.5f;

    int x0 = (int)floorf(min_x);
    int y0 = (int)floorf(min_y);
    int ix1 = (int)ceilf(max_x);
    int iy1 = (int)ceilf(max_y);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (ix1 > app->win_width) ix1 = app->win_width;
    if (iy1 > app->win_height) iy1 = app->win_height;

    RECT clip_rc;
    if (GetClipBox(app->hdc_mem, &clip_rc) != NULLREGION && clip_rc.right > clip_rc.left && clip_rc.bottom > clip_rc.top) {
        if (x0 < clip_rc.left) x0 = clip_rc.left;
        if (y0 < clip_rc.top) y0 = clip_rc.top;
        if (ix1 > clip_rc.right) ix1 = clip_rc.right;
        if (iy1 > clip_rc.bottom) iy1 = clip_rc.bottom;
    }
    if (x0 >= ix1 || y0 >= iy1) return;

    float fr = (float)((color >> 24) & 0xFF);
    float fg = (float)((color >> 16) & 0xFF);
    float fb = (float)((color >> 8) & 0xFF);
    float fa = (float)(color & 0xFF) / 255.0f;

    float dx = x2 - x1;
    float dy = y2 - y1;
    float l2 = dx * dx + dy * dy;
    int width = app->win_width;

    for (int y = y0; y < iy1; y++) {
        float py = (float)y + 0.5f;
        uint32_t* line = &app->pixels[y * width];
        for (int x = x0; x < ix1; x++) {
            float px = (float)x + 0.5f;
            float dist;
            if (l2 == 0.0f) {
                dist = sqrtf((px - x1) * (px - x1) + (py - y1) * (py - y1)) - radius;
            } else {
                float t = ((px - x1) * dx + (py - y1) * dy) / l2;
                t = rife_clampf(t, 0.0f, 1.0f);
                float proj_x = x1 + t * dx;
                float proj_y = y1 + t * dy;
                dist = sqrtf((px - proj_x) * (px - proj_x) + (py - proj_y) * (py - proj_y)) - radius;
            }
            if (dist > 1.0f) continue;
            float cov = rife_clampf(0.5f - dist, 0.0f, 1.0f);
            if (cov <= 0.0f) continue;

            uint32_t orig = line[x];
            float ob = (float)(orig & 0xFF);
            float og = (float)((orig >> 8) & 0xFF);
            float or_ = (float)((orig >> 16) & 0xFF);

            float eff_a = fa * cov;
            line[x] = ((uint32_t)rife_clampf(rife_lerpf(or_, fr, eff_a), 0.0f, 255.0f) << 16) |
                      ((uint32_t)rife_clampf(rife_lerpf(og, fg, eff_a), 0.0f, 255.0f) << 8)  |
                       (uint32_t)rife_clampf(rife_lerpf(ob, fb, eff_a), 0.0f, 255.0f);
        }
    }
}

static void rife_blend_arc_sector_pixels(AppContext* app, float cx, float cy, float r_in, float r_out, float ang_start, float ang_end, uint32_t color, uint32_t border_color) {
    if (!app || !app->pixels || r_out <= r_in || r_in < 0.0f) return;
    int x0 = (int)floorf(cx - r_out - 1.5f);
    int y0 = (int)floorf(cy - r_out - 1.5f);
    int x1 = (int)ceilf(cx + r_out + 1.5f);
    int y1 = (int)ceilf(cy + r_out + 1.5f);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > app->win_width) x1 = app->win_width;
    if (y1 > app->win_height) y1 = app->win_height;

    RECT clip_rc;
    if (GetClipBox(app->hdc_mem, &clip_rc) != NULLREGION && clip_rc.right > clip_rc.left && clip_rc.bottom > clip_rc.top) {
        if (x0 < clip_rc.left) x0 = clip_rc.left;
        if (y0 < clip_rc.top) y0 = clip_rc.top;
        if (x1 > clip_rc.right) x1 = clip_rc.right;
        if (y1 > clip_rc.bottom) y1 = clip_rc.bottom;
    }
    if (x0 >= x1 || y0 >= y1) return;

    float fr = (float)((color >> 24) & 0xFF);
    float fg = (float)((color >> 16) & 0xFF);
    float fb = (float)((color >> 8) & 0xFF);
    float fa = (float)(color & 0xFF) / 255.0f;

    float br = (float)((border_color >> 24) & 0xFF);
    float bg = (float)((border_color >> 16) & 0xFF);
    float bb = (float)((border_color >> 8) & 0xFF);
    float ba = (float)(border_color & 0xFF) / 255.0f;

    float span = ang_end - ang_start;
    while (span < 0.0f) span += 360.0f;
    bool is_full = (span >= 359.9f);
    int width = app->win_width;

    for (int y = y0; y < y1; y++) {
        float py = (float)y + 0.5f;
        float dy = py - cy;
        uint32_t* line = &app->pixels[y * width];
        for (int x = x0; x < x1; x++) {
            float px = (float)x + 0.5f;
            float dx = px - cx;
            float r = sqrtf(dx * dx + dy * dy);

            float d_radial = (r < r_in) ? (r_in - r) : ((r > r_out) ? (r - r_out) : 0.0f);
            if (d_radial > 1.0f) continue;

            float cov_radial = (r < r_in) ? rife_clampf(0.5f - d_radial, 0.0f, 1.0f) :
                              ((r > r_out) ? rife_clampf(0.5f - d_radial, 0.0f, 1.0f) : 1.0f);

            float cov_ang = 1.0f;
            if (!is_full) {
                float a = atan2f(dx, -dy) * (180.0f / 3.1415926535f);
                if (a < 0.0f) a += 360.0f;
                float rel_a = a - ang_start;
                while (rel_a < 0.0f) rel_a += 360.0f;
                while (rel_a >= 360.0f) rel_a -= 360.0f;

                if (rel_a > span) {
                    float diff_edge = fminf(rel_a - span, 360.0f - rel_a);
                    float d_arc = r * (diff_edge * (3.1415926535f / 180.0f));
                    cov_ang = rife_clampf(0.5f - d_arc, 0.0f, 1.0f);
                    if (cov_ang <= 0.0f) continue;
                }
            }

            float final_cov = cov_radial * cov_ang;
            if (final_cov <= 0.0f) continue;

            uint32_t orig = line[x];
            float ob = (float)(orig & 0xFF);
            float og = (float)((orig >> 8) & 0xFF);
            float or_ = (float)((orig >> 16) & 0xFF);

            float eff_fa = fa * final_cov;
            float cur_r = rife_lerpf(or_, fr, eff_fa);
            float cur_g = rife_lerpf(og, fg, eff_fa);
            float cur_b = rife_lerpf(ob, fb, eff_fa);

            if (ba > 0.001f) {
                float b_cov = 0.0f;
                float d_in = fabsf(r - r_in);
                float d_out = fabsf(r - r_out);
                float d_edge = fminf(d_in, d_out);
                if (d_edge < 1.0f) b_cov = rife_clampf(1.0f - d_edge, 0.0f, 1.0f);
                float eff_ba = ba * b_cov;
                cur_r = rife_lerpf(cur_r, br, eff_ba);
                cur_g = rife_lerpf(cur_g, bg, eff_ba);
                cur_b = rife_lerpf(cur_b, bb, eff_ba);
            }

            line[x] = ((uint32_t)rife_clampf(cur_r, 0.0f, 255.0f) << 16) |
                      ((uint32_t)rife_clampf(cur_g, 0.0f, 255.0f) << 8)  |
                       (uint32_t)rife_clampf(cur_b, 0.0f, 255.0f);
        }
    }
}

// -------------------------------------------------------------
// 提交与刷新渲染命令队列 (Flush Render Commands)
// -------------------------------------------------------------
static void rife_draw_text_u8(HDC hdc, int x, int y, const char* utf8) {
    if (!utf8 || utf8[0] == '\0') return;
    wchar_t wbuf[512];
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wbuf, 512);
    if (wlen > 0) {
        TextOutW(hdc, x, y, wbuf, wlen - 1);
    }
}

static void app_flush_render_commands(AppContext* app, RifeCore* core) {
    if (!app || !core) return;
    RenderCmd* curr = core->render_head;
    while (curr) {
        if (curr->type == CMD_TEXT) {
            if (curr->font_id == 1) SelectObject(app->hdc_mem, app->hfont_title);
            else if (curr->font_id == 2) SelectObject(app->hdc_mem, app->hfont_panel_title);
            else if (curr->font_id == 3) SelectObject(app->hdc_mem, app->hfont_sm);
            else if (curr->font_id == 4) SelectObject(app->hdc_mem, app->hfont_caption);
            else if (curr->font_id == 5) SelectObject(app->hdc_mem, app->hfont_bold);
            else if (curr->font_id == 6) SelectObject(app->hdc_mem, app->hfont_display);
            else SelectObject(app->hdc_mem, app->hfont_body);

            uint8_t r = (uint8_t)((curr->color >> 24) & 0xFF);
            uint8_t g = (uint8_t)((curr->color >> 16) & 0xFF);
            uint8_t b = (uint8_t)((curr->color >> 8) & 0xFF);
            SetTextColor(app->hdc_mem, RGB(r, g, b));
            rife_draw_text_u8(app->hdc_mem, (int)curr->x, (int)curr->y, curr->text);
        }
        else if (curr->type == CMD_TEXT_RECT) {
            if (curr->font_id == 1) SelectObject(app->hdc_mem, app->hfont_title);
            else if (curr->font_id == 2) SelectObject(app->hdc_mem, app->hfont_panel_title);
            else if (curr->font_id == 3) SelectObject(app->hdc_mem, app->hfont_sm);
            else if (curr->font_id == 4) SelectObject(app->hdc_mem, app->hfont_caption);
            else if (curr->font_id == 5) SelectObject(app->hdc_mem, app->hfont_bold);
            else if (curr->font_id == 6) SelectObject(app->hdc_mem, app->hfont_display);
            else SelectObject(app->hdc_mem, app->hfont_body);

            uint8_t r = (uint8_t)((curr->color >> 24) & 0xFF);
            uint8_t g = (uint8_t)((curr->color >> 16) & 0xFF);
            uint8_t b = (uint8_t)((curr->color >> 8) & 0xFF);
            SetTextColor(app->hdc_mem, RGB(r, g, b));

            wchar_t wbuf[256];
            int wlen = MultiByteToWideChar(CP_UTF8, 0, curr->text, -1, wbuf, 256);
            if (wlen > 0) {
                RECT rc = { (int)curr->x, (int)curr->y, (int)(curr->x + curr->w), (int)(curr->y + curr->h) };
                UINT flags = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
                if (curr->border_color == 1) flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
                else if (curr->border_color == 2) flags = DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
                DrawTextW(app->hdc_mem, wbuf, wlen - 1, &rc, flags);
            }
        }
        else if (curr->type == CMD_ROUND_RECT) {
            uint8_t a = (uint8_t)(curr->color & 0xFF);
            uint8_t ba = (uint8_t)(curr->border_color & 0xFF);
            if (a > 0 || ba > 0) {
                GdiFlush();
                rife_blend_round_rect_pixels(app, curr->x, curr->y, curr->w, curr->h, curr->radius, curr->color, curr->border_color);
            }
        }
        else if (curr->type == CMD_CIRCLE) {
            uint8_t a = (uint8_t)(curr->color & 0xFF);
            uint8_t ba = (uint8_t)(curr->border_color & 0xFF);
            if (a > 0 || ba > 0) {
                GdiFlush();
                rife_blend_circle_pixels(app, curr->x, curr->y, curr->radius, curr->color, curr->border_color);
            }
        }
        else if (curr->type == CMD_LINE) {
            uint8_t a = (uint8_t)(curr->color & 0xFF);
            if (a > 0) {
                GdiFlush();
                rife_draw_line_capsule_pixels(app, curr->x, curr->y, curr->w, curr->h, curr->radius, curr->color);
            }
        }
        else if (curr->type == CMD_ARC_SECTOR) {
            uint8_t a = (uint8_t)(curr->color & 0xFF);
            uint8_t ba = (uint8_t)(curr->border_color & 0xFF);
            if (a > 0 || ba > 0) {
                GdiFlush();
                rife_blend_arc_sector_pixels(app, curr->x, curr->y, curr->radius, curr->w, curr->angle_start, curr->angle_end, curr->color, curr->border_color);
            }
        }
        else if (curr->type == CMD_RECT) {
            uint8_t a = (uint8_t)(curr->color & 0xFF);
            if (a == 255) {
                uint8_t r = (uint8_t)((curr->color >> 24) & 0xFF);
                uint8_t g = (uint8_t)((curr->color >> 16) & 0xFF);
                uint8_t b = (uint8_t)((curr->color >> 8) & 0xFF);
                SetDCBrushColor(app->hdc_mem, RGB(r, g, b));
                RECT rc = { (int)curr->x, (int)curr->y, (int)(curr->x + curr->w), (int)(curr->y + curr->h) };
                FillRect(app->hdc_mem, &rc, (HBRUSH)GetStockObject(DC_BRUSH));
            } else if (a > 0) {
                GdiFlush();
                int rx0 = (int)curr->x;
                int ry0 = (int)curr->y;
                int rx1 = (int)(curr->x + curr->w);
                int ry1 = (int)(curr->y + curr->h);
                if (rx0 < 0) rx0 = 0;
                if (ry0 < 0) ry0 = 0;
                if (rx1 > app->win_width) rx1 = app->win_width;
                if (ry1 > app->win_height) ry1 = app->win_height;
                if (rx0 < rx1 && ry0 < ry1) {
                    float fa = (float)a / 255.0f;
                    float inv_a = 1.0f - fa;
                    float fr = (float)((curr->color >> 24) & 0xFF) * fa;
                    float fg = (float)((curr->color >> 16) & 0xFF) * fa;
                    float fb = (float)((curr->color >> 8) & 0xFF) * fa;
                    for (int y = ry0; y < ry1; y++) {
                        uint32_t* line = &app->pixels[y * app->win_width];
                        for (int x = rx0; x < rx1; x++) {
                            uint32_t orig = line[x];
                            float ob = (float)(orig & 0xFF) * inv_a + fb;
                            float og = (float)((orig >> 8) & 0xFF) * inv_a + fg;
                            float or_ = (float)((orig >> 16) & 0xFF) * inv_a + fr;
                            line[x] = ((uint32_t)or_ << 16) | ((uint32_t)og << 8) | (uint32_t)ob;
                        }
                    }
                }
            }
        }
        else if (curr->type == CMD_SCISSOR_PUSH) {
            HRGN rgn = CreateRectRgn((int)curr->x, (int)curr->y, (int)(curr->x + curr->w), (int)(curr->y + curr->h));
            SelectClipRgn(app->hdc_mem, rgn);
            DeleteObject(rgn);
        }
        else if (curr->type == CMD_SCISSOR_POP) {
            SelectClipRgn(app->hdc_mem, NULL);
        }
        curr = curr->next;
    }
}

// -------------------------------------------------------------
// 纯 C 过程式亚像素矢量图符光栅化引擎 (Procedural Vector Icons)
// -------------------------------------------------------------

// 矢量日历图标 (16x16)
static void rife_draw_vector_icon_calendar(RifeCore* core, float cx, float cy, uint32_t color) {
    rife_draw_round_rect(core, cx - 7.0f, cy - 7.0f, 14.0f, 14.0f, 3.0f, 0x00000000, color);
    rife_draw_rect(core, cx - 6.0f, cy - 4.0f, 12.0f, 1.5f, color);
    rife_draw_rect(core, cx - 4.0f, cy - 8.5f, 1.5f, 2.5f, color);
    rife_draw_rect(core, cx + 2.5f, cy - 8.5f, 1.5f, 2.5f, color);
    rife_draw_rect(core, cx - 3.5f, cy - 0.5f, 2.0f, 2.0f, color);
    rife_draw_rect(core, cx + 1.5f, cy - 0.5f, 2.0f, 2.0f, color);
    rife_draw_rect(core, cx - 3.5f, cy + 3.0f, 2.0f, 2.0f, color);
    rife_draw_rect(core, cx + 1.5f, cy + 3.0f, 2.0f, 2.0f, color);
}

// 矢量时钟图标 (16x16)
static void rife_draw_vector_icon_clock(RifeCore* core, float cx, float cy, uint32_t color) {
    rife_draw_circle(core, cx, cy, 7.0f, 0x00000000, color);
    rife_draw_circle(core, cx, cy, 1.5f, color, color);
    rife_draw_line(core, cx, cy, cx - 2.8f, cy - 2.8f, 1.5f, color);
    rife_draw_line(core, cx, cy, cx, cy - 4.8f, 1.5f, color);
}

// 矢量设置滑块图标 (16x16)
static void rife_draw_vector_icon_settings(RifeCore* core, float cx, float cy, uint32_t color) {
    rife_draw_line(core, cx - 7.0f, cy - 3.0f, cx + 7.0f, cy - 3.0f, 1.2f, color);
    rife_draw_circle(core, cx + 2.0f, cy - 3.0f, 2.2f, color, color);
    rife_draw_line(core, cx - 7.0f, cy + 3.0f, cx + 7.0f, cy + 3.0f, 1.2f, color);
    rife_draw_circle(core, cx - 3.0f, cy + 3.0f, 2.2f, color, color);
}

// 矢量主题太阳/月亮图标 (16x16)
static void rife_draw_vector_icon_theme(RifeCore* core, float cx, float cy, bool is_dark, uint32_t color) {
    if (is_dark) {
        rife_draw_circle(core, cx, cy, 6.0f, color, color);
        rife_draw_circle(core, cx + 2.5f, cy - 2.0f, 5.0f, 0x07090EFF, 0x07090EFF);
    } else {
        rife_draw_circle(core, cx, cy, 3.5f, color, color);
        rife_draw_line(core, cx, cy - 6.5f, cx, cy - 4.5f, 1.2f, color);
        rife_draw_line(core, cx, cy + 4.5f, cx, cy + 6.5f, 1.2f, color);
        rife_draw_line(core, cx - 6.5f, cy, cx - 4.5f, cy, 1.2f, color);
        rife_draw_line(core, cx + 4.5f, cy, cx + 6.5f, cy, 1.2f, color);
    }
}

// 现代 Windows 11 Fluent 风格窗口控制按钮
static void rife_draw_title_button(RifeCore* core, float bx, float by, float bw, float bh, int btn_type, int hover_state, bool is_max, bool is_dark) {
    uint32_t col_txt = is_dark ? 0x94A3B8FF : 0x64748BFF;
    float cx = bx + bw * 0.5f;
    float cy = by + bh * 0.5f;

    if (hover_state == 1) {
        if (btn_type == 2) {
            rife_draw_round_rect(core, bx, by, bw, bh, 6.0f, 0xEF4444FF, 0x00000000);
            col_txt = 0xFFFFFFFF;
        } else {
            uint32_t hov_bg = is_dark ? 0xFFFFFF15 : 0x0000000D;
            rife_draw_round_rect(core, bx, by, bw, bh, 6.0f, hov_bg, 0x00000000);
            col_txt = is_dark ? 0xF8FAFCFF : 0x0F172AFF;
        }
    }

    if (btn_type == 0) { // Minimize
        rife_draw_round_rect(core, cx - 5.0f, cy, 10.0f, 1.6f, 0.8f, col_txt, col_txt);
    } else if (btn_type == 1) { // Maximize / Restore
        if (is_max) {
            rife_draw_round_rect(core, cx - 3.0f, cy - 5.0f, 7.5f, 7.5f, 1.5f, 0x00000000, col_txt);
            rife_draw_round_rect(core, cx - 5.0f, cy - 3.0f, 7.5f, 7.5f, 1.5f, 0x00000000, col_txt);
        } else {
            rife_draw_round_rect(core, cx - 4.5f, cy - 4.5f, 9.0f, 9.0f, 1.5f, 0x00000000, col_txt);
        }
    } else if (btn_type == 2) { // Close
        rife_draw_line(core, cx - 4.0f, cy - 4.0f, cx + 4.0f, cy + 4.0f, 1.4f, col_txt);
        rife_draw_line(core, cx + 4.0f, cy - 4.0f, cx - 4.0f, cy + 4.0f, 1.4f, col_txt);
    }
}

// -------------------------------------------------------------
// 核心软件渲染管线 (Core Software Render Pipeline)
// -------------------------------------------------------------
static void app_render(AppContext* app, RifeCore* core) {
    if (!app || !core || app->win_width <= 0 || app->win_height <= 0) return;

    RifeSystemConfig* cfg = rife_get_system_config();
    bool is_dark = (cfg->palette == PALETTE_OBSIDIAN || cfg->cloud_color == CLOUD_COLOR_OBSIDIAN);
    bool is_zh = (cfg->language == LANG_ZH_CN);

    float ww = (float)app->win_width;
    float wh = (float)app->win_height;

    arena_reset(&core->frame_arena);
    core->render_head = NULL;
    core->render_tail = NULL;
    core->render_cmd_count = 0;

    // 1. 全局设计系统调色板 (Cosmic Obsidian & Porcelain Light)
    uint32_t bg_main     = is_dark ? 0x0C0E17FF : 0xF8FAFCFF;
    uint32_t bg_side     = is_dark ? 0x07090EFF : 0xF1F5F9FF;
    uint32_t col_div     = is_dark ? 0x1E223588 : 0xE2E8F0AA;
    uint32_t txt_title   = is_dark ? 0xF8FAFCFF : 0x0F172AFF;
    uint32_t txt_body    = is_dark ? 0xCBD5E1FF : 0x334155FF;
    uint32_t txt_muted   = is_dark ? 0x64748BFF : 0x94A3B8FF;
    uint32_t accent_pri  = is_dark ? 0x6366F1FF : 0x4F46E5FF;
    uint32_t accent_glow = is_dark ? 0x6366F126 : 0x4F46E514;

    // 2. 基础底板与侧边栏结构
    rife_draw_rect(core, 0.0f, 0.0f, ww, wh, bg_main);
    rife_draw_rect(core, 0.0f, 0.0f, SIDEBAR_WIDTH, wh, bg_side);
    rife_draw_rect(core, SIDEBAR_WIDTH - 1.0f, 0.0f, 1.0f, wh, col_div);

    // 3. 侧边栏品牌区域 (Branding Header: y 10 to 52)
    float logo_x = 16.0f;
    float logo_y = 12.0f;
    float logo_sz = 28.0f;
    rife_draw_round_rect(core, logo_x, logo_y, logo_sz, logo_sz, 7.5f, is_dark ? 0x4F46E5FF : 0x4338CAFF, is_dark ? 0x818CF8AA : 0x6366F1AA);
    rife_draw_round_rect(core, logo_x + 2.0f, logo_y + 1.0f, logo_sz - 4.0f, 1.0f, 1.0f, 0xFFFFFF55, 0x00000000);
    rife_draw_text_rect(core, logo_x, logo_y, logo_sz, logo_sz, "R", 0xFFFFFFFF, 5, 0);

    float brand_tx = logo_x + logo_sz + 10.0f;
    rife_draw_text_font(core, brand_tx, logo_y + 1.0f, "Rife", txt_title, 5);
    rife_draw_round_rect(core, brand_tx + 36.0f, logo_y + 3.0f, 38.0f, 15.0f, 4.0f, is_dark ? 0x6366F122 : 0x4F46E518, is_dark ? 0x6366F155 : 0x4F46E544);
    rife_draw_text_rect(core, brand_tx + 36.0f, logo_y + 3.0f, 38.0f, 15.0f, "STUDIO", is_dark ? 0xA5B4FCFF : 0x4F46E5FF, 4, 0);
    rife_draw_text_font(core, brand_tx, logo_y + 16.0f, is_zh ? "一体化时间工作台" : "Time Studio", txt_muted, 4);

    rife_draw_rect(core, 16.0f, 52.0f, SIDEBAR_WIDTH - 32.0f, 1.0f, col_div);

    // 4. 侧边栏导航分组 (Navigation Stack)
    rife_draw_text_font(core, 18.0f, 62.0f, is_zh ? "工作空间" : "WORKSPACE", txt_muted, 4);

    const char* nav_labels_zh[3] = { "多维日程", "极简时钟", "偏好设置" };
    const char* nav_labels_en[3] = { "Schedule", "Clock", "Settings" };
    const char* nav_shortcuts[3] = { "Alt+1", "Alt+2", "Alt+3" };

    float nav_y0 = 80.0f;
    float nav_item_h = 38.0f;
    float nav_item_w = SIDEBAR_WIDTH - 24.0f;
    float nav_item_x = 12.0f;
    float nav_gap = 5.0f;

    for (int i = 0; i < 3; i++) {
        float ny = nav_y0 + (float)i * (nav_item_h + nav_gap);
        bool is_active = (app->active_page == (AppPage)i);
        bool is_hover = (app->hovered_nav_idx == i && !is_active);

        uint32_t item_icon_col = is_active ? (is_dark ? 0xA5B4FCFF : 0x4F46E5FF) : (is_hover ? txt_title : txt_muted);
        uint32_t item_text_col = is_active ? txt_title : (is_hover ? txt_title : txt_body);

        if (is_active) {
            rife_draw_round_rect(core, nav_item_x, ny, nav_item_w, nav_item_h, 8.0f, accent_glow, is_dark ? 0x6366F144 : 0x4F46E533);
            rife_draw_round_rect(core, nav_item_x + 3.0f, ny + 9.0f, 3.0f, 20.0f, 1.5f, accent_pri, accent_pri);
        } else if (is_hover) {
            rife_draw_round_rect(core, nav_item_x, ny, nav_item_w, nav_item_h, 8.0f, is_dark ? 0xFFFFFF0A : 0x00000008, 0x00000000);
        }

        float icon_cx = nav_item_x + 20.0f;
        float icon_cy = ny + nav_item_h * 0.5f;
        if (i == 0)      rife_draw_vector_icon_calendar(core, icon_cx, icon_cy, item_icon_col);
        else if (i == 1) rife_draw_vector_icon_clock(core, icon_cx, icon_cy, item_icon_col);
        else if (i == 2) rife_draw_vector_icon_settings(core, icon_cx, icon_cy, item_icon_col);

        rife_draw_text_font(core, nav_item_x + 36.0f, ny + 11.0f, is_zh ? nav_labels_zh[i] : nav_labels_en[i], item_text_col, is_active ? 5 : 3);
        rife_draw_text_rect(core, nav_item_x + nav_item_w - 42.0f, ny, 36.0f, nav_item_h, nav_shortcuts[i], is_active ? accent_pri : txt_muted, 4, 0);
    }

    // 5. 侧边栏底部面板 (Bottom Controls & Privacy Signature)
    float theme_btn_y = wh - 84.0f;
    float theme_btn_w = nav_item_w;
    float theme_btn_x = nav_item_x;
    float theme_btn_h = 32.0f;

    rife_draw_round_rect(core, theme_btn_x, theme_btn_y, theme_btn_w, theme_btn_h, 7.0f, is_dark ? 0x14182488 : 0xFFFFFFCC, col_div);
    rife_draw_vector_icon_theme(core, theme_btn_x + 18.0f, theme_btn_y + theme_btn_h * 0.5f, is_dark, is_dark ? 0xF59E0BFF : 0x6366F1FF);
    rife_draw_text_font(core, theme_btn_x + 34.0f, theme_btn_y + 9.0f, is_dark ? "黑曜石深色" : "晨光浅色", txt_body, 3);

    float sw_w = 26.0f, sw_h = 15.0f;
    float sw_x = theme_btn_x + theme_btn_w - sw_w - 10.0f;
    float sw_y = theme_btn_y + (theme_btn_h - sw_h) * 0.5f;
    rife_draw_round_rect(core, sw_x, sw_y, sw_w, sw_h, 7.5f, is_dark ? 0x312E81AA : 0xE2E8F0FF, is_dark ? 0x4338CAAA : 0xCBD5E1FF);
    float knob_x = is_dark ? (sw_x + sw_w - 13.0f) : (sw_x + 2.0f);
    rife_draw_circle(core, knob_x + 5.5f, sw_y + 7.5f, 5.0f, is_dark ? 0x818CF8FF : 0x3B82F6FF, is_dark ? 0xA5B4FCFF : 0x60A5FAFF);

    float stat_y = wh - 44.0f;
    rife_draw_circle(core, 22.0f, stat_y + 4.0f, 3.0f, 0x10B981FF, 0x34D399FF);
    rife_draw_text_font(core, 30.0f, stat_y, is_zh ? "纯 C11 · 零堆开销" : "Pure C11 · Zero Churn", txt_muted, 4);

    rife_draw_text_rect(core, nav_item_x, wh - 24.0f, nav_item_w, 16.0f, "Rife v1.0 · Renly", txt_muted, 4, 0);

    // 6. 顶部现代一体化标题栏 (Titlebar: 44px)
    rife_draw_rect(core, SIDEBAR_WIDTH, TITLEBAR_HEIGHT - 1.0f, ww - SIDEBAR_WIDTH, 1.0f, col_div);

    // A. 页面面包屑与当前模块定位 (左侧)
    float bread_x = SIDEBAR_WIDTH + 20.0f;
    float bread_y = 12.0f;
    if (app->active_page == PAGE_SCHEDULE) {
        rife_draw_text_font(core, bread_x, bread_y, is_zh ? "多维日程" : "Schedule", txt_title, 5);
        rife_draw_text_font(core, bread_x + (is_zh ? 72.0f : 80.0f), bread_y + 1.0f, is_zh ? "· 时间矩阵与待办管理" : "· Matrix & Tasks", txt_muted, 4);
    } else if (app->active_page == PAGE_CLOCK) {
        rife_draw_text_font(core, bread_x, bread_y, is_zh ? "极简时钟" : "Clock", txt_title, 5);
        rife_draw_text_font(core, bread_x + (is_zh ? 72.0f : 60.0f), bread_y + 1.0f, is_zh ? "· 24H 环形流与全天负荷" : "· Radial Flow & Load", txt_muted, 4);
    } else if (app->active_page == PAGE_SETTINGS) {
        rife_draw_text_font(core, bread_x, bread_y, is_zh ? "偏好设置" : "Settings", txt_title, 5);
        rife_draw_text_font(core, bread_x + (is_zh ? 72.0f : 72.0f), bread_y + 1.0f, is_zh ? "· 视觉风格与系统配置" : "· Aesthetics & Kernel", txt_muted, 4);
    }

    // B. 中央动态日期标尺 (精致药丸胶囊)
    SYSTEMTIME st;
    GetLocalTime(&st);
    const char* wdays_zh[7] = { "周日", "周一", "周二", "周三", "周四", "周五", "周六" };
    const char* wdays_en[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    char date_str[64];
    if (is_zh) {
        snprintf(date_str, sizeof(date_str), "%d年%d月%d日 · %s", st.wYear, st.wMonth, st.wDay, wdays_zh[st.wDayOfWeek]);
    } else {
        snprintf(date_str, sizeof(date_str), "%s, %d-%02d-%02d", wdays_en[st.wDayOfWeek], st.wYear, st.wMonth, st.wDay);
    }
    float date_pill_w = 190.0f;
    float date_pill_h = 26.0f;
    float date_pill_x = (SIDEBAR_WIDTH + ww - 130.0f - date_pill_w) * 0.5f;
    float date_pill_y = (TITLEBAR_HEIGHT - date_pill_h) * 0.5f;
    rife_draw_round_rect(core, date_pill_x, date_pill_y, date_pill_w, date_pill_h, 6.0f, is_dark ? 0xFFFFFF08 : 0x00000006, 0x00000000);
    rife_draw_text_rect(core, date_pill_x, date_pill_y, date_pill_w, date_pill_h, date_str, txt_muted, 4, 0);

    // C. 右侧原生三态窗口控制按钮 (Min / Max / Close)
    float btn_w = 36.0f;
    float btn_h = 28.0f;
    float btn_gap = 3.0f;
    float btn_y = (TITLEBAR_HEIGHT - btn_h) * 0.5f;
    float btn_close_x = ww - 12.0f - btn_w;
    float btn_max_x   = btn_close_x - btn_gap - btn_w;
    float btn_min_x   = btn_max_x - btn_gap - btn_w;

    bool is_max = IsZoomed(app->hwnd);
    rife_draw_title_button(core, btn_min_x, btn_y, btn_w, btn_h, 0, (app->hovered_title_btn == 0) ? 1 : 0, is_max, is_dark);
    rife_draw_title_button(core, btn_max_x, btn_y, btn_w, btn_h, 1, (app->hovered_title_btn == 1) ? 1 : 0, is_max, is_dark);
    rife_draw_title_button(core, btn_close_x, btn_y, btn_w, btn_h, 2, (app->hovered_title_btn == 2) ? 1 : 0, is_max, is_dark);

    // 7. 右侧主工作区视口 (Viewport: Main Content Area)
    float client_x = SIDEBAR_WIDTH;
    float client_y = TITLEBAR_HEIGHT;
    float client_w = ww - SIDEBAR_WIDTH;
    float client_h = wh - TITLEBAR_HEIGHT;

    if (client_w > 100.0f && client_h > 100.0f) {
        rife_push_scissor_round(core, client_x, client_y, client_w, client_h, 0.0f);

        if (app->active_page == PAGE_SCHEDULE && app->calendar_inst) {
            g_calendar_plugin_app.render(app->calendar_inst, core, client_x, client_y, client_w, client_h);
        } else if (app->active_page == PAGE_CLOCK && app->clock_inst) {
            g_clock_plugin_app.render(app->clock_inst, core, client_x, client_y, client_w, client_h);
        } else if (app->active_page == PAGE_SETTINGS && app->settings_inst) {
            g_settings_plugin_app.render(app->settings_inst, core, client_x, client_y, client_w, client_h);
        }

        rife_pop_scissor(core);
    }

    // 8. 提交刷新 GDI & 像素渲染管线
    GdiFlush();
    SetBkMode(app->hdc_mem, TRANSPARENT);
    SelectObject(app->hdc_mem, GetStockObject(DC_PEN));
    SelectObject(app->hdc_mem, GetStockObject(DC_BRUSH));

    app_flush_render_commands(app, core);

    HDC hdc_win = GetDC(app->hwnd);
    BitBlt(hdc_win, 0, 0, app->win_width, app->win_height, app->hdc_mem, 0, 0, SRCCOPY);
    ReleaseDC(app->hwnd, hdc_win);

    core->render_head = NULL;
    core->render_tail = NULL;
    core->render_cmd_count = 0;
    core->needs_redraw = false;
}

void rife_render_flush(RifeCore* core) {
    if (s_app && core) {
        app_render(s_app, core);
    }
}

// -------------------------------------------------------------
// 原生 Win32 消息循环与事件调度 (Window Procedure)
// -------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppContext* app = s_app;
    RifeCore* core = s_core;

    switch (msg) {
    case WM_NCCALCSIZE: {
        // 彻底移除 Windows 传统粗糙边框与默认标题栏，实现现代沉浸式无边框
        if (wParam) {
            if (IsZoomed(hwnd)) {
                NCCALCSIZE_PARAMS* params = (NCCALCSIZE_PARAMS*)lParam;
                HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi = { sizeof(mi) };
                if (GetMonitorInfoW(hMon, &mi)) {
                    params->rgrc[0] = mi.rcWork;
                }
            }
            return 0;
        }
        return 0;
    }

    case WM_NCHITTEST: {
        if (!app) return DefWindowProcW(hwnd, msg, wParam, lParam);
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        ScreenToClient(hwnd, &pt);
        int w = app->win_width;
        int h = app->win_height;
        int border = 6;
        bool is_max = IsZoomed(hwnd);

        // 1. 非最大化时边缘缩放手柄
        if (!is_max) {
            if (pt.y < border && pt.x < border) return HTTOPLEFT;
            if (pt.y < border && pt.x >= w - border) return HTTOPRIGHT;
            if (pt.y >= h - border && pt.x < border) return HTBOTTOMLEFT;
            if (pt.y >= h - border && pt.x >= w - border) return HTBOTTOMRIGHT;
            if (pt.y < border) return HTTOP;
            if (pt.y >= h - border) return HTBOTTOM;
            if (pt.x < border) return HTLEFT;
            if (pt.x >= w - border) return HTRIGHT;
        }

        // 2. 标题栏区域判定
        if (pt.y >= 0 && pt.y < (int)TITLEBAR_HEIGHT) {
            // 右侧三态按钮区域：交给客户区处理点击
            if (pt.x >= w - 130) return HTCLIENT;
            // 其余标题栏空间：允许原生拖拽与双击最大化
            return HTCAPTION;
        }

        return HTCLIENT;
    }

    case WM_SIZE: {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        if (app && w > 0 && h > 0) {
            recreate_backbuffer(app, w, h);
            if (core) rife_request_redraw(core);
        }
        return 0;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = MIN_WINDOW_W;
        mmi->ptMinTrackSize.y = MIN_WINDOW_H;
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!app || !core) break;
        float mx = (float)GET_X_LPARAM(lParam);
        float my = (float)GET_Y_LPARAM(lParam);
        app->input.mouse_x = mx;
        app->input.mouse_y = my;

        if (!app->mouse_in_window) {
            app->mouse_in_window = true;
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
        }

        // 标题栏按钮悬停检测
        int prev_btn = app->hovered_title_btn;
        float btn_w = 36.0f;
        float btn_h = 28.0f;
        float btn_gap = 3.0f;
        float btn_y = (TITLEBAR_HEIGHT - btn_h) * 0.5f;
        float ww = (float)app->win_width;
        float btn_close_x = ww - 12.0f - btn_w;
        float btn_max_x   = btn_close_x - btn_gap - btn_w;
        float btn_min_x   = btn_max_x - btn_gap - btn_w;

        if (my >= btn_y && my <= btn_y + btn_h) {
            if (mx >= btn_min_x && mx <= btn_min_x + btn_w) app->hovered_title_btn = 0;
            else if (mx >= btn_max_x && mx <= btn_max_x + btn_w) app->hovered_title_btn = 1;
            else if (mx >= btn_close_x && mx <= btn_close_x + btn_w) app->hovered_title_btn = 2;
            else app->hovered_title_btn = -1;
        } else {
            app->hovered_title_btn = -1;
        }

        // 侧边栏项悬停检测
        int prev_nav = app->hovered_nav_idx;
        float nav_item_w = SIDEBAR_WIDTH - 24.0f;
        if (mx >= 12.0f && mx <= 12.0f + nav_item_w) {
            float nav_y0 = 80.0f;
            float nav_h = 38.0f;
            float nav_gap = 5.0f;
            app->hovered_nav_idx = -1;
            for (int i = 0; i < 3; i++) {
                float ny = nav_y0 + (float)i * (nav_h + nav_gap);
                if (my >= ny && my <= ny + nav_h) {
                    app->hovered_nav_idx = i;
                    break;
                }
            }
        } else {
            app->hovered_nav_idx = -1;
        }

        if (prev_btn != app->hovered_title_btn || prev_nav != app->hovered_nav_idx) {
            rife_request_redraw(core);
        }

        // 转发至当前激活页面 (视口相对坐标)
        float client_x = SIDEBAR_WIDTH;
        float client_y = TITLEBAR_HEIGHT;
        float client_w = ww - SIDEBAR_WIDTH;
        float client_h = (float)app->win_height - TITLEBAR_HEIGHT;

        RifeInput page_in = app->input;
        page_in.mouse_x -= client_x;
        page_in.mouse_y -= client_y;

        if (app->active_page == PAGE_SCHEDULE && app->calendar_inst) {
            g_calendar_plugin_app.update(app->calendar_inst, core, &page_in, client_w, client_h);
        } else if (app->active_page == PAGE_CLOCK && app->clock_inst) {
            g_clock_plugin_app.update(app->clock_inst, core, &page_in, client_w, client_h);
        } else if (app->active_page == PAGE_SETTINGS && app->settings_inst) {
            g_settings_plugin_app.update(app->settings_inst, core, &page_in, client_w, client_h);
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        if (!app || !core) break;
        app->mouse_in_window = false;
        app->hovered_title_btn = -1;
        app->hovered_nav_idx = -1;
        rife_request_redraw(core);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        if (!app || !core) break;
        SetFocus(hwnd);
        float mx = (float)GET_X_LPARAM(lParam);
        float my = (float)GET_Y_LPARAM(lParam);
        app->input.mouse_pressed[0] = true;
        app->input.mouse_down[0] = true;

        float ww = (float)app->win_width;
        float wh = (float)app->win_height;

        // 1. 标题栏按钮点击响应
        float btn_w = 36.0f;
        float btn_h = 28.0f;
        float btn_gap = 3.0f;
        float btn_y = (TITLEBAR_HEIGHT - btn_h) * 0.5f;
        float btn_close_x = ww - 12.0f - btn_w;
        float btn_max_x   = btn_close_x - btn_gap - btn_w;
        float btn_min_x   = btn_max_x - btn_gap - btn_w;

        if (my >= btn_y && my <= btn_y + btn_h) {
            if (mx >= btn_min_x && mx <= btn_min_x + btn_w) {
                ShowWindow(hwnd, SW_MINIMIZE);
                return 0;
            }
            if (mx >= btn_max_x && mx <= btn_max_x + btn_w) {
                ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
                return 0;
            }
            if (mx >= btn_close_x && mx <= btn_close_x + btn_w) {
                DestroyWindow(hwnd);
                return 0;
            }
        }

        // 2. 侧边栏导航点击响应
        float nav_item_w = SIDEBAR_WIDTH - 24.0f;
        if (mx >= 12.0f && mx <= 12.0f + nav_item_w) {
            float nav_y0 = 80.0f;
            float nav_h = 38.0f;
            float nav_gap = 5.0f;
            for (int i = 0; i < 3; i++) {
                float ny = nav_y0 + (float)i * (nav_h + nav_gap);
                if (my >= ny && my <= ny + nav_h) {
                    if (app->active_page != (AppPage)i) {
                        app->active_page = (AppPage)i;
                        rife_reclaim_physical_memory();
                        rife_request_redraw(core);
                    }
                    return 0;
                }
            }

            // 主题极简切换
            float theme_btn_y = wh - 84.0f;
            if (my >= theme_btn_y && my <= theme_btn_y + 32.0f) {
                RifeSystemConfig* cfg = rife_get_system_config();
                cfg->palette = (cfg->palette == PALETTE_OBSIDIAN) ? PALETTE_GEMINI : PALETTE_OBSIDIAN;
                rife_save_system_config();
                rife_reclaim_physical_memory();
                rife_request_redraw(core);
                return 0;
            }
        }

        // 3. 转发至当前激活页面
        float client_x = SIDEBAR_WIDTH;
        float client_y = TITLEBAR_HEIGHT;
        float client_w = ww - SIDEBAR_WIDTH;
        float client_h = wh - TITLEBAR_HEIGHT;

        RifeInput page_in = app->input;
        page_in.mouse_x -= client_x;
        page_in.mouse_y -= client_y;

        if (app->active_page == PAGE_SCHEDULE && app->calendar_inst) {
            g_calendar_plugin_app.update(app->calendar_inst, core, &page_in, client_w, client_h);
        } else if (app->active_page == PAGE_CLOCK && app->clock_inst) {
            g_clock_plugin_app.update(app->clock_inst, core, &page_in, client_w, client_h);
        } else if (app->active_page == PAGE_SETTINGS && app->settings_inst) {
            g_settings_plugin_app.update(app->settings_inst, core, &page_in, client_w, client_h);
        }
        rife_request_redraw(core);
        return 0;
    }

    case WM_LBUTTONUP: {
        if (!app || !core) break;
        app->input.mouse_down[0] = false;
        app->input.mouse_released[0] = true;

        float client_x = SIDEBAR_WIDTH;
        float client_y = TITLEBAR_HEIGHT;
        float client_w = (float)app->win_width - SIDEBAR_WIDTH;
        float client_h = (float)app->win_height - TITLEBAR_HEIGHT;

        RifeInput page_in = app->input;
        page_in.mouse_x -= client_x;
        page_in.mouse_y -= client_y;

        if (app->active_page == PAGE_SCHEDULE && app->calendar_inst) {
            g_calendar_plugin_app.update(app->calendar_inst, core, &page_in, client_w, client_h);
        } else if (app->active_page == PAGE_CLOCK && app->clock_inst) {
            g_clock_plugin_app.update(app->clock_inst, core, &page_in, client_w, client_h);
        } else if (app->active_page == PAGE_SETTINGS && app->settings_inst) {
            g_settings_plugin_app.update(app->settings_inst, core, &page_in, client_w, client_h);
        }
        rife_request_redraw(core);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        if (!app || !core) break;
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        app->input.scroll_delta = (float)delta / 120.0f;

        float client_x = SIDEBAR_WIDTH;
        float client_y = TITLEBAR_HEIGHT;
        float client_w = (float)app->win_width - SIDEBAR_WIDTH;
        float client_h = (float)app->win_height - TITLEBAR_HEIGHT;

        RifeInput page_in = app->input;
        page_in.mouse_x -= client_x;
        page_in.mouse_y -= client_y;

        if (app->active_page == PAGE_SCHEDULE && app->calendar_inst) {
            g_calendar_plugin_app.update(app->calendar_inst, core, &page_in, client_w, client_h);
        } else if (app->active_page == PAGE_CLOCK && app->clock_inst) {
            g_clock_plugin_app.update(app->clock_inst, core, &page_in, client_w, client_h);
        } else if (app->active_page == PAGE_SETTINGS && app->settings_inst) {
            g_settings_plugin_app.update(app->settings_inst, core, &page_in, client_w, client_h);
        }
        rife_request_redraw(core);
        return 0;
    }

    case WM_KEYDOWN: {
        if (!app || !core) break;
        int vk = (int)wParam;
        if (vk >= 0 && vk < 256) {
            app->input.key_down[vk] = true;
            app->input.key_pressed[vk] = true;
        }

        // Alt+1 / Alt+2 / Alt+3 快速切换工作空间页面
        if (GetKeyState(VK_MENU) & 0x8000) {
            if (vk == '1') {
                app->active_page = PAGE_SCHEDULE;
                rife_reclaim_physical_memory();
                rife_request_redraw(core);
                return 0;
            } else if (vk == '2') {
                app->active_page = PAGE_CLOCK;
                rife_reclaim_physical_memory();
                rife_request_redraw(core);
                return 0;
            } else if (vk == '3') {
                app->active_page = PAGE_SETTINGS;
                rife_reclaim_physical_memory();
                rife_request_redraw(core);
                return 0;
            }
        }

        // Ctrl+V 剪贴板文本粘贴
        if (vk == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (OpenClipboard(hwnd)) {
                HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                if (hData) {
                    wchar_t* wstr = (wchar_t*)GlobalLock(hData);
                    if (wstr) {
                        WideCharToMultiByte(CP_UTF8, 0, wstr, -1, app->input.text_input, sizeof(app->input.text_input), NULL, NULL);
                        GlobalUnlock(hData);
                    }
                }
                CloseClipboard();
            }
        }

        float client_x = SIDEBAR_WIDTH;
        float client_y = TITLEBAR_HEIGHT;
        float client_w = (float)app->win_width - SIDEBAR_WIDTH;
        float client_h = (float)app->win_height - TITLEBAR_HEIGHT;

        RifeInput page_in = app->input;
        page_in.mouse_x -= client_x;
        page_in.mouse_y -= client_y;

        if (app->active_page == PAGE_SCHEDULE && app->calendar_inst) {
            g_calendar_plugin_app.update(app->calendar_inst, core, &page_in, client_w, client_h);
        } else if (app->active_page == PAGE_CLOCK && app->clock_inst) {
            g_clock_plugin_app.update(app->clock_inst, core, &page_in, client_w, client_h);
        } else if (app->active_page == PAGE_SETTINGS && app->settings_inst) {
            g_settings_plugin_app.update(app->settings_inst, core, &page_in, client_w, client_h);
        }
        rife_request_redraw(core);
        return 0;
    }

    case WM_KEYUP: {
        if (!app) break;
        int vk = (int)wParam;
        if (vk >= 0 && vk < 256) app->input.key_down[vk] = false;
        return 0;
    }

    case WM_CHAR: {
        if (!app || !core) break;
        wchar_t wch = (wchar_t)wParam;
        if (wch >= 32) {
            wchar_t wbuf[2] = { wch, 0 };
            char utf8[8] = { 0 };
            int len = WideCharToMultiByte(CP_UTF8, 0, wbuf, 1, utf8, sizeof(utf8), NULL, NULL);
            if (len > 0) {
                size_t cur_len = strlen(app->input.text_input);
                if (cur_len + (size_t)len < sizeof(app->input.text_input) - 1) {
                    memcpy(app->input.text_input + cur_len, utf8, (size_t)len);
                    app->input.text_input[cur_len + len] = '\0';
                }
            }

            float client_x = SIDEBAR_WIDTH;
            float client_y = TITLEBAR_HEIGHT;
            float client_w = (float)app->win_width - SIDEBAR_WIDTH;
            float client_h = (float)app->win_height - TITLEBAR_HEIGHT;

            RifeInput page_in = app->input;
            page_in.mouse_x -= client_x;
            page_in.mouse_y -= client_y;

            if (app->active_page == PAGE_SCHEDULE && app->calendar_inst) {
                g_calendar_plugin_app.update(app->calendar_inst, core, &page_in, client_w, client_h);
            } else if (app->active_page == PAGE_CLOCK && app->clock_inst) {
                g_clock_plugin_app.update(app->clock_inst, core, &page_in, client_w, client_h);
            } else if (app->active_page == PAGE_SETTINGS && app->settings_inst) {
                g_settings_plugin_app.update(app->settings_inst, core, &page_in, client_w, client_h);
            }
            rife_request_redraw(core);
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (app && app->hdc_mem) {
            BitBlt(hdc, 0, 0, app->win_width, app->win_height, app->hdc_mem, 0, 0, SRCCOPY);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY: {
        PostQuitMessage(0);
        return 0;
    }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// -------------------------------------------------------------
// 主入口与生命周期 (Application Main Entry)
// -------------------------------------------------------------
int main(void) {
    timeBeginPeriod(1);

    // 1. 初始化系统配置
    rife_load_system_config();
    RifeSystemConfig* cfg = rife_get_system_config();

    // 2. 初始化微内核渲染与内存调度 (常驻 1MB, 帧缓冲 256KB)
    RifeCore core;
    if (!rife_core_init(&core, 1024 * 1024, 256 * 1024, 60)) {
        return 1;
    }
    s_core = &core;

    // 3. 构建应用上下文
    AppContext app;
    memset(&app, 0, sizeof(AppContext));
    s_app = &app;

    app.active_page = PAGE_SCHEDULE; // 默认展开多维日程工作台
    app.hovered_nav_idx = -1;
    app.hovered_title_btn = -1;

    // 4. 初始化三大核心模块实例 (直接挂载，零虚拟窗口，零中间总线)
    app.calendar_inst = g_calendar_plugin_app.create(&core);
    app.clock_inst    = g_clock_plugin_app.create(&core);
    app.settings_inst = g_settings_plugin_app.create(&core);

    // 5. 注册原生无边框窗口类
    HINSTANCE hInstance = GetModuleHandleW(NULL);
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    wc.hCursor       = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"RifeWindowClass";
    RegisterClassW(&wc);

    // 6. 计算屏幕居中几何参数
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int init_w = 1200;
    int init_h = 760;
    if (init_w > screen_w - 60) init_w = screen_w - 60;
    if (init_h > screen_h - 80) init_h = screen_h - 80;
    int init_x = (screen_w - init_w) / 2;
    int init_y = (screen_h - init_h) / 2;

    // 使用 WS_THICKFRAME 与 WS_CAPTION 结合 WM_NCCALCSIZE，原生获得 DWM 阴影与平滑吸附
    DWORD style = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CAPTION;
    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"RifeWindowClass",
        L"Rife",
        style,
        init_x, init_y, init_w, init_h,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) {
        rife_core_shutdown(&core);
        return 1;
    }
    app.hwnd = hwnd;

    // 7. 初始化内存 DC 与清晰字体
    HDC hdc_win = GetDC(hwnd);
    app.hdc_mem = CreateCompatibleDC(hdc_win);
    ReleaseDC(hwnd, hdc_win);

    recreate_backbuffer(&app, init_w, init_h);
    update_system_fonts(&app, cfg->font_scale);

    // 8. 呈现窗口并强制首次工作集物理紧凑
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    app_render(&app, &core);
    rife_reclaim_physical_memory();

    // 9. 现代 60FPS 极低能耗主循环 (Smart Idle Gating)
    bool running = true;
    int startup_trim_frames = 0;

    while (running) {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;

        // 启动后 30 帧再次执行一次物理内存深度紧凑，锁死在 ~5MB
        if (startup_trim_frames < 30) {
            startup_trim_frames++;
            if (startup_trim_frames == 30) {
                rife_reclaim_physical_memory();
            }
        }

        // 字体配置动态热更新检测
        if (cfg->font_scale != app.current_font_scale) {
            update_system_fonts(&app, cfg->font_scale);
            rife_request_redraw(&core);
        }

        // 渲染与重绘请求
        if (core.needs_redraw) {
            app_render(&app, &core);
        }

        // 帧末尾清理单次按键与输入状态
        memset(app.input.mouse_pressed, 0, sizeof(app.input.mouse_pressed));
        memset(app.input.mouse_released, 0, sizeof(app.input.mouse_released));
        memset(app.input.key_pressed, 0, sizeof(app.input.key_pressed));
        app.input.text_input[0] = '\0';
        app.input.scroll_delta = 0.0f;

        // 智能静止休眠：无重绘请求且无键盘鼠标输入时轻度挂起 CPU，功耗压制至 ~0.0%
        if (!core.needs_redraw) {
            WaitMessage();
        }
    }

    // 10. 资源回收与正常退出
    if (app.calendar_inst) g_calendar_plugin_app.destroy(app.calendar_inst);
    if (app.clock_inst)    g_clock_plugin_app.destroy(app.clock_inst);
    if (app.settings_inst) g_settings_plugin_app.destroy(app.settings_inst);

    destroy_fonts(&app);
    if (app.hdc_mem) {
        if (app.hbm_old) SelectObject(app.hdc_mem, app.hbm_old);
        if (app.hbm_mem) DeleteObject(app.hbm_mem);
        DeleteDC(app.hdc_mem);
    }

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
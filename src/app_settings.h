#pragma once
#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include "rife_core.h"

typedef enum {
    LANG_ZH_CN = 0,
    LANG_EN
} RifeLanguage;

typedef enum {
    FONT_SCALE_100 = 0,
    FONT_SCALE_125,
    FONT_SCALE_150
} FontScaleType;

typedef enum {
    PALETTE_GEMINI = 0,
    PALETTE_OBSIDIAN,
    PALETTE_SUNSET,
    PALETTE_CYBER
} AuraPaletteType;

typedef enum {
    DESKTOP_MODE_FLOATING = 0,
    DESKTOP_MODE_WALLPAPER
} DesktopModeType;

typedef enum {
    DOCK_ALIGN_CENTER = 0,
    DOCK_ALIGN_RIGHT
} DockAlignType;

typedef enum {
    ICON_STYLE_PROCEDURAL = 0,
    ICON_STYLE_NATIVE_ICO
} IconStyleType;

typedef struct {
    RifeLanguage language;
    FontScaleType font_scale;

    AuraPaletteType palette;
    bool aura_animated;
    int aura_speed_idx;
    float glass_alpha;
    bool specular_rim;
    float squircle_radius;
    bool cursor_disturbance;

    DesktopModeType desktop_mode;
    bool dock_always_visible;
    DockAlignType dock_align;

    IconStyleType icon_style;
    bool shortcut_grid_align;

    int target_fps_idx;
    bool background_throttle;
} RifeSystemConfig;

RifeApp app_settings_create_stub(void);
void app_settings_launch(RifeApp* self, RifeCore* core, float ww, float wh);
void app_settings_close(RifeApp* self);
bool app_settings_is_loaded(const RifeApp* self);
bool app_settings_is_active(const RifeApp* self);
void app_settings_set_viewport(RifeApp* self, float ww, float wh);
void app_settings_get_window_rect(const RifeApp* self, float* x, float* y, float* w, float* h, float* anim);
RifeSystemConfig* app_settings_get_config(RifeApp* self);

#endif
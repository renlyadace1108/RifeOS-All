#pragma once
#ifndef RIFE_APP_API_H
#define RIFE_APP_API_H

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
    CLOUD_COLOR_TRANSLUCENT = 0,
    CLOUD_COLOR_AZURE,
    CLOUD_COLOR_VIOLET,
    CLOUD_COLOR_OBSIDIAN
} CloudColorType;

typedef enum {
    BREATH_SPEED_VERY_SLOW = 0, // 6.0s 极缓 (默认)
    BREATH_SPEED_SLOW,          // 4.0s 舒缓
    BREATH_SPEED_NORMAL         // 2.5s 标准
} BreathSpeedType;

typedef enum {
    BREATH_COLOR_EMERALD = 0,   // 翡翠绿 (默认)
    BREATH_COLOR_CYAN,          // 极光青
    BREATH_COLOR_AZURE,         // 冰川蓝
    BREATH_COLOR_AMBER,         // 琥珀金
    BREATH_COLOR_VIOLET         // 暮色紫
} BreathColorType;

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
    CloudColorType cloud_color;
    BreathSpeedType breath_speed; // 呼吸时间/速率
    BreathColorType breath_color; // 呼吸灯色彩

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

typedef struct RifePluginApp {
    uint32_t app_id;
    const char* id;
    const char* name_zh;
    const char* name_en;
    const char* glyph;
    uint32_t color_top;
    uint32_t color_bot;
    float default_w;
    float default_h;
    bool pin_to_dock;

    void* (*create)(RifeCore* core);
    void  (*destroy)(void* inst);
    void  (*update)(void* inst, RifeCore* core, const RifeInput* input, float client_w, float client_h);
    void  (*render)(void* inst, RifeCore* core, float client_x, float client_y, float client_w, float client_h);
} RifePluginApp;

#endif
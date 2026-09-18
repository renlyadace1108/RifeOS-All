#include "app_manifest.h"

extern const RifePluginApp g_settings_plugin_app;

const RifePluginApp* g_installed_apps[] = {
    &g_settings_plugin_app,
};

const size_t g_installed_app_count = sizeof(g_installed_apps) / sizeof(g_installed_apps[0]);

static RifeSystemConfig g_system_config = {
    .language = LANG_ZH_CN,
    .font_scale = FONT_SCALE_125,
    .palette = PALETTE_GEMINI,
    .cloud_color = CLOUD_COLOR_TRANSLUCENT,
    .breath_speed = BREATH_SPEED_VERY_SLOW,
    .breath_color = BREATH_COLOR_EMERALD,
    .aura_animated = true,
    .aura_speed_idx = 1,
    .glass_alpha = 0.72f,
    .specular_rim = true,
    .squircle_radius = 20.0f,
    .cursor_disturbance = true,
    .desktop_mode = DESKTOP_MODE_FLOATING,
    .dock_always_visible = false,
    .dock_align = DOCK_ALIGN_CENTER,
    .icon_style = ICON_STYLE_PROCEDURAL,
    .shortcut_grid_align = true,
    .target_fps_idx = 0,
    .background_throttle = true
};

RifeSystemConfig* rife_get_system_config(void) {
    return &g_system_config;
}
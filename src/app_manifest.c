#include "app_manifest.h"

extern const RifePluginApp g_settings_plugin_app;
extern const RifePluginApp g_calendar_plugin_app;

const RifePluginApp* g_installed_apps[] = {
    &g_calendar_plugin_app,
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

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>

#define RIFE_CONFIG_MAGIC 0x43464947 // "CFIG"
#define RIFE_CONFIG_VERSION 1

typedef struct {
    uint32_t magic;
    uint32_t version;
    RifeSystemConfig config;
} RifeConfigStorage;

static void get_system_config_path(char* out_path, size_t max_len) {
    char appdata[MAX_PATH] = { 0 };
    DWORD len = GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        char rife_dir[MAX_PATH];
        snprintf(rife_dir, sizeof(rife_dir), "%s\\RifeOS", appdata);
        CreateDirectoryA(rife_dir, NULL);
        snprintf(out_path, max_len, "%s\\system_config.bin", rife_dir);
        return;
    }
    snprintf(out_path, max_len, "system_config.bin");
}

void rife_save_system_config(void) {
    char path[MAX_PATH];
    get_system_config_path(path, sizeof(path));
    FILE* fp = fopen(path, "wb");
    if (!fp) return;
    RifeConfigStorage store;
    store.magic = RIFE_CONFIG_MAGIC;
    store.version = RIFE_CONFIG_VERSION;
    store.config = g_system_config;
    fwrite(&store, sizeof(RifeConfigStorage), 1, fp);
    fclose(fp);
}

void rife_load_system_config(void) {
    char path[MAX_PATH];
    get_system_config_path(path, sizeof(path));
    FILE* fp = fopen(path, "rb");
    if (!fp) return;
    RifeConfigStorage store;
    if (fread(&store, sizeof(RifeConfigStorage), 1, fp) == 1 &&
        store.magic == RIFE_CONFIG_MAGIC && store.version == RIFE_CONFIG_VERSION) {
        g_system_config = store.config;
    }
    fclose(fp);
}
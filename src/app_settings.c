#define _CRT_SECURE_NO_WARNINGS
#include "rife_app_api.h"
#include "app_manifest.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

typedef struct {
    int current_tab;
} SettingsState;

static void* settings_create(RifeCore* core) {
    (void)core;
    SettingsState* state = (SettingsState*)malloc(sizeof(SettingsState));
    if (state) {
        state->current_tab = 0;
    }
    return state;
}

static void settings_destroy(void* inst) {
    if (inst) free(inst);
}

static void settings_update(void* inst, RifeCore* core, const RifeInput* input, float client_w, float client_h) {
    (void)client_w; (void)client_h;
    SettingsState* state = (SettingsState*)inst;
    if (!state || !input->mouse_pressed[0]) return;

    RifeSystemConfig* cfg = rife_get_system_config();
    float mx = input->mouse_x;
    float my = input->mouse_y;

    float side_w = 140.0f;
    for (int i = 0; i < 5; i++) {
        float tab_y = 10.0f + (float)i * 38.0f;
        if (mx >= 8.0f && mx <= side_w && my >= tab_y && my <= tab_y + 32.0f) {
            state->current_tab = i;
            rife_request_redraw(core);
            return;
        }
    }

    float rx = side_w + 20.0f;
    float ry = 10.0f;

    if (state->current_tab == 0) {
        for (int l = 0; l < 2; l++) {
            float px = rx + (float)l * 150.0f;
            if (mx >= px && mx <= px + 140.0f && my >= ry + 20.0f && my <= ry + 50.0f) {
                cfg->language = (RifeLanguage)l;
                rife_request_redraw(core);
                return;
            }
        }
        for (int sc = 0; sc < 3; sc++) {
            float px = rx + (float)sc * 100.0f;
            if (mx >= px && mx <= px + 92.0f && my >= ry + 76.0f && my <= ry + 106.0f) {
                cfg->font_scale = (FontScaleType)sc;
                rife_request_redraw(core);
                return;
            }
        }
        for (int m = 0; m < 2; m++) {
            float px = rx + (float)m * 150.0f;
            if (mx >= px && mx <= px + 140.0f && my >= ry + 134.0f && my <= ry + 164.0f) {
                cfg->desktop_mode = (DesktopModeType)m;
                rife_request_redraw(core);
                return;
            }
        }
        if (mx >= rx + 260.0f && mx <= rx + 330.0f && my >= ry + 172.0f && my <= ry + 200.0f) {
            cfg->dock_always_visible = !cfg->dock_always_visible;
            rife_request_redraw(core);
            return;
        }
        for (int a = 0; a < 2; a++) {
            float px = rx + (float)a * 150.0f;
            if (mx >= px && mx <= px + 140.0f && my >= ry + 224.0f && my <= ry + 254.0f) {
                cfg->dock_align = (DockAlignType)a;
                rife_request_redraw(core);
                return;
            }
        }
    }
    else if (state->current_tab == 1) {
        // 色彩预设
        for (int p = 0; p < 4; p++) {
            float px = rx + (float)p * 94.0f;
            if (mx >= px && mx <= px + 88.0f && my >= ry + 22.0f && my <= ry + 50.0f) {
                cfg->palette = (AuraPaletteType)p;
                rife_request_redraw(core);
                return;
            }
        }
        // 呼吸速率选择
        for (int bs = 0; bs < 3; bs++) {
            float px = rx + (float)bs * 105.0f;
            if (mx >= px && mx <= px + 98.0f && my >= ry + 74.0f && my <= ry + 102.0f) {
                cfg->breath_speed = (BreathSpeedType)bs;
                rife_request_redraw(core);
                return;
            }
        }
        // 呼吸灯颜色选择
        for (int bc = 0; bc < 5; bc++) {
            float px = rx + (float)bc * 72.0f;
            if (mx >= px && mx <= px + 66.0f && my >= ry + 128.0f && my <= ry + 156.0f) {
                cfg->breath_color = (BreathColorType)bc;
                rife_request_redraw(core);
                return;
            }
        }
        // 流体云材质
        for (int c = 0; c < 4; c++) {
            float px = rx + (float)c * 94.0f;
            if (mx >= px && mx <= px + 88.0f && my >= ry + 182.0f && my <= ry + 210.0f) {
                cfg->cloud_color = (CloudColorType)c;
                rife_request_redraw(core);
                return;
            }
        }
        // 动画开关
        if (mx >= rx + 260.0f && mx <= rx + 330.0f && my >= ry + 218.0f && my <= ry + 246.0f) {
            cfg->aura_animated = !cfg->aura_animated;
            rife_request_redraw(core);
            return;
        }
        // 1px 物理高光边框
        if (mx >= rx + 260.0f && mx <= rx + 330.0f && my >= ry + 252.0f && my <= ry + 280.0f) {
            cfg->specular_rim = !cfg->specular_rim;
            rife_request_redraw(core);
            return;
        }
    }
    else if (state->current_tab == 2) {
        for (int s = 0; s < 2; s++) {
            float px = rx + (float)s * 150.0f;
            if (mx >= px && mx <= px + 140.0f && my >= ry + 28.0f && my <= ry + 64.0f) {
                cfg->icon_style = (IconStyleType)s;
                rife_request_redraw(core);
                return;
            }
        }
        if (mx >= rx + 260.0f && mx <= rx + 330.0f && my >= ry + 82.0f && my <= ry + 114.0f) {
            cfg->shortcut_grid_align = !cfg->shortcut_grid_align;
            rife_request_redraw(core);
            return;
        }
    }
    else if (state->current_tab == 3) {
        for (int f = 0; f < 3; f++) {
            float px = rx + (float)f * 94.0f;
            if (mx >= px && mx <= px + 88.0f && my >= ry + 28.0f && my <= ry + 64.0f) {
                cfg->target_fps_idx = f;
                rife_request_redraw(core);
                return;
            }
        }
        if (mx >= rx + 260.0f && mx <= rx + 330.0f && my >= ry + 82.0f && my <= ry + 114.0f) {
            cfg->background_throttle = !cfg->background_throttle;
            rife_request_redraw(core);
            return;
        }
    }
}

static void settings_render(void* inst, RifeCore* core, float client_x, float client_y, float client_w, float client_h) {
    (void)client_w;
    SettingsState* state = (SettingsState*)inst;
    if (!state) return;

    RifeSystemConfig* cfg = rife_get_system_config();
    bool is_zh = (cfg->language == LANG_ZH_CN);

    float side_w = 140.0f;
    rife_draw_rect(core, client_x + side_w, client_y + 6.0f, 1.0f, client_h - 12.0f, 0xE2E8F0FF);

    const char* tabs_zh[5] = { "通用与语言", "视觉与光学", "应用与磁贴", "内核与遥测", "关于系统" };
    const char* tabs_en[5] = { "General & Lang", "Optics", "Apps & Tiles", "Telemetry", "About RifeOS" };
    for (int i = 0; i < 5; i++) {
        float tab_y = client_y + 10.0f + (float)i * 38.0f;
        bool is_active = (state->current_tab == i);
        if (is_active) {
            rife_draw_round_rect(core, client_x + 14.0f, tab_y, side_w - 24.0f, 30.0f, 8.0f, 0xE0F2FEFF, 0x0EA5E9FF);
            rife_draw_text_font(core, client_x + 22.0f, tab_y + 7.0f, is_zh ? tabs_zh[i] : tabs_en[i], 0x0284C7FF, 0);
        }
        else {
            rife_draw_text_font(core, client_x + 22.0f, tab_y + 7.0f, is_zh ? tabs_zh[i] : tabs_en[i], 0x475569FF, 0);
        }
    }

    float rx = client_x + side_w + 20.0f;
    float ry = client_y + 10.0f;

    if (state->current_tab == 0) {
        rife_draw_text_font(core, rx, ry, is_zh ? "系统语言 / Language" : "Language / 系统语言", 0x0F172AFF, 1);
        const char* langs[2] = { "简体中文 (ZH)", "English (US)" };
        for (int l = 0; l < 2; l++) {
            float px = rx + (float)l * 150.0f;
            bool cur = (cfg->language == l);
            rife_draw_round_rect(core, px, ry + 20.0f, 140.0f, 28.0f, 8.0f, cur ? 0xE0F2FEFF : 0xFFFFFFFF, cur ? 0x0284C7FF : 0xCBD5E1FF);
            rife_draw_text_font(core, px + 16.0f, ry + 26.0f, langs[l], cur ? 0x0284C7FF : 0x475569FF, 4);
        }

        rife_draw_text_font(core, rx, ry + 56.0f, is_zh ? "界面文字缩放 / Text Scaling" : "Text Scaling / 文字缩放", 0x0F172AFF, 1);
        const char* scales_zh[3] = { "100% 标准", "125% 适中", "150% 放大" };
        const char* scales_en[3] = { "100% Small", "125% Medium", "150% Large" };
        for (int sc = 0; sc < 3; sc++) {
            float px = rx + (float)sc * 100.0f;
            bool cur = (cfg->font_scale == sc);
            rife_draw_round_rect(core, px, ry + 76.0f, 92.0f, 28.0f, 8.0f, cur ? 0xE0F2FEFF : 0xFFFFFFFF, cur ? 0x0284C7FF : 0xCBD5E1FF);
            rife_draw_text_font(core, px + 12.0f, ry + 82.0f, is_zh ? scales_zh[sc] : scales_en[sc], cur ? 0x0284C7FF : 0x475569FF, 4);
        }

        rife_draw_text_font(core, rx, ry + 114.0f, is_zh ? "桌面层级宿主模式" : "Desktop Layer Mode", 0x0F172AFF, 1);
        const char* md_zh[2] = { "独立悬浮窗口", "WorkerW 壁纸嵌入" };
        const char* md_en[2] = { "Floating Window", "WorkerW Wallpaper" };
        for (int m = 0; m < 2; m++) {
            float px = rx + (float)m * 150.0f;
            bool cur = (cfg->desktop_mode == m);
            rife_draw_round_rect(core, px, ry + 134.0f, 140.0f, 28.0f, 8.0f, cur ? 0xE0F2FEFF : 0xFFFFFFFF, cur ? 0x0284C7FF : 0xCBD5E1FF);
            rife_draw_text_font(core, px + 12.0f, ry + 140.0f, is_zh ? md_zh[m] : md_en[m], cur ? 0x0284C7FF : 0x475569FF, 4);
        }

        rife_draw_text_font(core, rx, ry + 174.0f, is_zh ? "底栏永久保持可见" : "Dock Always Visible", 0x475569FF, 0);
        float sw_btn_x = rx + 270.0f;
        bool vis = cfg->dock_always_visible;
        rife_draw_round_rect(core, sw_btn_x, ry + 172.0f, 50.0f, 24.0f, 12.0f, vis ? 0x10B981FF : 0xE2E8F0FF, 0xCBD5E1FF);
        rife_draw_round_rect(core, vis ? (sw_btn_x + 28.0f) : (sw_btn_x + 3.0f), ry + 175.0f, 18.0f, 18.0f, 9.0f, 0xFFFFFFFF, 0xFFFFFFFF);

        rife_draw_text_font(core, rx, ry + 204.0f, is_zh ? "底栏停靠位置排版" : "Dock Screen Alignment", 0x0F172AFF, 1);
        const char* aln_zh[2] = { "屏幕居中 (macOS)", "靠右停靠 (ZUI)" };
        const char* aln_en[2] = { "Centered Dock", "Pinned Right (ZUI)" };
        for (int a = 0; a < 2; a++) {
            float px = rx + (float)a * 150.0f;
            bool cur = (cfg->dock_align == a);
            rife_draw_round_rect(core, px, ry + 224.0f, 140.0f, 28.0f, 8.0f, cur ? 0xE0F2FEFF : 0xFFFFFFFF, cur ? 0x0284C7FF : 0xCBD5E1FF);
            rife_draw_text_font(core, px + 12.0f, ry + 230.0f, is_zh ? aln_zh[a] : aln_en[a], cur ? 0x0284C7FF : 0x475569FF, 4);
        }
    }
    else if (state->current_tab == 1) {
        // 色彩预设
        rife_draw_text_font(core, rx, ry, is_zh ? "光场流体色彩预设" : "Aura Flow Palette", 0x0F172AFF, 1);
        const char* pals[4] = { "Gemini", "Obsidian", "Sunset", "Cyber" };
        for (int p = 0; p < 4; p++) {
            float px = rx + (float)p * 94.0f;
            bool cur = (cfg->palette == p);
            rife_draw_round_rect(core, px, ry + 22.0f, 88.0f, 26.0f, 8.0f, cur ? 0xE0F2FEFF : 0xFFFFFFFF, cur ? 0x0284C7FF : 0xCBD5E1FF);
            rife_draw_text_font(core, px + 14.0f, ry + 27.0f, pals[p], cur ? 0x0284C7FF : 0x475569FF, 4);
        }

        // 流体云呼吸时间/速率选项
        rife_draw_text_font(core, rx, ry + 54.0f, is_zh ? "流体云呼吸节律 (周期时长)" : "Breathing Period Interval", 0x0F172AFF, 1);
        const char* bs_zh[3] = { "6.0s 极缓 (推荐)", "4.0s 舒缓", "2.5s 标准" };
        const char* bs_en[3] = { "6.0s Ultra Calm", "4.0s Gentle", "2.5s Normal" };
        for (int bs = 0; bs < 3; bs++) {
            float px = rx + (float)bs * 105.0f;
            bool cur = (cfg->breath_speed == bs);
            rife_draw_round_rect(core, px, ry + 74.0f, 98.0f, 26.0f, 8.0f, cur ? 0xE0F2FEFF : 0xFFFFFFFF, cur ? 0x0284C7FF : 0xCBD5E1FF);
            rife_draw_text_font(core, px + 8.0f, ry + 79.0f, is_zh ? bs_zh[bs] : bs_en[bs], cur ? 0x0284C7FF : 0x475569FF, 4);
        }

        // 流体云呼吸灯色彩选项
        rife_draw_text_font(core, rx, ry + 108.0f, is_zh ? "流体云呼吸灯色彩" : "Breathing Light Color", 0x0F172AFF, 1);
        const char* bc_zh[5] = { "翡翠绿", "极光青", "冰川蓝", "琥珀金", "暮色紫" };
        const char* bc_en[5] = { "Emerald", "Cyan", "Azure", "Amber", "Violet" };
        uint32_t bc_colors[5] = { 0x22C55EFF, 0x06B6D4FF, 0x3B82F6FF, 0xF59E0BFF, 0xA855F7FF };
        for (int bc = 0; bc < 5; bc++) {
            float px = rx + (float)bc * 72.0f;
            bool cur = (cfg->breath_color == bc);
            rife_draw_round_rect(core, px, ry + 128.0f, 66.0f, 26.0f, 8.0f, cur ? 0xE0F2FEFF : 0xFFFFFFFF, cur ? bc_colors[bc] : 0xCBD5E1FF);
            rife_draw_round_rect(core, px + 8.0f, ry + 135.0f, 12.0f, 12.0f, 6.0f, bc_colors[bc], bc_colors[bc]);
            rife_draw_text_font(core, px + 24.0f, ry + 133.0f, is_zh ? bc_zh[bc] : bc_en[bc], cur ? 0x0284C7FF : 0x475569FF, 4);
        }

        // 流体云展开材质
        rife_draw_text_font(core, rx, ry + 162.0f, is_zh ? "流体云展开材质配色" : "Expanded Cloud Tint", 0x475569FF, 0);
        const char* cloud_zh[4] = { "通透晶白", "极光幽蓝", "暮色雾紫", "曜石暗影" };
        const char* cloud_en[4] = { "Crystal", "Azure", "Violet", "Obsidian" };
        for (int c = 0; c < 4; c++) {
            float px = rx + (float)c * 94.0f;
            bool cur = (cfg->cloud_color == c);
            rife_draw_round_rect(core, px, ry + 182.0f, 88.0f, 26.0f, 8.0f, cur ? 0xE0F2FEFF : 0xFFFFFFFF, cur ? 0x0284C7FF : 0xCBD5E1FF);
            rife_draw_text_font(core, px + 12.0f, ry + 187.0f, is_zh ? cloud_zh[c] : cloud_en[c], cur ? 0x0284C7FF : 0x475569FF, 4);
        }

        // 光场动画开关与高光边框
        rife_draw_text_font(core, rx, ry + 222.0f, is_zh ? "五色流动流体背景动画" : "Motion Flow Aura", 0x475569FF, 0);
        float sw_btn_x = rx + 270.0f;
        bool sw_on = cfg->aura_animated;
        rife_draw_round_rect(core, sw_btn_x, ry + 220.0f, 50.0f, 22.0f, 11.0f, sw_on ? 0x10B981FF : 0xE2E8F0FF, 0xCBD5E1FF);
        rife_draw_round_rect(core, sw_on ? (sw_btn_x + 30.0f) : (sw_btn_x + 2.0f), ry + 222.0f, 18.0f, 18.0f, 9.0f, 0xFFFFFFFF, 0xFFFFFFFF);

        rife_draw_text_font(core, rx, ry + 254.0f, is_zh ? "1px 物理高光反射边缘" : "Specular Reflection Rim", 0x475569FF, 0);
        bool rim_on = cfg->specular_rim;
        rife_draw_round_rect(core, sw_btn_x, ry + 252.0f, 50.0f, 22.0f, 11.0f, rim_on ? 0x10B981FF : 0xE2E8F0FF, 0xCBD5E1FF);
        rife_draw_round_rect(core, rim_on ? (sw_btn_x + 30.0f) : (sw_btn_x + 2.0f), ry + 254.0f, 18.0f, 18.0f, 9.0f, 0xFFFFFFFF, 0xFFFFFFFF);
    }
    else if (state->current_tab == 2) {
        rife_draw_text_font(core, rx, ry, is_zh ? "图标渲染引擎管道" : "Icon Rendering Pipeline", 0x0F172AFF, 1);
        const char* is_zh_arr[2] = { "自研微拟物矢量", "Windows 原生 ICO" };
        const char* is_en_arr[2] = { "Procedural Vector", "Native Shell (.ico)" };
        for (int s = 0; s < 2; s++) {
            float px = rx + (float)s * 150.0f;
            bool cur = (cfg->icon_style == s);
            rife_draw_round_rect(core, px, ry + 32.0f, 140.0f, 30.0f, 8.0f, cur ? 0xE0F2FEFF : 0xFFFFFFFF, cur ? 0x0284C7FF : 0xCBD5E1FF);
            rife_draw_text_font(core, px + 10.0f, ry + 40.0f, is_zh ? is_zh_arr[s] : is_en_arr[s], cur ? 0x0284C7FF : 0x475569FF, 4);
        }

        rife_draw_text_font(core, rx, ry + 88.0f, is_zh ? "桌面磁贴自动吸附网格 (6xN)" : "Snap Shortcuts to Grid (6xN)", 0x475569FF, 0);
        float sw_btn_x = rx + 270.0f;
        bool gr_on = cfg->shortcut_grid_align;
        rife_draw_round_rect(core, sw_btn_x, ry + 86.0f, 50.0f, 24.0f, 12.0f, gr_on ? 0x10B981FF : 0xE2E8F0FF, 0xCBD5E1FF);
        rife_draw_round_rect(core, gr_on ? (sw_btn_x + 28.0f) : (sw_btn_x + 3.0f), ry + 89.0f, 18.0f, 18.0f, 9.0f, 0xFFFFFFFF, 0xFFFFFFFF);
    }
    else if (state->current_tab == 3) {
        rife_draw_text_font(core, rx, ry, is_zh ? "硬件物理刷新率目标" : "Display Refresh Target", 0x0F172AFF, 1);
        const char* fps_opts_zh[3] = { "60 Hz 节能", "120 Hz 流畅", "144 Hz 电竞" };
        const char* fps_opts_en[3] = { "60 Hz Eco", "120 Hz Pro", "144 Hz Max" };
        for (int f = 0; f < 3; f++) {
            float px = rx + (float)f * 94.0f;
            bool cur = (cfg->target_fps_idx == f);
            rife_draw_round_rect(core, px, ry + 32.0f, 88.0f, 30.0f, 8.0f, cur ? 0xE0F2FEFF : 0xFFFFFFFF, cur ? 0x0284C7FF : 0xCBD5E1FF);
            rife_draw_text_font(core, px + 10.0f, ry + 40.0f, is_zh ? fps_opts_zh[f] : fps_opts_en[f], cur ? 0x0284C7FF : 0x475569FF, 4);
        }

        rife_draw_text_font(core, rx, ry + 88.0f, is_zh ? "后台失焦动态休眠降频" : "Background Dynamic Throttle", 0x475569FF, 0);
        float sw_btn_x = rx + 270.0f;
        bool th_on = cfg->background_throttle;
        rife_draw_round_rect(core, sw_btn_x, ry + 86.0f, 50.0f, 24.0f, 12.0f, th_on ? 0x10B981FF : 0xE2E8F0FF, 0xCBD5E1FF);
        rife_draw_round_rect(core, th_on ? (sw_btn_x + 28.0f) : (sw_btn_x + 3.0f), ry + 89.0f, 18.0f, 18.0f, 9.0f, 0xFFFFFFFF, 0xFFFFFFFF);

        char mem_buf[64];
        size_t used = core->persistent_arena.offset + core->frame_arena.offset;
        snprintf(mem_buf, sizeof(mem_buf), is_zh ? "Arena 内存分配: %zu KB (常驻 ~5MB)" : "Arena Commit: %zu KB (~5MB Working Set)", used / 1024);
        rife_draw_text_font(core, rx, ry + 130.0f, mem_buf, 0x64748BFF, 0);
        rife_draw_text_font(core, rx, ry + 156.0f, is_zh ? "微内核: 通用虚拟表插件总线架构" : "Kernel: Virtual Table Bus Architecture", 0x64748BFF, 0);
        rife_draw_text_font(core, rx, ry + 182.0f, is_zh ? "运行状态: 零泄漏 / 动态析构生效中" : "Status: Zero Churn / Dynamic Teardown Active", 0x10B981FF, 0);
    }
    else if (state->current_tab == 4) {
        rife_draw_round_rect(core, rx, ry, 360.0f, 170.0f, 16.0f, 0xFFFFFFFF, 0xE2E8F0FF);
        rife_draw_text_font(core, rx + 24.0f, ry + 22.0f, "RifeOS Workspace Host", 0x0F172AFF, 1);
        rife_draw_text_font(core, rx + 24.0f, ry + 52.0f, "Made by Renly", 0x0284C7FF, 1);

        rife_draw_text_font(core, rx + 24.0f, ry + 86.0f, is_zh ? "版本: v1.0.0 Micro Edition (x86_64)" : "Version: v1.0.0 Micro Edition (x86_64)", 0x64748BFF, 0);
        rife_draw_text_font(core, rx + 24.0f, ry + 110.0f, is_zh ? "架构: 纯 C 双 Arena 微内核 + 液态玻璃 SDF" : "Arch: Pure C Dual Arena Microkernel + Liquid SDF", 0x64748BFF, 0);
        rife_draw_text_font(core, rx + 24.0f, ry + 134.0f, is_zh ? "特性: 极缓呼吸微球 + 一体化折叠底座" : "Feat: Calm Breathing LED + Morphing Dock Shelf", 0x10B981FF, 0);
    }
}

const RifePluginApp g_settings_plugin_app = {
    .app_id = 2001,
    .id = "settings",
    .name_zh = "系统设置",
    .name_en = "Settings",
    .glyph = "*",
    .color_top = 0x475569FF,
    .color_bot = 0x334155FF,
    .default_w = 580.0f,
    .default_h = 390.0f,
    .pin_to_dock = true,
    .create = settings_create,
    .destroy = settings_destroy,
    .update = settings_update,
    .render = settings_render
};
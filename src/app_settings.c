#define _CRT_SECURE_NO_WARNINGS
#include "rife_app_api.h"
#include "app_manifest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

// 绘制卡片容器
static inline void draw_settings_card(RifeCore* core, float x, float y, float w, float h, bool is_dark) {
    if (is_dark) {
        rife_draw_round_rect(core, x, y, w, h, 10.0f, 0x201832F0, 0x382B54AA);
        rife_draw_round_rect(core, x + 3.0f, y + 1.0f, w - 6.0f, 1.0f, 1.0f, 0xFFFFFF15, 0x00000000);
    } else {
        rife_draw_round_rect(core, x, y, w, h, 10.0f, 0xFFFFFFF0, 0xE2E8F0AA);
        rife_draw_round_rect(core, x + 3.0f, y + 1.0f, w - 6.0f, 1.0f, 1.0f, 0xFFFFFF55, 0x00000000);
    }
}

// 绘制 iOS 风格滑动开关
static inline void draw_toggle_switch(RifeCore* core, float rx, float row_y, float row_h, bool on, bool is_dark) {
    float sw_w = 46.0f;
    float sw_h = 24.0f;
    float sw_x = rx - sw_w;
    float sw_y = row_y + (row_h - sw_h) * 0.5f;
    if (on) {
        rife_draw_round_rect(core, sw_x, sw_y, sw_w, sw_h, 12.0f, 0x10B981FF, 0x059669FF);
        rife_draw_round_rect(core, sw_x + 24.0f, sw_y + 2.0f, 20.0f, 20.0f, 10.0f, 0xFFFFFFFF, 0xE2E8F0FF);
    }
    else {
        uint32_t track_col = is_dark ? 0x2A203EFF : 0xE2E8F0FF;
        uint32_t border_col = is_dark ? 0x3F315CFF : 0xCBD5E1FF;
        uint32_t knob_col = is_dark ? 0x94A3B8FF : 0xFFFFFFFF;
        uint32_t knob_border = is_dark ? 0x64748BFF : 0xE2E8F0FF;
        rife_draw_round_rect(core, sw_x, sw_y, sw_w, sw_h, 12.0f, track_col, border_col);
        rife_draw_round_rect(core, sw_x + 2.0f, sw_y + 2.0f, 20.0f, 20.0f, 10.0f, knob_col, knob_border);
    }
}

// 绘制分段胶囊选择器
static inline void draw_segmented_pills(RifeCore* core, float rx, float row_y, float row_h, float it_w, const char* items[], int count, int cur_idx, bool is_dark) {
    float seg_h = 28.0f;
    float seg_w = (float)count * it_w + 4.0f;
    float seg_x = rx - seg_w;
    float seg_y = row_y + (row_h - seg_h) * 0.5f;

    uint32_t track_col = is_dark ? 0x161022FF : 0xF1F5F9FF;
    uint32_t track_border = is_dark ? 0x2E2447FF : 0xE2E8F0FF;
    rife_draw_round_rect(core, seg_x, seg_y, seg_w, seg_h, 6.0f, track_col, track_border);

    for (int k = 0; k < count; k++) {
        float ix = seg_x + 2.0f + (float)k * it_w;
        float iy = seg_y + 2.0f;
        float iw = it_w;
        float ih = seg_h - 4.0f;
        bool active = (k == cur_idx);
        if (active) {
            uint32_t act_col = is_dark ? 0x3B2E58FF : 0xFFFFFFFF;
            uint32_t act_border = is_dark ? 0x634E8CFF : 0xCBD5E1FF;
            uint32_t text_col = is_dark ? 0xF8FAFCFF : 0x0284C7FF;
            rife_draw_round_rect(core, ix, iy, iw, ih, 5.0f, act_col, act_border);
            rife_draw_text_font(core, ix + 10.0f, iy + 4.0f, items[k], text_col, 5);
        }
        else {
            uint32_t text_col = is_dark ? 0x94A3B8FF : 0x64748BFF;
            rife_draw_text_font(core, ix + 10.0f, iy + 4.0f, items[k], text_col, 0);
        }
    }
}

// 绘制行文字信息
static inline void draw_row_header(RifeCore* core, float card_x, float row_y, const char* title, const char* subtitle, bool is_dark) {
    uint32_t t_col = is_dark ? 0xF8FAFCFF : 0x0F172AFF;
    uint32_t s_col = 0x94A3B8FF;
    rife_draw_text_font(core, card_x + 16.0f, row_y + 8.0f, title, t_col, 0);
    rife_draw_text_font(core, card_x + 16.0f, row_y + 27.0f, subtitle, s_col, 3);
}

static void settings_update_internal(void* inst, RifeCore* core, const RifeInput* input, float client_w, float client_h) {
    (void)client_w; (void)client_h;
    SettingsState* state = (SettingsState*)inst;
    if (!state || !input->mouse_pressed[0]) return;

    RifeSystemConfig* cfg = rife_get_system_config();
    float mx = input->mouse_x;
    float my = input->mouse_y;

    float side_w = 175.0f;

    // 侧边栏 Tab 点击测试 (5项)
    for (int i = 0; i < 5; i++) {
        float tab_x = 10.0f;
        float tab_y = 54.0f + (float)i * 42.0f;
        float tab_w = side_w - 20.0f;
        float tab_h = 36.0f;
        if (mx >= tab_x && mx <= tab_x + tab_w && my >= tab_y && my <= tab_y + tab_h) {
            state->current_tab = i;
            rife_request_redraw(core);
            return;
        }
    }

    float rx = side_w + 22.0f;
    float rw = client_w - side_w - 44.0f;
    float card_right = rx + rw - 16.0f;
    float cy = 14.0f + 44.0f;

    if (state->current_tab == 0) {
        // Tab 0: 通用与桌面
        // Card 1
        float c1_y = cy;
        // Row 0: 语言 (2项, it_w = 76.0f)
        float r0_y = c1_y;
        float seg_w0 = 2.0f * 76.0f + 4.0f;
        float seg_x0 = card_right - seg_w0;
        float seg_y0 = r0_y + (50.0f - 28.0f) * 0.5f;
        for (int k = 0; k < 2; k++) {
            float ix = seg_x0 + 2.0f + (float)k * 76.0f;
            if (mx >= ix && mx <= ix + 76.0f && my >= seg_y0 && my <= seg_y0 + 24.0f) {
                cfg->language = (RifeLanguage)k;
                rife_request_redraw(core);
                return;
            }
        }
        // Row 1: 字号缩放 (3项, it_w = 54.0f)
        float r1_y = c1_y + 50.0f;
        float seg_w1 = 3.0f * 54.0f + 4.0f;
        float seg_x1 = card_right - seg_w1;
        float seg_y1 = r1_y + (50.0f - 28.0f) * 0.5f;
        for (int k = 0; k < 3; k++) {
            float ix = seg_x1 + 2.0f + (float)k * 54.0f;
            if (mx >= ix && mx <= ix + 54.0f && my >= seg_y1 && my <= seg_y1 + 24.0f) {
                cfg->font_scale = (FontScaleType)k;
                rife_request_redraw(core);
                return;
            }
        }

        // Card 2
        float c2_y = c1_y + 100.0f + 12.0f;
        // Row 0: 宿主层级 (2项, it_w = 76.0f)
        float r2_0_y = c2_y;
        float seg_w2_0 = 2.0f * 76.0f + 4.0f;
        float seg_x2_0 = card_right - seg_w2_0;
        float seg_y2_0 = r2_0_y + (50.0f - 28.0f) * 0.5f;
        for (int k = 0; k < 2; k++) {
            float ix = seg_x2_0 + 2.0f + (float)k * 76.0f;
            if (mx >= ix && mx <= ix + 76.0f && my >= seg_y2_0 && my <= seg_y2_0 + 24.0f) {
                cfg->desktop_mode = (DesktopModeType)k;
                rife_request_redraw(core);
                return;
            }
        }
        // Row 1: 底栏常驻 (Toggle)
        float r2_1_y = c2_y + 50.0f;
        float sw_x2_1 = card_right - 46.0f;
        float sw_y2_1 = r2_1_y + (50.0f - 24.0f) * 0.5f;
        if (mx >= sw_x2_1 && mx <= sw_x2_1 + 46.0f && my >= sw_y2_1 && my <= sw_y2_1 + 24.0f) {
            cfg->dock_always_visible = !cfg->dock_always_visible;
            rife_request_redraw(core);
            return;
        }
        // Row 2: 底栏停靠 (2项, it_w = 76.0f)
        float r2_2_y = c2_y + 100.0f;
        float seg_w2_2 = 2.0f * 76.0f + 4.0f;
        float seg_x2_2 = card_right - seg_w2_2;
        float seg_y2_2 = r2_2_y + (50.0f - 28.0f) * 0.5f;
        for (int k = 0; k < 2; k++) {
            float ix = seg_x2_2 + 2.0f + (float)k * 76.0f;
            if (mx >= ix && mx <= ix + 76.0f && my >= seg_y2_2 && my <= seg_y2_2 + 24.0f) {
                cfg->dock_align = (DockAlignType)k;
                rife_request_redraw(core);
                return;
            }
        }
    }
    else if (state->current_tab == 1) {
        // Tab 1: 视觉与光场
        // Card 1
        float c1_y = cy;
        // Row 0: 色彩预设 (4个色块芯片，每个宽 58px)
        float r0_y = c1_y;
        float chips_w = 4.0f * 60.0f;
        float chips_x = card_right - chips_w;
        float chips_y = r0_y + (52.0f - 28.0f) * 0.5f;
        for (int p = 0; p < 4; p++) {
            float px = chips_x + (float)p * 60.0f;
            if (mx >= px && mx <= px + 56.0f && my >= chips_y && my <= chips_y + 28.0f) {
                cfg->palette = (AuraPaletteType)p;
                // 光场流体色彩设置改变时，流体云跟随变色
                if (p == PALETTE_GEMINI) {
                    cfg->breath_color = BREATH_COLOR_CYAN;
                    cfg->cloud_color = CLOUD_COLOR_TRANSLUCENT;
                }
                else if (p == PALETTE_OBSIDIAN) {
                    cfg->breath_color = BREATH_COLOR_VIOLET;
                    cfg->cloud_color = CLOUD_COLOR_OBSIDIAN;
                }
                else if (p == PALETTE_SUNSET) {
                    cfg->breath_color = BREATH_COLOR_AMBER;
                    cfg->cloud_color = CLOUD_COLOR_VIOLET;
                }
                else if (p == PALETTE_CYBER) {
                    cfg->breath_color = BREATH_COLOR_AZURE;
                    cfg->cloud_color = CLOUD_COLOR_AZURE;
                }
                rife_request_redraw(core);
                return;
            }
        }
        // Row 1: 流动动画开关
        float r1_y = c1_y + 52.0f;
        float sw_x1 = card_right - 46.0f;
        float sw_y1 = r1_y + (52.0f - 24.0f) * 0.5f;
        if (mx >= sw_x1 && mx <= sw_x1 + 46.0f && my >= sw_y1 && my <= sw_y1 + 24.0f) {
            cfg->aura_animated = !cfg->aura_animated;
            rife_request_redraw(core);
            return;
        }

        // Card 2
        float c2_y = c1_y + 104.0f + 12.0f;
        // Row 0: 呼吸节律 (3项, it_w = 64.0f)
        float r2_0_y = c2_y;
        float seg_w2 = 3.0f * 64.0f + 4.0f;
        float seg_x2 = card_right - seg_w2;
        float seg_y2 = r2_0_y + (52.0f - 28.0f) * 0.5f;
        for (int bs = 0; bs < 3; bs++) {
            float ix = seg_x2 + 2.0f + (float)bs * 64.0f;
            if (mx >= ix && mx <= ix + 64.0f && my >= seg_y2 && my <= seg_y2 + 24.0f) {
                cfg->breath_speed = (BreathSpeedType)bs;
                rife_request_redraw(core);
                return;
            }
        }
        // Row 1: 呼吸灯色彩 (5个宝石圆点, 每个间隔 32px)
        float r2_1_y = c2_y + 52.0f;
        float dots_w = 5.0f * 32.0f;
        float dots_x = card_right - dots_w;
        float dots_y = r2_1_y + (52.0f - 24.0f) * 0.5f;
        for (int bc = 0; bc < 5; bc++) {
            float bx = dots_x + (float)bc * 32.0f;
            if (mx >= bx && mx <= bx + 28.0f && my >= dots_y && my <= dots_y + 24.0f) {
                cfg->breath_color = (BreathColorType)bc;
                rife_request_redraw(core);
                return;
            }
        }

        // Card 3
        float c3_y = c2_y + 104.0f + 12.0f;
        // Row 0: 展开材质 (4项, it_w = 62.0f)
        float r3_0_y = c3_y;
        float seg_w3 = 4.0f * 62.0f + 4.0f;
        float seg_x3 = card_right - seg_w3;
        float seg_y3 = r3_0_y + (52.0f - 28.0f) * 0.5f;
        for (int c = 0; c < 4; c++) {
            float ix = seg_x3 + 2.0f + (float)c * 62.0f;
            if (mx >= ix && mx <= ix + 62.0f && my >= seg_y3 && my <= seg_y3 + 24.0f) {
                cfg->cloud_color = (CloudColorType)c;
                rife_request_redraw(core);
                return;
            }
        }
        // Row 1: 物理高光反射边框 (Toggle)
        float r3_1_y = c3_y + 52.0f;
        float sw_x3 = card_right - 46.0f;
        float sw_y3 = r3_1_y + (52.0f - 24.0f) * 0.5f;
        if (mx >= sw_x3 && mx <= sw_x3 + 46.0f && my >= sw_y3 && my <= sw_y3 + 24.0f) {
            cfg->specular_rim = !cfg->specular_rim;
            rife_request_redraw(core);
            return;
        }
    }
    else if (state->current_tab == 2) {
        // Tab 2: 磁贴与渲染
        // Card 1: 图标管道 (2项, it_w = 88.0f)
        float c1_y = cy;
        float seg_w1 = 2.0f * 88.0f + 4.0f;
        float seg_x1 = card_right - seg_w1;
        float seg_y1 = c1_y + (56.0f - 28.0f) * 0.5f;
        for (int s = 0; s < 2; s++) {
            float ix = seg_x1 + 2.0f + (float)s * 88.0f;
            if (mx >= ix && mx <= ix + 88.0f && my >= seg_y1 && my <= seg_y1 + 24.0f) {
                cfg->icon_style = (IconStyleType)s;
                rife_request_redraw(core);
                return;
            }
        }
        // Card 2: 磁贴网格吸附 (Toggle)
        float c2_y = c1_y + 56.0f + 12.0f;
        float sw_x2 = card_right - 46.0f;
        float sw_y2 = c2_y + (56.0f - 24.0f) * 0.5f;
        if (mx >= sw_x2 && mx <= sw_x2 + 46.0f && my >= sw_y2 && my <= sw_y2 + 24.0f) {
            cfg->shortcut_grid_align = !cfg->shortcut_grid_align;
            rife_request_redraw(core);
            return;
        }
    }
    else if (state->current_tab == 3) {
        // Tab 3: 内核与性能
        // Card 1
        float c1_y = cy;
        // Row 0: 刷新率目标 (3项, it_w = 72.0f)
        float r0_y = c1_y;
        float seg_w0 = 3.0f * 72.0f + 4.0f;
        float seg_x0 = card_right - seg_w0;
        float seg_y0 = r0_y + (52.0f - 28.0f) * 0.5f;
        for (int f = 0; f < 3; f++) {
            float ix = seg_x0 + 2.0f + (float)f * 72.0f;
            if (mx >= ix && mx <= ix + 72.0f && my >= seg_y0 && my <= seg_y0 + 24.0f) {
                cfg->target_fps_idx = f;
                rife_request_redraw(core);
                return;
            }
        }
        // Row 1: 后台降频休眠 (Toggle)
        float r1_y = c1_y + 52.0f;
        float sw_x1 = card_right - 46.0f;
        float sw_y1 = r1_y + (52.0f - 24.0f) * 0.5f;
        if (mx >= sw_x1 && mx <= sw_x1 + 46.0f && my >= sw_y1 && my <= sw_y1 + 24.0f) {
            cfg->background_throttle = !cfg->background_throttle;
            rife_request_redraw(core);
            return;
        }
    }
}

static void settings_update(void* inst, RifeCore* core, const RifeInput* input, float client_w, float client_h) {
    RifeSystemConfig* cfg = rife_get_system_config();
    RifeSystemConfig prev_cfg = *cfg;

    settings_update_internal(inst, core, input, client_w, client_h);

    if (memcmp(&prev_cfg, cfg, sizeof(RifeSystemConfig)) != 0) {
        rife_save_system_config();
    }
}

static void settings_render(void* inst, RifeCore* core, float client_x, float client_y, float client_w, float client_h) {
    SettingsState* state = (SettingsState*)inst;
    if (!state) return;

    RifeSystemConfig* cfg = rife_get_system_config();
    bool is_zh = (cfg->language == LANG_ZH_CN);
    bool is_dark = (cfg->palette == PALETTE_OBSIDIAN || cfg->cloud_color == CLOUD_COLOR_OBSIDIAN);
    uint32_t div_col = is_dark ? 0x2B2144FF : 0xF1F5F9FF;

    float side_w = 175.0f;

    // 0. 底板背景与侧边栏底色 (防止穿透)
    rife_draw_rect(core, client_x, client_y, client_w, client_h, is_dark ? 0x161122FF : 0xFFFFFFFF);
    rife_draw_rect(core, client_x, client_y, side_w, client_h, is_dark ? 0x1A1428FF : 0xF8FAFCFF);

    // 1. 侧边栏垂直细分割线
    rife_draw_rect(core, client_x + side_w, client_y, 1.0f, client_h, is_dark ? 0x2D2342FF : 0xE2E8F0FF);

    // 2. 侧边栏顶部品牌与偏好标题
    rife_draw_round_rect(core, client_x + 14.0f, client_y + 14.0f, 26.0f, 26.0f, 6.0f, is_dark ? 0x241D35FF : 0x334155FF, is_dark ? 0x3F3358FF : 0x1E293BFF);
    rife_draw_text_font(core, client_x + 22.0f, client_y + 17.0f, "*", 0xFFFFFFFF, 1);
    rife_draw_text_font(core, client_x + 48.0f, client_y + 14.0f, is_zh ? "系统偏好设置" : "System Settings", is_dark ? 0xF8FAFCFF : 0x0F172AFF, 1);
    rife_draw_text_font(core, client_x + 48.0f, client_y + 29.0f, "RifeOS Preferences", is_dark ? 0xA78BFAFF : 0x94A3B8FF, 4);

    // 3. 侧边栏导航 Tab 列表
    const char* tabs_zh[5] = { "通用与语言", "视觉与光场", "磁贴与渲染", "内核与性能", "关于本系统" };
    const char* tabs_en[5] = { "General & Lang", "Optics & Flow", "Tiles & Render", "Kernel & Perf", "About System" };
    const char* tabs_icon[5] = { ">", "*", "#", "~", "i" };

    for (int i = 0; i < 5; i++) {
        float tab_x = client_x + 10.0f;
        float tab_y = client_y + 54.0f + (float)i * 42.0f;
        float tab_w = side_w - 20.0f;
        float tab_h = 36.0f;
        bool is_active = (state->current_tab == i);

        if (is_active) {
            // 激活胶囊底色与左侧高光条
            uint32_t bg = is_dark ? 0x2D2148FF : 0xEFF6FFFF;
            uint32_t bd = is_dark ? 0x5D458CFF : 0xBAE6FDFF;
            uint32_t bar = is_dark ? 0xA855F7FF : 0x0284C7FF;
            uint32_t txt = is_dark ? 0xF8FAFCFF : 0x0284C7FF;
            rife_draw_round_rect(core, tab_x, tab_y, tab_w, tab_h, 8.0f, bg, bd);
            rife_draw_round_rect(core, tab_x + 3.0f, tab_y + 8.0f, 3.0f, 20.0f, 1.5f, bar, bar);
            rife_draw_text_font(core, tab_x + 14.0f, tab_y + 9.0f, tabs_icon[i], bar, 1);
            rife_draw_text_font(core, tab_x + 30.0f, tab_y + 9.0f, is_zh ? tabs_zh[i] : tabs_en[i], txt, 5);
        }
        else {
            rife_draw_text_font(core, tab_x + 14.0f, tab_y + 9.0f, tabs_icon[i], is_dark ? 0x818CF8FF : 0x64748BFF, 0);
            rife_draw_text_font(core, tab_x + 30.0f, tab_y + 9.0f, is_zh ? tabs_zh[i] : tabs_en[i], is_dark ? 0x94A3B8FF : 0x475569FF, 0);
        }
    }

    // 4. 右侧内容区域
    float rx = client_x + side_w + 22.0f;
    float rw = client_w - side_w - 44.0f;
    float card_right = rx + rw - 16.0f;
    float ry = client_y + 14.0f;

    // 分类 Header (Title + Subtitle)
    const char* cat_titles_zh[5] = { "通用与桌面宿主", "光场光学与流体物理", "图标磁贴与渲染管道", "微内核调度与性能遥测", "关于 RifeOS" };
    const char* cat_titles_en[5] = { "General & Desktop Host", "Optics, Lightfield & Flow", "Icons & Rendering Engine", "Microkernel & Telemetry", "About RifeOS" };
    const char* cat_descs_zh[5] = {
        "管理系统界面语言、字体缩放比例以及底层桌面渲染宿主层级模式",
        "配置连续双线性流体色彩、五色复合简谐波场及顶部灵动微球参数",
        "切换桌面图标渲染引擎管道与磁贴自动网格吸附交互",
        "监控双 Arena 内存提交量，调节主循环硬件物理刷新率",
        "系统发行版本规格与开发者联络信息"
    };
    const char* cat_descs_en[5] = {
        "Configure language, font scale and desktop hosting substrate modes",
        "Customize fluid colorways, multi-harmonic field and bionic breathing LED",
        "Configure procedural vector pipelines and desktop shortcut snapping",
        "Real-time dual-arena heap telemetry and hardware refresh target pacing",
        "Release specifications and developer contact information"
    };

    rife_draw_text_font(core, rx, ry, is_zh ? cat_titles_zh[state->current_tab] : cat_titles_en[state->current_tab], is_dark ? 0xF8FAFCFF : 0x0F172AFF, 1);
    rife_draw_text_font(core, rx, ry + 22.0f, is_zh ? cat_descs_zh[state->current_tab] : cat_descs_en[state->current_tab], is_dark ? 0x94A3B8FF : 0x64748BFF, 3);

    float cy = ry + 44.0f;

    if (state->current_tab == 0) {
        // Tab 0: 通用与桌面
        // Card 1: 语言与排版 (100px)
        float c1_y = cy;
        draw_settings_card(core, rx, c1_y, rw, 100.0f, is_dark);

        // Row 0: 语言
        float r0_y = c1_y;
        draw_row_header(core, rx, r0_y, is_zh ? "系统显示语言" : "Display Language", is_zh ? "控制微内核与所有系统组件的语言环境" : "Select UI language for kernel & apps", is_dark);
        const char* lang_items[2] = { "简体中文", "English" };
        draw_segmented_pills(core, card_right, r0_y, 50.0f, 76.0f, lang_items, 2, (int)cfg->language, is_dark);

        // 分割线
        rife_draw_rect(core, rx + 16.0f, c1_y + 50.0f, rw - 32.0f, 1.0f, div_col);

        // Row 1: 字号缩放
        float r1_y = c1_y + 50.0f;
        draw_row_header(core, rx, r1_y, is_zh ? "界面排版缩放" : "UI Font Scale", is_zh ? "调节窗口标题与控件标签的相对比例" : "Adjust text scale for labels and titles", is_dark);
        const char* scale_items[3] = { "100%", "125%", "150%" };
        draw_segmented_pills(core, card_right, r1_y, 50.0f, 54.0f, scale_items, 3, (int)cfg->font_scale, is_dark);

        // Card 2: 桌面与底栏 (150px)
        float c2_y = c1_y + 100.0f + 12.0f;
        draw_settings_card(core, rx, c2_y, rw, 150.0f, is_dark);

        // Row 0: 宿主层级
        float r2_0_y = c2_y;
        draw_row_header(core, rx, r2_0_y, is_zh ? "桌面层级宿主模式" : "Desktop Host Mode", is_zh ? "切换桌面层级：悬浮窗口或嵌入系统壁纸" : "Floating window or WorkerW desktop embed", is_dark);
        const char* host_items[2] = { "独立悬浮", "壁纸嵌入" };
        draw_segmented_pills(core, card_right, r2_0_y, 50.0f, 76.0f, host_items, 2, (int)cfg->desktop_mode, is_dark);

        rife_draw_rect(core, rx + 16.0f, c2_y + 50.0f, rw - 32.0f, 1.0f, div_col);

        // Row 1: 底栏常驻可见 (Toggle)
        float r2_1_y = c2_y + 50.0f;
        draw_row_header(core, rx, r2_1_y, is_zh ? "底栏永久保持可见" : "Dock Always Visible", is_zh ? "关闭后在光标靠近屏幕底部时自动浮现" : "Auto-hides Dock until mouse hovers bottom edge", is_dark);
        draw_toggle_switch(core, card_right, r2_1_y, 50.0f, cfg->dock_always_visible, is_dark);

        rife_draw_rect(core, rx + 16.0f, c2_y + 100.0f, rw - 32.0f, 1.0f, div_col);

        // Row 2: 底栏停靠位置
        float r2_2_y = c2_y + 100.0f;
        draw_row_header(core, rx, r2_2_y, is_zh ? "底栏停靠位置排版" : "Dock Alignment", is_zh ? "底部 Dock 栏是停靠居中还是贴靠右侧" : "Align Dock to screen center or right corner", is_dark);
        const char* aln_items[2] = { "居中停靠", "右侧停靠" };
        draw_segmented_pills(core, card_right, r2_2_y, 50.0f, 76.0f, aln_items, 2, (int)cfg->dock_align, is_dark);
    }
    else if (state->current_tab == 1) {
        // Tab 1: 视觉与光场
        // Card 1: 色彩与动画 (104px)
        float c1_y = cy;
        draw_settings_card(core, rx, c1_y, rw, 104.0f, is_dark);

        // Row 0: 色彩预设 (Chips)
        float r0_y = c1_y;
        draw_row_header(core, rx, r0_y, is_zh ? "光场流体色彩预设" : "Aura Flow Palette", is_zh ? "五色连续双线性可分离流体色盘" : "Continuous dual-separable chromatic field", is_dark);

        const char* pal_names[4] = { "Gemini", "Obsidian", "Sunset", "Cyber" };
        uint32_t pal_dot_colors[4] = { 0x00D2FFFF, 0x881C87FF, 0xF97316FF, 0x06B6D4FF };
        float chips_w = 4.0f * 60.0f;
        float chips_x = card_right - chips_w;
        float chips_y = r0_y + (52.0f - 28.0f) * 0.5f;

        for (int p = 0; p < 4; p++) {
            float px = chips_x + (float)p * 60.0f;
            bool active = (cfg->palette == p);
            uint32_t bg = active ? (is_dark ? 0x382B55FF : 0xEFF6FFFF) : (is_dark ? 0x161022FF : 0xF8FAFCFF);
            uint32_t bd = active ? (is_dark ? 0xA855F7FF : 0x0284C7FF) : (is_dark ? 0x2E2447FF : 0xE2E8F0FF);
            uint32_t tx = active ? (is_dark ? 0xF8FAFCFF : 0x0284C7FF) : (is_dark ? 0x94A3B8FF : 0x475569FF);
            rife_draw_round_rect(core, px, chips_y, 56.0f, 28.0f, 6.0f, bg, bd);
            // 色彩圆点指示
            rife_draw_round_rect(core, px + 6.0f, chips_y + 9.0f, 10.0f, 10.0f, 5.0f, pal_dot_colors[p], pal_dot_colors[p]);
            rife_draw_text_font(core, px + 18.0f, chips_y + 6.0f, pal_names[p], tx, 4);
        }

        rife_draw_rect(core, rx + 16.0f, c1_y + 52.0f, rw - 32.0f, 1.0f, div_col);

        // Row 1: 流动动画开关 (Toggle)
        float r1_y = c1_y + 52.0f;
        draw_row_header(core, rx, r1_y, is_zh ? "流动流体背景动画" : "Harmonic Motion Flow", is_zh ? "多频复合简谐扰动与光标临场感应" : "Multi-frequency compound wave harmonics", is_dark);
        draw_toggle_switch(core, card_right, r1_y, 52.0f, cfg->aura_animated, is_dark);

        // Card 2: 呼吸节律与呼吸灯 (104px)
        float c2_y = c1_y + 104.0f + 12.0f;
        draw_settings_card(core, rx, c2_y, rw, 104.0f, is_dark);

        // Row 0: 呼吸节律
        float r2_0_y = c2_y;
        draw_row_header(core, rx, r2_0_y, is_zh ? "微球生理呼吸节律" : "Breathing Pulse Cycle", is_zh ? "顶部正圆微球高斯光子核晶呼吸周期" : "Gaussian core breathing cycle interval", is_dark);
        const char* bs_items[3] = { "6.0s 极缓", "4.0s 舒缓", "2.5s 标准" };
        draw_segmented_pills(core, card_right, r2_0_y, 52.0f, 64.0f, bs_items, 3, (int)cfg->breath_speed, is_dark);

        rife_draw_rect(core, rx + 16.0f, c2_y + 52.0f, rw - 32.0f, 1.0f, div_col);

        // Row 1: 呼吸灯核晶宝石色 (5个宝石圆点)
        float r2_1_y = c2_y + 52.0f;
        draw_row_header(core, rx, r2_1_y, is_zh ? "微球呼吸核晶色彩" : "Core LED Jewel Color", is_zh ? "流体云常态微球中心的仿生指示灯" : "Bionic photon LED jewel light at cloud core", is_dark);
        uint32_t bc_colors[5] = { 0x22C55EFF, 0x06B6D4FF, 0x3B82F6FF, 0xF59E0BFF, 0xA855F7FF };
        float dots_w = 5.0f * 32.0f;
        float dots_x = card_right - dots_w;
        float dots_y = r2_1_y + (52.0f - 24.0f) * 0.5f;

        for (int bc = 0; bc < 5; bc++) {
            float bx = dots_x + (float)bc * 32.0f;
            bool active = (cfg->breath_color == bc);
            if (active) {
                uint32_t ring_bg = is_dark ? 0x2E2246FF : 0xFFFFFFFF;
                uint32_t ring_bd = is_dark ? 0xA855F7FF : 0x0284C7FF;
                rife_draw_round_rect(core, bx + 2.0f, dots_y, 24.0f, 24.0f, 12.0f, ring_bg, ring_bd);
                rife_draw_round_rect(core, bx + 6.0f, dots_y + 4.0f, 16.0f, 16.0f, 8.0f, bc_colors[bc], bc_colors[bc]);
            }
            else {
                uint32_t dot_bd = is_dark ? 0x3F315CFF : 0xE2E8F0FF;
                rife_draw_round_rect(core, bx + 6.0f, dots_y + 4.0f, 16.0f, 16.0f, 8.0f, bc_colors[bc], dot_bd);
            }
        }

        // Card 3: 展开材质与高光边框 (104px)
        float c3_y = c2_y + 104.0f + 12.0f;
        draw_settings_card(core, rx, c3_y, rw, 104.0f, is_dark);

        // Row 0: 展开材质
        float r3_0_y = c3_y;
        draw_row_header(core, rx, r3_0_y, is_zh ? "展开流体云材质" : "Expanded Cloud Tint", is_zh ? "点击微球展开为胶囊药丸时的玻璃底色" : "Glass substrate tint when expanded to pill", is_dark);
        const char* cloud_items[4] = { "晶白", "幽蓝", "雾紫", "暗影" };
        draw_segmented_pills(core, card_right, r3_0_y, 52.0f, 62.0f, cloud_items, 4, (int)cfg->cloud_color, is_dark);

        rife_draw_rect(core, rx + 16.0f, c3_y + 52.0f, rw - 32.0f, 1.0f, div_col);

        // Row 1: 物理高光边框 (Toggle)
        float r3_1_y = c3_y + 52.0f;
        draw_row_header(core, rx, r3_1_y, is_zh ? "1px 物理高光反射边缘" : "Specular Reflection Rim", is_zh ? "玻璃外壳顶部的晶莹边缘微反光" : "1px crystal specular rim highlight on glass", is_dark);
        draw_toggle_switch(core, card_right, r3_1_y, 52.0f, cfg->specular_rim, is_dark);
    }
    else if (state->current_tab == 2) {
        // Tab 2: 磁贴与渲染
        // Card 1: 图标渲染引擎 (56px)
        float c1_y = cy;
        draw_settings_card(core, rx, c1_y, rw, 56.0f, is_dark);
        draw_row_header(core, rx, c1_y, is_zh ? "应用图标渲染引擎" : "Icon Rendering Pipeline", is_zh ? "自研几何微拟物矢量 (带高光) 或 Windows 原生 .ico" : "Procedural vector with sheen or native shell icons", is_dark);
        const char* icon_items[2] = { "微拟物矢量", "原生 ICO" };
        draw_segmented_pills(core, card_right, c1_y, 56.0f, 88.0f, icon_items, 2, (int)cfg->icon_style, is_dark);

        // Card 2: 磁贴网格吸附 (56px)
        float c2_y = c1_y + 56.0f + 12.0f;
        draw_settings_card(core, rx, c2_y, rw, 56.0f, is_dark);
        draw_row_header(core, rx, c2_y, is_zh ? "桌面磁贴网格吸附" : "Grid Snap Alignment", is_zh ? "拖动桌面图标与磁贴时自动对齐 6xN 网格" : "Automatically snaps shortcuts to a 6xN grid", is_dark);
        draw_toggle_switch(core, card_right, c2_y, 56.0f, cfg->shortcut_grid_align, is_dark);
    }
    else if (state->current_tab == 3) {
        // Tab 3: 内核与性能
        // Card 1: 刷新率与休眠 (104px)
        float c1_y = cy;
        draw_settings_card(core, rx, c1_y, rw, 104.0f, is_dark);

        // Row 0: 物理刷新率
        float r0_y = c1_y;
        draw_row_header(core, rx, r0_y, is_zh ? "硬件物理刷新率目标" : "Display Refresh Target", is_zh ? "微内核调度器的主循环目标垂直同步帧率" : "Microkernel tick loop target VSync frame rate", is_dark);
        const char* fps_items[3] = { "60 Hz 节能", "120 Hz 流畅", "144 Hz 电竞" };
        draw_segmented_pills(core, card_right, r0_y, 52.0f, 72.0f, fps_items, 3, cfg->target_fps_idx, is_dark);

        rife_draw_rect(core, rx + 16.0f, c1_y + 52.0f, rw - 32.0f, 1.0f, div_col);

        // Row 1: 后台降频 (Toggle)
        float r1_y = c1_y + 52.0f;
        draw_row_header(core, rx, r1_y, is_zh ? "后台失焦动态休眠" : "Smart Idle Gating", is_zh ? "窗口失焦且无动画时主动降频压制功耗" : "Dynamically drops frame rate when inactive to save battery", is_dark);
        draw_toggle_switch(core, card_right, r1_y, 52.0f, cfg->background_throttle, is_dark);

        // Card 2: 微内核遥测看板 (132px)
        float c2_y = c1_y + 104.0f + 12.0f;
        draw_settings_card(core, rx, c2_y, rw, 132.0f, is_dark);

        // Row 0: 双 Arena 内存
        float r2_0_y = c2_y;
        char mem_buf[64];
        size_t used = core->persistent_arena.offset + core->frame_arena.offset;
        snprintf(mem_buf, sizeof(mem_buf), is_zh ? "当前内存提交: %zu KB (常驻 ~5MB)" : "Working Set Commit: %zu KB (~5MB)", used / 1024);
        draw_row_header(core, rx, r2_0_y, is_zh ? "双 Arena 内存提交量" : "Dual Arena Commit", mem_buf, is_dark);
        if (is_dark) {
            rife_draw_round_rect(core, card_right - 100.0f, r2_0_y + 10.0f, 100.0f, 24.0f, 12.0f, 0x133E2BFF, 0x059669FF);
            rife_draw_text_font(core, card_right - 88.0f, r2_0_y + 14.0f, "极度健康 5MB", 0x34D399FF, 4);
        } else {
            rife_draw_round_rect(core, card_right - 100.0f, r2_0_y + 10.0f, 100.0f, 24.0f, 12.0f, 0xDCFCE7FF, 0x86EFACFF);
            rife_draw_text_font(core, card_right - 88.0f, r2_0_y + 14.0f, "极度健康 5MB", 0x16A34AFF, 4);
        }

        rife_draw_rect(core, rx + 16.0f, c2_y + 44.0f, rw - 32.0f, 1.0f, div_col);

        // Row 1: 插件总线
        float r2_1_y = c2_y + 44.0f;
        draw_row_header(core, rx, r2_1_y, is_zh ? "插件总线分发协议" : "Plugin Bus Protocol", is_zh ? "纯 C 虚表总线 / 零侵入解耦应用挂载" : "Pure C vtable bus / zero-touch manifest architecture", is_dark);
        if (is_dark) {
            rife_draw_round_rect(core, card_right - 100.0f, r2_1_y + 10.0f, 100.0f, 24.0f, 12.0f, 0x2A1F45FF, 0x7C3AEDFF);
            rife_draw_text_font(core, card_right - 92.0f, r2_1_y + 14.0f, "VTable Active", 0xC084FCFF, 4);
        } else {
            rife_draw_round_rect(core, card_right - 100.0f, r2_1_y + 10.0f, 100.0f, 24.0f, 12.0f, 0xE0F2FEFF, 0xBAE6FDFF);
            rife_draw_text_font(core, card_right - 92.0f, r2_1_y + 14.0f, "VTable Active", 0x0284C7FF, 4);
        }

        rife_draw_rect(core, rx + 16.0f, c2_y + 88.0f, rw - 32.0f, 1.0f, div_col);

        // Row 2: 零堆开销
        float r2_2_y = c2_y + 88.0f;
        draw_row_header(core, rx, r2_2_y, is_zh ? "堆内存抖动监控" : "Zero Heap Churn", is_zh ? "全帧循环零 malloc/free / 彻底杜绝内存碎片" : "Strictly 0 frame allocs / prevents fragmentation", is_dark);
        if (is_dark) {
            rife_draw_round_rect(core, card_right - 100.0f, r2_2_y + 10.0f, 100.0f, 24.0f, 12.0f, 0x133E2BFF, 0x059669FF);
            rife_draw_text_font(core, card_right - 92.0f, r2_2_y + 14.0f, "0 Churn / 0 Frag", 0x34D399FF, 4);
        } else {
            rife_draw_round_rect(core, card_right - 100.0f, r2_2_y + 10.0f, 100.0f, 24.0f, 12.0f, 0xECFDF5FF, 0x6EE7B7FF);
            rife_draw_text_font(core, card_right - 92.0f, r2_2_y + 14.0f, "0 Churn / 0 Frag", 0x059669FF, 4);
        }
    }
    else if (state->current_tab == 4) {
        // Tab 4: 关于系统 (商业发行版风格)
        float c1_y = cy;
        draw_settings_card(core, rx, c1_y, rw, 100.0f, is_dark);

        // 1. 软件标识与版本 Hero 卡片
        // 拟物图标：深色暗晶圆角徽标 + 白色 R 雕刻
        rife_draw_round_rect(core, rx + 24.0f, c1_y + 22.0f, 56.0f, 56.0f, 14.0f, is_dark ? 0x2C2147FF : 0x1E293BFF, is_dark ? 0x56417FFF : 0x0F172AFF);
        rife_draw_text_font(core, rx + 44.0f, c1_y + 32.0f, "R", 0xFFFFFFFF, 1);

        // 软件名称与发行版标签
        rife_draw_text_font(core, rx + 96.0f, c1_y + 24.0f, "RifeOS", is_dark ? 0xF8FAFCFF : 0x0F172AFF, 1);
        rife_draw_text_font(core, rx + 96.0f, c1_y + 48.0f, is_zh ? "桌面工作空间专业版" : "Desktop Workspace Edition", is_dark ? 0x94A3B8FF : 0x64748BFF, 4);

        // 版本胶囊药丸 (Version Pill)
        float ver_w = 96.0f;
        float ver_h = 26.0f;
        float ver_x = card_right - ver_w;
        float ver_y = c1_y + 24.0f;
        rife_draw_round_rect(core, ver_x, ver_y, ver_w, ver_h, 6.0f, is_dark ? 0x161022FF : 0xF1F5F9FF, is_dark ? 0x2E2447FF : 0xE2E8F0FF);
        rife_draw_text_font(core, ver_x + 14.0f, ver_y + 5.0f, "v1.0.0 Pro", is_dark ? 0xC084FCFF : 0x475569FF, 4);

        // 2. 开发者与联系方式卡片 (150px)
        float c2_y = c1_y + 100.0f + 14.0f;
        draw_settings_card(core, rx, c2_y, rw, 150.0f, is_dark);

        // Row 0: 开发者
        float r0_y = c2_y;
        draw_row_header(core, rx, r0_y, is_zh ? "系统开发者" : "Developer", is_zh ? "系统架构设计与工程实现" : "System Architecture & Engineering", is_dark);
        rife_draw_round_rect(core, card_right - 70.0f, r0_y + 13.0f, 70.0f, 24.0f, 6.0f, is_dark ? 0x2F214CFF : 0xEFF6FFFF, is_dark ? 0x6D28D9FF : 0xBAE6FDFF);
        rife_draw_text_font(core, card_right - 54.0f, r0_y + 17.0f, "Renly", is_dark ? 0xD8B4FEFF : 0x0284C7FF, 1);

        rife_draw_rect(core, rx + 16.0f, c2_y + 50.0f, rw - 32.0f, 1.0f, div_col);

        // Row 1: 联络邮箱
        float r1_y = c2_y + 50.0f;
        draw_row_header(core, rx, r1_y, is_zh ? "联络邮箱" : "Email Address", is_zh ? "技术交流与问题反馈" : "Feedback & Inquiries", is_dark);
        rife_draw_text_font(core, card_right - 188.0f, r1_y + 16.0f, "renly20061108@gmail.com", is_dark ? 0xCBD5E1FF : 0x475569FF, 4);

        rife_draw_rect(core, rx + 16.0f, c2_y + 100.0f, rw - 32.0f, 1.0f, div_col);

        // Row 2: 抖音
        float r2_y = c2_y + 100.0f;
        draw_row_header(core, rx, r2_y, is_zh ? "官方抖音" : "Douyin Handle", is_zh ? "日常开发日志与作品更新" : "Development logs & updates", is_dark);
        rife_draw_text_font(core, card_right - 60.0f, r2_y + 16.0f, "陈连山", is_dark ? 0xF8FAFCFF : 0x0F172AFF, 1);

        // 3. 底部商业版权微注 (Copyright Footer)
        rife_draw_text_font(core, rx + 4.0f, c2_y + 166.0f, "Copyright (C) 2026 Renly. All rights reserved.", is_dark ? 0x64748BFF : 0x94A3B8FF, 4);
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
    .default_w = 680.0f,
    .default_h = 470.0f,
    .pin_to_dock = true,
    .create = settings_create,
    .destroy = settings_destroy,
    .update = settings_update,
    .render = settings_render
};
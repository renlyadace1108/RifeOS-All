#define _CRT_SECURE_NO_WARNINGS
#include "app_clock.h"
#include "app_manifest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#define CLOCK_APP_ID 2003

typedef enum {
    CLOCK_DIAL_24H = 0,
    CLOCK_DIAL_12H = 1
} ClockDialMode;

typedef struct {
    int view_year;
    int view_month;
    int view_day;
    ClockDialMode dial_mode;

    RtodoStorage storage;
    FILETIME last_write_time;
    uint64_t last_check_ns;

    uint32_t hovered_event_id;
    int hovered_dial_mins;
    uint32_t selected_event_id;

    int right_panel_tab; // 0: 日程流, 1: 数据分析
    int agenda_filter;   // 0: 全部, 1: 未完成, 2: 已完成
    float list_scroll_y;
    float analytics_scroll_y;
} ClockState;

typedef struct {
    int total_events;
    int completed_events;
    int uncompleted_events;
    float completion_pct;

    int total_mins;
    int completed_mins;
    int remaining_mins;
    int free_mins;
    float day_utilization_pct;

    int tag_event_count[CAL_MAX_CUSTOM_TAGS];
    int tag_mins[CAL_MAX_CUSTOM_TAGS];
    float tag_pct[CAL_MAX_CUSTOM_TAGS];

    int phase_mins[4]; // 0: 凌晨(00~06), 1: 晨间(06~12), 2: 午后(12~18), 3: 晚间(18~24)
    int peak_phase_idx;
} ClockAnalytics;

static int clock_get_day_of_week(int y, int m, int d) {
    if (m < 3) {
        m += 12;
        y -= 1;
    }
    int k = y % 100;
    int j = y / 100;
    int h = (d + 13 * (m + 1) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    int dow = (h + 6) % 7; // 0: Sun, 1: Mon, ..., 6: Sat
    return dow;
}

static const char* clock_weekday_names[] = {
    "周日", "周一", "周二", "周三", "周四", "周五", "周六"
};

static int clock_days_in_month(int y, int m) {
    static const int dpm[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m < 1 || m > 12) return 30;
    if (m == 2) {
        bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
        return leap ? 29 : 28;
    }
    return dpm[m - 1];
}

static void clock_sync_storage_if_needed(ClockState* state) {
    uint64_t now_ns = rife_time_now_ns();
    if (now_ns - state->last_check_ns < 500000000ULL) { // 500ms 检查一次
        return;
    }
    state->last_check_ns = now_ns;

    char path[MAX_PATH];
    rtodo_get_storage_path(path, sizeof(path));
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) {
        if (CompareFileTime(&fad.ftLastWriteTime, &state->last_write_time) != 0) {
            state->last_write_time = fad.ftLastWriteTime;
            rtodo_load_storage(&state->storage);
        }
    }
}

static void clock_get_today(int* y, int* m, int* d) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    *y = st.wYear;
    *m = st.wMonth;
    *d = st.wDay;
}

static bool clock_is_today(const ClockState* state) {
    int ty, tm, td;
    clock_get_today(&ty, &tm, &td);
    return (state->view_year == ty && state->view_month == tm && state->view_day == td);
}

static void clock_compute_analytics(const ClockState* state, ClockAnalytics* a) {
    memset(a, 0, sizeof(ClockAnalytics));

    for (int i = 0; i < state->storage.event_count; i++) {
        const CalendarEvent* e = &state->storage.events[i];
        if (e->year != state->view_year || e->month != state->view_month || e->day != state->view_day) {
            continue;
        }

        a->total_events++;
        if (e->is_completed) a->completed_events++;
        else a->uncompleted_events++;

        int sm = e->start_hour * 60 + e->start_min;
        int em = e->end_hour * 60 + e->end_min;
        if (em <= sm) em = sm + 15;
        if (em > 1440) em = 1440;
        int dur = em - sm;

        a->total_mins += dur;
        if (e->is_completed) {
            a->completed_mins += dur;
        } else {
            a->remaining_mins += dur;
        }

        if (e->tag_idx >= 0 && e->tag_idx < state->storage.tag_count) {
            a->tag_event_count[e->tag_idx]++;
            a->tag_mins[e->tag_idx] += dur;
        }

        // 计算跨越 4 个生理节律时段的时长
        for (int p = 0; p < 4; p++) {
            int p_start = p * 360;
            int p_end = (p + 1) * 360;
            int o_start = (sm > p_start) ? sm : p_start;
            int o_end = (em < p_end) ? em : p_end;
            if (o_end > o_start) {
                a->phase_mins[p] += (o_end - o_start);
            }
        }
    }

    if (a->total_events > 0) {
        a->completion_pct = ((float)a->completed_events * 100.0f) / (float)a->total_events;
    }
    a->free_mins = 1440 - a->total_mins;
    if (a->free_mins < 0) a->free_mins = 0;
    a->day_utilization_pct = ((float)a->total_mins * 100.0f) / 1440.0f;

    if (a->total_mins > 0) {
        for (int t = 0; t < state->storage.tag_count; t++) {
            a->tag_pct[t] = ((float)a->tag_mins[t] * 100.0f) / (float)a->total_mins;
        }
    }

    int max_p_mins = -1;
    for (int p = 0; p < 4; p++) {
        if (a->phase_mins[p] > max_p_mins) {
            max_p_mins = a->phase_mins[p];
            a->peak_phase_idx = p;
        }
    }
}

// -------------------------------------------------------------
// 插件生命周期 (Plugin Lifecycle)
// -------------------------------------------------------------

static void* clock_create(RifeCore* core) {
    (void)core;
    ClockState* state = (ClockState*)malloc(sizeof(ClockState));
    if (!state) return NULL;
    memset(state, 0, sizeof(ClockState));

    clock_get_today(&state->view_year, &state->view_month, &state->view_day);
    state->dial_mode = CLOCK_DIAL_24H;
    state->hovered_dial_mins = -1;
    state->right_panel_tab = 0;
    state->agenda_filter = 0;

    char path[MAX_PATH];
    rtodo_get_storage_path(path, sizeof(path));
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) {
        state->last_write_time = fad.ftLastWriteTime;
    }
    if (!rtodo_load_storage(&state->storage)) {
        state->storage.magic = RTODO_MAGIC;
        state->storage.version = RTODO_VERSION;
        state->storage.tag_count = 3;
        snprintf(state->storage.tags[0].name, sizeof(state->storage.tags[0].name), "工作");
        state->storage.tags[0].color_bar = 0x3370FFFF;
        state->storage.tags[0].is_enabled = true;

        snprintf(state->storage.tags[1].name, sizeof(state->storage.tags[1].name), "生活");
        state->storage.tags[1].color_bar = 0x10B981FF;
        state->storage.tags[1].is_enabled = true;

        snprintf(state->storage.tags[2].name, sizeof(state->storage.tags[2].name), "重要");
        state->storage.tags[2].color_bar = 0xEF4444FF;
        state->storage.tags[2].is_enabled = true;
    }

    return state;
}

static void clock_destroy(void* inst) {
    if (inst) free(inst);
}

static void clock_update(void* inst, RifeCore* core, const RifeInput* input, float client_w, float client_h) {
    ClockState* state = (ClockState*)inst;
    if (!state) return;
    (void)core;

    // 1. 同步外部 rtodo_data.bin 数据更新
    clock_sync_storage_if_needed(state);

    float mx = input->mouse_x;
    float my = input->mouse_y;

    // 2. 悬停检测（表盘中心与半径）
    float dial_cx = (client_w - 330.0f) * 0.5f;
    float dial_cy = 44.0f + (client_h - 44.0f) * 0.5f;
    float dial_r_out = (client_h - 44.0f) * 0.40f;
    if (dial_r_out > 200.0f) dial_r_out = 200.0f;
    if (dial_r_out < 120.0f) dial_r_out = 120.0f;
    float dial_r_in = dial_r_out * 0.55f;

    float dx = mx - dial_cx;
    float dy = my - dial_cy;
    float dist = sqrtf(dx * dx + dy * dy);

    state->hovered_dial_mins = -1;
    uint32_t found_hover_id = 0;

    if (dist >= dial_r_in && dist <= dial_r_out + 8.0f) {
        float angle = atan2f(dx, -dy) * (180.0f / 3.14159265f);
        if (angle < 0.0f) angle += 360.0f;

        if (state->dial_mode == CLOCK_DIAL_24H) {
            int mins = (int)(angle * (1440.0f / 360.0f) + 0.5f);
            if (mins >= 1440) mins = 0;
            state->hovered_dial_mins = mins;

            for (int i = 0; i < state->storage.event_count; i++) {
                const CalendarEvent* e = &state->storage.events[i];
                if (e->year == state->view_year && e->month == state->view_month && e->day == state->view_day) {
                    int sm = e->start_hour * 60 + e->start_min;
                    int em = e->end_hour * 60 + e->end_min;
                    if (em <= sm) em = sm + 15;
                    if (mins >= sm && mins <= em) {
                        found_hover_id = e->id;
                        break;
                    }
                }
            }
        } else {
            int mins = (int)(angle * (720.0f / 360.0f) + 0.5f);
            if (mins >= 720) mins = 0;
            state->hovered_dial_mins = mins;

            for (int i = 0; i < state->storage.event_count; i++) {
                const CalendarEvent* e = &state->storage.events[i];
                if (e->year == state->view_year && e->month == state->view_month && e->day == state->view_day) {
                    int sm = (e->start_hour % 12) * 60 + e->start_min;
                    int em = (e->end_hour % 12) * 60 + e->end_min;
                    if (em <= sm) em = sm + 15;
                    if (mins >= sm && mins <= em) {
                        found_hover_id = e->id;
                        break;
                    }
                }
            }
        }
    }
    state->hovered_event_id = found_hover_id;

    // 3. 鼠标滚轮在右侧面板滚动
    if (fabsf(input->scroll_delta) > 0.01f) {
        if (mx >= client_w - 330.0f && mx <= client_w) {
            if (state->right_panel_tab == 0) {
                state->list_scroll_y += input->scroll_delta * 40.0f;
                if (state->list_scroll_y > 0.0f) state->list_scroll_y = 0.0f;
            } else {
                state->analytics_scroll_y += input->scroll_delta * 40.0f;
                if (state->analytics_scroll_y > 0.0f) state->analytics_scroll_y = 0.0f;
            }
        }
    }

    // 4. 单击事件响应
    if (input->mouse_pressed[0]) {
        // A. 顶栏交互
        if (my >= 0.0f && my <= 44.0f) {
            // 前一天 [<]
            if (mx >= 160.0f && mx <= 186.0f && my >= 8.0f && my <= 34.0f) {
                state->view_day--;
                if (state->view_day < 1) {
                    state->view_month--;
                    if (state->view_month < 1) {
                        state->view_month = 12;
                        state->view_year--;
                    }
                    state->view_day = clock_days_in_month(state->view_year, state->view_month);
                }
                return;
            }
            // 今天 [ 今天 ]
            if (mx >= 194.0f && mx <= 254.0f && my >= 8.0f && my <= 34.0f) {
                clock_get_today(&state->view_year, &state->view_month, &state->view_day);
                return;
            }
            // 后一天 [>]
            if (mx >= 262.0f && mx <= 288.0f && my >= 8.0f && my <= 34.0f) {
                state->view_day++;
                if (state->view_day > clock_days_in_month(state->view_year, state->view_month)) {
                    state->view_day = 1;
                    state->view_month++;
                    if (state->view_month > 12) {
                        state->view_month = 1;
                        state->view_year++;
                    }
                }
                return;
            }

            // 制式切换 [ 24小时制 ] / [ 12小时制 ]
            float mode_btn_x = client_w - 116.0f;
            if (mx >= mode_btn_x && mx <= mode_btn_x + 100.0f && my >= 8.0f && my <= 34.0f) {
                state->dial_mode = (state->dial_mode == CLOCK_DIAL_24H) ? CLOCK_DIAL_12H : CLOCK_DIAL_24H;
                return;
            }
        }

        // B. 右侧面板顶部标签切换 [ 日程流 ] / [ 📊 数据分析 ]
        float list_x = client_w - 330.0f;
        if (mx >= list_x && mx <= client_w && my >= 44.0f && my <= 82.0f) {
            float tab_w = (330.0f - 28.0f - 8.0f) * 0.5f;
            float tab0_x = list_x + 14.0f;
            float tab1_x = tab0_x + tab_w + 8.0f;
            if (mx >= tab0_x && mx <= tab0_x + tab_w) {
                state->right_panel_tab = 0;
                return;
            }
            if (mx >= tab1_x && mx <= tab1_x + tab_w) {
                state->right_panel_tab = 1;
                return;
            }
        }

        // C. 点击日程流中的完成圆圈或卡片
        if (state->right_panel_tab == 0 && mx >= list_x && mx <= client_w && my >= 84.0f) {
            float cur_cy = 44.0f + 40.0f + 36.0f + state->list_scroll_y;
            for (int i = 0; i < state->storage.event_count; i++) {
                CalendarEvent* e = &state->storage.events[i];
                if (e->year == state->view_year && e->month == state->view_month && e->day == state->view_day) {
                    if (state->agenda_filter == 1 && e->is_completed) continue;
                    if (state->agenda_filter == 2 && !e->is_completed) continue;

                    float card_y = cur_cy;
                    float card_h = 58.0f;

                    // 点击勾选圆圈
                    float chk_x = client_w - 40.0f;
                    float chk_y = card_y + 19.0f;
                    if (mx >= chk_x - 8.0f && mx <= chk_x + 16.0f && my >= chk_y - 8.0f && my <= chk_y + 16.0f) {
                        e->is_completed = !e->is_completed;
                        rtodo_save_storage(&state->storage);
                        return;
                    }

                    // 点击卡片聚焦
                    if (mx >= client_w - 320.0f && mx <= client_w - 10.0f && my >= card_y && my <= card_y + card_h) {
                        state->selected_event_id = e->id;
                        return;
                    }

                    cur_cy += card_h + 8.0f;
                }
            }
        }

        // D. 点击圆盘日程扇面聚焦选中
        if (state->hovered_event_id != 0) {
            state->selected_event_id = state->hovered_event_id;
            return;
        }
    }
}

static void clock_render(void* inst, RifeCore* core, float client_x, float client_y, float client_w, float client_h) {
    ClockState* state = (ClockState*)inst;
    if (!state || !core) return;

    RifeSystemConfig* cfg = rife_get_system_config();
    bool is_dark = (cfg->palette == PALETTE_OBSIDIAN || cfg->cloud_color == CLOUD_COLOR_OBSIDIAN);

    uint32_t col_txt_main = is_dark ? 0xF8FAFCFF : 0x0F172AFF;
    uint32_t col_txt_sub  = is_dark ? 0x94A3B8FF : 0x64748BFF;
    uint32_t col_txt_mute = is_dark ? 0x64748BFF : 0x94A3B8FF;
    uint32_t col_border   = is_dark ? 0x38285555 : 0xE2E8F088;
    uint32_t col_card_bg  = is_dark ? 0x1E1730AA : 0xFFFFFFCC;

    ClockAnalytics an;
    clock_compute_analytics(state, &an);

    // ---------------------------------------------------------
    // 1. 顶栏 (Top Bar: 44px)
    // ---------------------------------------------------------
    rife_draw_rect(core, client_x, client_y, client_w, 44.0f, is_dark ? 0x140E2288 : 0xF8FAFC88);
    rife_draw_rect(core, client_x, client_y + 43.0f, client_w, 1.0f, col_border);

    // 应用标题与图标
    rife_draw_round_rect(core, client_x + 16.0f, client_y + 10.0f, 24.0f, 24.0f, 6.0f, 0x6366F1FF, 0x818CF8FF);
    rife_draw_text_rect(core, client_x + 16.0f, client_y + 10.0f, 24.0f, 24.0f, "Rc", 0xFFFFFFFF, 3, 0);
    rife_draw_text_font(core, client_x + 48.0f, client_y + 13.0f, "Rclock 时钟图", col_txt_main, 5);

    // 日期切换器 [<] [ 今天 ] [>]
    rife_draw_round_rect(core, client_x + 160.0f, client_y + 8.0f, 26.0f, 26.0f, 6.0f, is_dark ? 0x2A204488 : 0xF1F5F9AA, col_border);
    rife_draw_text_rect(core, client_x + 160.0f, client_y + 8.0f, 26.0f, 26.0f, "<", col_txt_main, 3, 0);

    bool is_today = clock_is_today(state);
    uint32_t today_btn_bg = is_today ? (is_dark ? 0x4F46E5CC : 0x3B82F6CC) : (is_dark ? 0x2A204488 : 0xF1F5F9AA);
    uint32_t today_btn_txt = is_today ? 0xFFFFFFFF : col_txt_main;
    rife_draw_round_rect(core, client_x + 192.0f, client_y + 8.0f, 62.0f, 26.0f, 6.0f, today_btn_bg, col_border);
    rife_draw_text_rect(core, client_x + 192.0f, client_y + 8.0f, 62.0f, 26.0f, "今天", today_btn_txt, 3, 0);

    rife_draw_round_rect(core, client_x + 260.0f, client_y + 8.0f, 26.0f, 26.0f, 6.0f, is_dark ? 0x2A204488 : 0xF1F5F9AA, col_border);
    rife_draw_text_rect(core, client_x + 260.0f, client_y + 8.0f, 26.0f, 26.0f, ">", col_txt_main, 3, 0);

    // 日期标题
    char date_buf[64];
    int dow = clock_get_day_of_week(state->view_year, state->view_month, state->view_day);
    snprintf(date_buf, sizeof(date_buf), "%d年%d月%d日 %s", state->view_year, state->view_month, state->view_day, clock_weekday_names[dow]);
    rife_draw_text_font(core, client_x + 296.0f, client_y + 14.0f, date_buf, col_txt_sub, 3);

    // 右侧制式切换胶囊 [ 24小时制 ]
    float mode_btn_x = client_x + client_w - 116.0f;
    rife_draw_round_rect(core, mode_btn_x, client_y + 8.0f, 100.0f, 26.0f, 13.0f, is_dark ? 0x312E8188 : 0xEEF2FFCC, 0x6366F1AA);
    rife_draw_text_rect(core, mode_btn_x, client_y + 8.0f, 100.0f, 26.0f, (state->dial_mode == CLOCK_DIAL_24H) ? "24小时制" : "12小时制", is_dark ? 0xC7D2FEFF : 0x4338CAFF, 3, 0);

    // ---------------------------------------------------------
    // 2. 左侧圆形时钟图 (Radial Clock Dial)
    // ---------------------------------------------------------
    float dial_area_w = client_w - 330.0f;
    float dial_cx = client_x + dial_area_w * 0.5f;
    float dial_cy = client_y + 44.0f + (client_h - 44.0f) * 0.5f;
    float dial_r_out = (client_h - 44.0f) * 0.40f;
    if (dial_r_out > 200.0f) dial_r_out = 200.0f;
    if (dial_r_out < 120.0f) dial_r_out = 120.0f;
    float dial_r_in = dial_r_out * 0.55f;
    float hub_r = dial_r_in - 10.0f;

    // A. 昼夜环境光晕底盘 (Ambient Day/Night Shading)
    if (state->dial_mode == CLOCK_DIAL_24H) {
        // 白昼环境光 (06:00 ~ 18:00, 90° ~ 270°)
        rife_draw_arc_sector(core, dial_cx, dial_cy, dial_r_in - 2.0f, dial_r_out + 2.0f, 90.0f, 270.0f,
                             is_dark ? 0x1E3A8A18 : 0xBAE6FD25, 0);
        // 夜幕环境光 (18:00 ~ 06:00, 270° ~ 90°)
        rife_draw_arc_sector(core, dial_cx, dial_cy, dial_r_in - 2.0f, dial_r_out + 2.0f, 270.0f, 90.0f,
                             is_dark ? 0x0F172A44 : 0xE2E8F033, 0);
    }

    // B. 表盘环形轨道底色 (Dial Ring Track)
    rife_draw_arc_sector(core, dial_cx, dial_cy, dial_r_in, dial_r_out, 0.0f, 360.0f,
                         is_dark ? 0x1F163533 : 0xF8FAFC66, is_dark ? 0x38285555 : 0xE2E8F088);

    // C. 刻度线与时钟标注 (Ticks & Hour Markings)
    int total_hours = (state->dial_mode == CLOCK_DIAL_24H) ? 24 : 12;
    float deg_per_h = 360.0f / (float)total_hours;
    const float deg2rad = 0.0174532925f;

    for (int h = 0; h < total_hours; h++) {
        float ang_deg = (float)h * deg_per_h;
        float rad = ang_deg * deg2rad;
        float s = sinf(rad);
        float c = cosf(rad);

        bool is_major = (h % 3 == 0);
        float tick_len = is_major ? 10.0f : 5.0f;
        uint32_t tick_col = is_major ? (is_dark ? 0xA5B4FCFF : 0x475569FF) : (is_dark ? 0x47556988 : 0xCBD5E1AA);

        float x1 = dial_cx + s * (dial_r_out - tick_len);
        float y1 = dial_cy - c * (dial_r_out - tick_len);
        float x2 = dial_cx + s * dial_r_out;
        float y2 = dial_cy - c * dial_r_out;
        rife_draw_line(core, x1, y1, x2, y2, is_major ? 1.8f : 1.0f, tick_col);

        if (is_major || (state->dial_mode == CLOCK_DIAL_24H && (h % 2 == 0))) {
            float tx = dial_cx + s * (dial_r_out + 14.0f);
            float ty = dial_cy - c * (dial_r_out + 14.0f);
            char h_str[8];
            if (state->dial_mode == CLOCK_DIAL_24H) {
                snprintf(h_str, sizeof(h_str), "%02d", h);
            } else {
                snprintf(h_str, sizeof(h_str), "%d", (h == 0) ? 12 : h);
            }
            rife_draw_text_rect(core, tx - 12.0f, ty - 8.0f, 24.0f, 16.0f, h_str, is_major ? col_txt_sub : col_txt_mute, 4, 0);
        }
    }

    // D. 日程环形彩色扇面 (Event Radial Annular Sectors)
    for (int i = 0; i < state->storage.event_count; i++) {
        const CalendarEvent* e = &state->storage.events[i];
        if (e->year != state->view_year || e->month != state->view_month || e->day != state->view_day) {
            continue;
        }

        float start_ang = 0.0f;
        float end_ang = 0.0f;

        if (state->dial_mode == CLOCK_DIAL_24H) {
            int sm = e->start_hour * 60 + e->start_min;
            int em = e->end_hour * 60 + e->end_min;
            if (em <= sm) em = sm + 15;
            start_ang = (float)sm * (360.0f / 1440.0f);
            end_ang = (float)em * (360.0f / 1440.0f);
        } else {
            int sm = (e->start_hour % 12) * 60 + e->start_min;
            int em = (e->end_hour % 12) * 60 + e->end_min;
            if (em <= sm) em = sm + 15;
            start_ang = (float)sm * (360.0f / 720.0f);
            end_ang = (float)em * (360.0f / 720.0f);
        }

        if (end_ang - start_ang < 2.5f) {
            end_ang = start_ang + 2.5f;
        }

        uint32_t tag_color = 0x3370FFFF;
        if (e->tag_idx >= 0 && e->tag_idx < state->storage.tag_count) {
            tag_color = state->storage.tags[e->tag_idx].color_bar;
        }

        bool is_hovered = (e->id == state->hovered_event_id);
        bool is_selected = (e->id == state->selected_event_id);

        uint32_t fill_col = tag_color;
        uint32_t border_col = tag_color;

        if (e->is_completed) {
            fill_col = (tag_color & 0xFFFFFF00) | 0x44;
            border_col = (tag_color & 0xFFFFFF00) | 0x66;
        } else if (is_hovered || is_selected) {
            fill_col = (tag_color & 0xFFFFFF00) | 0xCC;
            border_col = 0xFFFFFFFF;
        } else {
            fill_col = (tag_color & 0xFFFFFF00) | 0x99;
            border_col = (tag_color & 0xFFFFFF00) | 0xDD;
        }

        float r_in = dial_r_in + 2.0f;
        float r_out = dial_r_out - 2.0f;
        if (is_hovered || is_selected) {
            r_in -= 3.0f;
            r_out += 4.0f;
        }

        rife_draw_arc_sector(core, dial_cx, dial_cy, r_in, r_out, start_ang, end_ang, fill_col, border_col);
    }

    // E. 实时时间激光指针与发光核晶 (Real-time Needle)
    if (is_today) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        float now_mins = (float)(st.wHour * 60 + st.wMinute) + (float)st.wSecond / 60.0f;
        float now_deg = 0.0f;
        if (state->dial_mode == CLOCK_DIAL_24H) {
            now_deg = now_mins * (360.0f / 1440.0f);
        } else {
            now_deg = fmodf(now_mins, 720.0f) * (360.0f / 720.0f);
        }
        float now_rad = now_deg * deg2rad;
        float s = sinf(now_rad);
        float c = cosf(now_rad);

        float nx1 = dial_cx + s * (hub_r - 2.0f);
        float ny1 = dial_cy - c * (hub_r - 2.0f);
        float nx2 = dial_cx + s * (dial_r_out + 8.0f);
        float ny2 = dial_cy - c * (dial_r_out + 8.0f);
        rife_draw_line(core, nx1, ny1, nx2, ny2, 2.5f, 0xF43F5EFF);
        rife_draw_circle(core, nx2, ny2, 4.5f, 0xF43F5EFF, 0xFFFFFFFF);
    }

    // F. 中央多功能液晶控制核 (Central Glass Hub)
    rife_draw_circle(core, dial_cx, dial_cy, hub_r, is_dark ? 0x181128F0 : 0xFFFFFFF2, is_dark ? 0x4C376EE0 : 0xE2E8F0E0);
    rife_draw_circle(core, dial_cx, dial_cy, hub_r - 4.0f, is_dark ? 0x20173688 : 0xF8FAFC88, 0);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    rife_draw_text_rect(core, dial_cx - 60.0f, dial_cy - 36.0f, 120.0f, 26.0f, time_str, col_txt_main, 1, 0);

    int cur_mins = st.wHour * 60 + st.wMinute;
    const CalendarEvent* cur_event = NULL;
    const CalendarEvent* next_event = NULL;
    int next_delta_mins = 9999;

    for (int i = 0; i < state->storage.event_count; i++) {
        const CalendarEvent* e = &state->storage.events[i];
        if (e->year == state->view_year && e->month == state->view_month && e->day == state->view_day) {
            int sm = e->start_hour * 60 + e->start_min;
            int em = e->end_hour * 60 + e->end_min;
            if (em <= sm) em = sm + 15;

            if (is_today) {
                if (cur_mins >= sm && cur_mins <= em) {
                    cur_event = e;
                } else if (sm > cur_mins && (sm - cur_mins) < next_delta_mins) {
                    next_delta_mins = sm - cur_mins;
                    next_event = e;
                }
            }
        }
    }

    if (is_today && cur_event) {
        rife_draw_round_rect(core, dial_cx - 52.0f, dial_cy - 4.0f, 104.0f, 18.0f, 9.0f, 0x10B98128, 0x10B98188);
        rife_draw_text_rect(core, dial_cx - 52.0f, dial_cy - 4.0f, 104.0f, 18.0f, "正在进行", 0x10B981FF, 4, 0);

        char ev_buf[32];
        snprintf(ev_buf, sizeof(ev_buf), "%.16s", cur_event->title);
        rife_draw_text_rect(core, dial_cx - 65.0f, dial_cy + 18.0f, 130.0f, 18.0f, ev_buf, col_txt_main, 3, 0);
    } else if (is_today && next_event) {
        rife_draw_round_rect(core, dial_cx - 52.0f, dial_cy - 4.0f, 104.0f, 18.0f, 9.0f, 0x3B82F622, 0x3B82F666);
        char cd_buf[32];
        if (next_delta_mins >= 60) {
            snprintf(cd_buf, sizeof(cd_buf), "%d小时%d分后", next_delta_mins / 60, next_delta_mins % 60);
        } else {
            snprintf(cd_buf, sizeof(cd_buf), "%d分钟后", next_delta_mins);
        }
        rife_draw_text_rect(core, dial_cx - 52.0f, dial_cy - 4.0f, 104.0f, 18.0f, cd_buf, 0x3B82F6FF, 4, 0);

        char ev_buf[32];
        snprintf(ev_buf, sizeof(ev_buf), "%.16s", next_event->title);
        rife_draw_text_rect(core, dial_cx - 65.0f, dial_cy + 18.0f, 130.0f, 18.0f, ev_buf, col_txt_main, 3, 0);
    } else {
        char stats_buf[32];
        float hrs = (float)an.total_mins / 60.0f;
        snprintf(stats_buf, sizeof(stats_buf), "%d个日程 · %.1fh", an.total_events, hrs);
        rife_draw_round_rect(core, dial_cx - 56.0f, dial_cy - 4.0f, 112.0f, 18.0f, 9.0f, is_dark ? 0x2A204466 : 0xF1F5F988, col_border);
        rife_draw_text_rect(core, dial_cx - 56.0f, dial_cy - 4.0f, 112.0f, 18.0f, stats_buf, col_txt_sub, 4, 0);
        rife_draw_text_rect(core, dial_cx - 50.0f, dial_cy + 18.0f, 100.0f, 18.0f, is_today ? "当前空闲" : "日程概览", col_txt_mute, 3, 0);
    }

    // G. 悬停气泡提示 (Hover Floating Tooltip)
    if (state->hovered_event_id != 0) {
        for (int i = 0; i < state->storage.event_count; i++) {
            const CalendarEvent* e = &state->storage.events[i];
            if (e->id == state->hovered_event_id) {
                float tip_w = 170.0f;
                float tip_h = 56.0f;
                float tip_x = core->input.mouse_x + 14.0f;
                float tip_y = core->input.mouse_y + 14.0f;
                if (tip_x + tip_w > client_x + dial_area_w) tip_x = core->input.mouse_x - tip_w - 10.0f;
                if (tip_y + tip_h > client_y + client_h) tip_y = core->input.mouse_y - tip_h - 10.0f;

                rife_draw_round_rect(core, tip_x, tip_y, tip_w, tip_h, 8.0f, is_dark ? 0x1E1535F0 : 0xFFFFFFFF, 0x818CF8AA);
                rife_draw_text_font(core, tip_x + 10.0f, tip_y + 8.0f, e->title, col_txt_main, 5);

                char t_buf[48];
                snprintf(t_buf, sizeof(t_buf), "%02d:%02d - %02d:%02d", e->start_hour, e->start_min, e->end_hour, e->end_min);
                rife_draw_text_font(core, tip_x + 10.0f, tip_y + 28.0f, t_buf, 0x38BDF8FF, 4);

                if (e->location[0] != '\0') {
                    rife_draw_text_font(core, tip_x + 10.0f, tip_y + 42.0f, e->location, col_txt_mute, 4);
                }
                break;
            }
        }
    }

    // ---------------------------------------------------------
    // 3. 右侧多维面板 (Right Panel: 330px)
    // ---------------------------------------------------------
    float list_x = client_x + client_w - 330.0f;
    float list_y = client_y + 44.0f;
    float list_w = 330.0f;
    float list_h = client_h - 44.0f;

    rife_draw_rect(core, list_x, list_y, 1.0f, list_h, col_border);
    rife_draw_rect(core, list_x + 1.0f, list_y, list_w - 1.0f, list_h, is_dark ? 0x16102655 : 0xF8FAFC44);

    // 双标签分段切换器 [ 日程流 (N) ]  [ 📊 数据分析 ]
    float tab_w = (list_w - 28.0f - 8.0f) * 0.5f;
    float tab0_x = list_x + 14.0f;
    float tab1_x = tab0_x + tab_w + 8.0f;
    float tab_y = list_y + 8.0f;
    float tab_h = 26.0f;

    bool tab0_active = (state->right_panel_tab == 0);
    uint32_t t0_bg = tab0_active ? (is_dark ? 0x4F46E5EE : 0x3B82F6EE) : (is_dark ? 0x2A204466 : 0xE2E8F088);
    uint32_t t0_txt = tab0_active ? 0xFFFFFFFF : col_txt_sub;
    rife_draw_round_rect(core, tab0_x, tab_y, tab_w, tab_h, 6.0f, t0_bg, tab0_active ? 0x818CF8AA : col_border);

    char tab0_label[32];
    snprintf(tab0_label, sizeof(tab0_label), "日程流 (%d)", an.total_events);
    rife_draw_text_rect(core, tab0_x, tab_y, tab_w, tab_h, tab0_label, t0_txt, 3, 0);

    bool tab1_active = (state->right_panel_tab == 1);
    uint32_t t1_bg = tab1_active ? (is_dark ? 0x4F46E5EE : 0x3B82F6EE) : (is_dark ? 0x2A204466 : 0xE2E8F088);
    uint32_t t1_txt = tab1_active ? 0xFFFFFFFF : col_txt_sub;
    rife_draw_round_rect(core, tab1_x, tab_y, tab_w, tab_h, 6.0f, t1_bg, tab1_active ? 0x818CF8AA : col_border);
    rife_draw_text_rect(core, tab1_x, tab_y, tab_w, tab_h, "数据分析", t1_txt, 3, 0);

    // 分割线
    rife_draw_rect(core, list_x + 14.0f, list_y + 40.0f, list_w - 28.0f, 1.0f, col_border);

    if (state->right_panel_tab == 0) {
        // =====================================================
        // Tab 0: 今日日程流 (Agenda Stream)
        // =====================================================
        // 顶部极简概览条 (Progress Overview Bar)
        char sum_bar[64];
        snprintf(sum_bar, sizeof(sum_bar), "规划 %.1fh · 完成度 %.0f%% (%d/%d)",
                 (float)an.total_mins / 60.0f, an.completion_pct, an.completed_events, an.total_events);
        rife_draw_text_font(core, list_x + 16.0f, list_y + 48.0f, sum_bar, col_txt_sub, 4);

        // 亚像素微型完成进度条
        float pbar_w = list_w - 32.0f;
        rife_draw_round_rect(core, list_x + 16.0f, list_y + 66.0f, pbar_w, 4.0f, 2.0f, is_dark ? 0x2A204488 : 0xE2E8F0AA, 0);
        if (an.total_events > 0 && an.completed_events > 0) {
            float fill_w = pbar_w * (an.completion_pct / 100.0f);
            if (fill_w < 6.0f) fill_w = 6.0f;
            rife_draw_round_rect(core, list_x + 16.0f, list_y + 66.0f, fill_w, 4.0f, 2.0f, 0x10B981FF, 0);
        }

        // 日程卡片滚动列表
        rife_push_scissor(core, list_x + 1.0f, list_y + 76.0f, list_w - 2.0f, list_h - 78.0f);

        float cur_card_y = list_y + 78.0f + state->list_scroll_y;
        int rendered_cards = 0;

        for (int i = 0; i < state->storage.event_count; i++) {
            const CalendarEvent* e = &state->storage.events[i];
            if (e->year != state->view_year || e->month != state->view_month || e->day != state->view_day) {
                continue;
            }

            rendered_cards++;
            float card_x = list_x + 14.0f;
            float card_w = list_w - 28.0f;
            float card_h = 58.0f;

            bool is_hover = (e->id == state->hovered_event_id);
            bool is_select = (e->id == state->selected_event_id);

            uint32_t bg_col = col_card_bg;
            uint32_t bdr_col = col_border;
            if (is_hover || is_select) {
                bg_col = is_dark ? 0x2A1F48EE : 0xFFFFFFFF;
                bdr_col = 0x818CF8CC;
            }

            rife_draw_round_rect(core, card_x, cur_card_y, card_w, card_h, 8.0f, bg_col, bdr_col);

            uint32_t tag_color = 0x3370FFFF;
            if (e->tag_idx >= 0 && e->tag_idx < state->storage.tag_count) {
                tag_color = state->storage.tags[e->tag_idx].color_bar;
            }
            rife_draw_round_rect(core, card_x + 4.0f, cur_card_y + 8.0f, 4.0f, card_h - 16.0f, 2.0f, tag_color, 0);

            char t_span[48];
            int dur_m = (e->end_hour * 60 + e->end_min) - (e->start_hour * 60 + e->start_min);
            if (dur_m < 0) dur_m += 1440;
            if (dur_m >= 60) {
                snprintf(t_span, sizeof(t_span), "%02d:%02d - %02d:%02d (%d小时%d分)",
                         e->start_hour, e->start_min, e->end_hour, e->end_min, dur_m / 60, dur_m % 60);
            } else {
                snprintf(t_span, sizeof(t_span), "%02d:%02d - %02d:%02d (%d分)",
                         e->start_hour, e->start_min, e->end_hour, e->end_min, dur_m);
            }
            rife_draw_text_font(core, card_x + 16.0f, cur_card_y + 10.0f, t_span, 0x38BDF8FF, 4);
            rife_draw_text_font(core, card_x + 16.0f, cur_card_y + 26.0f, e->title, e->is_completed ? col_txt_mute : col_txt_main, 5);

            if (e->location[0] != '\0') {
                rife_draw_text_font(core, card_x + 16.0f, cur_card_y + 42.0f, e->location, col_txt_mute, 4);
            }

            float chk_x = card_x + card_w - 24.0f;
            float chk_y = cur_card_y + 20.0f;
            if (e->is_completed) {
                rife_draw_circle(core, chk_x + 7.0f, chk_y + 7.0f, 8.0f, 0x10B981FF, 0x10B981FF);
                rife_draw_text_rect(core, chk_x, chk_y, 14.0f, 14.0f, "v", 0xFFFFFFFF, 4, 0);
            } else {
                rife_draw_circle(core, chk_x + 7.0f, chk_y + 7.0f, 8.0f, is_dark ? 0x2A204488 : 0xF1F5F988, col_border);
            }

            cur_card_y += card_h + 8.0f;
        }

        if (rendered_cards == 0) {
            rife_draw_round_rect(core, list_x + 20.0f, list_y + 90.0f, list_w - 40.0f, 110.0f, 12.0f, is_dark ? 0x1E173088 : 0xFFFFFF88, col_border);
            rife_draw_text_rect(core, list_x + 20.0f, list_y + 115.0f, list_w - 40.0f, 20.0f, "今日暂无日程安排", col_txt_sub, 3, 0);
            rife_draw_text_rect(core, list_x + 20.0f, list_y + 145.0f, list_w - 40.0f, 20.0f, "可在 Rtodo 中添加日程，数据实时同步", col_txt_mute, 4, 0);
        }

        rife_pop_scissor(core);

    } else {
        // =====================================================
        // Tab 1: 📊 深度数据分析看板 (Data Analytics Dashboard)
        // =====================================================
        rife_push_scissor(core, list_x + 1.0f, list_y + 42.0f, list_w - 2.0f, list_h - 44.0f);

        float ay = list_y + 46.0f + state->analytics_scroll_y;
        float cw = list_w - 28.0f;
        float cx = list_x + 14.0f;

        // --- 卡片 1: 今日时间利用与完成效率 ---
        float c1_h = 96.0f;
        rife_draw_round_rect(core, cx, ay, cw, c1_h, 10.0f, col_card_bg, col_border);
        rife_draw_text_font(core, cx + 14.0f, ay + 10.0f, "今日完成效率与负荷", col_txt_main, 5);

        // 完成率大数字与进度条
        char pct_str[32];
        snprintf(pct_str, sizeof(pct_str), "%.0f%%", an.completion_pct);
        rife_draw_text_font(core, cx + 14.0f, ay + 30.0f, pct_str, 0x10B981FF, 1);

        char task_cnt_str[32];
        snprintf(task_cnt_str, sizeof(task_cnt_str), "已完成 %d / %d 项", an.completed_events, an.total_events);
        rife_draw_text_font(core, cx + 90.0f, ay + 36.0f, task_cnt_str, col_txt_sub, 3);

        // 进度条
        rife_draw_round_rect(core, cx + 14.0f, ay + 58.0f, cw - 28.0f, 6.0f, 3.0f, is_dark ? 0x2A2044AA : 0xE2E8F0AA, 0);
        if (an.total_events > 0 && an.completed_events > 0) {
            float fw = (cw - 28.0f) * (an.completion_pct / 100.0f);
            if (fw < 6.0f) fw = 6.0f;
            rife_draw_round_rect(core, cx + 14.0f, ay + 58.0f, fw, 6.0f, 3.0f, 0x10B981FF, 0);
        }

        // 三个关键指标微标签: [规划工时] [剩余空闲] [全天利用率]
        char m1[32], m2[32], m3[32];
        snprintf(m1, sizeof(m1), "规划 %.1fh", (float)an.total_mins / 60.0f);
        snprintf(m2, sizeof(m2), "空闲 %.1fh", (float)an.free_mins / 60.0f);
        snprintf(m3, sizeof(m3), "利用率 %.0f%%", an.day_utilization_pct);
        rife_draw_text_font(core, cx + 14.0f, ay + 74.0f, m1, 0x38BDF8FF, 4);
        rife_draw_text_font(core, cx + 110.0f, ay + 74.0f, m2, col_txt_mute, 4);
        rife_draw_text_font(core, cx + 210.0f, ay + 74.0f, m3, col_txt_sub, 4);

        ay += c1_h + 10.0f;

        // --- 卡片 2: 分类工时占比矩阵 ---
        int active_tags_count = 0;
        for (int t = 0; t < state->storage.tag_count; t++) {
            if (an.tag_event_count[t] > 0) active_tags_count++;
        }

        float c2_h = 58.0f + ((active_tags_count > 0) ? (float)active_tags_count * 26.0f : 26.0f);
        rife_draw_round_rect(core, cx, ay, cw, c2_h, 10.0f, col_card_bg, col_border);
        rife_draw_text_font(core, cx + 14.0f, ay + 10.0f, "分类工时分布占比", col_txt_main, 5);

        // 全彩连续堆叠进度条 (Stacked Color Bar)
        float sbar_x = cx + 14.0f;
        float sbar_w = cw - 28.0f;
        float sbar_y = ay + 32.0f;
        float sbar_h = 8.0f;
        rife_draw_round_rect(core, sbar_x, sbar_y, sbar_w, sbar_h, 4.0f, is_dark ? 0x2A204488 : 0xE2E8F0AA, 0);

        if (an.total_mins > 0) {
            float cur_sx = sbar_x;
            for (int t = 0; t < state->storage.tag_count; t++) {
                if (an.tag_mins[t] > 0) {
                    float sw = sbar_w * ((float)an.tag_mins[t] / (float)an.total_mins);
                    if (sw < 4.0f) sw = 4.0f;
                    if (cur_sx + sw > sbar_x + sbar_w) sw = sbar_x + sbar_w - cur_sx;
                    uint32_t t_col = state->storage.tags[t].color_bar;
                    rife_draw_round_rect(core, cur_sx, sbar_y, sw, sbar_h, 4.0f, t_col, 0);
                    cur_sx += sw;
                }
            }
        }

        // 分类明细列表
        float row_y = ay + 48.0f;
        if (active_tags_count == 0) {
            rife_draw_text_font(core, cx + 14.0f, row_y, "今日暂无分类数据", col_txt_mute, 4);
        } else {
            for (int t = 0; t < state->storage.tag_count; t++) {
                if (an.tag_event_count[t] == 0) continue;
                uint32_t t_col = state->storage.tags[t].color_bar;

                // 标签彩点
                rife_draw_circle(core, cx + 18.0f, row_y + 7.0f, 4.0f, t_col, t_col);
                // 标签名
                rife_draw_text_font(core, cx + 28.0f, row_y, state->storage.tags[t].name, col_txt_main, 4);
                // 项数
                char cnt_b[16];
                snprintf(cnt_b, sizeof(cnt_b), "%d项", an.tag_event_count[t]);
                rife_draw_text_font(core, cx + 100.0f, row_y, cnt_b, col_txt_mute, 4);
                // 时长与占比
                char dur_b[32];
                snprintf(dur_b, sizeof(dur_b), "%.1fh (%.0f%%)", (float)an.tag_mins[t] / 60.0f, an.tag_pct[t]);
                rife_draw_text_font(core, cx + 180.0f, row_y, dur_b, col_txt_sub, 4);

                row_y += 26.0f;
            }
        }

        ay += c2_h + 10.0f;

        // --- 卡片 3: 昼夜节律时段分布 ---
        float c3_h = 136.0f;
        rife_draw_round_rect(core, cx, ay, cw, c3_h, 10.0f, col_card_bg, col_border);
        rife_draw_text_font(core, cx + 14.0f, ay + 10.0f, "昼夜节律负荷分布", col_txt_main, 5);

        static const char* phase_names[] = { "凌晨 (00-06)", "晨间 (06-12)", "午后 (12-18)", "晚间 (18-24)" };
        static const uint32_t phase_cols[] = { 0x64748BFF, 0x38BDF8FF, 0xF59E0BFF, 0x818CF8FF };

        float p_row_y = ay + 32.0f;
        float max_p_bar_w = 120.0f;

        for (int p = 0; p < 4; p++) {
            rife_draw_text_font(core, cx + 14.0f, p_row_y, phase_names[p], col_txt_sub, 4);

            // 柱状槽与填充条
            float bar_x = cx + 106.0f;
            rife_draw_round_rect(core, bar_x, p_row_y + 4.0f, max_p_bar_w, 6.0f, 3.0f, is_dark ? 0x2A204488 : 0xE2E8F0AA, 0);

            if (an.phase_mins[p] > 0) {
                // 满格按 6 小时 (360 分钟) 计算
                float p_pct = (float)an.phase_mins[p] / 360.0f;
                if (p_pct > 1.0f) p_pct = 1.0f;
                float fill_pw = max_p_bar_w * p_pct;
                if (fill_pw < 5.0f) fill_pw = 5.0f;
                rife_draw_round_rect(core, bar_x, p_row_y + 4.0f, fill_pw, 6.0f, 3.0f, phase_cols[p], 0);
            }

            char p_dur[24];
            snprintf(p_dur, sizeof(p_dur), "%.1fh", (float)an.phase_mins[p] / 60.0f);
            rife_draw_text_font(core, cx + 236.0f, p_row_y, p_dur, (p == an.peak_phase_idx && an.phase_mins[p] > 0) ? phase_cols[p] : col_txt_mute, 4);

            p_row_y += 24.0f;
        }

        ay += c3_h + 10.0f;

        // --- 卡片 4: 智能时间画像与洞察 ---
        float c4_h = 76.0f;
        rife_draw_round_rect(core, cx, ay, cw, c4_h, 10.0f, is_dark ? 0x221B3CEE : 0xEFF6FFCC, 0x818CF888);
        rife_draw_text_font(core, cx + 14.0f, ay + 10.0f, "智能时间洞察", is_dark ? 0xC7D2FEFF : 0x3B82F6FF, 5);

        if (an.total_events == 0) {
            rife_draw_text_font(core, cx + 14.0f, ay + 32.0f, "今日暂无日程安排，时间完全由你掌控。", col_txt_main, 4);
            rife_draw_text_font(core, cx + 14.0f, ay + 50.0f, "可在 Rtodo 中添加日程，数据实时同步。", col_txt_mute, 4);
        } else if (an.completed_events == an.total_events) {
            rife_draw_text_font(core, cx + 14.0f, ay + 32.0f, "太棒了！今日规划的全部日程已 100% 达成！", 0x10B981FF, 4);
            rife_draw_text_font(core, cx + 14.0f, ay + 50.0f, "执行力极佳，尽情享受美妙的闲暇时光吧。", col_txt_main, 4);
        } else if (an.peak_phase_idx == 2 && an.phase_mins[2] > 60) {
            rife_draw_text_font(core, cx + 14.0f, ay + 32.0f, "今日精力重心在午后时段，注意保持节奏。", col_txt_main, 4);
            rife_draw_text_font(core, cx + 14.0f, ay + 50.0f, "高强度任务间歇建议稍作小憩，效率更高。", col_txt_mute, 4);
        } else if (an.peak_phase_idx == 1 && an.phase_mins[1] > 60) {
            rife_draw_text_font(core, cx + 14.0f, ay + 32.0f, "今日主要任务集中在晨间，一日之计在于晨。", col_txt_main, 4);
            rife_draw_text_font(core, cx + 14.0f, ay + 50.0f, "早起攻坚核心事项，下午从容应对常规琐事。", col_txt_mute, 4);
        } else {
            rife_draw_text_font(core, cx + 14.0f, ay + 32.0f, "今日日程节奏分布均衡，专注与休息兼备。", col_txt_main, 4);
            char ins_sub[64];
            snprintf(ins_sub, sizeof(ins_sub), "目前尚有 %d 项日程待完成，保持优秀状态！", an.uncompleted_events);
            rife_draw_text_font(core, cx + 14.0f, ay + 50.0f, ins_sub, col_txt_sub, 4);
        }

        rife_pop_scissor(core);
    }
}

// -------------------------------------------------------------
// 插件声明契约 (Plugin Manifest Export)
// -------------------------------------------------------------

const RifePluginApp g_clock_plugin_app = {
    .app_id = CLOCK_APP_ID,
    .id = "rclock",
    .name_zh = "Rclock",
    .name_en = "Rclock",
    .glyph = "Rc",
    .color_top = 0x6366F1FF, // 暮光靛蓝
    .color_bot = 0x4338CAFF,
    .default_w = 880.0f,
    .default_h = 580.0f,
    .pin_to_dock = true,
    .create = clock_create,
    .destroy = clock_destroy,
    .update = clock_update,
    .render = clock_render
};

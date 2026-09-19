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

    int agenda_filter; // 0: 全部, 1: 未完成, 2: 已完成
    float list_scroll_y;

    // 新建日程模态框
    bool show_add_modal;
    char new_title[48];
    char new_loc[32];
    int new_tag_idx;
    int new_start_hour;
    int new_start_min;
    int new_end_hour;
    int new_end_min;
    int active_field; // 1: title, 2: loc
    float cursor_blink;
} ClockState;

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
    state->agenda_filter = 0;

    // 默认新建日程时间：10:00 至 11:00
    state->new_start_hour = 10;
    state->new_start_min = 0;
    state->new_end_hour = 11;
    state->new_end_min = 0;

    // 初次加载存储
    char path[MAX_PATH];
    rtodo_get_storage_path(path, sizeof(path));
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) {
        state->last_write_time = fad.ftLastWriteTime;
    }
    if (!rtodo_load_storage(&state->storage)) {
        // 初始默认标签
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
    (void)client_w;
    (void)client_h;

    // 1. 同步外部 rtodo_data.bin 数据更新
    clock_sync_storage_if_needed(state);

    state->cursor_blink += 1.0f / 60.0f;
    if (state->cursor_blink >= 1.0f) state->cursor_blink -= 1.0f;

    float mx = input->mouse_x;
    float my = input->mouse_y;

    // 2. 模态框打字输入处理
    if (state->show_add_modal) {
        if (input->text_input[0] != '\0') {
            char* target = (state->active_field == 1) ? state->new_title : state->new_loc;
            size_t max_l = (state->active_field == 1) ? sizeof(state->new_title) : sizeof(state->new_loc);
            size_t cur_len = strlen(target);
            size_t add_len = strlen(input->text_input);
            if (cur_len + add_len < max_l - 1) {
                strcat(target, input->text_input);
            }
        }
        if (input->key_pressed[VK_BACK]) {
            char* target = (state->active_field == 1) ? state->new_title : state->new_loc;
            size_t len = strlen(target);
            if (len > 0) {
                // 多字节 UTF-8 退格
                size_t i = len - 1;
                while (i > 0 && ((unsigned char)target[i] & 0xC0) == 0x80) {
                    i--;
                }
                target[i] = '\0';
            }
        }
        if (input->key_pressed[VK_ESCAPE]) {
            state->show_add_modal = false;
        }
        if (input->key_pressed[VK_TAB]) {
            state->active_field = (state->active_field == 1) ? 2 : 1;
        }
    }

    // 3. 悬停检测（表盘中心与半径）
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

            // 查找悬停的日程
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
        }
        else { // 12H 模式
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

    // 鼠标滚轮在右侧日程列表滚动
    if (fabsf(input->scroll_delta) > 0.01f) {
        if (mx >= client_w - 330.0f && mx <= client_w) {
            state->list_scroll_y += input->scroll_delta * 40.0f;
            if (state->list_scroll_y > 0.0f) state->list_scroll_y = 0.0f;
        }
    }

    // 4. 单击事件响应
    if (input->mouse_pressed[0]) {
        // A. 模态框处于开启状态
        if (state->show_add_modal) {
            float mw = 400.0f;
            float mh = 310.0f;
            float modal_x = (client_w - mw) * 0.5f;
            float modal_y = (client_h - mh) * 0.5f;

            // 标题输入框聚焦
            if (mx >= modal_x + 20.0f && mx <= modal_x + mw - 20.0f && my >= modal_y + 60.0f && my <= modal_y + 94.0f) {
                state->active_field = 1;
                return;
            }
            // 地点输入框聚焦
            if (mx >= modal_x + 20.0f && mx <= modal_x + mw - 20.0f && my >= modal_y + 104.0f && my <= modal_y + 138.0f) {
                state->active_field = 2;
                return;
            }
            // 标签选择
            for (int t = 0; t < state->storage.tag_count; t++) {
                float tag_btn_x = modal_x + 20.0f + (float)t * 62.0f;
                float tag_btn_y = modal_y + 150.0f;
                if (mx >= tag_btn_x && mx <= tag_btn_x + 56.0f && my >= tag_btn_y && my <= tag_btn_y + 26.0f) {
                    state->new_tag_idx = t;
                    return;
                }
            }
            // 时间微调 (开始时间 [-] [+]，结束时间 [-] [+])
            // 开始小时
            if (mx >= modal_x + 65.0f && mx <= modal_x + 85.0f && my >= modal_y + 190.0f && my <= modal_y + 214.0f) {
                state->new_start_hour = (state->new_start_hour + 23) % 24;
                return;
            }
            if (mx >= modal_x + 115.0f && mx <= modal_x + 135.0f && my >= modal_y + 190.0f && my <= modal_y + 214.0f) {
                state->new_start_hour = (state->new_start_hour + 1) % 24;
                return;
            }
            // 开始分钟
            if (mx >= modal_x + 145.0f && mx <= modal_x + 165.0f && my >= modal_y + 190.0f && my <= modal_y + 214.0f) {
                state->new_start_min = (state->new_start_min + 45) % 60;
                return;
            }
            if (mx >= modal_x + 195.0f && mx <= modal_x + 215.0f && my >= modal_y + 190.0f && my <= modal_y + 214.0f) {
                state->new_start_min = (state->new_start_min + 15) % 60;
                return;
            }
            // 快捷时长 [+30分] [+1h]
            if (mx >= modal_x + 235.0f && mx <= modal_x + 285.0f && my >= modal_y + 190.0f && my <= modal_y + 214.0f) {
                int total_m = state->new_start_hour * 60 + state->new_start_min + 30;
                state->new_end_hour = (total_m / 60) % 24;
                state->new_end_min = total_m % 60;
                return;
            }
            if (mx >= modal_x + 295.0f && mx <= modal_x + 345.0f && my >= modal_y + 190.0f && my <= modal_y + 214.0f) {
                int total_m = state->new_start_hour * 60 + state->new_start_min + 60;
                state->new_end_hour = (total_m / 60) % 24;
                state->new_end_min = total_m % 60;
                return;
            }
            // 取消按钮
            if (mx >= modal_x + mw - 180.0f && mx <= modal_x + mw - 100.0f && my >= modal_y + mh - 44.0f && my <= modal_y + mh - 14.0f) {
                state->show_add_modal = false;
                return;
            }
            // 确定创建按钮
            if (mx >= modal_x + mw - 90.0f && mx <= modal_x + mw - 20.0f && my >= modal_y + mh - 44.0f && my <= modal_y + mh - 14.0f) {
                if (state->storage.event_count < CAL_MAX_EVENTS) {
                    CalendarEvent* ne = &state->storage.events[state->storage.event_count++];
                    ne->id = (uint32_t)time(NULL) + state->storage.event_count;
                    if (state->new_title[0] != '\0') {
                        strncpy(ne->title, state->new_title, sizeof(ne->title) - 1);
                    } else {
                        snprintf(ne->title, sizeof(ne->title), "时钟日程");
                    }
                    strncpy(ne->location, state->new_loc, sizeof(ne->location) - 1);
                    ne->desc[0] = '\0';
                    ne->tag_idx = state->new_tag_idx;
                    ne->year = state->view_year;
                    ne->month = state->view_month;
                    ne->day = state->view_day;
                    ne->start_hour = state->new_start_hour;
                    ne->start_min = state->new_start_min;
                    ne->end_hour = state->new_end_hour;
                    ne->end_min = state->new_end_min;
                    ne->is_completed = false;

                    // 写入持久化并同步
                    rtodo_save_storage(&state->storage);
                }
                state->show_add_modal = false;
                return;
            }
            return;
        }

        // B. 顶栏交互
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
            float mode_btn_x = client_w - 230.0f;
            if (mx >= mode_btn_x && mx <= mode_btn_x + 96.0f && my >= 8.0f && my <= 34.0f) {
                state->dial_mode = (state->dial_mode == CLOCK_DIAL_24H) ? CLOCK_DIAL_12H : CLOCK_DIAL_24H;
                return;
            }

            // 新建日程按钮 [+ 新建日程]
            float add_btn_x = client_w - 124.0f;
            if (mx >= add_btn_x && mx <= add_btn_x + 110.0f && my >= 8.0f && my <= 34.0f) {
                state->show_add_modal = true;
                state->new_title[0] = '\0';
                state->new_loc[0] = '\0';
                state->active_field = 1;
                return;
            }
        }

        // C. 点击时钟圆盘空白处新建日程
        if (state->hovered_dial_mins >= 0 && state->hovered_event_id == 0) {
            state->show_add_modal = true;
            state->new_title[0] = '\0';
            state->new_loc[0] = '\0';
            state->active_field = 1;
            int snap_m = (state->hovered_dial_mins / 15) * 15;
            state->new_start_hour = snap_m / 60;
            state->new_start_min = snap_m % 60;
            int end_m = snap_m + 30;
            state->new_end_hour = (end_m / 60) % 24;
            state->new_end_min = end_m % 60;
            return;
        }

        // D. 右侧日程流点击完成切换
        if (mx >= client_w - 330.0f && mx <= client_w && my >= 44.0f) {
            float cur_cy = 44.0f + 40.0f + state->list_scroll_y;
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

    // 右侧制式切换胶囊 [24小时制]
    float mode_btn_x = client_x + client_w - 230.0f;
    rife_draw_round_rect(core, mode_btn_x, client_y + 8.0f, 96.0f, 26.0f, 13.0f, is_dark ? 0x312E8188 : 0xEEF2FFCC, 0x6366F1AA);
    rife_draw_text_rect(core, mode_btn_x, client_y + 8.0f, 96.0f, 26.0f, (state->dial_mode == CLOCK_DIAL_24H) ? "24小时制" : "12小时制", is_dark ? 0xC7D2FEFF : 0x4338CAFF, 3, 0);

    // [+ 新建日程] 按钮
    float add_btn_x = client_x + client_w - 124.0f;
    rife_draw_round_rect(core, add_btn_x, client_y + 8.0f, 110.0f, 26.0f, 6.0f, is_dark ? 0x4F46E5EE : 0x3B82F6EE, 0x818CF8FF);
    rife_draw_text_rect(core, add_btn_x, client_y + 8.0f, 110.0f, 26.0f, "+ 新建日程", 0xFFFFFFFF, 5, 0);

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

        bool is_major = (state->dial_mode == CLOCK_DIAL_24H) ? (h % 3 == 0) : (h % 3 == 0);
        float tick_len = is_major ? 10.0f : 5.0f;
        uint32_t tick_col = is_major ? (is_dark ? 0xA5B4FCFF : 0x475569FF) : (is_dark ? 0x47556988 : 0xCBD5E1AA);

        float x1 = dial_cx + s * (dial_r_out - tick_len);
        float y1 = dial_cy - c * (dial_r_out - tick_len);
        float x2 = dial_cx + s * dial_r_out;
        float y2 = dial_cy - c * dial_r_out;
        rife_draw_line(core, x1, y1, x2, y2, is_major ? 1.8f : 1.0f, tick_col);

        // 小时刻度文字 (00, 03, 06, 09, 12, 15, 18, 21 等)
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
            end_ang = start_ang + 2.5f; // 保证极短任务依然有清晰可视宽度
        }

        // 提取关联标签颜色
        uint32_t tag_color = 0x3370FFFF; // 默认蓝
        if (e->tag_idx >= 0 && e->tag_idx < state->storage.tag_count) {
            tag_color = state->storage.tags[e->tag_idx].color_bar;
        }

        bool is_hovered = (e->id == state->hovered_event_id);
        bool is_selected = (e->id == state->selected_event_id);

        uint32_t fill_col = tag_color;
        uint32_t border_col = tag_color;

        if (e->is_completed) {
            // 已完成以优雅的低饱和半透呈现
            fill_col = (tag_color & 0xFFFFFF00) | 0x44;
            border_col = (tag_color & 0xFFFFFF00) | 0x66;
        } else if (is_hovered || is_selected) {
            // 悬停/选中高亮呼吸发光
            fill_col = (tag_color & 0xFFFFFF00) | 0xCC;
            border_col = 0xFFFFFFFF; // 1px 晶莹高光白边
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

        // 激光指针红线
        float nx1 = dial_cx + s * (hub_r - 2.0f);
        float ny1 = dial_cy - c * (hub_r - 2.0f);
        float nx2 = dial_cx + s * (dial_r_out + 8.0f);
        float ny2 = dial_cy - c * (dial_r_out + 8.0f);
        rife_draw_line(core, nx1, ny1, nx2, ny2, 2.5f, 0xF43F5EFF); // 宝石红

        // 激光指针顶端发光晶圆点
        rife_draw_circle(core, nx2, ny2, 4.5f, 0xF43F5EFF, 0xFFFFFFFF);
    }

    // F. 中央多功能液晶控制核 (Central Glass Hub)
    rife_draw_circle(core, dial_cx, dial_cy, hub_r, is_dark ? 0x181128F0 : 0xFFFFFFF2, is_dark ? 0x4C376EE0 : 0xE2E8F0E0);
    rife_draw_circle(core, dial_cx, dial_cy, hub_r - 4.0f, is_dark ? 0x20173688 : 0xF8FAFC88, 0);

    // 数字时钟
    SYSTEMTIME st;
    GetLocalTime(&st);
    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    rife_draw_text_rect(core, dial_cx - 60.0f, dial_cy - 36.0f, 120.0f, 26.0f, time_str, col_txt_main, 1, 0);

    // 状态胶囊计算
    int cur_mins = st.wHour * 60 + st.wMinute;
    const CalendarEvent* cur_event = NULL;
    const CalendarEvent* next_event = NULL;
    int next_delta_mins = 9999;
    int today_total_mins = 0;
    int today_event_count = 0;

    for (int i = 0; i < state->storage.event_count; i++) {
        const CalendarEvent* e = &state->storage.events[i];
        if (e->year == state->view_year && e->month == state->view_month && e->day == state->view_day) {
            today_event_count++;
            int sm = e->start_hour * 60 + e->start_min;
            int em = e->end_hour * 60 + e->end_min;
            if (em <= sm) em = sm + 15;
            today_total_mins += (em - sm);

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
        // 当前正处于某日程中
        rife_draw_round_rect(core, dial_cx - 52.0f, dial_cy - 4.0f, 104.0f, 18.0f, 9.0f, 0x10B98128, 0x10B98188);
        rife_draw_text_rect(core, dial_cx - 52.0f, dial_cy - 4.0f, 104.0f, 18.0f, "正在进行", 0x10B981FF, 4, 0);

        char ev_buf[32];
        snprintf(ev_buf, sizeof(ev_buf), "%.16s", cur_event->title);
        rife_draw_text_rect(core, dial_cx - 65.0f, dial_cy + 18.0f, 130.0f, 18.0f, ev_buf, col_txt_main, 3, 0);
    } else if (is_today && next_event) {
        // 即将进行下一个日程
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
        // 统计信息
        char stats_buf[32];
        float hrs = (float)today_total_mins / 60.0f;
        snprintf(stats_buf, sizeof(stats_buf), "%d个日程 · %.1fh", today_event_count, hrs);
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
    // 3. 右侧今日日程动态流 (Agenda Stream: 330px)
    // ---------------------------------------------------------
    float list_x = client_x + client_w - 330.0f;
    float list_y = client_y + 44.0f;
    float list_w = 330.0f;
    float list_h = client_h - 44.0f;

    // 分割线与底板
    rife_draw_rect(core, list_x, list_y, 1.0f, list_h, col_border);
    rife_draw_rect(core, list_x + 1.0f, list_y, list_w - 1.0f, list_h, is_dark ? 0x16102655 : 0xF8FAFC44);

    // 列表标题栏
    rife_draw_text_font(core, list_x + 16.0f, list_y + 14.0f, "今日日程流", col_txt_main, 5);
    char cnt_str[16];
    snprintf(cnt_str, sizeof(cnt_str), "%d", today_event_count);
    rife_draw_round_rect(core, list_x + 94.0f, list_y + 12.0f, 22.0f, 18.0f, 9.0f, is_dark ? 0x312E81AA : 0xEEF2FFAA, 0);
    rife_draw_text_rect(core, list_x + 94.0f, list_y + 12.0f, 22.0f, 18.0f, cnt_str, is_dark ? 0xA5B4FCFF : 0x4F46E5FF, 4, 0);

    // 日程列表裁剪与呈现
    rife_push_scissor(core, list_x + 1.0f, list_y + 40.0f, list_w - 2.0f, list_h - 42.0f);

    float cur_card_y = list_y + 42.0f + state->list_scroll_y;
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

        // 左侧标签色彩条 (3px)
        uint32_t tag_color = 0x3370FFFF;
        if (e->tag_idx >= 0 && e->tag_idx < state->storage.tag_count) {
            tag_color = state->storage.tags[e->tag_idx].color_bar;
        }
        rife_draw_round_rect(core, card_x + 4.0f, cur_card_y + 8.0f, 4.0f, card_h - 16.0f, 2.0f, tag_color, 0);

        // 时间胶囊
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

        // 标题
        rife_draw_text_font(core, card_x + 16.0f, cur_card_y + 26.0f, e->title, e->is_completed ? col_txt_mute : col_txt_main, 5);

        // 地点
        if (e->location[0] != '\0') {
            rife_draw_text_font(core, card_x + 16.0f, cur_card_y + 42.0f, e->location, col_txt_mute, 4);
        }

        // 完成勾选圆圈
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
        // 空状态提示
        rife_draw_round_rect(core, list_x + 20.0f, list_y + 60.0f, list_w - 40.0f, 100.0f, 12.0f, is_dark ? 0x1E173088 : 0xFFFFFF88, col_border);
        rife_draw_text_rect(core, list_x + 20.0f, list_y + 85.0f, list_w - 40.0f, 20.0f, "今日暂无日程安排", col_txt_sub, 3, 0);
        rife_draw_text_rect(core, list_x + 20.0f, list_y + 110.0f, list_w - 40.0f, 20.0f, "点击时钟圆盘或右上角新建", col_txt_mute, 4, 0);
    }

    rife_pop_scissor(core);

    // ---------------------------------------------------------
    // 4. 新建日程模态框 (Quick Add Modal)
    // ---------------------------------------------------------
    if (state->show_add_modal) {
        // 半透暗色遮罩
        rife_draw_rect(core, client_x, client_y, client_w, client_h, 0x00000055);

        float mw = 400.0f;
        float mh = 300.0f;
        float mx = client_x + (client_w - mw) * 0.5f;
        float my = client_y + (client_h - mh) * 0.5f;

        rife_draw_round_rect(core, mx, my, mw, mh, 14.0f, is_dark ? 0x1E1432F8 : 0xFFFFFFF8, 0x818CF8AA);
        rife_draw_text_font(core, mx + 20.0f, my + 18.0f, "新建时钟日程", col_txt_main, 1);

        // 标题输入框
        rife_draw_round_rect(core, mx + 20.0f, my + 54.0f, mw - 40.0f, 32.0f, 6.0f,
                             is_dark ? 0x2A1C44AA : 0xF1F5F9CC, (state->active_field == 1) ? 0x6366F1FF : col_border);
        if (state->new_title[0] != '\0') {
            rife_draw_text_font(core, mx + 28.0f, my + 62.0f, state->new_title, col_txt_main, 3);
        } else {
            rife_draw_text_font(core, mx + 28.0f, my + 62.0f, "输入日程标题...", col_txt_mute, 3);
        }

        // 地点输入框
        rife_draw_round_rect(core, mx + 20.0f, my + 94.0f, mw - 40.0f, 32.0f, 6.0f,
                             is_dark ? 0x2A1C44AA : 0xF1F5F9CC, (state->active_field == 2) ? 0x6366F1FF : col_border);
        if (state->new_loc[0] != '\0') {
            rife_draw_text_font(core, mx + 28.0f, my + 102.0f, state->new_loc, col_txt_main, 3);
        } else {
            rife_draw_text_font(core, mx + 28.0f, my + 102.0f, "输入地点 (可选)...", col_txt_mute, 3);
        }

        // 分类标签选择
        rife_draw_text_font(core, mx + 20.0f, my + 138.0f, "标签:", col_txt_sub, 3);
        for (int t = 0; t < state->storage.tag_count; t++) {
            float t_btn_x = mx + 60.0f + (float)t * 62.0f;
            bool is_sel = (state->new_tag_idx == t);
            uint32_t t_col = state->storage.tags[t].color_bar;
            rife_draw_round_rect(core, t_btn_x, my + 134.0f, 56.0f, 24.0f, 6.0f,
                                 is_sel ? (t_col | 0x33) : (is_dark ? 0x2A1C4488 : 0xF1F5F9AA),
                                 is_sel ? t_col : col_border);
            rife_draw_text_rect(core, t_btn_x, my + 134.0f, 56.0f, 24.0f, state->storage.tags[t].name, is_sel ? t_col : col_txt_sub, 3, 0);
        }

        // 时间调整
        char time_range_buf[64];
        snprintf(time_range_buf, sizeof(time_range_buf), "时间: %02d:%02d 至 %02d:%02d",
                 state->new_start_hour, state->new_start_min, state->new_end_hour, state->new_end_min);
        rife_draw_text_font(core, mx + 20.0f, my + 176.0f, time_range_buf, 0x38BDF8FF, 5);

        // 快捷时长 [+30分] [+1h]
        rife_draw_round_rect(core, mx + 220.0f, my + 172.0f, 64.0f, 24.0f, 6.0f, is_dark ? 0x312E8188 : 0xEEF2FFAA, 0x6366F1AA);
        rife_draw_text_rect(core, mx + 220.0f, my + 172.0f, 64.0f, 24.0f, "+30分", is_dark ? 0xC7D2FEFF : 0x4338CAFF, 4, 0);

        rife_draw_round_rect(core, mx + 294.0f, my + 172.0f, 64.0f, 24.0f, 6.0f, is_dark ? 0x312E8188 : 0xEEF2FFAA, 0x6366F1AA);
        rife_draw_text_rect(core, mx + 294.0f, my + 172.0f, 64.0f, 24.0f, "+1小时", is_dark ? 0xC7D2FEFF : 0x4338CAFF, 4, 0);

        // 底部操作按钮 [取消] [创建日程]
        rife_draw_round_rect(core, mx + mw - 180.0f, my + mh - 44.0f, 76.0f, 30.0f, 6.0f, is_dark ? 0x2A1C44AA : 0xF1F5F9CC, col_border);
        rife_draw_text_rect(core, mx + mw - 180.0f, my + mh - 44.0f, 76.0f, 30.0f, "取消", col_txt_sub, 3, 0);

        rife_draw_round_rect(core, mx + mw - 94.0f, my + mh - 44.0f, 84.0f, 30.0f, 6.0f, 0x6366F1FF, 0x818CF8FF);
        rife_draw_text_rect(core, mx + mw - 94.0f, my + mh - 44.0f, 84.0f, 30.0f, "创建日程", 0xFFFFFFFF, 5, 0);
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

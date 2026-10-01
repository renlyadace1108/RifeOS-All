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
    POMO_MODE_FOCUS = 0, // 专注 (25m)
    POMO_MODE_SHORT = 1, // 短休 (5m)
    POMO_MODE_LONG  = 2  // 长休 (15m)
} PomoMode;

typedef enum {
    CLOCK_DIAL_24H = 0,
    CLOCK_DIAL_12H = 1
} ClockDialMode;

typedef struct {
    // 页面主视图: 0: 🍅 番茄专注 (默认核心), 1: 🕒 24H 昼夜表盘
    int view_mode;

    // 24H 昼夜表盘视图参数
    int view_year;
    int view_month;
    int view_day;
    ClockDialMode dial_mode;

    // 滴答数据持久化与自动同步
    RtodoStorage storage;
    FILETIME last_write_time;
    uint64_t last_check_ns;

    // 番茄专注核心状态机
    PomoMode pomo_mode;             // 0: 专注, 1: 短休, 2: 长休
    bool pomo_running;              // 是否正在倒计时
    int pomo_remaining_sec;         // 剩余秒数 (默认 1500)
    int pomo_total_sec;             // 本轮总秒数 (默认 1500)
    uint64_t pomo_last_tick_ns;     // 纳秒高精度计时标尺
    int pomo_custom_focus_mins;     // 专注时长设定 (默认 25 分钟)
    int pomo_completed_today;       // 今日已达成番茄数
    char pomo_status_msg[64];       // 沉浸激励文本

    // 当前绑定的专注待办任务
    uint32_t active_focus_task_id;
    char active_focus_title[64];

    // 24H 表盘交互
    uint32_t hovered_event_id;
    int hovered_dial_mins;
    uint32_t selected_event_id;

    // 右侧多功能面板
    int right_panel_tab;            // 0: 专注待办, 1: 专注数据统计
    int agenda_filter;              // 0: 全部, 1: 未完成, 2: 已完成
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
            // 联动外部设置的专注任务
            if (state->storage.active_focus_task_id > 0) {
                state->active_focus_task_id = state->storage.active_focus_task_id;
                snprintf(state->active_focus_title, sizeof(state->active_focus_title), "%s", state->storage.active_focus_title);
            }
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

    state->view_mode = 0; // 默认：番茄专注
    state->pomo_mode = POMO_MODE_FOCUS;
    state->pomo_custom_focus_mins = 25;
    state->pomo_total_sec = 25 * 60;
    state->pomo_remaining_sec = state->pomo_total_sec;
    state->pomo_running = false;
    snprintf(state->pomo_status_msg, sizeof(state->pomo_status_msg), "保持纯粹心流 · 每一秒专注都在塑造卓越");

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
    if (rtodo_load_storage(&state->storage)) {
        if (state->storage.active_focus_task_id > 0) {
            state->active_focus_task_id = state->storage.active_focus_task_id;
            snprintf(state->active_focus_title, sizeof(state->active_focus_title), "%s", state->storage.active_focus_title);
        }
    } else {
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

    // 1. 同步外部 rtodo_data.bin 数据更新
    clock_sync_storage_if_needed(state);

    // 2. 高精度纳秒级番茄倒计时驱动 (Zero Lag & Drift-Free)
    if (state->pomo_running) {
        uint64_t now_ns = rife_time_now_ns();
        if (state->pomo_last_tick_ns == 0) {
            state->pomo_last_tick_ns = now_ns;
        }
        uint64_t elapsed_ns = now_ns - state->pomo_last_tick_ns;
        if (elapsed_ns >= 1000000000ULL) { // 经过 1 秒以上
            int secs = (int)(elapsed_ns / 1000000000ULL);
            state->pomo_last_tick_ns += (uint64_t)secs * 1000000000ULL;
            state->pomo_remaining_sec -= secs;

            if (state->pomo_remaining_sec <= 0) {
                state->pomo_remaining_sec = 0;
                state->pomo_running = false;

                if (state->pomo_mode == POMO_MODE_FOCUS) {
                    state->pomo_completed_today++;
                    state->storage.total_pomodoros++;
                    state->storage.total_focus_mins += (state->pomo_total_sec / 60);

                    // 增加已关联任务的番茄数
                    if (state->active_focus_task_id > 0) {
                        for (int i = 0; i < state->storage.event_count; i++) {
                            if (state->storage.events[i].id == state->active_focus_task_id) {
                                state->storage.events[i].pomodoro_count++;
                                break;
                            }
                        }
                    }
                    rtodo_save_storage(&state->storage);

                    // 自动切换为 5 分钟短休
                    state->pomo_mode = POMO_MODE_SHORT;
                    state->pomo_total_sec = 5 * 60;
                    state->pomo_remaining_sec = state->pomo_total_sec;
                    snprintf(state->pomo_status_msg, sizeof(state->pomo_status_msg), "太棒了！已完成 1 个番茄，休息 5 分钟吧☕");
                } else {
                    // 休息结束，自动切回专注模式
                    state->pomo_mode = POMO_MODE_FOCUS;
                    state->pomo_total_sec = state->pomo_custom_focus_mins * 60;
                    state->pomo_remaining_sec = state->pomo_total_sec;
                    snprintf(state->pomo_status_msg, sizeof(state->pomo_status_msg), "休息结束，准备好开始下一个心流番茄了吗？💪");
                }
            }
            rife_request_redraw(core);
        }
    }

    // 3. 键盘空格键触发 开始/暂停，R 键重置
    if (input->key_pressed[VK_SPACE]) {
        state->pomo_running = !state->pomo_running;
        if (state->pomo_running) {
            state->pomo_last_tick_ns = rife_time_now_ns();
            snprintf(state->pomo_status_msg, sizeof(state->pomo_status_msg), "专注中 · 排除干扰，保持纯粹心流");
        } else {
            snprintf(state->pomo_status_msg, sizeof(state->pomo_status_msg), "已暂停 · 点击继续或重置");
        }
        rife_request_redraw(core);
        return;
    }
    if (input->key_pressed['R']) {
        state->pomo_running = false;
        state->pomo_remaining_sec = state->pomo_total_sec;
        snprintf(state->pomo_status_msg, sizeof(state->pomo_status_msg), "已重置计时");
        rife_request_redraw(core);
        return;
    }

    float mx = input->mouse_x;
    float my = input->mouse_y;

    // 4. 滚轮在右侧面板滚动
    if (fabsf(input->scroll_delta) > 0.01f) {
        if (mx >= client_w - 340.0f && mx <= client_w) {
            if (state->right_panel_tab == 0) {
                state->list_scroll_y += input->scroll_delta * 40.0f;
                if (state->list_scroll_y > 0.0f) state->list_scroll_y = 0.0f;
            } else {
                state->analytics_scroll_y += input->scroll_delta * 40.0f;
                if (state->analytics_scroll_y > 0.0f) state->analytics_scroll_y = 0.0f;
            }
            rife_request_redraw(core);
        }
    }

    // 5. 鼠标单击事件响应
    if (input->mouse_pressed[0]) {
        // A. 顶栏交互 (44px)
        if (my >= 0.0f && my <= 44.0f) {
            // 模式切换胶囊: 专注(25m), 短休(5m), 长休(15m)
            float tabs_w = 270.0f;
            float tabs_x = (client_w - 340.0f - tabs_w) * 0.5f;
            if (tabs_x < 180.0f) tabs_x = 180.0f;
            float tab_item_w = tabs_w / 3.0f;

            if (mx >= tabs_x && mx <= tabs_x + tabs_w && my >= 8.0f && my <= 36.0f) {
                int clicked_tab = (int)((mx - tabs_x) / tab_item_w);
                if (clicked_tab == 0) {
                    state->pomo_mode = POMO_MODE_FOCUS;
                    state->pomo_total_sec = state->pomo_custom_focus_mins * 60;
                    state->pomo_remaining_sec = state->pomo_total_sec;
                    state->pomo_running = false;
                    snprintf(state->pomo_status_msg, sizeof(state->pomo_status_msg), "已切换至专注模式 (25m)");
                } else if (clicked_tab == 1) {
                    state->pomo_mode = POMO_MODE_SHORT;
                    state->pomo_total_sec = 5 * 60;
                    state->pomo_remaining_sec = state->pomo_total_sec;
                    state->pomo_running = false;
                    snprintf(state->pomo_status_msg, sizeof(state->pomo_status_msg), "已切换至短休模式 (5m)");
                } else if (clicked_tab == 2) {
                    state->pomo_mode = POMO_MODE_LONG;
                    state->pomo_total_sec = 15 * 60;
                    state->pomo_remaining_sec = state->pomo_total_sec;
                    state->pomo_running = false;
                    snprintf(state->pomo_status_msg, sizeof(state->pomo_status_msg), "已切换至长休模式 (15m)");
                }
                rife_request_redraw(core);
                return;
            }

            // 右上角视图切换: [ 🍅 番茄专注 ] / [ 🕒 24H表盘 ]
            float view_sw_x = client_w - 340.0f - 110.0f;
            if (mx >= view_sw_x && mx <= view_sw_x + 100.0f && my >= 8.0f && my <= 36.0f) {
                state->view_mode = (state->view_mode == 0) ? 1 : 0;
                rife_request_redraw(core);
                return;
            }

            // 24H 模式下的前一天 / 今天 / 后一天
            if (state->view_mode == 1) {
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
                    rife_request_redraw(core);
                    return;
                }
                if (mx >= 194.0f && mx <= 254.0f && my >= 8.0f && my <= 34.0f) {
                    clock_get_today(&state->view_year, &state->view_month, &state->view_day);
                    rife_request_redraw(core);
                    return;
                }
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
                    rife_request_redraw(core);
                    return;
                }
            }
        }

        // B. 右侧面板 Tab 切换: [ 📋 专注待办 ] / [ 📊 专注统计 ]
        float list_x = client_w - 340.0f;
        if (mx >= list_x && mx <= client_w && my >= 44.0f && my <= 82.0f) {
            float tab_w = (340.0f - 28.0f - 8.0f) * 0.5f;
            float tab0_x = list_x + 14.0f;
            float tab1_x = tab0_x + tab_w + 8.0f;
            if (mx >= tab0_x && mx <= tab0_x + tab_w) {
                state->right_panel_tab = 0;
                rife_request_redraw(core);
                return;
            }
            if (mx >= tab1_x && mx <= tab1_x + tab_w) {
                state->right_panel_tab = 1;
                rife_request_redraw(core);
                return;
            }
        }

        // C. 右侧面板待办列表交互
        if (state->right_panel_tab == 0 && mx >= list_x && mx <= client_w && my >= 84.0f) {
            float card_y = 84.0f + 10.0f + state->list_scroll_y;
            float card_w = 340.0f - 28.0f;
            float card_x = list_x + 14.0f;
            for (int i = 0; i < state->storage.event_count; i++) {
                CalendarEvent* e = &state->storage.events[i];
                float chk_x = card_x + 20.0f;
                float chk_cy = card_y + 27.0f;
                float dx = mx - chk_x, dy = my - chk_cy;

                // 点击勾选圆圈
                if (dx * dx + dy * dy <= 14.0f * 14.0f) {
                    e->is_completed = !e->is_completed;
                    rtodo_save_storage(&state->storage);
                    rife_request_redraw(core);
                    return;
                }

                // 点击卡片设为专注目标
                if (mx >= card_x && mx <= card_x + card_w && my >= card_y && my <= card_y + 54.0f) {
                    state->active_focus_task_id = e->id;
                    snprintf(state->active_focus_title, sizeof(state->active_focus_title), "%s", e->title);
                    state->storage.active_focus_task_id = e->id;
                    snprintf(state->storage.active_focus_title, sizeof(state->storage.active_focus_title), "%s", e->title);
                    rtodo_save_storage(&state->storage);
                    rife_request_redraw(core);
                    return;
                }

                card_y += 62.0f;
            }
        }

        // D. 中央区域番茄交互
        if (state->view_mode == 0) {
            float area_w = client_w - 340.0f;
            float dial_cx = area_w * 0.5f;
            float dial_cy = 44.0f + (client_h - 44.0f) * 0.38f;
            float dial_r_out = (client_h - 44.0f) * 0.30f;
            if (dial_r_out > 175.0f) dial_r_out = 175.0f;
            if (dial_r_out < 125.0f) dial_r_out = 125.0f;

            // 1. 目标任务清除按钮 [×]
            if (state->active_focus_task_id > 0) {
                float pill_y = dial_cy + 34.0f;
                float clear_btn_x = dial_cx + 70.0f;
                if (mx >= clear_btn_x - 14.0f && mx <= clear_btn_x + 14.0f && my >= pill_y - 10.0f && my <= pill_y + 10.0f) {
                    state->active_focus_task_id = 0;
                    state->active_focus_title[0] = '\0';
                    state->storage.active_focus_task_id = 0;
                    state->storage.active_focus_title[0] = '\0';
                    rtodo_save_storage(&state->storage);
                    rife_request_redraw(core);
                    return;
                }
            }

            // 2. 主控制按钮行: [ ▶ 开始专注 / ⏸ 暂停 ] [ ↺ 重置 ] [ ✓ 完成 ]
            float ctrl_y = dial_cy + dial_r_out + 30.0f;
            if (my >= ctrl_y && my <= ctrl_y + 44.0f) {
                if (state->pomo_running) {
                    // 暂停按钮
                    float pause_w = 120.0f;
                    float pause_x = dial_cx - 120.0f;
                    if (mx >= pause_x && mx <= pause_x + pause_w) {
                        state->pomo_running = false;
                        snprintf(state->pomo_status_msg, sizeof(state->pomo_status_msg), "已暂停 · 点击继续");
                        rife_request_redraw(core);
                        return;
                    }

                    // 提前完成本轮按钮
                    float fin_w = 110.0f;
                    float fin_x = dial_cx + 10.0f;
                    if (mx >= fin_x && mx <= fin_x + fin_w) {
                        state->pomo_remaining_sec = 0;
                        state->pomo_running = false;
                        if (state->pomo_mode == POMO_MODE_FOCUS) {
                            state->pomo_completed_today++;
                            state->storage.total_pomodoros++;
                            state->storage.total_focus_mins += (state->pomo_total_sec / 60);
                            if (state->active_focus_task_id > 0) {
                                for (int i = 0; i < state->storage.event_count; i++) {
                                    if (state->storage.events[i].id == state->active_focus_task_id) {
                                        state->storage.events[i].pomodoro_count++;
                                        break;
                                    }
                                }
                            }
                            rtodo_save_storage(&state->storage);
                            state->pomo_mode = POMO_MODE_SHORT;
                            state->pomo_total_sec = 5 * 60;
                            state->pomo_remaining_sec = state->pomo_total_sec;
                            snprintf(state->pomo_status_msg, sizeof(state->pomo_status_msg), "提前完成！已记录 1 个番茄，休息一下吧☕");
                        }
                        rife_request_redraw(core);
                        return;
                    }
                } else {
                    // 开始专注按钮
                    float start_w = 150.0f;
                    float start_x = dial_cx - start_w * 0.5f - 40.0f;
                    if (mx >= start_x && mx <= start_x + start_w) {
                        state->pomo_running = true;
                        state->pomo_last_tick_ns = rife_time_now_ns();
                        snprintf(state->pomo_status_msg, sizeof(state->pomo_status_msg), "专注中 · 保持心流");
                        rife_request_redraw(core);
                        return;
                    }

                    // 重置按钮
                    float rst_w = 70.0f;
                    float rst_x = start_x + start_w + 12.0f;
                    if (mx >= rst_x && mx <= rst_x + rst_w) {
                        state->pomo_running = false;
                        state->pomo_remaining_sec = state->pomo_total_sec;
                        snprintf(state->pomo_status_msg, sizeof(state->pomo_status_msg), "已重置计时");
                        rife_request_redraw(core);
                        return;
                    }
                }
            }

            // 3. 快速时长设定胶囊: 15m, 25m, 35m, 45m, 60m
            float pill_row_y = ctrl_y + 54.0f;
            if (my >= pill_row_y && my <= pill_row_y + 26.0f) {
                int presets[5] = { 15, 25, 35, 45, 60 };
                float pw = 52.0f, p_gap = 8.0f;
                float px0 = dial_cx - (pw * 5.0f + p_gap * 4.0f) * 0.5f;
                for (int p = 0; p < 5; p++) {
                    float px = px0 + (float)p * (pw + p_gap);
                    if (mx >= px && mx <= px + pw) {
                        state->pomo_custom_focus_mins = presets[p];
                        if (state->pomo_mode == POMO_MODE_FOCUS) {
                            state->pomo_total_sec = presets[p] * 60;
                            state->pomo_remaining_sec = state->pomo_total_sec;
                            state->pomo_running = false;
                        }
                        rife_request_redraw(core);
                        return;
                    }
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
    bool is_zh   = (cfg->language == LANG_ZH_CN);

    uint32_t col_txt_main = is_dark ? 0xF8FAFCFF : 0x0F172AFF;
    uint32_t col_txt_sub  = is_dark ? 0xCBD5E1FF : 0x334155FF;
    uint32_t col_txt_mute = is_dark ? 0x94A3B8FF : 0x64748BFF;
    uint32_t col_border   = is_dark ? 0x1E202BFF : 0xE2E8F0FF;
    uint32_t col_card_bg  = is_dark ? 0x14151DFF : 0xFFFFFFFF;
    uint32_t col_blue     = is_dark ? 0x6366F1FF : 0x4F46E5FF;
    uint32_t col_emerald  = 0x10B981FF;
    uint32_t col_cyan     = 0x0EA5E9FF;

    // ---------------------------------------------------------
    // 1. 顶栏 (Top Bar: 44px)
    // ---------------------------------------------------------
    rife_draw_rect(core, client_x, client_y, client_w, 44.0f, is_dark ? 0x0E0F14FF : 0xFFFFFFFF);
    rife_draw_rect(core, client_x, client_y + 43.0f, client_w, 1.0f, col_border);

    // 应用标题与图标
    rife_draw_round_rect(core, client_x + 16.0f, client_y + 10.0f, 24.0f, 24.0f, 6.0f, col_blue, 0);
    rife_draw_text_rect(core, client_x + 16.0f, client_y + 10.0f, 24.0f, 24.0f, "🍅", 0xFFFFFFFF, 4, 0);
    rife_draw_text_font(core, client_x + 48.0f, client_y + 13.0f, is_zh ? "滴答番茄专注" : "Pomodoro Focus", col_txt_main, 1);

    // 模式切换胶囊: 专注(25m), 短休(5m), 长休(15m)
    float area_left_w = client_w - 340.0f;
    float tabs_w = 270.0f;
    float tabs_x = client_x + (area_left_w - tabs_w) * 0.5f;
    if (tabs_x < client_x + 180.0f) tabs_x = client_x + 180.0f;
    float tab_item_w = tabs_w / 3.0f;

    const char* mode_names_zh[3] = { "🍅 专注", "☕ 短休", "🌴 长休" };
    const char* mode_names_en[3] = { "Focus", "Short", "Long" };

    for (int m = 0; m < 3; m++) {
        float tx = tabs_x + (float)m * tab_item_w;
        bool is_cur = (state->pomo_mode == (PomoMode)m);
        uint32_t bg_c = is_cur ? (is_dark ? 0x1E202BFF : 0xEEF2FFFF) : (is_dark ? 0x12131AFF : 0xF8FAFCFF);
        uint32_t bd_c = is_cur ? col_blue : col_border;
        uint32_t fg_c = is_cur ? col_blue : col_txt_sub;
        rife_draw_round_rect(core, tx, client_y + 8.0f, tab_item_w - 4.0f, 28.0f, 6.0f, bg_c, bd_c);
        rife_draw_text_rect(core, tx, client_y + 8.0f, tab_item_w - 4.0f, 28.0f, is_zh ? mode_names_zh[m] : mode_names_en[m], fg_c, 4, 0);
    }

    // 右上角视图切换胶囊: [ 🍅 番茄专注 ] / [ 🕒 24H表盘 ]
    float view_sw_x = client_x + area_left_w - 110.0f;
    rife_draw_round_rect(core, view_sw_x, client_y + 9.0f, 100.0f, 26.0f, 5.0f, is_dark ? 0x1A1C28FF : 0xEEF2FFFF, is_dark ? 0x2A2E42FF : 0xC7D2FEFF);
    rife_draw_text_rect(core, view_sw_x, client_y + 9.0f, 100.0f, 26.0f, (state->view_mode == 0) ? "🍅 番茄模式" : "🕒 24H表盘", col_blue, 4, 0);

    // =========================================================
    // 2. 中央番茄时钟区 (Pomodoro Center Workspace)
    // =========================================================
    if (state->view_mode == 0) {
        float dial_cx = client_x + area_left_w * 0.5f;
        float dial_cy = client_y + 44.0f + (client_h - 44.0f) * 0.38f;
        float dial_r_out = (client_h - 44.0f) * 0.30f;
        if (dial_r_out > 175.0f) dial_r_out = 175.0f;
        if (dial_r_out < 125.0f) dial_r_out = 125.0f;
        float dial_r_in = dial_r_out - 16.0f;

        // A. 优雅圆形环形轨道底色 (Track Background)
        rife_draw_arc_sector(core, dial_cx, dial_cy, dial_r_in, dial_r_out, 0.0f, 360.0f,
                             is_dark ? 0x1A1C27FF : 0xEEF2F6FF, 0);

        // B. 动态彩色进度弧面 (Progress Countdown Arc)
        float progress = 1.0f;
        if (state->pomo_total_sec > 0) {
            progress = (float)state->pomo_remaining_sec / (float)state->pomo_total_sec;
            if (progress < 0.0f) progress = 0.0f;
            if (progress > 1.0f) progress = 1.0f;
        }
        float span = 360.0f * progress;
        if (span > 0.5f) {
            uint32_t arc_col = col_blue;
            if (state->pomo_mode == POMO_MODE_SHORT) arc_col = col_emerald;
            else if (state->pomo_mode == POMO_MODE_LONG) arc_col = col_cyan;

            rife_draw_arc_sector(core, dial_cx, dial_cy, dial_r_in, dial_r_out, 0.0f, span, arc_col, 0);

            // 弧面头部高亮端点指示晶核
            float rad = span * 0.0174532925f;
            float r_mid = (dial_r_in + dial_r_out) * 0.5f;
            float tip_x = dial_cx + sinf(rad) * r_mid;
            float tip_y = dial_cy - cosf(rad) * r_mid;
            rife_draw_circle(core, tip_x, tip_y, 7.0f, 0xFFFFFFFF, arc_col);
        }

        // C. 中心发光核晶 (Center Focus Hub)
        float hub_r = dial_r_in - 12.0f;
        rife_draw_circle(core, dial_cx, dial_cy, hub_r, is_dark ? 0x12131BFF : 0xFFFFFFFF, col_border);

        // 状态徽标 (Status Pill)
        float st_w = 110.0f, st_h = 22.0f;
        float st_y = dial_cy - 48.0f;
        if (state->pomo_running) {
            rife_draw_round_rect(core, dial_cx - st_w * 0.5f, st_y, st_w, st_h, 11.0f, 0x10B98122, 0x10B98166);
            rife_draw_text_rect(core, dial_cx - st_w * 0.5f, st_y, st_w, st_h, "● 专注中 · 心流", 0x10B981FF, 4, 0);
        } else if (state->pomo_remaining_sec < state->pomo_total_sec) {
            rife_draw_round_rect(core, dial_cx - st_w * 0.5f, st_y, st_w, st_h, 11.0f, 0xF59E0B22, 0xF59E0B66);
            rife_draw_text_rect(core, dial_cx - st_w * 0.5f, st_y, st_w, st_h, "○ 暂停中 · 待续", 0xF59E0BFF, 4, 0);
        } else {
            rife_draw_round_rect(core, dial_cx - st_w * 0.5f, st_y, st_w, st_h, 11.0f, is_dark ? 0x1E202B88 : 0xEEF2FF88, col_border);
            rife_draw_text_rect(core, dial_cx - st_w * 0.5f, st_y, st_w, st_h, "✓ 准备就绪 · 开始", col_txt_mute, 4, 0);
        }

        // 巨大数字倒计时文本 (Huge Timer Text: MM:SS)
        int disp_m = state->pomo_remaining_sec / 60;
        int disp_s = state->pomo_remaining_sec % 60;
        char cd_str[32];
        snprintf(cd_str, sizeof(cd_str), "%02d:%02d", disp_m, disp_s);
        rife_draw_text_rect(core, dial_cx - 90.0f, dial_cy - 20.0f, 180.0f, 38.0f, cd_str, col_txt_main, 6, 0);

        // 绑定的当前专注目标胶囊
        float pill_y = dial_cy + 26.0f;
        if (state->active_focus_task_id > 0) {
            char task_badge[80];
            snprintf(task_badge, sizeof(task_badge), "🎯 %.18s  ×", state->active_focus_title);
            rife_draw_round_rect(core, dial_cx - 80.0f, pill_y, 160.0f, 22.0f, 11.0f, is_dark ? 0x1E1F30FF : 0xEEF2FFFF, col_blue);
            rife_draw_text_rect(core, dial_cx - 80.0f, pill_y, 160.0f, 22.0f, task_badge, col_blue, 4, 0);
        } else {
            rife_draw_text_rect(core, dial_cx - 80.0f, pill_y, 160.0f, 20.0f, is_zh ? "自由专注 · 深度思考" : "Free Focus", col_txt_mute, 4, 0);
        }

        // D. 激励状态说明文案
        float msg_y = dial_cy + dial_r_out + 12.0f;
        rife_draw_text_rect(core, dial_cx - 160.0f, msg_y, 320.0f, 18.0f, state->pomo_status_msg, col_txt_sub, 4, 0);

        // E. 核心交互按钮行 (Control CTA Buttons)
        float ctrl_y = dial_cy + dial_r_out + 36.0f;
        if (state->pomo_running) {
            // 暂停按钮
            float pause_w = 120.0f;
            float pause_x = dial_cx - 120.0f;
            rife_draw_round_rect(core, pause_x, ctrl_y, pause_w, 40.0f, 8.0f, 0xF59E0BFF, 0);
            rife_draw_text_rect(core, pause_x, ctrl_y, pause_w, 40.0f, is_zh ? "⏸ 暂停" : "Pause", 0xFFFFFFFF, 5, 0);

            // 提前完成本轮按钮
            float fin_w = 110.0f;
            float fin_x = dial_cx + 10.0f;
            rife_draw_round_rect(core, fin_x, ctrl_y, fin_w, 40.0f, 8.0f, col_emerald, 0);
            rife_draw_text_rect(core, fin_x, ctrl_y, fin_w, 40.0f, is_zh ? "✓ 完成本轮" : "Done", 0xFFFFFFFF, 5, 0);
        } else {
            // 开始专注按钮
            float start_w = 150.0f;
            float start_x = dial_cx - start_w * 0.5f - 40.0f;
            rife_draw_round_rect(core, start_x, ctrl_y, start_w, 40.0f, 8.0f, col_blue, 0);
            rife_draw_text_rect(core, start_x, ctrl_y, start_w, 40.0f, is_zh ? "▶ 开始专注" : "Start Focus", 0xFFFFFFFF, 5, 0);

            // 重置按钮
            float rst_w = 70.0f;
            float rst_x = start_x + start_w + 12.0f;
            rife_draw_round_rect(core, rst_x, ctrl_y, rst_w, 40.0f, 8.0f, is_dark ? 0x1E202BFF : 0xFFFFFFFF, col_border);
            rife_draw_text_rect(core, rst_x, ctrl_y, rst_w, 40.0f, is_zh ? "↺ 重置" : "Reset", col_txt_sub, 5, 0);
        }

        // F. 快速预设时长药丸胶囊: 15m, 25m, 35m, 45m, 60m
        float pill_row_y = ctrl_y + 54.0f;
        int presets[5] = { 15, 25, 35, 45, 60 };
        const char* p_labels[5] = { "15m", "25m", "35m", "45m", "60m" };
        float pw = 52.0f, p_gap = 8.0f;
        float px0 = dial_cx - (pw * 5.0f + p_gap * 4.0f) * 0.5f;

        for (int p = 0; p < 5; p++) {
            float px = px0 + (float)p * (pw + p_gap);
            bool is_sel = (state->pomo_custom_focus_mins == presets[p]);
            uint32_t p_bg = is_sel ? (is_dark ? 0x1E202BFF : 0xEEF2FFFF) : (is_dark ? 0x14151DFF : 0xFFFFFFFF);
            uint32_t p_bd = is_sel ? col_blue : col_border;
            rife_draw_round_rect(core, px, pill_row_y, pw, 26.0f, 5.0f, p_bg, p_bd);
            rife_draw_text_rect(core, px, pill_row_y, pw, 26.0f, p_labels[p], is_sel ? col_blue : col_txt_mute, 4, 0);
        }
    } else {
        // =========================================================
        // 24H 昼夜时钟表盘视图 (24-Hour Chronometer Dial)
        // =========================================================
        float dial_cx = client_x + area_left_w * 0.5f;
        float dial_cy = client_y + 44.0f + (client_h - 44.0f) * 0.5f;
        float dial_r_out = (client_h - 44.0f) * 0.40f;
        if (dial_r_out > 200.0f) dial_r_out = 200.0f;
        if (dial_r_out < 120.0f) dial_r_out = 120.0f;
        float dial_r_in = dial_r_out * 0.55f;
        float hub_r = dial_r_in - 10.0f;

        // 昼夜环境光
        rife_draw_arc_sector(core, dial_cx, dial_cy, dial_r_in - 2.0f, dial_r_out + 2.0f, 90.0f, 270.0f,
                             is_dark ? 0x1E3A8A18 : 0xBAE6FD25, 0);
        rife_draw_arc_sector(core, dial_cx, dial_cy, dial_r_in - 2.0f, dial_r_out + 2.0f, 270.0f, 90.0f,
                             is_dark ? 0x0F172A44 : 0xE2E8F033, 0);

        // 表盘轨道底色
        rife_draw_arc_sector(core, dial_cx, dial_cy, dial_r_in, dial_r_out, 0.0f, 360.0f,
                             is_dark ? 0x202124FF : 0xF1F3F4FF, col_border);

        // 刻度线
        int total_hours = (state->dial_mode == CLOCK_DIAL_24H) ? 24 : 12;
        float deg_per_h = 360.0f / (float)total_hours;
        const float deg2rad = 0.0174532925f;

        for (int h = 0; h < total_hours; h++) {
            float ang_deg = (float)h * deg_per_h;
            float rad = ang_deg * deg2rad;
            float s = sinf(rad), c = cosf(rad);

            bool is_major = (h % 3 == 0);
            float tick_len = is_major ? 10.0f : 5.0f;
            uint32_t tick_col = is_major ? (is_dark ? 0xA5B4FCFF : 0x475569FF) : (is_dark ? 0x47556988 : 0xCBD5E1AA);

            float x1 = dial_cx + s * (dial_r_out - tick_len);
            float y1 = dial_cy - c * (dial_r_out - tick_len);
            float x2 = dial_cx + s * dial_r_out;
            float y2 = dial_cy - c * dial_r_out;
            rife_draw_line(core, x1, y1, x2, y2, is_major ? 1.8f : 1.0f, tick_col);
        }

        // 实时指针
        if (clock_is_today(state)) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            float now_mins = (float)(st.wHour * 60 + st.wMinute) + (float)st.wSecond / 60.0f;
            float now_deg = now_mins * (360.0f / 1440.0f);
            float now_rad = now_deg * deg2rad;
            float s = sinf(now_rad), c = cosf(now_rad);

            float nx1 = dial_cx + s * (hub_r - 2.0f);
            float ny1 = dial_cy - c * (hub_r - 2.0f);
            float nx2 = dial_cx + s * (dial_r_out + 8.0f);
            float ny2 = dial_cy - c * (dial_r_out + 8.0f);
            rife_draw_line(core, nx1, ny1, nx2, ny2, 2.5f, 0xF43F5EFF);
            rife_draw_circle(core, nx2, ny2, 4.5f, 0xF43F5EFF, 0xFFFFFFFF);
        }

        // 中央数字核
        rife_draw_circle(core, dial_cx, dial_cy, hub_r, is_dark ? 0x1E1F22FF : 0xFFFFFFFF, col_border);
        SYSTEMTIME st;
        GetLocalTime(&st);
        char time_str[32];
        snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
        rife_draw_text_rect(core, dial_cx - 60.0f, dial_cy - 12.0f, 120.0f, 24.0f, time_str, col_txt_main, 1, 0);
    }

    // =========================================================
    // 3. 右侧多功能工作流面板 (Right Panel: 340px)
    // =========================================================
    float list_x = client_x + client_w - 340.0f;
    float list_y = client_y + 44.0f;
    float list_w = 340.0f;
    float list_h = client_h - 44.0f;

    rife_draw_rect(core, list_x, list_y, 1.0f, list_h, col_border);
    rife_draw_rect(core, list_x + 1.0f, list_y, list_w - 1.0f, list_h, is_dark ? 0x101116FF : 0xF8FAFCFF);

    // 双标签切换器: [ 📋 专注待办 ]  [ 📊 今日专注统计 ]
    float tab_w = (list_w - 28.0f - 8.0f) * 0.5f;
    float tab0_x = list_x + 14.0f;
    float tab1_x = tab0_x + tab_w + 8.0f;
    float tab_y = list_y + 8.0f;
    float tab_h = 28.0f;

    bool tab0_active = (state->right_panel_tab == 0);
    uint32_t t0_bg = tab0_active ? (is_dark ? 0x1E202BFF : 0xEEF2FFFF) : (is_dark ? 0x14151DFF : 0xF1F3F4FF);
    uint32_t t0_txt = tab0_active ? col_blue : col_txt_sub;
    rife_draw_round_rect(core, tab0_x, tab_y, tab_w, tab_h, 6.0f, t0_bg, tab0_active ? col_blue : col_border);
    rife_draw_text_rect(core, tab0_x, tab_y, tab_w, tab_h, is_zh ? "📋 专注待办" : "Tasks", t0_txt, 4, 0);

    bool tab1_active = (state->right_panel_tab == 1);
    uint32_t t1_bg = tab1_active ? (is_dark ? 0x1E202BFF : 0xEEF2FFFF) : (is_dark ? 0x14151DFF : 0xF1F3F4FF);
    uint32_t t1_txt = tab1_active ? col_blue : col_txt_sub;
    rife_draw_round_rect(core, tab1_x, tab_y, tab_w, tab_h, 6.0f, t1_bg, tab1_active ? col_blue : col_border);
    rife_draw_text_rect(core, tab1_x, tab_y, tab_w, tab_h, is_zh ? "📊 专注统计" : "Stats", t1_txt, 4, 0);

    // 分割线
    rife_draw_rect(core, list_x + 14.0f, list_y + 42.0f, list_w - 28.0f, 1.0f, col_border);

    if (state->right_panel_tab == 0) {
        // =====================================================
        // Tab 0: 专注待办列表 (Focus Tasks Queue)
        // =====================================================
        rife_draw_text_font(core, list_x + 16.0f, list_y + 48.0f, is_zh ? "点击任务卡片设为专注目标：" : "Select task to focus:", col_txt_mute, 4);

        rife_push_scissor(core, list_x + 1.0f, list_y + 68.0f, list_w - 2.0f, list_h - 70.0f);

        float cur_card_y = list_y + 70.0f + state->list_scroll_y;
        int shown_tasks = 0;

        for (int i = 0; i < state->storage.event_count; i++) {
            CalendarEvent* e = &state->storage.events[i];
            bool is_active_target = (e->id == state->active_focus_task_id);

            float card_x = list_x + 14.0f;
            float card_w = list_w - 28.0f;
            float card_h = 56.0f;

            uint32_t bg_col = col_card_bg;
            uint32_t bd_col = is_active_target ? col_blue : col_border;
            if (is_active_target) {
                bg_col = is_dark ? 0x1A1C28FF : 0xEEF2FFFF;
            }

            rife_draw_round_rect(core, card_x, cur_card_y, card_w, card_h, 8.0f, bg_col, bd_col);

            // 复选圆圈
            float chk_x = card_x + 20.0f;
            float chk_cy = cur_card_y + 28.0f;
            if (e->is_completed) {
                rife_draw_circle(core, chk_x, chk_cy, 8.0f, col_emerald, col_emerald);
                rife_draw_text_rect(core, chk_x - 7.0f, chk_cy - 7.0f, 14.0f, 14.0f, "✓", 0xFFFFFFFF, 4, 0);
            } else {
                rife_draw_circle(core, chk_x, chk_cy, 8.0f, 0x00000000, is_active_target ? col_blue : col_border);
            }

            // 任务标题与划线
            rife_draw_text_font(core, card_x + 36.0f, cur_card_y + 10.0f, e->title, e->is_completed ? col_txt_mute : col_txt_main, is_active_target ? 5 : 0);
            if (e->is_completed) {
                float tw = (float)strlen(e->title) * 7.0f;
                rife_draw_line(core, card_x + 36.0f, cur_card_y + 18.0f, card_x + 36.0f + tw, cur_card_y + 18.0f, 1.0f, col_txt_mute);
            }

            // 底部元数据: 清单名或日期
            const char* l_name = (e->list_idx >= 0 && e->list_idx < state->storage.list_count) ? state->storage.lists[e->list_idx].name : "待办";
            rife_draw_text_font(core, card_x + 36.0f, cur_card_y + 32.0f, l_name, col_txt_mute, 4);

            // 右侧番茄计数 Badge
            if (is_active_target) {
                float badge_w = 64.0f;
                float bx = card_x + card_w - badge_w - 10.0f;
                rife_draw_round_rect(core, bx, cur_card_y + 16.0f, badge_w, 24.0f, 4.0f, col_blue, 0);
                rife_draw_text_rect(core, bx, cur_card_y + 16.0f, badge_w, 24.0f, "专注中", 0xFFFFFFFF, 4, 0);
            } else if (e->pomodoro_count > 0) {
                char p_buf[16];
                snprintf(p_buf, sizeof(p_buf), "🍅 x %d", e->pomodoro_count);
                float badge_w = 54.0f;
                float bx = card_x + card_w - badge_w - 10.0f;
                rife_draw_round_rect(core, bx, cur_card_y + 16.0f, badge_w, 24.0f, 4.0f, is_dark ? 0x222432FF : 0xE2E8F0FF, 0);
                rife_draw_text_rect(core, bx, cur_card_y + 16.0f, badge_w, 24.0f, p_buf, col_txt_sub, 4, 0);
            }

            cur_card_y += 62.0f;
            shown_tasks++;
        }

        if (shown_tasks == 0) {
            rife_draw_round_rect(core, list_x + 20.0f, list_y + 90.0f, list_w - 40.0f, 100.0f, 10.0f, col_card_bg, col_border);
            rife_draw_text_rect(core, list_x + 20.0f, list_y + 115.0f, list_w - 40.0f, 20.0f, is_zh ? "当前清单暂无待办" : "No Tasks", col_txt_sub, 3, 0);
            rife_draw_text_rect(core, list_x + 20.0f, list_y + 140.0f, list_w - 40.0f, 20.0f, is_zh ? "在待办清单中添加任务" : "Add tasks in Tasks view", col_txt_mute, 4, 0);
        }

        rife_pop_scissor(core);

    } else {
        // =====================================================
        // Tab 1: 今日专注统计看板 (Pomodoro Analytics)
        // =====================================================
        rife_push_scissor(core, list_x + 1.0f, list_y + 44.0f, list_w - 2.0f, list_h - 46.0f);

        float ay = list_y + 48.0f + state->analytics_scroll_y;
        float cw = list_w - 28.0f;
        float cx = list_x + 14.0f;

        // --- 卡片 1: 今日专注统计总览 ---
        float c1_h = 110.0f;
        rife_draw_round_rect(core, cx, ay, cw, c1_h, 10.0f, col_card_bg, col_border);
        rife_draw_text_font(core, cx + 14.0f, ay + 12.0f, is_zh ? "今日专注成就" : "Today's Achievements", col_txt_main, 5);

        // 番茄数大数字
        char pomo_cnt_str[32];
        snprintf(pomo_cnt_str, sizeof(pomo_cnt_str), "%d 个番茄", state->storage.total_pomodoros);
        rife_draw_text_font(core, cx + 14.0f, ay + 36.0f, pomo_cnt_str, col_blue, 1);

        char min_cnt_str[32];
        snprintf(min_cnt_str, sizeof(min_cnt_str), "累计 %d 分钟", state->storage.total_focus_mins);
        rife_draw_text_font(core, cx + 140.0f, ay + 42.0f, min_cnt_str, col_txt_sub, 3);

        // 目标达成进度条 (按 8 个番茄为 100% 满分目标)
        float goal_pct = ((float)state->storage.total_pomodoros / 8.0f) * 100.0f;
        if (goal_pct > 100.0f) goal_pct = 100.0f;

        rife_draw_round_rect(core, cx + 14.0f, ay + 72.0f, cw - 28.0f, 6.0f, 3.0f, is_dark ? 0x202230FF : 0xE2E8F0AA, 0);
        if (state->storage.total_pomodoros > 0) {
            float fw = (cw - 28.0f) * (goal_pct / 100.0f);
            if (fw < 8.0f) fw = 8.0f;
            rife_draw_round_rect(core, cx + 14.0f, ay + 72.0f, fw, 6.0f, 3.0f, col_blue, 0);
        }

        char p_sub[64];
        snprintf(p_sub, sizeof(p_sub), is_zh ? "目标达成度: %.0f%% (目标 8 个番茄)" : "Goal: %.0f%%", goal_pct);
        rife_draw_text_font(core, cx + 14.0f, ay + 86.0f, p_sub, col_txt_mute, 4);

        ay += c1_h + 12.0f;

        // --- 卡片 2: 已专注攻坚任务明细 ---
        float c2_h = 130.0f;
        rife_draw_round_rect(core, cx, ay, cw, c2_h, 10.0f, col_card_bg, col_border);
        rife_draw_text_font(core, cx + 14.0f, ay + 12.0f, is_zh ? "攻坚任务明细" : "Focused Tasks", col_txt_main, 5);

        float task_sub_y = ay + 36.0f;
        int focused_task_count = 0;
        for (int i = 0; i < state->storage.event_count && focused_task_count < 3; i++) {
            const CalendarEvent* e = &state->storage.events[i];
            if (e->pomodoro_count > 0) {
                char item_buf[64];
                snprintf(item_buf, sizeof(item_buf), "%.16s", e->title);
                rife_draw_text_font(core, cx + 14.0f, task_sub_y, item_buf, col_txt_sub, 4);

                char cnt_b[16];
                snprintf(cnt_b, sizeof(cnt_b), "🍅 x %d", e->pomodoro_count);
                rife_draw_text_rect(core, cx + cw - 70.0f, task_sub_y, 60.0f, 16.0f, cnt_b, col_blue, 4, 0);

                task_sub_y += 24.0f;
                focused_task_count++;
            }
        }
        if (focused_task_count == 0) {
            rife_draw_text_font(core, cx + 14.0f, task_sub_y, is_zh ? "今日尚未记录攻坚任务" : "No focused tasks yet", col_txt_mute, 4);
        }

        ay += c2_h + 12.0f;

        // --- 卡片 3: 心流灵感箴言 ---
        float c3_h = 80.0f;
        rife_draw_round_rect(core, cx, ay, cw, c3_h, 10.0f, is_dark ? 0x1A1828EE : 0xEEF2FFCC, is_dark ? 0x2E2848FF : 0xC7D2FEFF);
        rife_draw_text_font(core, cx + 14.0f, ay + 12.0f, is_zh ? "💡 深度工作法则" : "Deep Work Rule", col_blue, 5);
        rife_draw_text_font(core, cx + 14.0f, ay + 34.0f, is_zh ? "25分钟全力攻坚单一目标，彻底隔离通知；" : "Single-task with full intensity for 25m;", col_txt_sub, 4);
        rife_draw_text_font(core, cx + 14.0f, ay + 52.0f, is_zh ? "5分钟身心完全放空，心流自此源源不断。" : "Rest completely for 5m to sustain flow.", col_txt_mute, 4);

        rife_pop_scissor(core);
    }
}

// -------------------------------------------------------------
// 插件声明契约 (Plugin Manifest Export)
// -------------------------------------------------------------

const RifePluginApp g_clock_plugin_app = {
    .app_id = CLOCK_APP_ID,
    .id = "rclock",
    .name_zh = "滴答番茄",
    .name_en = "TickPomodoro",
    .glyph = "🍅",
    .color_top = 0x6366F1FF, // 电光靛蓝
    .color_bot = 0x4338CAFF,
    .default_w = 980.0f,
    .default_h = 640.0f,
    .pin_to_dock = true,
    .create = clock_create,
    .destroy = clock_destroy,
    .update = clock_update,
    .render = clock_render
};

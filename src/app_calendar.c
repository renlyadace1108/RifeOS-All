#define _CRT_SECURE_NO_WARNINGS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "app_calendar.h"
#include "app_manifest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

// -------------------------------------------------------------
// Rtodo 状态机定义 (Zero Heap Churn: 单次创建，帧循环零分配)
// -------------------------------------------------------------

typedef struct {
    CalendarViewMode view_mode; // 周视图 / 日视图 / 月视图 / 日程列表
    int cur_year;
    int cur_month;   // 1 - 12
    int cur_day;     // 1 - 31
    int cur_hour;    // 0 - 23
    int cur_min;     // 0 - 59
    int cur_wday;    // 0=Mon .. 6=Sun

    int view_year;
    int view_month;
    int view_day;

    // 自定义标签系统 (用户自由定义，完全自定义)
    CustomTag tags[CAL_MAX_CUSTOM_TAGS];
    int tag_count;

    // 日程列表 (本地持久化)
    CalendarEvent events[CAL_MAX_EVENTS];
    int event_count;

    int selected_event_id;

    // 新建日程模态弹窗
    bool show_new_modal;
    int modal_tag_idx;
    int new_hour;
    int new_min;          // 0 或 30 分钟
    int new_duration_idx; // 0: 30m, 1: 1h, 2: 1.5h, 3: 2h

    // 交互式文本输入与焦点状态机
    // active_field: 0=none, 1=title, 2=location, 3=desc, 4=new_tag_name
    int active_field;
    char input_title[48];
    char input_location[32];
    char input_desc[64];
    float cursor_blink_t;

    // 新建自定义标签子弹窗
    bool show_new_tag_modal;
    char new_tag_name[24];
    int new_tag_color_idx; // 0..5

    float scroll_y;
} CalendarState;

// -------------------------------------------------------------
// 公历与日期计算算法
// -------------------------------------------------------------

static inline bool is_leap_year(int y) {
    return ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0));
}

static inline int days_in_month(int y, int m) {
    static const int d[13] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && is_leap_year(y)) return 29;
    if (m >= 1 && m <= 12) return d[m];
    return 30;
}

// 坂本算法 (Sakamoto's Algorithm): 0=Sunday, 1=Monday .. 6=Saturday
static inline int get_day_of_week_sun(int y, int m, int d) {
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    int yr = y;
    if (m < 3) yr -= 1;
    return (yr + yr / 4 - yr / 100 + yr / 400 + t[m - 1] + d) % 7;
}

// 获取某日所在周的周日对应日期 (Sunday Start)
static void get_week_sunday(int y, int m, int d, int* out_y, int* out_m, int* out_d) {
    int w = get_day_of_week_sun(y, m, d);
    int target_d = d - w;
    int target_m = m;
    int target_y = y;

    if (target_d < 1) {
        target_m -= 1;
        if (target_m < 1) {
            target_m = 12;
            target_y -= 1;
        }
        target_d += days_in_month(target_y, target_m);
    }
    *out_y = target_y;
    *out_m = target_m;
    *out_d = target_d;
}

// 某日期加上 offset 天
static void add_days(int y, int m, int d, int offset, int* out_y, int* out_m, int* out_d) {
    int cur_y = y;
    int cur_m = m;
    int cur_d = d + offset;

    while (cur_d > days_in_month(cur_y, cur_m)) {
        cur_d -= days_in_month(cur_y, cur_m);
        cur_m += 1;
        if (cur_m > 12) {
            cur_m = 1;
            cur_y += 1;
        }
    }

    while (cur_d < 1) {
        cur_m -= 1;
        if (cur_m < 1) {
            cur_m = 12;
            cur_y -= 1;
        }
        cur_d += days_in_month(cur_y, cur_m);
    }

    *out_y = cur_y;
    *out_m = cur_m;
    *out_d = cur_d;
}

// 更新当前系统真实时钟
static void sync_system_clock(CalendarState* state) {
    time_t raw = time(NULL);
    struct tm* t = localtime(&raw);
    if (t) {
        state->cur_year = t->tm_year + 1900;
        state->cur_month = t->tm_mon + 1;
        state->cur_day = t->tm_mday;
        state->cur_hour = t->tm_hour;
        state->cur_min = t->tm_min;
        state->cur_wday = (t->tm_wday + 6) % 7;
    }
}

// -------------------------------------------------------------
// Rtodo 自定义标签色彩体系与本地持久化 (Zero Heap Churn)
// -------------------------------------------------------------

#define RTODO_MAGIC 0x544F444F // "TODO"
#define RTODO_VERSION 2

static const uint32_t s_tag_palette[6] = {
    0x3370FFFF, // 品牌蓝 (Blue)
    0x8B5CF6FF, // 薰衣紫 (Purple)
    0x10B981FF, // 薄荷绿 (Emerald)
    0xF59E0BFF, // 暖琥珀 (Amber)
    0xEF4444FF, // 珊瑚红 (Rose)
    0x06B6D4FF  // 极光青 (Cyan)
};

typedef struct {
    uint32_t bg;
    uint32_t border;
    uint32_t bar;
    uint32_t text;
} TagColorStyle;

static TagColorStyle get_tag_style(uint32_t bar_color, bool is_dark) {
    TagColorStyle s;
    s.bar = bar_color;
    uint8_t r = (uint8_t)((bar_color >> 24) & 0xFF);
    uint8_t g = (uint8_t)((bar_color >> 16) & 0xFF);
    uint8_t b = (uint8_t)((bar_color >> 8) & 0xFF);
    if (is_dark) {
        s.bg = ((uint32_t)(r * 0.22f) << 24) | ((uint32_t)(g * 0.22f) << 16) | ((uint32_t)(b * 0.22f) << 8) | 0xB8;
        s.border = ((uint32_t)(r * 0.40f) << 24) | ((uint32_t)(g * 0.40f) << 16) | ((uint32_t)(b * 0.40f) << 8) | 0xCC;
        s.text = ((uint32_t)(r * 0.4f + 150.0f) << 24) | ((uint32_t)(g * 0.4f + 150.0f) << 16) | ((uint32_t)(b * 0.4f + 150.0f) << 8) | 0xFF;
    } else {
        s.bg = ((uint32_t)(r * 0.12f + 220.0f) << 24) | ((uint32_t)(g * 0.12f + 220.0f) << 16) | ((uint32_t)(b * 0.12f + 220.0f) << 8) | 0xD4;
        s.border = ((uint32_t)(r * 0.25f + 185.0f) << 24) | ((uint32_t)(g * 0.25f + 185.0f) << 16) | ((uint32_t)(b * 0.25f + 185.0f) << 8) | 0xD4;
        s.text = ((uint32_t)(r * 0.6f) << 24) | ((uint32_t)(g * 0.6f) << 16) | ((uint32_t)(b * 0.6f) << 8) | 0xFF;
    }
    return s;
}

static TagColorStyle get_event_style(const CalendarState* state, int tag_idx, bool is_dark) {
    if (tag_idx >= 0 && tag_idx < state->tag_count) {
        return get_tag_style(state->tags[tag_idx].color_bar, is_dark);
    }
    return get_tag_style(0x3370FFFF, is_dark);
}

static const char* get_event_tag_name(const CalendarState* state, int tag_idx) {
    if (tag_idx >= 0 && tag_idx < state->tag_count) {
        return state->tags[tag_idx].name;
    }
    return "日程";
}

static bool is_event_visible(const CalendarState* state, const CalendarEvent* e) {
    if (e->tag_idx >= 0 && e->tag_idx < state->tag_count) {
        return state->tags[e->tag_idx].is_enabled;
    }
    return true;
}

static void utf8_pop_back(char* str) {
    if (!str || str[0] == '\0') return;
    size_t len = strlen(str);
    if (len == 0) return;
    size_t i = len - 1;
    while (i > 0 && ((unsigned char)str[i] & 0xC0) == 0x80) {
        i--;
    }
    str[i] = '\0';
}

static void calc_event_end_time(int start_h, int start_m, int duration_idx, int* end_h, int* end_m) {
    int duration_mins = 60;
    switch (duration_idx) {
        case 0: duration_mins = 30; break;
        case 1: duration_mins = 60; break;
        case 2: duration_mins = 90; break;
        case 3: duration_mins = 120; break;
        default: duration_mins = 60; break;
    }
    int total_mins = start_h * 60 + start_m + duration_mins;
    *end_h = total_mins / 60;
    *end_m = total_mins % 60;
    if (*end_h > 23) {
        *end_h = 23;
        *end_m = 59;
    }
}

typedef struct {
    uint32_t magic;
    uint32_t version;
    int tag_count;
    CustomTag tags[CAL_MAX_CUSTOM_TAGS];
    int event_count;
    CalendarEvent events[CAL_MAX_EVENTS];
} RtodoStorage;

static void init_default_tags(CalendarState* state) {
    state->tag_count = 3;
    snprintf(state->tags[0].name, sizeof(state->tags[0].name), "工作");
    state->tags[0].color_bar = 0x3370FFFF; // Blue
    state->tags[0].is_enabled = true;

    snprintf(state->tags[1].name, sizeof(state->tags[1].name), "生活");
    state->tags[1].color_bar = 0x10B981FF; // Green
    state->tags[1].is_enabled = true;

    snprintf(state->tags[2].name, sizeof(state->tags[2].name), "重要");
    state->tags[2].color_bar = 0xEF4444FF; // Red
    state->tags[2].is_enabled = true;

    state->event_count = 0; // 干净初始状态：绝不注入假测试数据
}

static void save_calendar_data(const CalendarState* state) {
    FILE* fp = fopen("rtodo_data.bin", "wb");
    if (!fp) return;
    RtodoStorage store;
    memset(&store, 0, sizeof(RtodoStorage));
    store.magic = RTODO_MAGIC;
    store.version = RTODO_VERSION;
    store.tag_count = state->tag_count;
    if (store.tag_count > CAL_MAX_CUSTOM_TAGS) store.tag_count = CAL_MAX_CUSTOM_TAGS;
    if (store.tag_count > 0) {
        memcpy(store.tags, state->tags, sizeof(CustomTag) * store.tag_count);
    }
    store.event_count = state->event_count;
    if (store.event_count > CAL_MAX_EVENTS) store.event_count = CAL_MAX_EVENTS;
    if (store.event_count > 0) {
        memcpy(store.events, state->events, sizeof(CalendarEvent) * store.event_count);
    }
    fwrite(&store, sizeof(RtodoStorage), 1, fp);
    fclose(fp);
}

static void load_calendar_data(CalendarState* state) {
    FILE* fp = fopen("rtodo_data.bin", "rb");
    if (!fp) {
        init_default_tags(state);
        return;
    }
    RtodoStorage store;
    if (fread(&store, sizeof(RtodoStorage), 1, fp) == 1 && store.magic == RTODO_MAGIC && store.version == RTODO_VERSION) {
        state->tag_count = store.tag_count;
        if (state->tag_count > CAL_MAX_CUSTOM_TAGS) state->tag_count = CAL_MAX_CUSTOM_TAGS;
        if (state->tag_count > 0) {
            memcpy(state->tags, store.tags, sizeof(CustomTag) * state->tag_count);
        }
        state->event_count = store.event_count;
        if (state->event_count > CAL_MAX_EVENTS) state->event_count = CAL_MAX_EVENTS;
        if (state->event_count > 0) {
            memcpy(state->events, store.events, sizeof(CalendarEvent) * store.event_count);
        }
    } else {
        init_default_tags(state);
    }
    fclose(fp);
}

// -------------------------------------------------------------
// 插件生命周期 (Plugin Lifecycle)
// -------------------------------------------------------------

static void* calendar_create(RifeCore* core) {
    (void)core;
    CalendarState* state = (CalendarState*)malloc(sizeof(CalendarState));
    if (!state) return NULL;

    memset(state, 0, sizeof(CalendarState));
    state->view_mode = CAL_VIEW_WEEK; // 默认经典周视图
    sync_system_clock(state);

    state->view_year = state->cur_year;
    state->view_month = state->cur_month;
    state->view_day = state->cur_day;

    load_calendar_data(state);

    state->selected_event_id = -1;
    state->show_new_modal = false;
    state->modal_tag_idx = 0;
    state->new_hour = 10;
    state->new_min = 0;
    state->new_duration_idx = 1; // 1h
    state->active_field = 0;
    state->cursor_blink_t = 0.0f;
    state->show_new_tag_modal = false;
    state->new_tag_color_idx = 0;

    state->scroll_y = 8.0f * 60.0f; // 默认平滑定位于早 08:00 工作时段
    return state;
}

static void calendar_destroy(void* inst) {
    if (inst) free(inst);
}

// -------------------------------------------------------------
// 交互更新 (Update)
// -------------------------------------------------------------

static void calendar_update(void* inst, RifeCore* core, const RifeInput* input, float client_w, float client_h) {
    CalendarState* state = (CalendarState*)inst;
    if (!state) return;

    sync_system_clock(state);

    // 光标闪烁时钟推进
    state->cursor_blink_t += 0.04f;
    if (state->cursor_blink_t > 1.0f) state->cursor_blink_t -= 1.0f;

    // 键盘按键与字符文本输入处理
    if (state->active_field > 0) {
        // A. ESC 关闭或失焦
        if (input->key_pressed[VK_ESCAPE]) {
            if (state->show_new_tag_modal) {
                state->show_new_tag_modal = false;
                state->active_field = 1;
            } else if (state->show_new_modal) {
                state->show_new_modal = false;
                state->active_field = 0;
            }
            rife_request_redraw(core);
            return;
        }

        // B. Tab 循环切换输入框焦点
        if (input->key_pressed[VK_TAB]) {
            if (state->show_new_modal && !state->show_new_tag_modal) {
                state->active_field = (state->active_field % 3) + 1; // 1 -> 2 -> 3 -> 1
                rife_request_redraw(core);
                return;
            }
        }

        // C. Backspace 退格删除 (UTF-8 安全多字节边界)
        if (input->key_pressed[VK_BACK]) {
            if (state->active_field == 1) {
                utf8_pop_back(state->input_title);
            } else if (state->active_field == 2) {
                utf8_pop_back(state->input_location);
            } else if (state->active_field == 3) {
                utf8_pop_back(state->input_desc);
            } else if (state->active_field == 4) {
                utf8_pop_back(state->new_tag_name);
            }
            rife_request_redraw(core);
        }

        // D. 字符文本输入与 Ctrl+V 剪贴板文本写入
        if (input->text_input[0] != '\0') {
            const char* p = input->text_input;
            while (*p) {
                if (*p == '\r' || *p == '\n') {
                    if (state->active_field == 4) {
                        // 确认新建标签
                        if (state->new_tag_name[0] != '\0' && state->tag_count < CAL_MAX_CUSTOM_TAGS) {
                            int idx = state->tag_count++;
                            snprintf(state->tags[idx].name, sizeof(state->tags[idx].name), "%s", state->new_tag_name);
                            state->tags[idx].color_bar = s_tag_palette[state->new_tag_color_idx];
                            state->tags[idx].is_enabled = true;
                            state->modal_tag_idx = idx;
                            save_calendar_data(state);
                        }
                        state->show_new_tag_modal = false;
                        state->active_field = 1;
                    } else if (state->active_field == 1) {
                        state->active_field = 2; // 回车切换至地点
                    } else if (state->active_field == 2) {
                        state->active_field = 3; // 回车切换至备注
                    }
                    p++;
                    continue;
                }
                if (*p == '\b') {
                    p++;
                    continue;
                }

                if (state->active_field == 1) {
                    size_t len = strlen(state->input_title);
                    if (len < sizeof(state->input_title) - 4) {
                        state->input_title[len] = *p;
                        state->input_title[len + 1] = '\0';
                    }
                } else if (state->active_field == 2) {
                    size_t len = strlen(state->input_location);
                    if (len < sizeof(state->input_location) - 4) {
                        state->input_location[len] = *p;
                        state->input_location[len + 1] = '\0';
                    }
                } else if (state->active_field == 3) {
                    size_t len = strlen(state->input_desc);
                    if (len < sizeof(state->input_desc) - 4) {
                        state->input_desc[len] = *p;
                        state->input_desc[len + 1] = '\0';
                    }
                } else if (state->active_field == 4) {
                    size_t len = strlen(state->new_tag_name);
                    if (len < sizeof(state->new_tag_name) - 4) {
                        state->new_tag_name[len] = *p;
                        state->new_tag_name[len + 1] = '\0';
                    }
                }
                p++;
            }
            rife_request_redraw(core);
        }
    }

    // 鼠标滚轮纵向平滑滚动 (周视图与日视图大表格上下滚动)
    if (input->scroll_delta != 0.0f) {
        if (state->view_mode == CAL_VIEW_WEEK || state->view_mode == CAL_VIEW_DAY) {
            float hour_h = 60.0f;
            float header_bar_h = (state->view_mode == CAL_VIEW_WEEK) ? 52.0f : 36.0f;
            float avail_h = (client_h - 44.0f) - header_bar_h - 4.0f;
            float total_h = 24.0f * hour_h;
            float max_scroll = total_h - avail_h;
            if (max_scroll < 0.0f) max_scroll = 0.0f;

            state->scroll_y -= input->scroll_delta * 48.0f;
            if (state->scroll_y < 0.0f) state->scroll_y = 0.0f;
            if (state->scroll_y > max_scroll) state->scroll_y = max_scroll;
            rife_request_redraw(core);
            return;
        }
    }

    if (!input->mouse_pressed[0]) return;

    float mx = input->mouse_x;
    float my = input->mouse_y;

    float header_h = 44.0f;
    float sidebar_w = 180.0f;

    // 0. 子弹窗：新建自定义标签模态框
    if (state->show_new_tag_modal) {
        float tw = 300.0f;
        float th = 170.0f;
        float tx0 = (client_w - tw) * 0.5f;
        float ty0 = (client_h - th) * 0.5f;

        if (mx >= tx0 && mx <= tx0 + tw && my >= ty0 && my <= ty0 + th) {
            // A. 点击标签名称输入框
            if (mx >= tx0 + 16.0f && mx <= tx0 + tw - 16.0f && my >= ty0 + 40.0f && my <= ty0 + 72.0f) {
                state->active_field = 4;
                rife_request_redraw(core);
                return;
            }
            // B. 点击 6 种色彩圆点
            float dot_y = ty0 + 82.0f;
            for (int i = 0; i < 6; i++) {
                float dot_x = tx0 + 20.0f + (float)i * 32.0f;
                if (mx >= dot_x && mx <= dot_x + 22.0f && my >= dot_y && my <= dot_y + 22.0f) {
                    state->new_tag_color_idx = i;
                    rife_request_redraw(core);
                    return;
                }
            }
            // C. 底部操作按钮
            float act_y = ty0 + th - 40.0f;
            // 取消
            if (mx >= tx0 + tw - 150.0f && mx <= tx0 + tw - 86.0f && my >= act_y && my <= act_y + 28.0f) {
                state->show_new_tag_modal = false;
                state->active_field = 1;
                rife_request_redraw(core);
                return;
            }
            // 确定创建
            if (mx >= tx0 + tw - 80.0f && mx <= tx0 + tw - 16.0f && my >= act_y && my <= act_y + 28.0f) {
                if (state->new_tag_name[0] != '\0' && state->tag_count < CAL_MAX_CUSTOM_TAGS) {
                    int idx = state->tag_count++;
                    snprintf(state->tags[idx].name, sizeof(state->tags[idx].name), "%s", state->new_tag_name);
                    state->tags[idx].color_bar = s_tag_palette[state->new_tag_color_idx];
                    state->tags[idx].is_enabled = true;
                    state->modal_tag_idx = idx;
                    save_calendar_data(state);
                }
                state->show_new_tag_modal = false;
                state->active_field = 1;
                rife_request_redraw(core);
                return;
            }
            return;
        } else {
            state->show_new_tag_modal = false;
            state->active_field = 1;
            rife_request_redraw(core);
            return;
        }
    }

    // 1. 浮动弹窗：新建日程模态框
    if (state->show_new_modal) {
        float mw = 400.0f;
        float mh = 330.0f;
        float mx0 = (client_w - mw) * 0.5f;
        float my0 = (client_h - mh) * 0.5f;
        float box_w = mw - 32.0f;

        if (mx >= mx0 && mx <= mx0 + mw && my >= my0 && my <= my0 + mh) {
            // A. 点击标题输入框
            float title_y = my0 + 42.0f;
            if (mx >= mx0 + 16.0f && mx <= mx0 + 16.0f + box_w && my >= title_y && my <= title_y + 32.0f) {
                state->active_field = 1;
                rife_request_redraw(core);
                return;
            }

            // B. 自定义标签胶囊行与 [+ 标签]
            float tag_y = my0 + 82.0f;
            if (my >= tag_y && my <= tag_y + 26.0f) {
                float px = mx0 + 16.0f;
                for (int c = 0; c < state->tag_count; c++) {
                    float pw = 60.0f;
                    if (mx >= px && mx <= px + pw) {
                        state->modal_tag_idx = c;
                        rife_request_redraw(core);
                        return;
                    }
                    px += pw + 8.0f;
                }
                // 点击 [+ 标签] 按钮
                float add_w = 64.0f;
                if (mx >= px && mx <= px + add_w) {
                    state->show_new_tag_modal = true;
                    state->new_tag_name[0] = '\0';
                    state->new_tag_color_idx = 0;
                    state->active_field = 4;
                    rife_request_redraw(core);
                    return;
                }
            }

            // C. 时间与时长微调联动
            float time_y = my0 + 118.0f;
            // 减小时
            if (mx >= mx0 + 56.0f && mx <= mx0 + 80.0f && my >= time_y && my <= time_y + 24.0f) {
                if (state->new_hour > 0) state->new_hour--;
                rife_request_redraw(core);
                return;
            }
            // 分钟切换 (00 / 30)
            if (mx >= mx0 + 84.0f && mx <= mx0 + 128.0f && my >= time_y && my <= time_y + 24.0f) {
                state->new_min = (state->new_min == 0) ? 30 : 0;
                rife_request_redraw(core);
                return;
            }
            // 加小时
            if (mx >= mx0 + 132.0f && mx <= mx0 + 156.0f && my >= time_y && my <= time_y + 24.0f) {
                if (state->new_hour < 23) state->new_hour++;
                rife_request_redraw(core);
                return;
            }
            // 时长胶囊 (4项: 30分, 1小时, 1.5时, 2小时)
            for (int d = 0; d < 4; d++) {
                float bx = mx0 + 204.0f + (float)d * 45.0f;
                if (mx >= bx && mx <= bx + 42.0f && my >= time_y && my <= time_y + 24.0f) {
                    state->new_duration_idx = d;
                    rife_request_redraw(core);
                    return;
                }
            }

            // D. 点击地点输入框
            float loc_y = my0 + 156.0f;
            if (mx >= mx0 + 16.0f && mx <= mx0 + 16.0f + box_w && my >= loc_y && my <= loc_y + 30.0f) {
                state->active_field = 2;
                rife_request_redraw(core);
                return;
            }

            // E. 点击备注/描述输入框
            float desc_y = my0 + 194.0f;
            if (mx >= mx0 + 16.0f && mx <= mx0 + 16.0f + box_w && my >= desc_y && my <= desc_y + 30.0f) {
                state->active_field = 3;
                rife_request_redraw(core);
                return;
            }

            // F. 底部操作按钮：确认创建 / 取消
            float btn_y = my0 + mh - 44.0f;
            // 确认创建
            if (mx >= mx0 + mw - 96.0f && mx <= mx0 + mw - 16.0f && my >= btn_y && my <= btn_y + 32.0f) {
                if (state->event_count < CAL_MAX_EVENTS) {
                    CalendarEvent* ne = &state->events[state->event_count++];
                    static uint32_t s_next_id = 1000;
                    ne->id = s_next_id++;
                    if (state->input_title[0] != '\0') {
                        snprintf(ne->title, sizeof(ne->title), "%s", state->input_title);
                    } else {
                        snprintf(ne->title, sizeof(ne->title), "%s", "新日程");
                    }
                    snprintf(ne->location, sizeof(ne->location), "%s", state->input_location);
                    snprintf(ne->desc, sizeof(ne->desc), "%s", state->input_desc);
                    ne->tag_idx = state->modal_tag_idx;
                    ne->year = state->view_year;
                    ne->month = state->view_month;
                    ne->day = state->view_day;
                    ne->start_hour = state->new_hour;
                    ne->start_min = state->new_min;
                    calc_event_end_time(ne->start_hour, ne->start_min, state->new_duration_idx, &ne->end_hour, &ne->end_min);
                    ne->is_completed = false;
                    save_calendar_data(state);
                }
                state->show_new_modal = false;
                state->active_field = 0;
                state->input_title[0] = '\0';
                state->input_location[0] = '\0';
                state->input_desc[0] = '\0';
                rife_request_redraw(core);
                return;
            }
            // 取消
            if (mx >= mx0 + mw - 170.0f && mx <= mx0 + mw - 104.0f && my >= btn_y && my <= btn_y + 32.0f) {
                state->show_new_modal = false;
                state->active_field = 0;
                rife_request_redraw(core);
                return;
            }
            return;
        } else {
            // 点击外部关闭弹窗
            state->show_new_modal = false;
            state->active_field = 0;
            rife_request_redraw(core);
            return;
        }
    }

    // 2. 详情浮层卡片点击关闭或操作 (支持完成标记与物理删除)
    if (state->selected_event_id >= 0) {
        float pop_w = 280.0f;
        float pop_h = 175.0f;
        float pop_x = client_w - pop_w - 20.0f;
        float pop_y = 60.0f;
        if (mx >= pop_x && mx <= pop_x + pop_w && my >= pop_y && my <= pop_y + pop_h) {
            float act_y = pop_y + pop_h - 38.0f;
            // 标记完成 / 切换
            if (mx >= pop_x + 14.0f && mx <= pop_x + 100.0f && my >= act_y && my <= act_y + 26.0f) {
                for (int i = 0; i < state->event_count; i++) {
                    if ((int)state->events[i].id == state->selected_event_id) {
                        state->events[i].is_completed = !state->events[i].is_completed;
                        save_calendar_data(state);
                        break;
                    }
                }
                rife_request_redraw(core);
                return;
            }
            // 删除日程 (直接移除并写盘)
            if (mx >= pop_x + 104.0f && mx <= pop_x + 188.0f && my >= act_y && my <= act_y + 26.0f) {
                for (int i = 0; i < state->event_count; i++) {
                    if ((int)state->events[i].id == state->selected_event_id) {
                        for (int j = i; j < state->event_count - 1; j++) {
                            state->events[j] = state->events[j + 1];
                        }
                        state->event_count--;
                        save_calendar_data(state);
                        break;
                    }
                }
                state->selected_event_id = -1;
                rife_request_redraw(core);
                return;
            }
            // 关闭详情
            if (mx >= pop_x + pop_w - 68.0f && mx <= pop_x + pop_w - 14.0f && my >= act_y && my <= act_y + 26.0f) {
                state->selected_event_id = -1;
                rife_request_redraw(core);
                return;
            }
            return;
        } else {
            state->selected_event_id = -1;
            rife_request_redraw(core);
        }
    }

    // 3. 顶部 Header 交互
    if (my >= 0.0f && my <= header_h) {
        float today_btn_x = sidebar_w + 16.0f;
        // A. "今天" 快速重置按钮
        if (mx >= today_btn_x && mx <= today_btn_x + 50.0f && my >= 10.0f && my <= 38.0f) {
            state->view_year = state->cur_year;
            state->view_month = state->cur_month;
            state->view_day = state->cur_day;
            float hour_h = 60.0f;
            int target_h = (state->cur_hour >= 2) ? (state->cur_hour - 2) : 0;
            if (target_h > 16) target_h = 16;
            state->scroll_y = (float)target_h * hour_h;
            rife_request_redraw(core);
            return;
        }
        // B. 前翻页 `<` 按钮
        float nav_x = today_btn_x + 56.0f;
        if (mx >= nav_x && mx <= nav_x + 26.0f && my >= 10.0f && my <= 38.0f) {
            if (state->view_mode == CAL_VIEW_WEEK) {
                add_days(state->view_year, state->view_month, state->view_day, -7, &state->view_year, &state->view_month, &state->view_day);
            } else if (state->view_mode == CAL_VIEW_DAY) {
                add_days(state->view_year, state->view_month, state->view_day, -1, &state->view_year, &state->view_month, &state->view_day);
            } else {
                state->view_month--;
                if (state->view_month < 1) { state->view_month = 12; state->view_year--; }
            }
            rife_request_redraw(core);
            return;
        }
        // C. 后翻页 `>` 按钮
        float nav_r_x = nav_x + 30.0f;
        if (mx >= nav_r_x && mx <= nav_r_x + 26.0f && my >= 10.0f && my <= 38.0f) {
            if (state->view_mode == CAL_VIEW_WEEK) {
                add_days(state->view_year, state->view_month, state->view_day, 7, &state->view_year, &state->view_month, &state->view_day);
            } else if (state->view_mode == CAL_VIEW_DAY) {
                add_days(state->view_year, state->view_month, state->view_day, 1, &state->view_year, &state->view_month, &state->view_day);
            } else {
                state->view_month++;
                if (state->view_month > 12) { state->view_month = 1; state->view_year++; }
            }
            rife_request_redraw(core);
            return;
        }
        // D. 视图模式三段切换胶囊 [日] [周] [月]
        static const CalendarViewMode s_tab_modes[3] = { CAL_VIEW_DAY, CAL_VIEW_WEEK, CAL_VIEW_MONTH };
        float seg_item_w = 44.0f;
        float seg_w = 3.0f * seg_item_w + 4.0f;
        float seg_x = client_w - seg_w - 20.0f;
        if (mx >= seg_x && mx <= seg_x + seg_w && my >= 10.0f && my <= 38.0f) {
            for (int v = 0; v < 3; v++) {
                float ix = seg_x + 2.0f + (float)v * seg_item_w;
                if (mx >= ix && mx <= ix + seg_item_w) {
                    state->view_mode = s_tab_modes[v];
                    rife_request_redraw(core);
                    return;
                }
            }
        }
    }

    // 4. 右下角 FAB 悬浮新建日程按钮点击 (44x44 圆形)
    float fab_sz = 44.0f;
    float fab_x = client_w - fab_sz - 24.0f;
    float fab_y = client_h - fab_sz - 24.0f;
    if (mx >= fab_x && mx <= fab_x + fab_sz && my >= fab_y && my <= fab_y + fab_sz) {
        state->show_new_modal = true;
        state->active_field = 1;
        state->input_title[0] = '\0';
        state->input_location[0] = '\0';
        state->input_desc[0] = '\0';
        rife_request_redraw(core);
        return;
    }

    // 5. 左侧侧边栏交互 (迷你月历 & 搜索 & 自定义分类)
    if (mx >= 0.0f && mx <= sidebar_w && my > header_h) {
        float mini_y = header_h + 12.0f;
        // 迷你日历月份左右切换
        if (my >= mini_y && my <= mini_y + 24.0f) {
            if (mx >= sidebar_w - 48.0f && mx <= sidebar_w - 26.0f) {
                state->view_month--;
                if (state->view_month < 1) { state->view_month = 12; state->view_year--; }
                rife_request_redraw(core);
                return;
            }
            if (mx >= sidebar_w - 26.0f && mx <= sidebar_w - 6.0f) {
                state->view_month++;
                if (state->view_month > 12) { state->view_month = 1; state->view_year++; }
                rife_request_redraw(core);
                return;
            }
        }

        // A. 迷你月历点击日期跳转 (周日首列)
        float days_y = mini_y + 40.0f;
        float cell_sz = 22.0f;
        float grid_w = 7.0f * cell_sz;
        float grid_x = (sidebar_w - grid_w) * 0.5f;

        int first_wday = get_day_of_week_sun(state->view_year, state->view_month, 1);
        int total_days = days_in_month(state->view_year, state->view_month);

        for (int d = 1; d <= total_days; d++) {
            int slot = first_wday + d - 1;
            int row = slot / 7;
            int col = slot % 7;
            float cx = grid_x + (float)col * cell_sz;
            float cy = days_y + (float)row * cell_sz;

            if (mx >= cx && mx <= cx + cell_sz && my >= cy && my <= cy + cell_sz) {
                state->view_day = d;
                rife_request_redraw(core);
                return;
            }
        }

        // B. 侧边栏搜索栏快捷新建点击
        float search_y = days_y + 6.0f * cell_sz + 8.0f;
        float search_w = sidebar_w - 32.0f;
        if (mx >= 16.0f + search_w - 24.0f && mx <= 16.0f + search_w && my >= search_y && my <= search_y + 28.0f) {
            state->show_new_modal = true;
            state->active_field = 1;
            state->input_title[0] = '\0';
            state->input_location[0] = '\0';
            state->input_desc[0] = '\0';
            rife_request_redraw(core);
            return;
        }

        // C. 自定义标签列表勾选与新建
        float sec_y = search_y + 36.0f;
        // 点击右上角 [+] 打开新建分类弹窗
        if (mx >= sidebar_w - 36.0f && mx <= sidebar_w - 12.0f && my >= sec_y && my <= sec_y + 22.0f) {
            state->show_new_tag_modal = true;
            state->new_tag_name[0] = '\0';
            state->new_tag_color_idx = 0;
            state->active_field = 4;
            rife_request_redraw(core);
            return;
        }

        for (int c = 0; c < state->tag_count; c++) {
            float row_y = sec_y + 24.0f + (float)c * 26.0f;
            if (mx >= 16.0f && mx <= sidebar_w - 16.0f && my >= row_y && my <= row_y + 24.0f) {
                state->tags[c].is_enabled = !state->tags[c].is_enabled;
                save_calendar_data(state);
                rife_request_redraw(core);
                return;
            }
        }
    }

    // 6. 右侧工作区交互：周视图 / 日视图 / 月视图 / 日程列表
    if (mx > sidebar_w && my > header_h) {
        if (state->view_mode == CAL_VIEW_WEEK) {
            float main_x = sidebar_w;
            float main_w = client_w - sidebar_w;
            float ruler_w = 46.0f;
            float grid_left = main_x + ruler_w;
            float col_w = (main_w - ruler_w - 6.0f) / 7.0f;
            float header_bar_h = 52.0f;
            float grid_top = header_h + header_bar_h;
            float hour_h = 60.0f;

            int sun_y, sun_m, sun_d;
            get_week_sunday(state->view_year, state->view_month, state->view_day, &sun_y, &sun_m, &sun_d);

            // A. 先检查是否点击了已有日程卡片
            for (int i = 0; i < state->event_count; i++) {
                CalendarEvent* e = &state->events[i];
                if (!is_event_visible(state, e)) continue;

                for (int c = 0; c < 7; c++) {
                    int col_y, col_m, col_d;
                    add_days(sun_y, sun_m, sun_d, c, &col_y, &col_m, &col_d);
                    if (e->year == col_y && e->month == col_m && e->day == col_d) {
                        float cx = grid_left + (float)c * col_w + 2.0f;
                        float cw = col_w - 4.0f;
                        float start_f = (float)e->start_hour + (float)e->start_min / 60.0f;
                        float end_f = (float)e->end_hour + (float)e->end_min / 60.0f;
                        float cy = grid_top - state->scroll_y + start_f * hour_h + 1.0f;
                        float ch = (end_f - start_f) * hour_h - 2.0f;
                        if (ch < 24.0f) ch = 24.0f;

                        if (my >= grid_top && my <= client_h - 6.0f && mx >= cx && mx <= cx + cw && my >= cy && my <= cy + ch) {
                            state->selected_event_id = (int)e->id;
                            rife_request_redraw(core);
                            return;
                        }
                    }
                }
            }

            // B. 点击空白时间网格：快速创建该日该时段日程
            if (my >= grid_top && my <= client_h - 6.0f && mx >= grid_left && mx <= grid_left + 7.0f * col_w) {
                int c = (int)((mx - grid_left) / col_w);
                if (c < 0) c = 0; if (c > 6) c = 6;
                int h = (int)floorf((my - grid_top + state->scroll_y) / hour_h);
                if (h < 0) h = 0; if (h > 23) h = 23;

                int target_y, target_m, target_d;
                add_days(sun_y, sun_m, sun_d, c, &target_y, &target_m, &target_d);
                state->view_year = target_y;
                state->view_month = target_m;
                state->view_day = target_d;
                state->new_hour = h;
                state->new_min = 0;
                state->show_new_modal = true;
                state->active_field = 1;
                state->input_title[0] = '\0';
                state->input_location[0] = '\0';
                state->input_desc[0] = '\0';
                rife_request_redraw(core);
                return;
            }
        }
        else if (state->view_mode == CAL_VIEW_DAY) {
            float day_ruler_w = 56.0f;
            float day_header_h = 36.0f;
            float day_grid_top = header_h + day_header_h;
            float day_hour_h = 60.0f;
            float ex = sidebar_w + day_ruler_w + 14.0f;
            float ew = client_w - sidebar_w - day_ruler_w - 32.0f;

            // A. 先检查是否点击了已有日程卡片
            for (int i = 0; i < state->event_count; i++) {
                CalendarEvent* e = &state->events[i];
                if (!is_event_visible(state, e)) continue;
                if (e->year == state->view_year && e->month == state->view_month && e->day == state->view_day) {
                    float start_f = (float)e->start_hour + (float)e->start_min / 60.0f;
                    float end_f = (float)e->end_hour + (float)e->end_min / 60.0f;
                    float ey = day_grid_top - state->scroll_y + start_f * day_hour_h;
                    float eh = (end_f - start_f) * day_hour_h;
                    if (eh < 34.0f) eh = 34.0f;

                    if (my >= day_grid_top && my <= client_h - 6.0f && mx >= ex && mx <= ex + ew && my >= ey && my <= ey + eh) {
                        state->selected_event_id = (int)e->id;
                        rife_request_redraw(core);
                        return;
                    }
                }
            }

            // B. 点击日视图空白区域新建
            if (my >= day_grid_top && my <= client_h - 6.0f && mx >= ex && mx <= ex + ew) {
                int h = (int)floorf((my - day_grid_top + state->scroll_y) / day_hour_h);
                if (h < 0) h = 0; if (h > 23) h = 23;
                state->new_hour = h;
                state->new_min = 0;
                state->show_new_modal = true;
                state->active_field = 1;
                state->input_title[0] = '\0';
                state->input_location[0] = '\0';
                state->input_desc[0] = '\0';
                rife_request_redraw(core);
                return;
            }
        }
        else if (state->view_mode == CAL_VIEW_MONTH) {
            float main_x = sidebar_w;
            float main_w = client_w - sidebar_w;
            float m_col_w = main_w / 7.0f;
            float m_row_h = (client_h - header_h - 28.0f) / 6.0f;
            float m_top = header_h + 28.0f;

            if (my >= m_top && my <= client_h - 4.0f && mx >= main_x && mx <= main_x + main_w) {
                int c = (int)((mx - main_x) / m_col_w);
                int r = (int)((my - m_top) / m_row_h);
                if (c >= 0 && c < 7 && r >= 0 && r < 6) {
                    int first_w = get_day_of_week_sun(state->view_year, state->view_month, 1);
                    int total_d = days_in_month(state->view_year, state->view_month);
                    int slot = r * 7 + c;
                    int d = slot - first_w + 1;
                    if (d >= 1 && d <= total_d) {
                        float cx = main_x + (float)c * m_col_w;
                        float cy = m_top + (float)r * m_row_h;

                        // 检查是否点击了当日日程芯片
                        int item_idx = 0;
                        for (int i = 0; i < state->event_count; i++) {
                            CalendarEvent* e = &state->events[i];
                            if (!is_event_visible(state, e)) continue;
                            if (e->year == state->view_year && e->month == state->view_month && e->day == d) {
                                float chip_y = cy + 32.0f + (float)item_idx * 20.0f;
                                if (chip_y + 18.0f < cy + m_row_h) {
                                    if (mx >= cx + 4.0f && mx <= cx + m_col_w - 4.0f && my >= chip_y && my <= chip_y + 18.0f) {
                                        state->selected_event_id = (int)e->id;
                                        rife_request_redraw(core);
                                        return;
                                    }
                                    item_idx++;
                                }
                            }
                        }

                        // 点击月历格子空白区域：设定日期并弹出新建
                        state->view_day = d;
                        state->new_hour = 10;
                        state->new_min = 0;
                        state->show_new_modal = true;
                        state->active_field = 1;
                        state->input_title[0] = '\0';
                        state->input_location[0] = '\0';
                        state->input_desc[0] = '\0';
                        rife_request_redraw(core);
                        return;
                    }
                }
            }
        }
        else if (state->view_mode == CAL_VIEW_AGENDA) {
            float main_x = sidebar_w;
            float main_w = client_w - sidebar_w;
            float list_y = header_h + 16.0f;
            float card_y = list_y + 36.0f;
            float cw = main_w - 48.0f;
            float ch = 48.0f;

            for (int i = 0; i < state->event_count; i++) {
                CalendarEvent* e = &state->events[i];
                if (!is_event_visible(state, e)) continue;

                float cy = card_y;
                // 复选框点击：完成 / 取消完成
                if (mx >= main_x + 36.0f && mx <= main_x + 60.0f && my >= cy + 12.0f && my <= cy + 36.0f) {
                    e->is_completed = !e->is_completed;
                    save_calendar_data(state);
                    rife_request_redraw(core);
                    return;
                }
                // 卡片正文点击：查看详情
                if (mx >= main_x + 24.0f && mx <= main_x + 24.0f + cw && my >= cy && my <= cy + ch) {
                    state->selected_event_id = (int)e->id;
                    rife_request_redraw(core);
                    return;
                }
                card_y += ch + 10.0f;
                if (card_y + ch > client_h) break;
            }
        }
    }
}

// -------------------------------------------------------------
// 通用轻量文本输入框绘制器 (Text Box Widget)
// -------------------------------------------------------------

static void draw_input_box(RifeCore* core, float x, float y, float w, float h,
                           const char* text, const char* placeholder,
                           bool is_focused, bool cursor_on,
                           bool is_dark, uint32_t accent_color, uint32_t border_col,
                           uint32_t text_title, uint32_t text_muted)
{
    uint32_t bg_col = is_dark ? (is_focused ? 0x281F3DFF : 0x1E172EFF) : (is_focused ? 0xFFFFFFFF : 0xF8FAFCFF);
    uint32_t brd_col = is_focused ? accent_color : border_col;
    rife_draw_round_rect(core, x, y, w, h, 6.0f, bg_col, brd_col);

    if (text && text[0] != '\0') {
        rife_draw_text_font(core, x + 10.0f, y + 7.0f, text, text_title, 0);
        if (is_focused && cursor_on) {
            float tw = 0.0f;
            const unsigned char* p = (const unsigned char*)text;
            while (*p) {
                if (*p < 0x80) { tw += 7.5f; p++; }
                else if ((*p & 0xE0) == 0xC0) { tw += 12.0f; p += 2; }
                else if ((*p & 0xF0) == 0xE0) { tw += 13.0f; p += 3; }
                else { tw += 14.0f; p += 4; }
            }
            float cur_x = x + 10.0f + tw + 1.0f;
            if (cur_x < x + w - 8.0f) {
                rife_draw_rect(core, cur_x, y + 6.0f, 1.5f, h - 12.0f, accent_color);
            }
        }
    } else {
        if (is_focused) {
            if (cursor_on) {
                rife_draw_rect(core, x + 10.0f, y + 6.0f, 1.5f, h - 12.0f, accent_color);
            }
            if (placeholder) {
                rife_draw_text_font(core, x + 14.0f, y + 7.0f, placeholder, is_dark ? 0x6B5888FF : 0xCBD5E1FF, 3);
            }
        } else {
            if (placeholder) {
                rife_draw_text_font(core, x + 10.0f, y + 7.0f, placeholder, text_muted, 3);
            }
        }
    }
}

// -------------------------------------------------------------
// Rtodo 多维渲染器 (Render)
// -------------------------------------------------------------

static void calendar_render(void* inst, RifeCore* core, float client_x, float client_y, float client_w, float client_h) {
    CalendarState* state = (CalendarState*)inst;
    if (!state) return;

    RifeSystemConfig* cfg = rife_get_system_config();
    bool is_zh = (cfg->language == LANG_ZH_CN);
    bool is_dark = (cfg->palette == PALETTE_OBSIDIAN || cfg->cloud_color == CLOUD_COLOR_OBSIDIAN);

    // 主色与背景令牌 (液态玻璃全景透光)
    uint32_t bg_sidebar = is_dark ? 0x16112235 : 0xFFFFFF28;
    uint32_t border_col = is_dark ? 0x47346A55 : 0x00000014;
    uint32_t text_title = is_dark ? 0xF8FAFCFF : 0x0F172AFF;
    uint32_t text_muted = is_dark ? 0x94A3B8FF : 0x64748BFF;
    uint32_t rtodo_blue = 0x3370FFFF;

    float header_h = 48.0f;
    float sidebar_w = 185.0f;

    // A. 基础容器底色 (液态玻璃全透贯通，左侧细腻磨砂侧栏)
    if ((bg_sidebar & 0xFF) > 0) {
        rife_draw_round_rect(core, client_x, client_y, sidebar_w, client_h, 18.0f, bg_sidebar, 0x00000000);
        rife_draw_rect(core, client_x + sidebar_w - 18.0f, client_y, 18.0f, client_h, bg_sidebar);
    }
    rife_draw_rect(core, client_x + sidebar_w, client_y, 1.0f, client_h, border_col);
    rife_draw_rect(core, client_x + sidebar_w, client_y + header_h, client_w - sidebar_w, 1.0f, border_col);

    // B. 顶部 Header 区域 (极简清爽通透)
    float main_x = client_x + sidebar_w;
    float main_w = client_w - sidebar_w;
    float main_y = client_y + header_h;
    float main_h = client_h - header_h;

    // 1. "今天" 按钮 (50x28, 优雅晶莹微倒角)
    float today_btn_x = main_x + 16.0f;
    float today_btn_y = client_y + 10.0f;
    rife_draw_round_rect(core, today_btn_x, today_btn_y, 50.0f, 28.0f, 6.0f, is_dark ? 0x221A3388 : 0xFFFFFF88, border_col);
    rife_draw_text_font(core, today_btn_x + 12.0f, today_btn_y + 6.0f, is_zh ? "今天" : "Today", text_title, 0);

    // 2. 前翻 / 后翻箭头按钮
    float nav_x = today_btn_x + 56.0f;
    rife_draw_round_rect(core, nav_x, today_btn_y, 26.0f, 28.0f, 6.0f, is_dark ? 0x221A3388 : 0xFFFFFF88, border_col);
    rife_draw_text_font(core, nav_x + 9.0f, today_btn_y + 6.0f, "<", text_title, 0);

    float nav_r_x = nav_x + 30.0f;
    rife_draw_round_rect(core, nav_r_x, today_btn_y, 26.0f, 28.0f, 6.0f, is_dark ? 0x221A3388 : 0xFFFFFF88, border_col);
    rife_draw_text_font(core, nav_r_x + 9.0f, today_btn_y + 6.0f, ">", text_title, 0);

    // 3. 当前年月大标题 (18px Normal)
    char title_buf[64];
    static const char* mon_names[13] = { "", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    if (is_zh) {
        snprintf(title_buf, sizeof(title_buf), "%d年%d月", state->view_year, state->view_month);
    } else {
        snprintf(title_buf, sizeof(title_buf), "%s %d", mon_names[state->view_month], state->view_year);
    }
    rife_draw_text_font(core, nav_r_x + 38.0f, today_btn_y + 4.0f, title_buf, text_title, 2);

    // 4. 右侧视图切换三段胶囊 [日] [周] [月]
    static const CalendarViewMode s_tab_modes[3] = { CAL_VIEW_DAY, CAL_VIEW_WEEK, CAL_VIEW_MONTH };
    const char* view_labels_zh[3] = { "日", "周", "月" };
    const char* view_labels_en[3] = { "Day", "Week", "Month" };

    float seg_item_w = 44.0f;
    float seg_w = 3.0f * seg_item_w + 4.0f;
    float seg_x = client_x + client_w - seg_w - 20.0f;
    float seg_y = today_btn_y;
    rife_draw_round_rect(core, seg_x, seg_y, seg_w, 28.0f, 6.0f, is_dark ? 0x1E172C66 : 0x0000000A, border_col);

    for (int v = 0; v < 3; v++) {
        float vx = seg_x + 2.0f + (float)v * seg_item_w;
        bool is_act = (state->view_mode == s_tab_modes[v]);
        if (is_act) {
            uint32_t act_bg = is_dark ? 0x382A57CC : 0xFFFFFFCC;
            uint32_t act_brd = is_dark ? 0x634E8CFF : 0xCBD5E1CC;
            rife_draw_round_rect(core, vx, seg_y + 1.5f, seg_item_w, 25.0f, 5.0f, act_bg, act_brd);
            rife_draw_text_font(core, vx + (is_zh ? 16.0f : 8.0f), seg_y + 5.0f, is_zh ? view_labels_zh[v] : view_labels_en[v], rtodo_blue, 5);
        } else {
            rife_draw_text_font(core, vx + (is_zh ? 16.0f : 8.0f), seg_y + 5.0f, is_zh ? view_labels_zh[v] : view_labels_en[v], text_muted, 0);
        }
    }

    // C. 左侧侧边栏 (迷你月历 & 搜索 & 自定义分类)
    float sb_x = client_x;
    float sb_y = client_y + header_h;

    // 1. 侧边栏顶部年月与翻页
    char mini_m_str[32];
    if (is_zh) snprintf(mini_m_str, sizeof(mini_m_str), "%d年%d月", state->view_year, state->view_month);
    else snprintf(mini_m_str, sizeof(mini_m_str), "%s %d", mon_names[state->view_month], state->view_year);
    rife_draw_text_font(core, sb_x + 16.0f, sb_y + 12.0f, mini_m_str, text_title, 1);
    rife_draw_text_font(core, sb_x + sidebar_w - 44.0f, sb_y + 12.0f, "<", text_muted, 0);
    rife_draw_text_font(core, sb_x + sidebar_w - 22.0f, sb_y + 12.0f, ">", text_muted, 0);

    // 2. 迷你月历周表头 (周日首位)
    float cell_sz = 22.0f;
    float mini_w = 7.0f * cell_sz;
    float mini_x = sb_x + (sidebar_w - mini_w) * 0.5f;
    float mini_y = sb_y + 38.0f;

    const char* mini_week_zh[7] = { "日", "一", "二", "三", "四", "五", "六" };
    for (int i = 0; i < 7; i++) {
        rife_draw_text_font(core, mini_x + (float)i * cell_sz + 5.0f, mini_y, mini_week_zh[i], text_muted, 4);
    }

    // 3. 迷你月历数字
    int first_wday = get_day_of_week_sun(state->view_year, state->view_month, 1);
    int total_days = days_in_month(state->view_year, state->view_month);
    float days_y = mini_y + 18.0f;

    for (int d = 1; d <= total_days; d++) {
        int slot = first_wday + d - 1;
        int row = slot / 7;
        int col = slot % 7;
        float cx = mini_x + (float)col * cell_sz;
        float cy = days_y + (float)row * cell_sz;

        bool is_today = (state->view_year == state->cur_year && state->view_month == state->cur_month && d == state->cur_day);
        bool is_sel = (d == state->view_day);

        if (is_today) {
            rife_draw_round_rect(core, cx + 1.0f, cy + 1.0f, cell_sz - 2.0f, cell_sz - 2.0f, 10.0f, is_dark ? 0x2A2045FF : 0xEFF6FFFF, rtodo_blue);
        } else if (is_sel) {
            rife_draw_round_rect(core, cx + 1.0f, cy + 1.0f, cell_sz - 2.0f, cell_sz - 2.0f, 4.0f, is_dark ? 0x241C38FF : 0xF1F5F9FF, border_col);
        }

        char d_str[8];
        snprintf(d_str, sizeof(d_str), "%d", d);
        float tx = cx + (d < 10 ? 7.0f : 4.0f);
        uint32_t tc = is_today ? rtodo_blue : (is_sel ? (is_dark ? 0xC084FCFF : rtodo_blue) : text_title);
        rife_draw_text_font(core, tx, cy + 3.0f, d_str, tc, 4);
    }

    // 4. 侧边栏搜索栏
    float search_y = days_y + 6.0f * cell_sz + 8.0f;
    float search_w = sidebar_w - 32.0f;
    rife_draw_round_rect(core, sb_x + 16.0f, search_y, search_w, 28.0f, 14.0f, is_dark ? 0x201832FF : 0xF5F6F7FF, border_col);
    rife_draw_text_font(core, sb_x + 28.0f, search_y + 6.0f, is_zh ? "🔍 搜索日程、会议..." : "🔍 Search...", text_muted, 4);
    rife_draw_text_font(core, sb_x + 16.0f + search_w - 18.0f, search_y + 5.0f, "+", text_muted, 1);

    // 5. 用户自定义分类/日历列表 (动态展示自定义标签)
    float sec_y = search_y + 36.0f;
    rife_draw_text_font(core, sb_x + 16.0f, sec_y, is_zh ? "我的日历" : "My Calendars", text_title, 5);
    // 侧边栏新建分类快捷 [+] 按钮
    rife_draw_text_font(core, sb_x + sidebar_w - 24.0f, sec_y - 1.0f, "+", rtodo_blue, 1);

    for (int c = 0; c < state->tag_count; c++) {
        float row_y = sec_y + 24.0f + (float)c * 26.0f;
        CustomTag* tag = &state->tags[c];
        TagColorStyle cc = get_tag_style(tag->color_bar, is_dark);
        bool checked = tag->is_enabled;

        if (checked) {
            rife_draw_round_rect(core, sb_x + 16.0f, row_y + 3.0f, 13.0f, 13.0f, 3.0f, cc.bar, cc.bar);
            rife_draw_rect(core, sb_x + 18.0f, row_y + 8.0f, 2.0f, 4.0f, 0xFFFFFFFF);
            rife_draw_rect(core, sb_x + 20.0f, row_y + 10.0f, 2.0f, 2.0f, 0xFFFFFFFF);
            rife_draw_rect(core, sb_x + 22.0f, row_y + 6.0f, 2.0f, 6.0f, 0xFFFFFFFF);
        } else {
            rife_draw_round_rect(core, sb_x + 16.0f, row_y + 3.0f, 13.0f, 13.0f, 3.0f, is_dark ? 0x221B33FF : 0xFFFFFFFF, border_col);
        }

        rife_draw_text_font(core, sb_x + 36.0f, row_y + 1.0f, tag->name, text_title, 3);
    }

    // D. 右侧主工作区 (周视图 / 日视图 / 月视图)
    uint32_t grid_line_col = is_dark ? 0x38285533 : 0x0000000E; // 极简微淡网格线

    // ==========================================
    // 视图 1：周视图 (Week View - 纵向滚动舒展大表格)
    // ==========================================
    if (state->view_mode == CAL_VIEW_WEEK) {
        float ruler_w = 46.0f;
        float grid_left = main_x + ruler_w;
        float col_w = (main_w - ruler_w - 6.0f) / 7.0f;
        float header_bar_h = 52.0f;
        float grid_top = main_y + header_bar_h;
        float avail_h = main_h - header_bar_h - 4.0f;
        float hour_h = 60.0f; // 舒适大格子，每小时 60px
        float total_content_h = 24.0f * hour_h;
        float max_scroll = total_content_h - avail_h;
        if (max_scroll < 0.0f) max_scroll = 0.0f;
        if (state->scroll_y < 0.0f) state->scroll_y = 0.0f;
        if (state->scroll_y > max_scroll) state->scroll_y = max_scroll;

        int sun_y, sun_m, sun_d;
        get_week_sunday(state->view_year, state->view_month, state->view_day, &sun_y, &sun_m, &sun_d);

        // 1. 周列头固定顶栏 (周日首位，上层星期，下层21px醒目大数字)
        rife_draw_rect(core, main_x, main_y, main_w, header_bar_h, is_dark ? 0x16112240 : 0xFFFFFF35);
        rife_draw_text_font(core, main_x + 6.0f, main_y + 12.0f, "GMT+8", text_muted, 4);

        const char* wk_names[7] = { "周日", "周一", "周二", "周三", "周四", "周五", "周六" };
        const char* wk_names_en[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };

        for (int c = 0; c < 7; c++) {
            float cx = grid_left + (float)c * col_w;
            float col_mid_x = cx + col_w * 0.5f;
            int cy_y, cy_m, cy_d;
            add_days(sun_y, sun_m, sun_d, c, &cy_y, &cy_m, &cy_d);
            bool is_col_today = (cy_y == state->cur_year && cy_m == state->cur_month && cy_d == state->cur_day);

            // 列分割细线 (表头)
            rife_draw_rect(core, cx, main_y, 1.0f, header_bar_h, grid_line_col);

            // 上层：星期文字居中 (11px)
            rife_draw_text_font(core, col_mid_x - 11.0f, main_y + 8.0f, is_zh ? wk_names[c] : wk_names_en[c], is_col_today ? rtodo_blue : text_muted, 4);

            // 下层：日期大数字居中 (21px Bold，font_id == 6)
            char d_buf[8];
            snprintf(d_buf, sizeof(d_buf), "%d", cy_d);
            float d_off = (cy_d >= 10) ? 10.0f : 5.0f;
            rife_draw_text_font(core, col_mid_x - d_off, main_y + 24.0f, d_buf, is_col_today ? rtodo_blue : text_title, 6);
        }
        // 表头下边缘底线
        rife_draw_rect(core, main_x, grid_top, main_w, 1.0f, border_col);

        // 2. 纵向可滚动时间网格区域 (硬件视口裁剪)
        rife_push_scissor(core, main_x, grid_top + 1.0f, main_w, avail_h);

        // 绘制 7 列纵向贯通网格线
        for (int c = 0; c < 7; c++) {
            float cx = grid_left + (float)c * col_w;
            rife_draw_rect(core, cx, grid_top, 1.0f, avail_h, grid_line_col);
        }
        rife_draw_rect(core, grid_left + 7.0f * col_w, grid_top, 1.0f, avail_h, grid_line_col);

        // 时间标尺与横向网格线 (00:00 ~ 24:00 共 24 小时大格子)
        for (int h = 0; h <= 24; h++) {
            float hy = grid_top - state->scroll_y + (float)h * hour_h;
            if (hy + 20.0f < grid_top || hy - 20.0f > grid_top + avail_h) continue;

            char h_str[8];
            snprintf(h_str, sizeof(h_str), "%02d:00", h);
            rife_draw_text_font(core, main_x + 8.0f, hy - 6.0f, h_str, text_muted, 4);
            rife_draw_rect(core, grid_left, hy, col_w * 7.0f, 1.0f, grid_line_col);
        }

        // 3. 渲染事件卡片 (舒展大格卡片)
        for (int i = 0; i < state->event_count; i++) {
            CalendarEvent* e = &state->events[i];
            if (!is_event_visible(state, e)) continue;

            for (int c = 0; c < 7; c++) {
                int cy_y, cy_m, cy_d;
                add_days(sun_y, sun_m, sun_d, c, &cy_y, &cy_m, &cy_d);
                if (e->year == cy_y && e->month == cy_m && e->day == cy_d) {
                    float cx = grid_left + (float)c * col_w + 2.0f;
                    float cw = col_w - 4.0f;
                    float start_f = (float)e->start_hour + (float)e->start_min / 60.0f;
                    float end_f = (float)e->end_hour + (float)e->end_min / 60.0f;
                    float cy = grid_top - state->scroll_y + start_f * hour_h + 1.0f;
                    float ch = (end_f - start_f) * hour_h - 2.0f;
                    if (ch < 24.0f) ch = 24.0f;

                    if (cy + ch < grid_top || cy > grid_top + avail_h) continue;

                    TagColorStyle cc = get_event_style(state, e->tag_idx, is_dark);

                    rife_push_scissor(core, cx + 1.0f, cy, cw - 2.0f, ch);
                    rife_draw_round_rect(core, cx, cy, cw, ch, 6.0f, cc.bg, cc.border);
                    rife_draw_round_rect(core, cx + 2.0f, cy + 3.0f, 3.0f, ch - 6.0f, 1.5f, cc.bar, cc.bar);

                    rife_draw_text_font(core, cx + 8.0f, cy + 4.0f, e->title, cc.text, 3);
                    if (ch >= 36.0f) {
                        char time_buf[32];
                        snprintf(time_buf, sizeof(time_buf), "%02d:%02d-%02d:%02d", e->start_hour, e->start_min, e->end_hour, e->end_min);
                        rife_draw_text_font(core, cx + 8.0f, cy + 20.0f, time_buf, text_muted, 4);
                    }
                    if (ch >= 52.0f && e->location[0]) {
                        rife_draw_text_font(core, cx + 8.0f, cy + 34.0f, e->location, text_muted, 4);
                    }
                    rife_pop_scissor(core);
                }
            }
        }

        // 4. 实时时间红线 (Current Time Red Indicator Line)
        float cur_f = (float)state->cur_hour + (float)state->cur_min / 60.0f;
        float red_y = grid_top - state->scroll_y + cur_f * hour_h;
        if (red_y >= grid_top && red_y <= grid_top + avail_h) {
            char red_str[16];
            snprintf(red_str, sizeof(red_str), "%02d:%02d", state->cur_hour, state->cur_min);
            rife_draw_round_rect(core, main_x + 4.0f, red_y - 8.0f, 38.0f, 16.0f, 4.0f, 0xF53F3FFF, 0xF53F3FFF);
            rife_draw_text_font(core, main_x + 7.0f, red_y - 7.0f, red_str, 0xFFFFFFFF, 4);
            rife_draw_rect(core, grid_left, red_y - 0.5f, col_w * 7.0f, 1.5f, 0xF53F3FFF);

            for (int c = 0; c < 7; c++) {
                int cy_y, cy_m, cy_d;
                add_days(sun_y, sun_m, sun_d, c, &cy_y, &cy_m, &cy_d);
                if (cy_y == state->cur_year && cy_m == state->cur_month && cy_d == state->cur_day) {
                    float red_dot_x = grid_left + (float)c * col_w;
                    rife_draw_round_rect(core, red_dot_x - 3.0f, red_y - 3.0f, 6.0f, 6.0f, 3.0f, 0xF53F3FFF, 0xFFFFFFFF);
                    break;
                }
            }
        }

        // 5. 右侧微动滚动条 (Scrollbar Thumb)
        if (max_scroll > 0.0f) {
            float sb_bar_w = 4.0f;
            float sb_bar_x = main_x + main_w - sb_bar_w - 2.0f;
            float thumb_h = avail_h * (avail_h / total_content_h);
            if (thumb_h < 36.0f) thumb_h = 36.0f;
            float thumb_y = grid_top + (state->scroll_y / max_scroll) * (avail_h - thumb_h);
            rife_draw_round_rect(core, sb_bar_x, thumb_y, sb_bar_w, thumb_h, 2.0f, is_dark ? 0x634E8C66 : 0xCBD5E199, 0);
        }

        rife_pop_scissor(core);
    }
    // ==========================================
    // 视图 2：日视图 (Day View - 纵向滚动大时间轴)
    // ==========================================
    else if (state->view_mode == CAL_VIEW_DAY) {
        float day_ruler_w = 56.0f;
        float day_header_h = 36.0f;
        float day_grid_top = main_y + day_header_h;
        float day_avail_h = main_h - day_header_h - 4.0f;
        float day_hour_h = 60.0f;
        float total_day_h = 24.0f * day_hour_h;
        float max_scroll = total_day_h - day_avail_h;
        if (max_scroll < 0.0f) max_scroll = 0.0f;
        if (state->scroll_y < 0.0f) state->scroll_y = 0.0f;
        if (state->scroll_y > max_scroll) state->scroll_y = max_scroll;

        // 固定顶栏标题 (磨砂毛玻璃)
        rife_draw_rect(core, main_x, main_y, main_w, day_header_h, is_dark ? 0x16112240 : 0xFFFFFF35);
        rife_draw_text_font(core, main_x + 18.0f, main_y + 10.0f, is_zh ? "今日重点时间轴" : "Daily Agenda Timeline", text_title, 1);
        rife_draw_rect(core, main_x, day_grid_top, main_w, 1.0f, border_col);

        rife_push_scissor(core, main_x, day_grid_top + 1.0f, main_w, day_avail_h);

        // 时间刻度 00:00 ~ 24:00
        for (int h = 0; h <= 24; h++) {
            float hy = day_grid_top - state->scroll_y + (float)h * day_hour_h;
            if (hy + 20.0f < day_grid_top || hy - 20.0f > day_grid_top + day_avail_h) continue;

            char h_str[8];
            snprintf(h_str, sizeof(h_str), "%02d:00", h);
            rife_draw_text_font(core, main_x + 14.0f, hy - 6.0f, h_str, text_muted, 4);
            rife_draw_rect(core, main_x + day_ruler_w + 14.0f, hy, main_w - day_ruler_w - 30.0f, 1.0f, grid_line_col);
        }

        // 渲染单日大日程卡片
        float ex = main_x + day_ruler_w + 14.0f;
        float ew = main_w - day_ruler_w - 32.0f;

        for (int i = 0; i < state->event_count; i++) {
            CalendarEvent* e = &state->events[i];
            if (!is_event_visible(state, e)) continue;
            if (e->year == state->view_year && e->month == state->view_month && e->day == state->view_day) {
                float start_f = (float)e->start_hour + (float)e->start_min / 60.0f;
                float end_f = (float)e->end_hour + (float)e->end_min / 60.0f;
                float ey = day_grid_top - state->scroll_y + start_f * day_hour_h;
                float eh = (end_f - start_f) * day_hour_h;
                if (eh < 34.0f) eh = 34.0f;

                if (ey + eh < day_grid_top || ey > day_grid_top + day_avail_h) continue;

                TagColorStyle cc = get_event_style(state, e->tag_idx, is_dark);
                rife_push_scissor(core, ex, ey, ew, eh);

                rife_draw_round_rect(core, ex, ey, ew, eh, 8.0f, cc.bg, cc.border);
                rife_draw_round_rect(core, ex + 2.0f, ey + 2.0f, 5.0f, eh - 4.0f, 2.5f, cc.bar, cc.bar);

                rife_draw_text_font(core, ex + 14.0f, ey + 6.0f, e->title, cc.text, 1);
                char t_sub[64];
                snprintf(t_sub, sizeof(t_sub), "%02d:%02d - %02d:%02d · %s", e->start_hour, e->start_min, e->end_hour, e->end_min, e->location);
                rife_draw_text_font(core, ex + 14.0f, ey + 24.0f, t_sub, text_muted, 3);
                if (eh >= 56.0f && e->desc[0]) {
                    rife_draw_text_font(core, ex + 14.0f, ey + 42.0f, e->desc, text_muted, 4);
                }

                rife_pop_scissor(core);
            }
        }

        // 当前时间红线 (日视图 - 红色防重叠圆角胶囊徽标)
        float cur_f = (float)state->cur_hour + (float)state->cur_min / 60.0f;
        float red_y = day_grid_top - state->scroll_y + cur_f * day_hour_h;
        if (red_y >= day_grid_top && red_y <= day_grid_top + day_avail_h) {
            char red_str[16];
            snprintf(red_str, sizeof(red_str), "%02d:%02d", state->cur_hour, state->cur_min);
            rife_draw_round_rect(core, main_x + 6.0f, red_y - 8.0f, 38.0f, 16.0f, 4.0f, 0xF53F3FFF, 0xF53F3FFF);
            rife_draw_text_font(core, main_x + 9.0f, red_y - 7.0f, red_str, 0xFFFFFFFF, 4);
            rife_draw_rect(core, ex, red_y - 0.5f, ew, 1.5f, 0xF53F3FFF);
        }

        // 滚动条
        if (max_scroll > 0.0f) {
            float sb_bar_w = 4.0f;
            float sb_bar_x = main_x + main_w - sb_bar_w - 2.0f;
            float thumb_h = day_avail_h * (day_avail_h / total_day_h);
            if (thumb_h < 36.0f) thumb_h = 36.0f;
            float thumb_y = day_grid_top + (state->scroll_y / max_scroll) * (day_avail_h - thumb_h);
            rife_draw_round_rect(core, sb_bar_x, thumb_y, sb_bar_w, thumb_h, 2.0f, is_dark ? 0x634E8C66 : 0xCBD5E199, 0);
        }

        rife_pop_scissor(core);
    }
    // ==========================================
    // 视图 3：月视图 (Month View - 35/42格大月历)
    // ==========================================
    else if (state->view_mode == CAL_VIEW_MONTH) {
        float m_col_w = main_w / 7.0f;
        float m_row_h = (main_h - 28.0f) / 6.0f;
        float m_top = main_y + 28.0f;

        const char* wk_names[7] = { "周日", "周一", "周二", "周三", "周四", "周五", "周六" };
        for (int c = 0; c < 7; c++) {
            float mid_x = main_x + (float)c * m_col_w + m_col_w * 0.5f;
            rife_draw_text_font(core, mid_x - 12.0f, main_y + 6.0f, wk_names[c], text_muted, 3);
        }
        rife_draw_rect(core, main_x, m_top, main_w, 1.0f, border_col);

        int first_w = get_day_of_week_sun(state->view_year, state->view_month, 1);
        int total_d = days_in_month(state->view_year, state->view_month);

        for (int d = 1; d <= total_d; d++) {
            int slot = first_w + d - 1;
            int r = slot / 7;
            int c = slot % 7;
            if (r >= 6) break;

            float cx = main_x + (float)c * m_col_w;
            float cy = m_top + (float)r * m_row_h;

            rife_draw_round_rect(core, cx, cy, m_col_w, m_row_h, 0.0f, 0x00000000, border_col);

            char d_str[8];
            snprintf(d_str, sizeof(d_str), "%d", d);
            bool is_td = (state->view_year == state->cur_year && state->view_month == state->cur_month && d == state->cur_day);
            if (is_td) {
                rife_draw_round_rect(core, cx + 8.0f, cy + 6.0f, 22.0f, 22.0f, 11.0f, rtodo_blue, rtodo_blue);
                rife_draw_text_font(core, cx + (d >= 10 ? 12.0f : 15.0f), cy + 8.0f, d_str, 0xFFFFFFFF, 5);
            } else {
                rife_draw_text_font(core, cx + 10.0f, cy + 8.0f, d_str, text_title, 3);
            }

            // 该日日程圆点胶囊
            int item_idx = 0;
            for (int i = 0; i < state->event_count; i++) {
                CalendarEvent* e = &state->events[i];
                if (!is_event_visible(state, e)) continue;
                if (e->year == state->view_year && e->month == state->view_month && e->day == d) {
                    TagColorStyle cc = get_event_style(state, e->tag_idx, is_dark);
                    float chip_y = cy + 32.0f + (float)item_idx * 20.0f;
                    if (chip_y + 18.0f < cy + m_row_h) {
                        rife_draw_round_rect(core, cx + 4.0f, chip_y, m_col_w - 8.0f, 18.0f, 4.0f, cc.bg, cc.border);
                        rife_draw_round_rect(core, cx + 6.0f, chip_y + 3.0f, 3.0f, 12.0f, 1.5f, cc.bar, cc.bar);
                        rife_draw_text_font(core, cx + 14.0f, chip_y + 2.0f, e->title, cc.text, 4);
                        item_idx++;
                    }
                }
            }
        }
    }
    // ==========================================
    // 视图 4：日程清单列表 (Agenda View - 信息流卡片)
    // ==========================================
    else if (state->view_mode == CAL_VIEW_AGENDA) {
        float list_y = main_y + 16.0f;
        rife_draw_text_font(core, main_x + 24.0f, list_y, is_zh ? "全部日程待办清单" : "Agenda & Task Overview", text_title, 1);

        int visible_events = 0;
        for (int i = 0; i < state->event_count; i++) {
            if (is_event_visible(state, &state->events[i])) visible_events++;
        }

        if (visible_events == 0) {
            float empty_cx = main_x + main_w * 0.5f;
            float empty_cy = main_y + main_h * 0.42f;

            // 柔和磨砂空状态大圆盘
            rife_draw_round_rect(core, empty_cx - 36.0f, empty_cy - 36.0f, 72.0f, 72.0f, 36.0f, is_dark ? 0x271D4288 : 0xF1F5F9CC, border_col);
            // 内部小徽标勾选图标
            rife_draw_round_rect(core, empty_cx - 16.0f, empty_cy - 16.0f, 32.0f, 32.0f, 16.0f, rtodo_blue, rtodo_blue);
            rife_draw_rect(core, empty_cx - 7.0f, empty_cy + 2.0f, 2.5f, 6.0f, 0xFFFFFFFF);
            rife_draw_rect(core, empty_cx - 4.5f, empty_cy + 6.0f, 4.0f, 2.5f, 0xFFFFFFFF);
            rife_draw_rect(core, empty_cx - 0.5f, empty_cy - 4.0f, 2.5f, 12.0f, 0xFFFFFFFF);

            rife_draw_text_font(core, empty_cx - (is_zh ? 48.0f : 60.0f), empty_cy + 48.0f, is_zh ? "暂无待办日程" : "No Upcoming Events", text_title, 1);
            rife_draw_text_font(core, empty_cx - (is_zh ? 116.0f : 140.0f), empty_cy + 74.0f, is_zh ? "点击右下角 '+' 或网格空白处快速创建新日程" : "Click '+' or any grid slot to create an event", text_muted, 3);
        } else {
            float card_y = list_y + 36.0f;
            for (int i = 0; i < state->event_count; i++) {
                CalendarEvent* e = &state->events[i];
                if (!is_event_visible(state, e)) continue;

                TagColorStyle cc = get_event_style(state, e->tag_idx, is_dark);
                float cw = main_w - 48.0f;
                float ch = 48.0f;

                rife_draw_round_rect(core, main_x + 24.0f, card_y, cw, ch, 8.0f, cc.bg, cc.border);
                rife_draw_round_rect(core, main_x + 26.0f, card_y + 3.0f, 4.0f, ch - 6.0f, 2.0f, cc.bar, cc.bar);

                // 精致矢量对勾完成状态
                if (e->is_completed) {
                    rife_draw_round_rect(core, main_x + 40.0f, card_y + 16.0f, 16.0f, 16.0f, 4.0f, 0x10B981FF, 0x10B981FF);
                    rife_draw_rect(core, main_x + 43.0f, card_y + 24.0f, 2.0f, 4.0f, 0xFFFFFFFF);
                    rife_draw_rect(core, main_x + 45.0f, card_y + 26.0f, 2.0f, 2.0f, 0xFFFFFFFF);
                    rife_draw_rect(core, main_x + 47.0f, card_y + 21.0f, 2.0f, 7.0f, 0xFFFFFFFF);
                } else {
                    rife_draw_round_rect(core, main_x + 40.0f, card_y + 16.0f, 16.0f, 16.0f, 4.0f, is_dark ? 0x221B35FF : 0xFFFFFFFF, border_col);
                }

                // 标题
                rife_draw_text_font(core, main_x + 68.0f, card_y + 8.0f, e->title, e->is_completed ? text_muted : cc.text, 1);

                // 时间与地点标签
                char meta_str[64];
                snprintf(meta_str, sizeof(meta_str), "%d月%d日 %02d:%02d - %02d:%02d · %s", e->month, e->day, e->start_hour, e->start_min, e->end_hour, e->end_min, e->location[0] ? e->location : "无地点");
                rife_draw_text_font(core, main_x + 68.0f, card_y + 26.0f, meta_str, text_muted, 3);

                card_y += ch + 10.0f;
                if (card_y + ch > client_y + client_h) break;
            }
        }
    }

    // ==========================================
    // E. 右下角悬浮新建日程操作按钮 (FAB - Floating Action Button)
    // ==========================================
    float fab_sz = 44.0f;
    float fab_x = client_x + client_w - fab_sz - 24.0f;
    float fab_y = client_y + client_h - fab_sz - 24.0f;
    rife_draw_round_rect(core, fab_x, fab_y + 2.0f, fab_sz, fab_sz, fab_sz * 0.5f, is_dark ? 0x00000066 : 0x3370FF33, is_dark ? 0x00000066 : 0x3370FF33);
    rife_draw_round_rect(core, fab_x, fab_y, fab_sz, fab_sz, fab_sz * 0.5f, rtodo_blue, rtodo_blue);
    rife_draw_rect(core, fab_x + 21.0f, fab_y + 13.0f, 2.0f, 18.0f, 0xFFFFFFFF);
    rife_draw_rect(core, fab_x + 13.0f, fab_y + 21.0f, 18.0f, 2.0f, 0xFFFFFFFF);

    // ==========================================
    // 弹窗浮层：新建日程模态卡片 (New Event Modal - 100% 用户自定义)
    // ==========================================
    if (state->show_new_modal) {
        float mw = 400.0f;
        float mh = 330.0f;
        float mx0 = client_x + (client_w - mw) * 0.5f;
        float my0 = client_y + (client_h - mh) * 0.5f;
        float box_w = mw - 32.0f;
        bool cursor_on = (state->cursor_blink_t < 0.5f);

        // 暗晶柔和阴影与卡片底板 (磨砂毛玻璃浮层)
        rife_draw_round_rect(core, mx0, my0, mw, mh, 12.0f, is_dark ? 0x231A38F2 : 0xFFFFFFF2, is_dark ? 0x4D3678FF : 0xCBD5E1FF);

        // 标题
        rife_draw_text_font(core, mx0 + 16.0f, my0 + 14.0f, is_zh ? "新建日程" : "New Event", text_title, 1);

        // 1. 标题输入框 (Title Input)
        float title_y = my0 + 42.0f;
        draw_input_box(core, mx0 + 16.0f, title_y, box_w, 32.0f,
                       state->input_title,
                       is_zh ? "日程标题 (如: 团队例会、设计评审...)" : "Event Title...",
                       state->active_field == 1, cursor_on,
                       is_dark, rtodo_blue, border_col, text_title, text_muted);

        // 2. 自定义分类标签胶囊行与 [+ 标签]
        float tag_y = my0 + 82.0f;
        float px = mx0 + 16.0f;
        for (int c = 0; c < state->tag_count; c++) {
            CustomTag* tag = &state->tags[c];
            TagColorStyle ts = get_tag_style(tag->color_bar, is_dark);
            bool is_sel = (state->modal_tag_idx == c);
            float pw = 60.0f;
            rife_draw_round_rect(core, px, tag_y, pw, 26.0f, 6.0f, is_sel ? ts.bar : ts.bg, ts.border);
            rife_draw_text_font(core, px + 10.0f, tag_y + 5.0f, tag->name, is_sel ? 0xFFFFFFFF : ts.text, 4);
            px += pw + 8.0f;
        }
        // [+ 标签] 按钮
        float add_w = 64.0f;
        rife_draw_round_rect(core, px, tag_y, add_w, 26.0f, 6.0f, is_dark ? 0x2A1F42FF : 0xF1F5F9FF, border_col);
        rife_draw_text_font(core, px + 8.0f, tag_y + 5.0f, is_zh ? "+ 标签" : "+ Tag", rtodo_blue, 4);

        // 3. 时间与时长选择
        float time_y = my0 + 118.0f;
        rife_draw_text_font(core, mx0 + 16.0f, time_y + 4.0f, is_zh ? "时间:" : "Time:", text_muted, 3);

        // 时间调整器 [-] HH:MM [+]
        rife_draw_round_rect(core, mx0 + 56.0f, time_y, 24.0f, 24.0f, 4.0f, is_dark ? 0x2A1F42FF : 0xF1F5F9FF, border_col);
        rife_draw_text_font(core, mx0 + 64.0f, time_y + 3.0f, "-", text_title, 0);

        char h_buf[16];
        snprintf(h_buf, sizeof(h_buf), "%02d:%02d", state->new_hour, state->new_min);
        rife_draw_round_rect(core, mx0 + 84.0f, time_y, 44.0f, 24.0f, 4.0f, is_dark ? 0x221838FF : 0xFFFFFFFF, border_col);
        rife_draw_text_font(core, mx0 + 88.0f, time_y + 4.0f, h_buf, text_title, 4);

        rife_draw_round_rect(core, mx0 + 132.0f, time_y, 24.0f, 24.0f, 4.0f, is_dark ? 0x2A1F42FF : 0xF1F5F9FF, border_col);
        rife_draw_text_font(core, mx0 + 139.0f, time_y + 3.0f, "+", text_title, 0);

        // 时长选择器
        rife_draw_text_font(core, mx0 + 168.0f, time_y + 4.0f, is_zh ? "时长:" : "Dur:", text_muted, 3);
        const char* dur_labels_zh[4] = { "30分", "1小时", "1.5时", "2小时" };
        const char* dur_labels_en[4] = { "30m", "1h", "1.5h", "2h" };
        for (int d = 0; d < 4; d++) {
            float bx = mx0 + 204.0f + (float)d * 45.0f;
            bool is_d_act = (state->new_duration_idx == d);
            rife_draw_round_rect(core, bx, time_y, 42.0f, 24.0f, 4.0f, is_d_act ? rtodo_blue : (is_dark ? 0x221838FF : 0xF1F5F9FF), is_d_act ? rtodo_blue : border_col);
            rife_draw_text_font(core, bx + 6.0f, time_y + 4.0f, is_zh ? dur_labels_zh[d] : dur_labels_en[d], is_d_act ? 0xFFFFFFFF : text_muted, 4);
        }

        // 4. 地点输入框 (Location Input)
        float loc_y = my0 + 156.0f;
        draw_input_box(core, mx0 + 16.0f, loc_y, box_w, 30.0f,
                       state->input_location,
                       is_zh ? "地点 / 会议链接 (如: 线上会议、会议室A...)" : "Location / Meeting URL...",
                       state->active_field == 2, cursor_on,
                       is_dark, rtodo_blue, border_col, text_title, text_muted);

        // 5. 描述/备注输入框 (Notes Input)
        float desc_y = my0 + 194.0f;
        draw_input_box(core, mx0 + 16.0f, desc_y, box_w, 30.0f,
                       state->input_desc,
                       is_zh ? "备注信息 / 议程大纲 (选填)..." : "Notes / Agenda Outline...",
                       state->active_field == 3, cursor_on,
                       is_dark, rtodo_blue, border_col, text_title, text_muted);

        // 6. 预期起止时间与日期详情
        int end_h = 0, end_m = 0;
        calc_event_end_time(state->new_hour, state->new_min, state->new_duration_idx, &end_h, &end_m);
        char sched_summary[64];
        snprintf(sched_summary, sizeof(sched_summary), "%d月%d日 %02d:%02d 至 %02d:%02d", state->view_month, state->view_day, state->new_hour, state->new_min, end_h, end_m);
        rife_draw_text_font(core, mx0 + 16.0f, my0 + 236.0f, sched_summary, text_title, 3);

        // 分割线
        rife_draw_rect(core, mx0 + 16.0f, my0 + 262.0f, mw - 32.0f, 1.0f, border_col);

        // 底部按钮：取消 / 确认创建
        float btn_y = my0 + mh - 44.0f;
        rife_draw_round_rect(core, mx0 + mw - 170.0f, btn_y, 66.0f, 32.0f, 6.0f, is_dark ? 0x241D35FF : 0xF1F5F9FF, border_col);
        rife_draw_text_font(core, mx0 + mw - 150.0f, btn_y + 7.0f, is_zh ? "取消" : "Cancel", text_muted, 0);

        rife_draw_round_rect(core, mx0 + mw - 96.0f, btn_y, 80.0f, 32.0f, 6.0f, rtodo_blue, rtodo_blue);
        rife_draw_text_font(core, mx0 + mw - 80.0f, btn_y + 7.0f, is_zh ? "确认创建" : "Create", 0xFFFFFFFF, 5);
    }

    // ==========================================
    // 弹窗浮层：新建自定义标签模态卡片 (New Custom Tag Modal)
    // ==========================================
    if (state->show_new_tag_modal) {
        float tw = 300.0f;
        float th = 170.0f;
        float tx0 = client_x + (client_w - tw) * 0.5f;
        float ty0 = client_y + (client_h - th) * 0.5f;
        bool cursor_on = (state->cursor_blink_t < 0.5f);

        // 半透遮罩
        rife_draw_rect(core, client_x, client_y, client_w, client_h, 0x00000055);

        // 卡片底板
        rife_draw_round_rect(core, tx0, ty0, tw, th, 12.0f, is_dark ? 0x221838F8 : 0xFFFFFFF8, is_dark ? 0x5D458FFF : 0xCBD5E1FF);

        rife_draw_text_font(core, tx0 + 16.0f, ty0 + 14.0f, is_zh ? "新建自定义分类标签" : "New Custom Tag", text_title, 1);

        // 标签名称输入框
        draw_input_box(core, tx0 + 16.0f, ty0 + 40.0f, tw - 32.0f, 32.0f,
                       state->new_tag_name,
                       is_zh ? "标签名称 (如: 健身、项目A...)" : "Tag Name...",
                       state->active_field == 4, cursor_on,
                       is_dark, s_tag_palette[state->new_tag_color_idx], border_col, text_title, text_muted);

        // 6 种色彩圆点
        float dot_y = ty0 + 82.0f;
        for (int i = 0; i < 6; i++) {
            float dot_x = tx0 + 20.0f + (float)i * 32.0f;
            uint32_t col = s_tag_palette[i];
            bool is_sel = (state->new_tag_color_idx == i);
            rife_draw_round_rect(core, dot_x, dot_y, 22.0f, 22.0f, 11.0f, col, is_sel ? 0xFFFFFFFF : col);
            if (is_sel) {
                rife_draw_round_rect(core, dot_x + 7.0f, dot_y + 7.0f, 8.0f, 8.0f, 4.0f, 0xFFFFFFFF, 0xFFFFFFFF);
            }
        }

        // 底部按钮：取消 / 确定
        float act_y = ty0 + th - 40.0f;
        rife_draw_round_rect(core, tx0 + tw - 150.0f, act_y, 60.0f, 28.0f, 6.0f, is_dark ? 0x241D35FF : 0xF1F5F9FF, border_col);
        rife_draw_text_font(core, tx0 + tw - 134.0f, act_y + 6.0f, is_zh ? "取消" : "Cancel", text_muted, 0);

        uint32_t sel_color = s_tag_palette[state->new_tag_color_idx];
        rife_draw_round_rect(core, tx0 + tw - 80.0f, act_y, 64.0f, 28.0f, 6.0f, sel_color, sel_color);
        rife_draw_text_font(core, tx0 + tw - 64.0f, act_y + 6.0f, is_zh ? "确定" : "Save", 0xFFFFFFFF, 5);
    }

    // ==========================================
    // 弹窗浮层：日程详情卡片 (Event Popover)
    // ==========================================
    if (state->selected_event_id >= 0) {
        CalendarEvent* cur_e = NULL;
        for (int i = 0; i < state->event_count; i++) {
            if ((int)state->events[i].id == state->selected_event_id) {
                cur_e = &state->events[i];
                break;
            }
        }
        if (cur_e) {
            float pop_w = 280.0f;
            float pop_h = 175.0f;
            float pop_x = client_x + client_w - pop_w - 20.0f;
            float pop_y = client_y + 60.0f;

            rife_draw_round_rect(core, pop_x, pop_y, pop_w, pop_h, 10.0f, is_dark ? 0x221838F2 : 0xFFFFFFF2, is_dark ? 0x47336EFF : 0xCBD5E1FF);

            TagColorStyle cc = get_event_style(state, cur_e->tag_idx, is_dark);
            const char* tag_str = get_event_tag_name(state, cur_e->tag_idx);
            rife_draw_round_rect(core, pop_x + 14.0f, pop_y + 14.0f, 64.0f, 20.0f, 4.0f, cc.bg, cc.border);
            rife_draw_text_font(core, pop_x + 20.0f, pop_y + 16.0f, tag_str, cc.text, 4);

            rife_draw_text_font(core, pop_x + 14.0f, pop_y + 40.0f, cur_e->title, text_title, 1);

            char time_loc[64];
            snprintf(time_loc, sizeof(time_loc), "%d月%d日 %02d:%02d-%02d:%02d · %s", cur_e->month, cur_e->day, cur_e->start_hour, cur_e->start_min, cur_e->end_hour, cur_e->end_min, cur_e->location[0] ? cur_e->location : "无地点");
            rife_draw_text_font(core, pop_x + 14.0f, pop_y + 64.0f, time_loc, text_muted, 3);

            rife_draw_text_font(core, pop_x + 14.0f, pop_y + 86.0f, cur_e->desc[0] ? cur_e->desc : "暂无详细备注", text_muted, 3);

            // 操作：标记完成 / 删除日程 / 关闭
            float act_y = pop_y + pop_h - 38.0f;
            // 完成状态切换
            rife_draw_round_rect(core, pop_x + 14.0f, act_y, 86.0f, 26.0f, 6.0f, cur_e->is_completed ? 0x10B981FF : (is_dark ? 0x2D2248FF : 0xEFF6FFFF), rtodo_blue);
            rife_draw_text_font(core, pop_x + 20.0f, act_y + 5.0f, cur_e->is_completed ? (is_zh ? "已完成" : "Completed") : (is_zh ? "标记完成" : "Mark Done"), cur_e->is_completed ? 0xFFFFFFFF : (is_dark ? 0xD8B4FEFF : rtodo_blue), 5);

            // 删除日程 (醒目警示红框)
            rife_draw_round_rect(core, pop_x + 104.0f, act_y, 80.0f, 26.0f, 6.0f, is_dark ? 0x33141CB8 : 0xFEF2F2B8, 0xEF444488);
            rife_draw_text_font(core, pop_x + 116.0f, act_y + 5.0f, is_zh ? "删除日程" : "Delete", 0xEF4444FF, 5);

            // 关闭按钮
            rife_draw_round_rect(core, pop_x + pop_w - 68.0f, act_y, 54.0f, 26.0f, 6.0f, is_dark ? 0x231A35FF : 0xF1F5F9FF, border_col);
            rife_draw_text_font(core, pop_x + pop_w - 52.0f, act_y + 5.0f, is_zh ? "关闭" : "Close", text_muted, 0);
        }
    }
}

// -------------------------------------------------------------
// 插件应用注册导出 (Plugin App Export)
// -------------------------------------------------------------

const RifePluginApp g_calendar_plugin_app = {
    .app_id = 2002,
    .id = "rtodo",
    .name_zh = "Rtodo",
    .name_en = "Rtodo",
    .glyph = "R",
    .color_top = 0x3370FFFF, // Rtodo 品牌蓝
    .color_bot = 0x1E40AFFF,
    .default_w = 940.0f,
    .default_h = 600.0f,
    .pin_to_dock = true,
    .create = calendar_create,
    .destroy = calendar_destroy,
    .update = calendar_update,
    .render = calendar_render
};

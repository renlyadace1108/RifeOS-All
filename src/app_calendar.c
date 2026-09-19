#define _CRT_SECURE_NO_WARNINGS
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

    bool filter_category[CAL_CAT_COUNT];

    CalendarEvent events[CAL_MAX_EVENTS];
    int event_count;

    int selected_event_id;

    // 新建日程浮动卡片
    bool show_new_modal;
    int new_cat;
    int new_hour;
    int new_duration_idx; // 0: 30m, 1: 1h, 2: 2h
    int new_preset_idx;

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

// 坂本算法 (Sakamoto's Algorithm): 0=Monday .. 6=Sunday
static inline int get_day_of_week(int y, int m, int d) {
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    int yr = y;
    if (m < 3) yr -= 1;
    int w = (yr + yr / 4 - yr / 100 + yr / 400 + t[m - 1] + d) % 7;
    return (w + 6) % 7;
}

// 获取某日所在周的周一对应日期
static void get_week_monday(int y, int m, int d, int* out_y, int* out_m, int* out_d) {
    int w = get_day_of_week(y, m, d);
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
// 初始化精美初始日程数据 (开箱即丰满)
// -------------------------------------------------------------

static void init_preset_events(CalendarState* state) {
    state->event_count = 0;

    int mon_y, mon_m, mon_d;
    get_week_monday(state->cur_year, state->cur_month, state->cur_day, &mon_y, &mon_m, &mon_d);

    int d_tue_y, d_tue_m, d_tue_d;
    int d_wed_y, d_wed_m, d_wed_d;
    int d_thu_y, d_thu_m, d_thu_d;
    int d_fri_y, d_fri_m, d_fri_d;
    int d_sat_y, d_sat_m, d_sat_d;

    add_days(mon_y, mon_m, mon_d, 1, &d_tue_y, &d_tue_m, &d_tue_d);
    add_days(mon_y, mon_m, mon_d, 2, &d_wed_y, &d_wed_m, &d_wed_d);
    add_days(mon_y, mon_m, mon_d, 3, &d_thu_y, &d_thu_m, &d_thu_d);
    add_days(mon_y, mon_m, mon_d, 4, &d_fri_y, &d_fri_m, &d_fri_d);
    add_days(mon_y, mon_m, mon_d, 5, &d_sat_y, &d_sat_m, &d_sat_d);

    // 1. 周一：微内核架构早会
    CalendarEvent* e1 = &state->events[state->event_count++];
    e1->id = 1;
    snprintf(e1->title, sizeof(e1->title), "%s", "RifeOS 微内核架构周会");
    snprintf(e1->location, sizeof(e1->location), "%s", "RifeOS 视频会议 / 1号会议室");
    snprintf(e1->desc, sizeof(e1->desc), "%s", "双 Arena 架构吞吐率与帧周期分析");
    e1->category = CAL_CAT_WORK;
    e1->year = mon_y; e1->month = mon_m; e1->day = mon_d;
    e1->start_hour = 9; e1->start_min = 30;
    e1->end_hour = 10; e1->end_min = 30;
    e1->is_completed = true;

    // 2. 周二：Rtodo 多维视图联调
    CalendarEvent* e2 = &state->events[state->event_count++];
    e2->id = 2;
    snprintf(e2->title, sizeof(e2->title), "%s", "Rtodo 多维交互走查");
    snprintf(e2->location, sizeof(e2->location), "%s", "桌面研发工位");
    snprintf(e2->desc, sizeof(e2->desc), "%s", "时间标尺微像素对齐与红线指示器");
    e2->category = CAL_CAT_REVIEW;
    e2->year = d_tue_y; e2->month = d_tue_m; e2->day = d_tue_d;
    e2->start_hour = 14; e2->start_min = 0;
    e2->end_hour = 15; e2->end_min = 30;
    e2->is_completed = false;

    // 3. 周三：双 Arena 零堆内存抖动压测
    CalendarEvent* e3 = &state->events[state->event_count++];
    e3->id = 3;
    snprintf(e3->title, sizeof(e3->title), "%s", "零堆内存抖动压力测试");
    snprintf(e3->location, sizeof(e3->location), "%s", "微内核遥测工作站");
    snprintf(e3->desc, sizeof(e3->desc), "%s", "验证 60FPS 帧内 malloc 调用严格为 0");
    e3->category = CAL_CAT_WORK;
    e3->year = d_wed_y; e3->month = d_wed_m; e3->day = d_wed_d;
    e3->start_hour = 11; e3->start_min = 0;
    e3->end_hour = 12; e3->end_min = 0;
    e3->is_completed = false;

    // 4. 周四：黑曜石深色暗晶设计评审
    CalendarEvent* e4 = &state->events[state->event_count++];
    e4->id = 4;
    snprintf(e4->title, sizeof(e4->title), "%s", "Obsidian 深色暗晶评审");
    snprintf(e4->location, sizeof(e4->location), "%s", "美学体验实验室");
    snprintf(e4->desc, sizeof(e4->desc), "%s", "色度反差消除与紫晶柔和漫反射");
    e4->category = CAL_CAT_REVIEW;
    e4->year = d_thu_y; e4->month = d_thu_m; e4->day = d_thu_d;
    e4->start_hour = 16; e4->start_min = 0;
    e4->end_hour = 17; e4->end_min = 30;
    e4->is_completed = false;

    // 5. 周五：RifeOS v1.0 商业版发布里程碑
    CalendarEvent* e5 = &state->events[state->event_count++];
    e5->id = 5;
    snprintf(e5->title, sizeof(e5->title), "%s", "RifeOS v1.0.0 正式上线");
    snprintf(e5->location, sizeof(e5->location), "%s", "全球线上发布主厅");
    snprintf(e5->desc, sizeof(e5->desc), "%s", "纯 GUI 单文件安装包与桌面交付");
    e5->category = CAL_CAT_MILESTONE;
    e5->year = d_fri_y; e5->month = d_fri_m; e5->day = d_fri_d;
    e5->start_hour = 15; e5->start_min = 0;
    e5->end_hour = 16; e5->end_min = 30;
    e5->is_completed = false;

    // 6. 周六：周末体能训练与慢跑
    CalendarEvent* e6 = &state->events[state->event_count++];
    e6->id = 6;
    snprintf(e6->title, sizeof(e6->title), "%s", "体能强化与 10KM 户外慢跑");
    snprintf(e6->location, sizeof(e6->location), "%s", "森林公园");
    snprintf(e6->desc, sizeof(e6->desc), "%s", "保持充沛活力与清晰思考");
    e6->category = CAL_CAT_PERSONAL;
    e6->year = d_sat_y; e6->month = d_sat_m; e6->day = d_sat_d;
    e6->start_hour = 17; e6->start_min = 30;
    e6->end_hour = 19; e6->end_min = 0;
    e6->is_completed = false;

    // 7. 今天新增的一项日程（便于用户立即看见当前红线与日程交互）
    CalendarEvent* e7 = &state->events[state->event_count++];
    e7->id = 7;
    snprintf(e7->title, sizeof(e7->title), "%s", "Rtodo 全场景交互验收");
    snprintf(e7->location, sizeof(e7->location), "%s", "RifeOS 宿主工作台");
    snprintf(e7->desc, sizeof(e7->desc), "%s", "测试周/日/月/日程列表模式切换");
    e7->category = CAL_CAT_WORK;
    e7->year = state->cur_year; e7->month = state->cur_month; e7->day = state->cur_day;
    int sh = (state->cur_hour >= 8 && state->cur_hour <= 19) ? state->cur_hour : 10;
    e7->start_hour = sh; e7->start_min = 0;
    e7->end_hour = sh + 1; e7->end_min = 30;
    e7->is_completed = false;
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

    for (int i = 0; i < CAL_CAT_COUNT; i++) {
        state->filter_category[i] = true; // 默认全选
    }

    state->selected_event_id = -1;
    state->show_new_modal = false;
    state->new_cat = CAL_CAT_WORK;
    state->new_hour = 10;
    state->new_duration_idx = 1; // 1h
    state->new_preset_idx = 0;

    init_preset_events(state);
    return state;
}

static void calendar_destroy(void* inst) {
    if (inst) free(inst);
}

// -------------------------------------------------------------
// Rtodo 色彩体系 (Rtodo Design Tokens)
// -------------------------------------------------------------

typedef struct {
    uint32_t bg;
    uint32_t border;
    uint32_t bar;
    uint32_t text;
} CategoryColor;

static CategoryColor get_category_color(CalendarCategory cat, bool is_dark) {
    CategoryColor c;
    if (is_dark) {
        switch (cat) {
            case CAL_CAT_WORK: // 科技蓝
                c.bg = 0x1A284AFF; c.border = 0x274380FF; c.bar = 0x3B82F6FF; c.text = 0xBFDBFEFF; break;
            case CAL_CAT_REVIEW: // 紫晶
                c.bg = 0x2A1A4AFF; c.border = 0x472B80FF; c.bar = 0xA855F7FF; c.text = 0xE9D5FFFF; break;
            case CAL_CAT_PERSONAL: // 翡翠
                c.bg = 0x0F3325FF; c.border = 0x1A593FFF; c.bar = 0x10B981FF; c.text = 0xA7F3D0FF; break;
            case CAL_CAT_MILESTONE: // 琥珀
            default:
                c.bg = 0x38240AFF; c.border = 0x613D0EFF; c.bar = 0xF59E0BFF; c.text = 0xFDE68AFF; break;
        }
    } else {
        switch (cat) {
            case CAL_CAT_WORK: // Rtodo 品牌蓝
                c.bg = 0xEFF6FFFF; c.border = 0xBFDBFEFF; c.bar = 0x3370FFFF; c.text = 0x1E40AFFF; break;
            case CAL_CAT_REVIEW: // 薰衣紫
                c.bg = 0xFAF5FFFF; c.border = 0xE9D5FFFF; c.bar = 0x8B5CF6FF; c.text = 0x6B21A8FF; break;
            case CAL_CAT_PERSONAL: // 薄荷绿
                c.bg = 0xECFDF5FF; c.border = 0xA7F3D0FF; c.bar = 0x10B981FF; c.text = 0x065F46FF; break;
            case CAL_CAT_MILESTONE: // 温暖橙
            default:
                c.bg = 0xFFFBEBFF; c.border = 0xFDE68AFF; c.bar = 0xF59E0BFF; c.text = 0x92400EFF; break;
        }
    }
    return c;
}

// -------------------------------------------------------------
// 交互更新 (Update)
// -------------------------------------------------------------

static void calendar_update(void* inst, RifeCore* core, const RifeInput* input, float client_w, float client_h) {
    (void)client_h;
    CalendarState* state = (CalendarState*)inst;
    if (!state) return;

    sync_system_clock(state);

    if (!input->mouse_pressed[0]) return;

    float mx = input->mouse_x;
    float my = input->mouse_y;

    RifeSystemConfig* cfg = rife_get_system_config();
    (void)cfg;

    float header_h = 46.0f;
    float sidebar_w = 190.0f;

    // 1. 浮动弹窗：新建日程模态框
    if (state->show_new_modal) {
        float mw = 360.0f;
        float mh = 280.0f;
        float mx0 = (client_w - mw) * 0.5f;
        float my0 = (client_h - mh) * 0.5f;

        // 点击卡片内部选项
        if (mx >= mx0 && mx <= mx0 + mw && my >= my0 && my <= my0 + mh) {
            // A. 分类切换 (4项)
            float cat_y = my0 + 64.0f;
            for (int c = 0; c < 4; c++) {
                float bx = mx0 + 16.0f + (float)c * 82.0f;
                if (mx >= bx && mx <= bx + 76.0f && my >= cat_y && my <= cat_y + 26.0f) {
                    state->new_cat = c;
                    rife_request_redraw(core);
                    return;
                }
            }
            // B. 预设标题 (4项)
            float pre_y = my0 + 116.0f;
            for (int p = 0; p < 4; p++) {
                float row = (float)(p / 2);
                float col = (float)(p % 2);
                float bx = mx0 + 16.0f + col * 166.0f;
                float by = pre_y + row * 32.0f;
                if (mx >= bx && mx <= bx + 158.0f && my >= by && my <= by + 26.0f) {
                    state->new_preset_idx = p;
                    rife_request_redraw(core);
                    return;
                }
            }
            // C. 开始时间 (+/- 1小时)
            float time_y = my0 + 188.0f;
            if (mx >= mx0 + 16.0f && mx <= mx0 + 44.0f && my >= time_y && my <= time_y + 26.0f) {
                if (state->new_hour > 8) state->new_hour--;
                rife_request_redraw(core);
                return;
            }
            if (mx >= mx0 + 110.0f && mx <= mx0 + 138.0f && my >= time_y && my <= time_y + 26.0f) {
                if (state->new_hour < 21) state->new_hour++;
                rife_request_redraw(core);
                return;
            }
            // D. 底部操作按钮：确认添加 / 取消
            float btn_y = my0 + mh - 44.0f;
            // 确认添加
            if (mx >= mx0 + mw - 96.0f && mx <= mx0 + mw - 16.0f && my >= btn_y && my <= btn_y + 30.0f) {
                if (state->event_count < CAL_MAX_EVENTS) {
                    static const char* presets[4] = {
                        "产品交互体验走查",
                        "核心技术方案评审",
                        "深度专注与思考",
                        "版本交付发布复盘"
                    };
                    CalendarEvent* ne = &state->events[state->event_count++];
                    ne->id = (uint32_t)(state->event_count + 100);
                    snprintf(ne->title, sizeof(ne->title), "%s", presets[state->new_preset_idx]);
                    snprintf(ne->location, sizeof(ne->location), "%s", "RifeOS 工作空间");
                    snprintf(ne->desc, sizeof(ne->desc), "%s", "由用户快速添加的待办日程");
                    ne->category = (CalendarCategory)state->new_cat;
                    ne->year = state->view_year;
                    ne->month = state->view_month;
                    ne->day = state->view_day;
                    ne->start_hour = state->new_hour;
                    ne->start_min = 0;
                    ne->end_hour = state->new_hour + 1;
                    ne->end_min = 30;
                    ne->is_completed = false;
                }
                state->show_new_modal = false;
                rife_request_redraw(core);
                return;
            }
            // 取消
            if (mx >= mx0 + mw - 170.0f && mx <= mx0 + mw - 104.0f && my >= btn_y && my <= btn_y + 30.0f) {
                state->show_new_modal = false;
                rife_request_redraw(core);
                return;
            }
            return;
        } else {
            // 点击外部关闭弹窗
            state->show_new_modal = false;
            rife_request_redraw(core);
            return;
        }
    }

    // 2. 详情浮层卡片点击关闭或操作
    if (state->selected_event_id >= 0) {
        float pop_w = 260.0f;
        float pop_h = 170.0f;
        float pop_x = client_w - pop_w - 20.0f;
        float pop_y = 60.0f;
        if (mx >= pop_x && mx <= pop_x + pop_w && my >= pop_y && my <= pop_y + pop_h) {
            // 标记完成 / 切换
            if (mx >= pop_x + 16.0f && mx <= pop_x + 120.0f && my >= pop_y + pop_h - 38.0f && my <= pop_y + pop_h - 10.0f) {
                for (int i = 0; i < state->event_count; i++) {
                    if ((int)state->events[i].id == state->selected_event_id) {
                        state->events[i].is_completed = !state->events[i].is_completed;
                        break;
                    }
                }
                rife_request_redraw(core);
                return;
            }
            // 关闭详情
            if (mx >= pop_x + pop_w - 74.0f && mx <= pop_x + pop_w - 16.0f && my >= pop_y + pop_h - 38.0f && my <= pop_y + pop_h - 10.0f) {
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
        float today_btn_x = sidebar_w + 14.0f;
        // A. "今天" 快速重置按钮
        if (mx >= today_btn_x && mx <= today_btn_x + 52.0f && my >= 10.0f && my <= 38.0f) {
            state->view_year = state->cur_year;
            state->view_month = state->cur_month;
            state->view_day = state->cur_day;
            rife_request_redraw(core);
            return;
        }
        // B. 前翻页 `<` 按钮
        float nav_x = today_btn_x + 58.0f;
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
        // D. "+ 新建日程" 按钮
        float new_btn_w = 92.0f;
        float new_btn_x = client_w - new_btn_w - 14.0f;
        if (mx >= new_btn_x && mx <= new_btn_x + new_btn_w && my >= 10.0f && my <= 38.0f) {
            state->show_new_modal = true;
            rife_request_redraw(core);
            return;
        }
        // E. 视图模式多段切换胶囊 [日] [周] [月] [日程]
        float seg_w = 4.0f * 48.0f + 4.0f;
        float seg_x = new_btn_x - seg_w - 14.0f;
        if (mx >= seg_x && mx <= seg_x + seg_w && my >= 10.0f && my <= 38.0f) {
            for (int v = 0; v < 4; v++) {
                float ix = seg_x + 2.0f + (float)v * 48.0f;
                if (mx >= ix && mx <= ix + 48.0f) {
                    state->view_mode = (CalendarViewMode)v;
                    rife_request_redraw(core);
                    return;
                }
            }
        }
    }

    // 4. 左侧侧边栏交互 (迷你月历 & 分类过滤)
    if (mx >= 0.0f && mx <= sidebar_w && my > header_h) {
        // A. 迷你月历点击日期跳转
        float mini_y0 = header_h + 34.0f;
        float cell_sz = 24.0f;
        float grid_w = 7.0f * cell_sz;
        float grid_x = (sidebar_w - grid_w) * 0.5f;

        int first_wday = get_day_of_week(state->view_year, state->view_month, 1);
        int total_days = days_in_month(state->view_year, state->view_month);

        for (int d = 1; d <= total_days; d++) {
            int slot = first_wday + d - 1;
            int row = slot / 7;
            int col = slot % 7;
            float cx = grid_x + (float)col * cell_sz;
            float cy = mini_y0 + (float)row * cell_sz;

            if (mx >= cx && mx <= cx + cell_sz && my >= cy && my <= cy + cell_sz) {
                state->view_day = d;
                rife_request_redraw(core);
                return;
            }
        }

        // B. 分类过滤器勾选 (4项)
        float cat_filter_y = mini_y0 + 6.0f * cell_sz + 46.0f;
        for (int c = 0; c < CAL_CAT_COUNT; c++) {
            float row_y = cat_filter_y + (float)c * 28.0f;
            if (mx >= 16.0f && mx <= sidebar_w - 16.0f && my >= row_y && my <= row_y + 24.0f) {
                state->filter_category[c] = !state->filter_category[c];
                rife_request_redraw(core);
                return;
            }
        }
    }

    // 5. 右侧工作区交互：周视图与日视图日程块点击
    if (mx > sidebar_w && my > header_h) {
        // 检查点击事件色块
        if (state->view_mode == CAL_VIEW_WEEK) {
            float main_x = sidebar_w;
            float main_w = client_w - sidebar_w;
            float ruler_w = 48.0f;
            float grid_left = main_x + ruler_w;
            float col_w = (main_w - ruler_w) / 7.0f;
            float header_bar_h = 48.0f;
            float grid_top = header_h + header_bar_h;
            float avail_h = (client_h - header_h) - header_bar_h - 16.0f;
            float hour_h = 38.0f;
            if (hour_h * 12.0f > avail_h) {
                hour_h = floorf(avail_h / 12.0f);
            }

            int mon_y, mon_m, mon_d;
            get_week_monday(state->view_year, state->view_month, state->view_day, &mon_y, &mon_m, &mon_d);

            for (int i = 0; i < state->event_count; i++) {
                CalendarEvent* e = &state->events[i];
                if (!state->filter_category[e->category]) continue;

                // 检查是否落入本周某列
                for (int c = 0; c < 7; c++) {
                    int col_y, col_m, col_d;
                    add_days(mon_y, mon_m, mon_d, c, &col_y, &col_m, &col_d);
                    if (e->year == col_y && e->month == col_m && e->day == col_d) {
                        float cx = grid_left + (float)c * col_w + 3.0f;
                        float cw = col_w - 6.0f;
                        float start_f = (float)(e->start_hour - 8) + (float)e->start_min / 60.0f;
                        float end_f = (float)(e->end_hour - 8) + (float)e->end_min / 60.0f;
                        if (start_f < 0.0f) start_f = 0.0f;
                        float cy = grid_top + start_f * hour_h + 1.0f;
                        float ch = (end_f - start_f) * hour_h - 2.0f;
                        if (ch < 24.0f) ch = 24.0f;

                        if (mx >= cx && mx <= cx + cw && my >= cy && my <= cy + ch) {
                            state->selected_event_id = (int)e->id;
                            rife_request_redraw(core);
                            return;
                        }
                    }
                }
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

    // 主色与背景令牌
    uint32_t bg_main = is_dark ? 0x161122FF : 0xFFFFFFFF;
    uint32_t bg_sidebar = is_dark ? 0x1A1428FF : 0xF8FAFCFF;
    uint32_t border_col = is_dark ? 0x2E2447FF : 0xE2E8F0FF;
    uint32_t text_title = is_dark ? 0xF8FAFCFF : 0x0F172AFF;
    uint32_t text_muted = is_dark ? 0x94A3B8FF : 0x64748BFF;
    uint32_t rtodo_blue = 0x3370FFFF;

    float header_h = 48.0f;
    float sidebar_w = 190.0f;

    // A. 基础容器底色
    rife_draw_rect(core, client_x, client_y, client_w, client_h, bg_main);
    rife_draw_rect(core, client_x, client_y, sidebar_w, client_h, bg_sidebar);
    rife_draw_rect(core, client_x + sidebar_w, client_y, 1.0f, client_h, border_col);
    rife_draw_rect(core, client_x, client_y + header_h, client_w, 1.0f, border_col);

    // B. 顶部 Header 区域
    // 1. 左侧 Rtodo 品牌标识与精致日历徽章
    rife_draw_round_rect(core, client_x + 14.0f, client_y + 11.0f, 26.0f, 26.0f, 6.0f, rtodo_blue, rtodo_blue);
    rife_draw_round_rect(core, client_x + 14.0f, client_y + 11.0f, 26.0f, 8.0f, 4.0f, 0x1E40AFFF, 0x1E40AFFF);
    rife_draw_round_rect(core, client_x + 18.0f, client_y + 16.0f, 3.0f, 3.0f, 1.0f, 0xFFFFFFFF, 0xFFFFFFFF);
    rife_draw_round_rect(core, client_x + 24.0f, client_y + 16.0f, 3.0f, 3.0f, 1.0f, 0xFFFFFFFF, 0xFFFFFFFF);
    rife_draw_round_rect(core, client_x + 30.0f, client_y + 16.0f, 3.0f, 3.0f, 1.0f, 0xFFFFFFFF, 0xFFFFFFFF);
    rife_draw_round_rect(core, client_x + 18.0f, client_y + 22.0f, 3.0f, 3.0f, 1.0f, 0xFFFFFFFF, 0xFFFFFFFF);
    rife_draw_round_rect(core, client_x + 24.0f, client_y + 22.0f, 3.0f, 3.0f, 1.0f, 0xFFFFFFFF, 0xFFFFFFFF);
    rife_draw_round_rect(core, client_x + 30.0f, client_y + 22.0f, 3.0f, 3.0f, 1.0f, 0xFFFFFFFF, 0xFFFFFFFF);

    rife_draw_text_font(core, client_x + 48.0f, client_y + 14.0f, is_zh ? "Rtodo 日程" : "Rtodo", text_title, 1);

    // 2. "今天" 按钮
    float today_btn_x = client_x + sidebar_w + 14.0f;
    rife_draw_round_rect(core, today_btn_x, client_y + 11.0f, 52.0f, 26.0f, 6.0f, is_dark ? 0x241D35FF : 0xFFFFFFFF, is_dark ? 0x3D2E5CFF : 0xCBD5E1FF);
    rife_draw_text_font(core, today_btn_x + 13.0f, client_y + 15.0f, is_zh ? "今天" : "Today", text_title, 0);

    // 3. 前翻/后翻箭头
    float nav_x = today_btn_x + 58.0f;
    rife_draw_round_rect(core, nav_x, client_y + 11.0f, 26.0f, 26.0f, 6.0f, is_dark ? 0x201832FF : 0xFFFFFFFF, is_dark ? 0x35284EFF : 0xE2E8F0FF);
    rife_draw_text_font(core, nav_x + 9.0f, client_y + 15.0f, "<", text_title, 0);

    float nav_r_x = nav_x + 30.0f;
    rife_draw_round_rect(core, nav_r_x, client_y + 11.0f, 26.0f, 26.0f, 6.0f, is_dark ? 0x201832FF : 0xFFFFFFFF, is_dark ? 0x35284EFF : 0xE2E8F0FF);
    rife_draw_text_font(core, nav_r_x + 9.0f, client_y + 15.0f, ">", text_title, 0);

    // 4. 当前年月标题
    char title_buf[64];
    static const char* mon_names[13] = { "", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    if (is_zh) {
        snprintf(title_buf, sizeof(title_buf), "%d年 %d月", state->view_year, state->view_month);
    } else {
        snprintf(title_buf, sizeof(title_buf), "%s %d", mon_names[state->view_month], state->view_year);
    }
    rife_draw_text_font(core, nav_r_x + 36.0f, client_y + 14.0f, title_buf, text_title, 1);

    // 5. "+ 新建日程" 主按钮 (靠右自适应对齐)
    float new_btn_w = 92.0f;
    float new_btn_x = client_x + client_w - new_btn_w - 14.0f;
    rife_draw_round_rect(core, new_btn_x, client_y + 10.0f, new_btn_w, 28.0f, 6.0f, rtodo_blue, rtodo_blue);
    rife_draw_text_font(core, new_btn_x + 14.0f, client_y + 14.0f, is_zh ? "+ 新建日程" : "+ New Event", 0xFFFFFFFF, 5);

    // 6. 视图切换多段药丸 [日] [周] [月] [日程]
    float seg_w = 4.0f * 48.0f + 4.0f;
    float seg_x = new_btn_x - seg_w - 14.0f;
    float seg_y = client_y + 10.0f;
    rife_draw_round_rect(core, seg_x, seg_y, seg_w, 28.0f, 6.0f, is_dark ? 0x1F192EFF : 0xF1F5F9FF, border_col);

    const char* view_labels_zh[4] = { "日", "周", "月", "日程" };
    const char* view_labels_en[4] = { "Day", "Week", "Mon", "List" };

    for (int v = 0; v < 4; v++) {
        float vx = seg_x + 2.0f + (float)v * 48.0f;
        bool is_act = ((int)state->view_mode == v);
        if (is_act) {
            uint32_t act_bg = is_dark ? 0x382A57FF : 0xFFFFFFFF;
            uint32_t act_brd = is_dark ? 0x634E8CFF : 0x93C5FDFF;
            uint32_t act_txt = is_dark ? 0xF8FAFCFF : rtodo_blue;
            rife_draw_round_rect(core, vx, seg_y + 2.0f, 48.0f, 24.0f, 5.0f, act_bg, act_brd);
            rife_draw_text_font(core, vx + (is_zh ? (v == 3 ? 11.0f : 17.0f) : 10.0f), seg_y + 5.0f, is_zh ? view_labels_zh[v] : view_labels_en[v], act_txt, 5);
        } else {
            rife_draw_text_font(core, vx + (is_zh ? (v == 3 ? 11.0f : 17.0f) : 10.0f), seg_y + 5.0f, is_zh ? view_labels_zh[v] : view_labels_en[v], text_muted, 0);
        }
    }

    // C. 左侧侧边栏 (迷你月历 & 分类图例)
    float sb_x = client_x;
    float sb_y = client_y + header_h;

    // 1. 迷你月历周表头
    float cell_sz = 24.0f;
    float mini_w = 7.0f * cell_sz;
    float mini_x = sb_x + (sidebar_w - mini_w) * 0.5f;
    float mini_y = sb_y + 12.0f;

    const char* mini_week_zh[7] = { "一", "二", "三", "四", "五", "六", "日" };
    for (int i = 0; i < 7; i++) {
        rife_draw_text_font(core, mini_x + (float)i * cell_sz + 6.0f, mini_y, mini_week_zh[i], text_muted, 4);
    }

    // 2. 迷你月历数字
    int first_wday = get_day_of_week(state->view_year, state->view_month, 1);
    int total_days = days_in_month(state->view_year, state->view_month);
    float days_y = mini_y + 20.0f;

    for (int d = 1; d <= total_days; d++) {
        int slot = first_wday + d - 1;
        int row = slot / 7;
        int col = slot % 7;
        float cx = mini_x + (float)col * cell_sz;
        float cy = days_y + (float)row * cell_sz;

        bool is_today = (state->view_year == state->cur_year && state->view_month == state->cur_month && d == state->cur_day);
        bool is_sel = (d == state->view_day);

        if (is_today) {
            rife_draw_round_rect(core, cx + 2.0f, cy + 2.0f, cell_sz - 4.0f, cell_sz - 4.0f, 10.0f, rtodo_blue, rtodo_blue);
        } else if (is_sel) {
            rife_draw_round_rect(core, cx + 2.0f, cy + 2.0f, cell_sz - 4.0f, cell_sz - 4.0f, 5.0f, is_dark ? 0x2D214AFF : 0xEFF6FFFF, rtodo_blue);
        }

        char d_str[8];
        snprintf(d_str, sizeof(d_str), "%d", d);
        float tx = cx + (d < 10 ? 8.0f : 4.0f);
        uint32_t tc = is_today ? 0xFFFFFFFF : (is_sel ? (is_dark ? 0xC084FCFF : rtodo_blue) : text_title);
        rife_draw_text_font(core, tx, cy + 4.0f, d_str, tc, 4);
    }

    // 3. 侧边栏分割线
    float filter_start_y = days_y + 6.0f * cell_sz + 10.0f;
    rife_draw_rect(core, sb_x + 14.0f, filter_start_y, sidebar_w - 28.0f, 1.0f, border_col);

    // 4. 日历分类图例及过滤器 (4类)
    rife_draw_text_font(core, sb_x + 16.0f, filter_start_y + 10.0f, is_zh ? "我的日历分类" : "My Calendars", text_muted, 4);

    const char* cat_names_zh[CAL_CAT_COUNT] = { "工作协同", "架构评审", "个人聚焦", "关键发布" };
    const char* cat_names_en[CAL_CAT_COUNT] = { "Work & Sync", "Review", "Personal", "Milestones" };

    for (int c = 0; c < CAL_CAT_COUNT; c++) {
        float row_y = filter_start_y + 32.0f + (float)c * 28.0f;
        CategoryColor cc = get_category_color((CalendarCategory)c, is_dark);
        bool checked = state->filter_category[c];

        // 复选框小方块与精美几何矢量对勾
        if (checked) {
            rife_draw_round_rect(core, sb_x + 16.0f, row_y + 3.0f, 14.0f, 14.0f, 3.5f, cc.bar, cc.bar);
            rife_draw_rect(core, sb_x + 19.0f, row_y + 9.0f, 2.0f, 4.0f, 0xFFFFFFFF);
            rife_draw_rect(core, sb_x + 21.0f, row_y + 11.0f, 2.0f, 2.0f, 0xFFFFFFFF);
            rife_draw_rect(core, sb_x + 23.0f, row_y + 7.0f, 2.0f, 6.0f, 0xFFFFFFFF);
        } else {
            rife_draw_round_rect(core, sb_x + 16.0f, row_y + 3.0f, 14.0f, 14.0f, 3.5f, is_dark ? 0x221B33FF : 0xFFFFFFFF, border_col);
        }

        rife_draw_text_font(core, sb_x + 36.0f, row_y + 2.0f, is_zh ? cat_names_zh[c] : cat_names_en[c], text_title, 3);
    }

    // 5. 侧边栏底部待办小统计
    float summary_y = client_y + client_h - 48.0f;
    rife_draw_round_rect(core, sb_x + 12.0f, summary_y, sidebar_w - 24.0f, 36.0f, 8.0f, is_dark ? 0x221935FF : 0xF1F5F9FF, border_col);
    rife_draw_text_font(core, sb_x + 20.0f, summary_y + 10.0f, is_zh ? "今日日程" : "Today Events", text_muted, 3);
    char cnt_str[16];
    snprintf(cnt_str, sizeof(cnt_str), "%d 项", state->event_count);
    float badge_w = 42.0f;
    float badge_x = sb_x + sidebar_w - 24.0f - badge_w;
    rife_draw_round_rect(core, badge_x, summary_y + 7.0f, badge_w, 22.0f, 11.0f, is_dark ? 0x2D214AFF : 0xEFF6FFFF, rtodo_blue);
    rife_draw_text_font(core, badge_x + 8.0f, summary_y + 10.0f, cnt_str, is_dark ? 0xC084FCFF : rtodo_blue, 5);

    // D. 右侧主工作区 (周视图 / 日视图 / 月视图 / 日程列表)
    float main_x = client_x + sidebar_w;
    float main_y = client_y + header_h;
    float main_w = client_w - sidebar_w;
    float main_h = client_h - header_h;

    // ==========================================
    // 视图 1：周视图 (Week View)
    // ==========================================
    if (state->view_mode == CAL_VIEW_WEEK) {
        float ruler_w = 48.0f;
        float grid_left = main_x + ruler_w;
        float col_w = (main_w - ruler_w) / 7.0f;
        float header_bar_h = 48.0f;
        float grid_top = main_y + header_bar_h;
        float avail_h = main_h - header_bar_h - 16.0f;
        float hour_h = 38.0f;
        if (hour_h * 12.0f > avail_h) {
            hour_h = floorf(avail_h / 12.0f);
        }

        int mon_y, mon_m, mon_d;
        get_week_monday(state->view_year, state->view_month, state->view_day, &mon_y, &mon_m, &mon_d);

        // 1. 周列头 (周一至周日) - 严格居中、层次清晰的现代化排版
        const char* wk_names[7] = { "周一", "周二", "周三", "周四", "周五", "周六", "周日" };
        for (int c = 0; c < 7; c++) {
            float cx = grid_left + (float)c * col_w;
            float col_mid_x = cx + col_w * 0.5f;
            int cy_y, cy_m, cy_d;
            add_days(mon_y, mon_m, mon_d, c, &cy_y, &cy_m, &cy_d);
            bool is_col_today = (cy_y == state->cur_year && cy_m == state->cur_month && cy_d == state->cur_day);

            rife_draw_rect(core, cx, main_y, 1.0f, main_h, border_col);

            // 顶部：星期文字水平居中
            rife_draw_text_font(core, col_mid_x - 12.0f, main_y + 5.0f, wk_names[c], is_col_today ? rtodo_blue : text_muted, 3);

            // 底部：日期数字水平居中（今天带品牌蓝高光实心圆形胶囊）
            char d_buf[8];
            snprintf(d_buf, sizeof(d_buf), "%d", cy_d);
            float d_off = (cy_d >= 10) ? 6.0f : 4.0f;

            if (is_col_today) {
                rife_draw_round_rect(core, col_mid_x - 12.0f, main_y + 20.0f, 24.0f, 24.0f, 12.0f, rtodo_blue, rtodo_blue);
                rife_draw_text_font(core, col_mid_x - d_off, main_y + 24.0f, d_buf, 0xFFFFFFFF, 5);
            } else {
                rife_draw_text_font(core, col_mid_x - d_off, main_y + 24.0f, d_buf, text_title, 1);
            }
        }
        rife_draw_rect(core, main_x, grid_top, main_w, 1.0f, border_col);

        // 2. 时间标尺 (08:00 ~ 20:00)
        for (int h = 8; h <= 20; h++) {
            float hy = grid_top + (float)(h - 8) * hour_h;
            if (hy > client_y + client_h) break;

            char h_str[8];
            snprintf(h_str, sizeof(h_str), "%02d:00", h);
            rife_draw_text_font(core, main_x + 8.0f, hy - 7.0f, h_str, text_muted, 4);
            rife_draw_rect(core, grid_left, hy, main_w - ruler_w, 1.0f, is_dark ? 0x1F182EFF : 0xF1F5F9FF);
        }

        // 3. 渲染事件卡片 (Event Chips - 支持矩形安全裁剪，杜绝跨日溢出)
        for (int i = 0; i < state->event_count; i++) {
            CalendarEvent* e = &state->events[i];
            if (!state->filter_category[e->category]) continue;

            for (int c = 0; c < 7; c++) {
                int cy_y, cy_m, cy_d;
                add_days(mon_y, mon_m, mon_d, c, &cy_y, &cy_m, &cy_d);
                if (e->year == cy_y && e->month == cy_m && e->day == cy_d) {
                    float cx = grid_left + (float)c * col_w + 3.0f;
                    float cw = col_w - 6.0f;
                    float start_f = (float)(e->start_hour - 8) + (float)e->start_min / 60.0f;
                    float end_f = (float)(e->end_hour - 8) + (float)e->end_min / 60.0f;
                    if (start_f < 0.0f) start_f = 0.0f;
                    float cy = grid_top + start_f * hour_h + 1.0f;
                    float ch = (end_f - start_f) * hour_h - 2.0f;
                    if (ch < 24.0f) ch = 24.0f;

                    CategoryColor cc = get_category_color(e->category, is_dark);

                    // 安全裁剪：防止过长标题溢出到邻近日
                    rife_push_scissor(core, cx, cy, cw, ch);

                    // 卡片背景与边框
                    rife_draw_round_rect(core, cx, cy, cw, ch, 6.0f, cc.bg, cc.border);
                    // 左侧 3.5px 标志重色强调条 (Accent Indicator Bar)
                    rife_draw_round_rect(core, cx + 1.5f, cy + 2.0f, 3.5f, ch - 4.0f, 1.75f, cc.bar, cc.bar);

                    // 标题与时间
                    rife_draw_text_font(core, cx + 8.0f, cy + 4.0f, e->title, cc.text, 3);
                    if (ch >= 36.0f) {
                        char time_buf[32];
                        snprintf(time_buf, sizeof(time_buf), "%02d:%02d-%02d:%02d", e->start_hour, e->start_min, e->end_hour, e->end_min);
                        rife_draw_text_font(core, cx + 8.0f, cy + 19.0f, time_buf, text_muted, 4);
                    }

                    rife_pop_scissor(core);
                }
            }
        }

        // 4. 实时时间红线 (Current Time Red Indicator Line)
        for (int c = 0; c < 7; c++) {
            int cy_y, cy_m, cy_d;
            add_days(mon_y, mon_m, mon_d, c, &cy_y, &cy_m, &cy_d);
            if (cy_y == state->cur_year && cy_m == state->cur_month && cy_d == state->cur_day) {
                if (state->cur_hour >= 8 && state->cur_hour <= 20) {
                    float cur_f = (float)(state->cur_hour - 8) + (float)state->cur_min / 60.0f;
                    float red_y = grid_top + cur_f * hour_h;
                    float red_x = grid_left + (float)c * col_w;
                    // 红线横穿今天这一列
                    rife_draw_rect(core, red_x, red_y - 1.0f, col_w, 2.0f, 0xF43F5EFF);
                    // 左侧发光时间圆点
                    rife_draw_round_rect(core, red_x - 3.5f, red_y - 3.5f, 7.0f, 7.0f, 3.5f, 0xF43F5EFF, 0xFFFFFFFF);
                }
                break;
            }
        }
    }
    // ==========================================
    // 视图 2：日视图 (Day View - 高信息密度单日)
    // ==========================================
    else if (state->view_mode == CAL_VIEW_DAY) {
        float day_ruler_w = 60.0f;
        float day_grid_top = main_y + 12.0f;
        float day_hour_h = 42.0f;

        rife_draw_text_font(core, main_x + 18.0f, day_grid_top, is_zh ? "今日重点时间轴" : "Daily Agenda Timeline", text_title, 1);

        for (int h = 8; h <= 20; h++) {
            float hy = day_grid_top + 32.0f + (float)(h - 8) * day_hour_h;
            char h_str[8];
            snprintf(h_str, sizeof(h_str), "%02d:00", h);
            rife_draw_text_font(core, main_x + 18.0f, hy - 7.0f, h_str, text_muted, 4);
            rife_draw_rect(core, main_x + day_ruler_w + 14.0f, hy, main_w - day_ruler_w - 30.0f, 1.0f, is_dark ? 0x1F182EFF : 0xF1F5F9FF);
        }

        // 渲染单日大日程卡片
        for (int i = 0; i < state->event_count; i++) {
            CalendarEvent* e = &state->events[i];
            if (!state->filter_category[e->category]) continue;
            if (e->year == state->view_year && e->month == state->view_month && e->day == state->view_day) {
                float start_f = (float)(e->start_hour - 8) + (float)e->start_min / 60.0f;
                float end_f = (float)(e->end_hour - 8) + (float)e->end_min / 60.0f;
                float ey = day_grid_top + 32.0f + start_f * day_hour_h;
                float eh = (end_f - start_f) * day_hour_h;
                if (eh < 34.0f) eh = 34.0f;
                float ex = main_x + day_ruler_w + 24.0f;
                float ew = main_w - day_ruler_w - 50.0f;

                CategoryColor cc = get_category_color(e->category, is_dark);
                rife_push_scissor(core, ex, ey, ew, eh);

                rife_draw_round_rect(core, ex, ey, ew, eh, 8.0f, cc.bg, cc.border);
                rife_draw_round_rect(core, ex + 2.0f, ey + 2.0f, 5.0f, eh - 4.0f, 2.5f, cc.bar, cc.bar);

                rife_draw_text_font(core, ex + 14.0f, ey + 6.0f, e->title, cc.text, 1);
                char t_sub[64];
                snprintf(t_sub, sizeof(t_sub), "%02d:%02d - %02d:%02d · %s", e->start_hour, e->start_min, e->end_hour, e->end_min, e->location);
                rife_draw_text_font(core, ex + 14.0f, ey + 24.0f, t_sub, text_muted, 3);

                rife_pop_scissor(core);
            }
        }
    }
    // ==========================================
    // 视图 3：月视图 (Month View - 35/42格大月历)
    // ==========================================
    else if (state->view_mode == CAL_VIEW_MONTH) {
        float m_col_w = main_w / 7.0f;
        float m_row_h = (main_h - 28.0f) / 5.0f;
        float m_top = main_y + 28.0f;

        const char* wk_names[7] = { "周一", "周二", "周三", "周四", "周五", "周六", "周日" };
        for (int c = 0; c < 7; c++) {
            float mid_x = main_x + (float)c * m_col_w + m_col_w * 0.5f;
            rife_draw_text_font(core, mid_x - 12.0f, main_y + 6.0f, wk_names[c], text_muted, 3);
        }
        rife_draw_rect(core, main_x, m_top, main_w, 1.0f, border_col);

        int first_w = get_day_of_week(state->view_year, state->view_month, 1);
        int total_d = days_in_month(state->view_year, state->view_month);

        for (int d = 1; d <= total_d; d++) {
            int slot = first_w + d - 1;
            int r = slot / 7;
            int c = slot % 7;
            if (r >= 5) break;

            float cx = main_x + (float)c * m_col_w;
            float cy = m_top + (float)r * m_row_h;

            rife_draw_rect(core, cx, cy, m_col_w, m_row_h, border_col);

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
                if (e->year == state->view_year && e->month == state->view_month && e->day == d) {
                    CategoryColor cc = get_category_color(e->category, is_dark);
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

        float card_y = list_y + 36.0f;
        for (int i = 0; i < state->event_count; i++) {
            CalendarEvent* e = &state->events[i];
            if (!state->filter_category[e->category]) continue;

            CategoryColor cc = get_category_color(e->category, is_dark);
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
            rife_draw_text_font(core, main_x + 68.0f, card_y + 8.0f, e->title, cc.text, 1);

            // 时间与地点标签
            char meta_str[64];
            snprintf(meta_str, sizeof(meta_str), "%d月%d日 %02d:%02d - %02d:%02d · %s", e->month, e->day, e->start_hour, e->start_min, e->end_hour, e->end_min, e->location);
            rife_draw_text_font(core, main_x + 68.0f, card_y + 26.0f, meta_str, text_muted, 3);

            card_y += ch + 10.0f;
            if (card_y + ch > client_y + client_h) break;
        }
    }

    // ==========================================
    // 弹窗浮层：新建日程模态卡片 (New Event Modal)
    // ==========================================
    if (state->show_new_modal) {
        float mw = 360.0f;
        float mh = 280.0f;
        float mx0 = client_x + (client_w - mw) * 0.5f;
        float my0 = client_y + (client_h - mh) * 0.5f;

        // 暗晶柔和阴影与卡片底板
        rife_draw_round_rect(core, mx0, my0, mw, mh, 12.0f, is_dark ? 0x231A38FF : 0xFFFFFFFF, is_dark ? 0x4D3678FF : 0xCBD5E1FF);

        // 标题
        rife_draw_text_font(core, mx0 + 16.0f, my0 + 16.0f, is_zh ? "快速创建日程" : "Quick New Event", text_title, 1);
        rife_draw_text_font(core, mx0 + 16.0f, my0 + 44.0f, is_zh ? "选择分类与日程类型" : "Select Category & Preset", text_muted, 3);

        // 分类标签胶囊 (4类)
        float cat_y = my0 + 64.0f;
        for (int c = 0; c < 4; c++) {
            float bx = mx0 + 16.0f + (float)c * 82.0f;
            bool is_c_act = (state->new_cat == c);
            CategoryColor cc = get_category_color((CalendarCategory)c, is_dark);
            rife_draw_round_rect(core, bx, cat_y, 76.0f, 26.0f, 6.0f, is_c_act ? cc.bar : cc.bg, cc.border);
            rife_draw_text_font(core, bx + 12.0f, cat_y + 5.0f, is_zh ? cat_names_zh[c] : cat_names_en[c], is_c_act ? 0xFFFFFFFF : cc.text, 4);
        }

        // 预设标题 (4项)
        static const char* presets[4] = {
            "产品交互体验走查",
            "核心技术方案评审",
            "深度专注与思考",
            "版本交付发布复盘"
        };
        float pre_y = my0 + 104.0f;
        for (int p = 0; p < 4; p++) {
            float row = (float)(p / 2);
            float col = (float)(p % 2);
            float bx = mx0 + 16.0f + col * 166.0f;
            float by = pre_y + row * 32.0f;
            bool is_p_act = (state->new_preset_idx == p);
            rife_draw_round_rect(core, bx, by, 158.0f, 26.0f, 6.0f, is_p_act ? (is_dark ? 0x3D2C63FF : 0xEFF6FFFF) : (is_dark ? 0x1B142BFF : 0xF8FAFCFF), is_p_act ? rtodo_blue : border_col);
            rife_draw_text_font(core, bx + 12.0f, by + 5.0f, presets[p], is_p_act ? (is_dark ? 0xD8B4FEFF : rtodo_blue) : text_title, 3);
        }

        // 时间选择
        float t_y = my0 + 180.0f;
        rife_draw_text_font(core, mx0 + 16.0f, t_y + 6.0f, is_zh ? "开始时间:" : "Start Time:", text_muted, 3);
        rife_draw_round_rect(core, mx0 + 82.0f, t_y, 28.0f, 26.0f, 5.0f, is_dark ? 0x2A1F42FF : 0xF1F5F9FF, border_col);
        rife_draw_text_font(core, mx0 + 92.0f, t_y + 4.0f, "-", text_title, 0);

        char h_buf[16];
        snprintf(h_buf, sizeof(h_buf), "%02d:00", state->new_hour);
        rife_draw_text_font(core, mx0 + 118.0f, t_y + 4.0f, h_buf, text_title, 1);

        rife_draw_round_rect(core, mx0 + 168.0f, t_y, 28.0f, 26.0f, 5.0f, is_dark ? 0x2A1F42FF : 0xF1F5F9FF, border_col);
        rife_draw_text_font(core, mx0 + 177.0f, t_y + 4.0f, "+", text_title, 0);

        // 底部按钮：取消 / 确认添加
        float btn_y = my0 + mh - 44.0f;
        rife_draw_round_rect(core, mx0 + mw - 170.0f, btn_y, 66.0f, 30.0f, 6.0f, is_dark ? 0x241D35FF : 0xF1F5F9FF, border_col);
        rife_draw_text_font(core, mx0 + mw - 150.0f, btn_y + 6.0f, is_zh ? "取消" : "Cancel", text_muted, 0);

        rife_draw_round_rect(core, mx0 + mw - 96.0f, btn_y, 80.0f, 30.0f, 6.0f, rtodo_blue, rtodo_blue);
        rife_draw_text_font(core, mx0 + mw - 80.0f, btn_y + 6.0f, is_zh ? "确认创建" : "Create", 0xFFFFFFFF, 5);
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
            float pop_w = 260.0f;
            float pop_h = 170.0f;
            float pop_x = client_x + client_w - pop_w - 20.0f;
            float pop_y = client_y + 60.0f;

            rife_draw_round_rect(core, pop_x, pop_y, pop_w, pop_h, 10.0f, is_dark ? 0x221838FF : 0xFFFFFFFF, is_dark ? 0x47336EFF : 0xCBD5E1FF);

            CategoryColor cc = get_category_color(cur_e->category, is_dark);
            rife_draw_round_rect(core, pop_x + 14.0f, pop_y + 14.0f, 64.0f, 20.0f, 4.0f, cc.bg, cc.border);
            rife_draw_text_font(core, pop_x + 20.0f, pop_y + 16.0f, is_zh ? cat_names_zh[cur_e->category] : cat_names_en[cur_e->category], cc.text, 4);

            rife_draw_text_font(core, pop_x + 14.0f, pop_y + 40.0f, cur_e->title, text_title, 1);

            char time_loc[64];
            snprintf(time_loc, sizeof(time_loc), "%02d:%02d-%02d:%02d · %s", cur_e->start_hour, cur_e->start_min, cur_e->end_hour, cur_e->end_min, cur_e->location);
            rife_draw_text_font(core, pop_x + 14.0f, pop_y + 64.0f, time_loc, text_muted, 3);

            rife_draw_text_font(core, pop_x + 14.0f, pop_y + 86.0f, cur_e->desc, text_muted, 3);

            // 操作：标记完成 / 关闭
            float act_y = pop_y + pop_h - 38.0f;
            rife_draw_round_rect(core, pop_x + 14.0f, act_y, 104.0f, 26.0f, 6.0f, cur_e->is_completed ? 0x10B981FF : (is_dark ? 0x2D2248FF : 0xEFF6FFFF), rtodo_blue);
            rife_draw_text_font(core, pop_x + 24.0f, act_y + 5.0f, cur_e->is_completed ? (is_zh ? "已完成" : "Completed") : (is_zh ? "标记为完成" : "Mark Done"), cur_e->is_completed ? 0xFFFFFFFF : (is_dark ? 0xD8B4FEFF : rtodo_blue), 5);

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
    .name_zh = "Rtodo 日程",
    .name_en = "Rtodo",
    .glyph = "R",
    .color_top = 0x3370FFFF, // Rtodo 品牌蓝
    .color_bot = 0x1E40AFFF,
    .default_w = 900.0f,
    .default_h = 580.0f,
    .pin_to_dock = true,
    .create = calendar_create,
    .destroy = calendar_destroy,
    .update = calendar_update,
    .render = calendar_render
};

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
// 滴答清单 (TickTick) 状态机定义 (Zero Heap Churn: 纯 C11 微内核)
// -------------------------------------------------------------

typedef struct {
    CalendarViewMode view_mode; // 0: 待办三栏流, 1: 四象限, 2: 周视图, 3: 日视图, 4: 月视图, 5: 清单
    int cur_year;
    int cur_month;   // 1 - 12
    int cur_day;     // 1 - 31
    int cur_hour;    // 0 - 23
    int cur_min;     // 0 - 59
    int cur_wday;    // 0=Sun .. 6=Sat

    int view_year;
    int view_month;
    int view_day;

    // 自定义标签系统
    CustomTag tags[CAL_MAX_CUSTOM_TAGS];
    int tag_count;

    // 自定义清单系统 (Lists)
    TickList lists[TICK_MAX_LISTS];
    int list_count;

    // 待办任务列表 (本地持久化)
    CalendarEvent events[CAL_MAX_EVENTS];
    int event_count;

    // 当前选中的待办事项（用于在右侧 Inspector 呈现详情）
    uint32_t selected_event_id;

    // 智能清单选中项:
    // 0: 收集箱, 1: 今天, 2: 明天, 3: 最近7天, 4: 全部待办, 5: 已完成, 6+: 自定义清单 (list_idx = idx - 6)
    int active_list_idx;

    // 任务状态过滤: 0: 全部, 1: 未完成 (默认), 2: 已完成
    int task_filter;

    // 中栏极速添加框
    char quick_add_title[64];
    int quick_add_priority; // 0: none, 1: low, 2: med, 3: high

    // 右栏 Inspector 输入字段
    char inspector_title[64];
    char inspector_desc[128];
    char inspector_subtask[48];

    // 四象限添加输入
    int matrix_active_quad; // 0: none, 1..4
    char matrix_add_title[64];

    // 全局输入焦点:
    // 0: none, 1: quick_add_title, 2: inspector_title, 3: inspector_desc, 4: inspector_subtask, 5: search_query, 6: matrix_add_title, 7: modal_title, 8: modal_desc
    int active_field;
    float cursor_blink_t;

    // 侧边栏搜索
    char search_query[48];

    // 滚动条偏移
    float scroll_tasks;
    float scroll_inspector;
    float scroll_matrix[4];
    float scroll_y; // 日历视图滚动
    float time_scale;

    // 番茄专注全局统计
    int total_pomodoros;
    int total_focus_mins;
    uint32_t active_focus_task_id;
    char active_focus_title[64];

    // 新建日程模态弹窗 (用于日历或全屏快速创建)
    bool show_new_modal;
    int modal_tag_idx;
    int modal_list_idx;
    int modal_priority;
    int new_hour;
    int new_min;
    int new_end_hour;
    int new_end_min;
    int new_duration_idx;
    char input_title[64];
    char input_location[32];
    char input_desc[128];

    // 时间网格拖拽选区 (日历模式)
    bool is_dragging_time;
    int drag_col;
    int drag_target_year;
    int drag_target_month;
    int drag_target_day;
    int drag_start_mins;
    int drag_cur_mins;
    float drag_start_y;
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

static void sync_system_clock(CalendarState* state) {
    time_t raw = time(NULL);
    struct tm* t = localtime(&raw);
    if (t) {
        state->cur_year = t->tm_year + 1900;
        state->cur_month = t->tm_mon + 1;
        state->cur_day = t->tm_mday;
        state->cur_hour = t->tm_hour;
        state->cur_min = t->tm_min;
        state->cur_wday = t->tm_wday; // 0=Sunday
    }
}

// -------------------------------------------------------------
// 色彩体系与持久化路径 (Persistence & Styles)
// -------------------------------------------------------------

static const uint32_t s_priority_colors[4] = {
    0x64748BFF, // None: Slate Muted Gray
    0x3B82F6FF, // Low: Blue
    0xF59E0BFF, // Medium: Amber
    0xEF4444FF  // High: Red
};

void rtodo_get_storage_path(char* out_path, size_t max_len) {
    char appdata[MAX_PATH] = { 0 };
    DWORD len = GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        char rife_dir[MAX_PATH];
        snprintf(rife_dir, sizeof(rife_dir), "%s\\RifeOS", appdata);
        CreateDirectoryA(rife_dir, NULL);
        snprintf(out_path, max_len, "%s\\rtodo_data.bin", rife_dir);
        return;
    }
    snprintf(out_path, max_len, "rtodo_data.bin");
}

bool rtodo_save_storage(const RtodoStorage* in_storage) {
    if (!in_storage) return false;
    char path[MAX_PATH];
    rtodo_get_storage_path(path, sizeof(path));
    FILE* fp = fopen(path, "wb");
    if (!fp) return false;
    fwrite(in_storage, sizeof(RtodoStorage), 1, fp);
    fclose(fp);
    return true;
}

bool rtodo_load_storage(RtodoStorage* out_storage) {
    if (!out_storage) return false;
    char path[MAX_PATH];
    rtodo_get_storage_path(path, sizeof(path));
    FILE* fp = fopen(path, "rb");
    if (!fp) return false;
    bool ok = false;
    if (fread(out_storage, sizeof(RtodoStorage), 1, fp) == 1 &&
        out_storage->magic == RTODO_MAGIC && out_storage->version == RTODO_VERSION) {
        ok = true;
    }
    fclose(fp);
    return ok;
}

static void init_default_data(CalendarState* state) {
    state->tag_count = 3;
    snprintf(state->tags[0].name, sizeof(state->tags[0].name), "重要");
    state->tags[0].color_bar = 0xEF4444FF;
    state->tags[0].is_enabled = true;
    snprintf(state->tags[1].name, sizeof(state->tags[1].name), "工作");
    state->tags[1].color_bar = 0x6366F1FF;
    state->tags[1].is_enabled = true;
    snprintf(state->tags[2].name, sizeof(state->tags[2].name), "生活");
    state->tags[2].color_bar = 0x10B981FF;
    state->tags[2].is_enabled = true;

    state->list_count = 4;
    snprintf(state->lists[0].name, sizeof(state->lists[0].name), "收集箱");
    state->lists[0].color = 0x818CF8FF;
    snprintf(state->lists[1].name, sizeof(state->lists[1].name), "工作");
    state->lists[1].color = 0x6366F1FF;
    snprintf(state->lists[2].name, sizeof(state->lists[2].name), "生活");
    state->lists[2].color = 0x10B981FF;
    snprintf(state->lists[3].name, sizeof(state->lists[3].name), "学习");
    state->lists[3].color = 0x06B6D4FF;

    // 默认注入 4 条经典高拟真滴答清单体验任务，展示完整的子任务、优先级、象限与专注联动
    state->event_count = 4;

    // 任务 1: 体验滴答清单三栏式工作流 (高优先级, 象限1, 带3个子任务)
    CalendarEvent* e1 = &state->events[0];
    e1->id = 1001;
    snprintf(e1->title, sizeof(e1->title), "体验滴答清单三栏式工作流");
    snprintf(e1->desc, sizeof(e1->desc), "支持子任务打勾、优先级旗帜、四象限分拣与 25 分钟番茄专注");
    e1->list_idx = 1; // 工作
    e1->tag_idx = 0;  // 重要
    e1->priority = TICK_PRIORITY_HIGH;
    e1->quadrant = 1; // 重要且紧急
    e1->has_date = true;
    e1->year = state->cur_year; e1->month = state->cur_month; e1->day = state->cur_day;
    e1->has_time = true;
    e1->start_hour = 14; e1->start_min = 0; e1->end_hour = 15; e1->end_min = 0;
    e1->is_completed = false;
    e1->subtask_count = 3;
    snprintf(e1->subtasks[0].title, sizeof(e1->subtasks[0].title), "探索智能清单（收集箱、今天、明天）");
    e1->subtasks[0].is_done = true;
    snprintf(e1->subtasks[1].title, sizeof(e1->subtasks[1].title), "进入四象限看板 (Alt+2) 查看任务矩阵");
    e1->subtasks[1].is_done = false;
    snprintf(e1->subtasks[2].title, sizeof(e1->subtasks[2].title), "点击右侧「开始番茄专注」沉浸攻坚");
    e1->subtasks[2].is_done = false;

    // 任务 2: 制定本周核心研发规划 (中优先级, 象限2)
    CalendarEvent* e2 = &state->events[1];
    e2->id = 1002;
    snprintf(e2->title, sizeof(e2->title), "在四象限看板中分拣本周重要事项");
    snprintf(e2->desc, sizeof(e2->desc), "象限II：重要不紧急，自我提升与长期价值投资的核心");
    e2->list_idx = 1; // 工作
    e2->tag_idx = 1;  // 工作
    e2->priority = TICK_PRIORITY_MEDIUM;
    e2->quadrant = 2; // 重要不紧急
    e2->has_date = true;
    e2->year = state->cur_year; e2->month = state->cur_month; e2->day = state->cur_day;
    e2->has_time = true;
    e2->start_hour = 16; e2->start_min = 30; e2->end_hour = 17; e2->end_min = 30;
    e2->is_completed = false;

    // 任务 3: 阅读开源微内核架构源码 (低优先级, 象限3)
    CalendarEvent* e3 = &state->events[2];
    e3->id = 1003;
    snprintf(e3->title, sizeof(e3->title), "开启一次 25 分钟番茄专注攻坚");
    snprintf(e3->desc, sizeof(e3->desc), "25分钟高能专注，零打扰，完成后自动记录番茄成果");
    e3->list_idx = 3; // 学习
    e3->tag_idx = -1;
    e3->priority = TICK_PRIORITY_LOW;
    e3->quadrant = 3; // 紧急不重要
    e3->has_date = true;
    e3->year = state->cur_year; e3->month = state->cur_month; e3->day = state->cur_day;
    e3->has_time = false;
    e3->is_completed = false;

    // 任务 4: 运动健身 45 分钟 (无优先级, 象限4)
    CalendarEvent* e4 = &state->events[3];
    e4->id = 1004;
    snprintf(e4->title, sizeof(e4->title), "有氧慢跑与核心力量训练 45 分钟");
    snprintf(e4->desc, sizeof(e4->desc), "健康生活，保持旺盛精力");
    e4->list_idx = 2; // 生活
    e4->tag_idx = 2;  // 生活
    e4->priority = TICK_PRIORITY_NONE;
    e4->quadrant = 4; // 不重要不紧急
    e4->has_date = false;
    e4->is_completed = false;

    state->selected_event_id = 1001; // 默认选中首个任务以便展示右侧详情
    snprintf(state->inspector_title, sizeof(state->inspector_title), "%s", e1->title);
    snprintf(state->inspector_desc, sizeof(state->inspector_desc), "%s", e1->desc);
}

static void save_state_to_storage(const CalendarState* state) {
    RtodoStorage store;
    memset(&store, 0, sizeof(RtodoStorage));
    store.magic = RTODO_MAGIC;
    store.version = RTODO_VERSION;
    store.tag_count = state->tag_count;
    if (store.tag_count > CAL_MAX_CUSTOM_TAGS) store.tag_count = CAL_MAX_CUSTOM_TAGS;
    if (store.tag_count > 0) memcpy(store.tags, state->tags, sizeof(CustomTag) * store.tag_count);

    store.list_count = state->list_count;
    if (store.list_count > TICK_MAX_LISTS) store.list_count = TICK_MAX_LISTS;
    if (store.list_count > 0) memcpy(store.lists, state->lists, sizeof(TickList) * store.list_count);

    store.event_count = state->event_count;
    if (store.event_count > CAL_MAX_EVENTS) store.event_count = CAL_MAX_EVENTS;
    if (store.event_count > 0) memcpy(store.events, state->events, sizeof(CalendarEvent) * store.event_count);

    store.total_pomodoros = state->total_pomodoros;
    store.total_focus_mins = state->total_focus_mins;
    store.active_focus_task_id = state->active_focus_task_id;
    snprintf(store.active_focus_title, sizeof(store.active_focus_title), "%s", state->active_focus_title);

    rtodo_save_storage(&store);
}

static void load_storage_to_state(CalendarState* state) {
    RtodoStorage store;
    if (rtodo_load_storage(&store)) {
        state->tag_count = store.tag_count;
        if (state->tag_count > CAL_MAX_CUSTOM_TAGS) state->tag_count = CAL_MAX_CUSTOM_TAGS;
        if (state->tag_count > 0) memcpy(state->tags, store.tags, sizeof(CustomTag) * state->tag_count);

        state->list_count = store.list_count;
        if (state->list_count > TICK_MAX_LISTS) state->list_count = TICK_MAX_LISTS;
        if (state->list_count > 0) memcpy(state->lists, store.lists, sizeof(TickList) * state->list_count);

        state->event_count = store.event_count;
        if (state->event_count > CAL_MAX_EVENTS) state->event_count = CAL_MAX_EVENTS;
        if (state->event_count > 0) memcpy(state->events, store.events, sizeof(CalendarEvent) * state->event_count);

        state->total_pomodoros = store.total_pomodoros;
        state->total_focus_mins = store.total_focus_mins;
        state->active_focus_task_id = store.active_focus_task_id;
        snprintf(state->active_focus_title, sizeof(state->active_focus_title), "%s", store.active_focus_title);
    } else {
        init_default_data(state);
        save_state_to_storage(state);
    }
}

void rtodo_set_active_view(void* inst, int view_idx) {
    CalendarState* state = (CalendarState*)inst;
    if (!state) return;
    if (view_idx >= 0 && view_idx <= 5) {
        state->view_mode = (CalendarViewMode)view_idx;
    }
}

uint32_t rtodo_get_active_focus_task(void* inst, char* out_title, size_t max_title) {
    CalendarState* state = (CalendarState*)inst;
    if (!state) return 0;
    if (out_title && max_title > 0) {
        snprintf(out_title, max_title, "%s", state->active_focus_title);
    }
    return state->active_focus_task_id;
}

void rtodo_add_pomodoro_to_active_task(void* inst) {
    CalendarState* state = (CalendarState*)inst;
    if (!state) return;
    state->total_pomodoros++;
    state->total_focus_mins += 25;
    if (state->active_focus_task_id > 0) {
        for (int i = 0; i < state->event_count; i++) {
            if (state->events[i].id == state->active_focus_task_id) {
                state->events[i].pomodoro_count++;
                break;
            }
        }
    }
    save_state_to_storage(state);
}

void rtodo_open_create_modal(void* inst) {
    CalendarState* state = (CalendarState*)inst;
    if (!state) return;
    state->view_mode = CAL_VIEW_TASKS;
    state->active_field = 1; // 聚焦中栏极速添加框
}

// -------------------------------------------------------------
// 文本输入退格辅助 (UTF-8 Boundary Safe Pop-back)
// -------------------------------------------------------------
static void utf8_pop_back(char* s) {
    if (!s || s[0] == '\0') return;
    size_t len = strlen(s);
    if (len == 0) return;
    size_t i = len - 1;
    while (i > 0 && (s[i] & 0xC0) == 0x80) {
        i--;
    }
    s[i] = '\0';
}

// -------------------------------------------------------------
// 任务匹配与统计过滤器 (Task Matching & Counts)
// -------------------------------------------------------------

static bool is_task_in_list(const CalendarState* state, const CalendarEvent* e, int list_idx) {
    if (!state || !e) return false;
    if (list_idx == 0) { // 收集箱 (Inbox)
        return (e->list_idx == 0);
    } else if (list_idx == 1) { // 今天 (Today)
        return (e->has_date && e->year == state->cur_year && e->month == state->cur_month && e->day == state->cur_day);
    } else if (list_idx == 2) { // 明天 (Tomorrow)
        int ty, tm, td;
        add_days(state->cur_year, state->cur_month, state->cur_day, 1, &ty, &tm, &td);
        return (e->has_date && e->year == ty && e->month == tm && e->day == td);
    } else if (list_idx == 3) { // 最近7天 (Next 7 Days)
        if (!e->has_date) return false;
        for (int d = 0; d < 7; d++) {
            int ty, tm, td;
            add_days(state->cur_year, state->cur_month, state->cur_day, d, &ty, &tm, &td);
            if (e->year == ty && e->month == tm && e->day == td) return true;
        }
        return false;
    } else if (list_idx == 4) { // 全部待办
        return true;
    } else if (list_idx == 5) { // 已完成
        return e->is_completed;
    } else if (list_idx >= 6) { // 自定义清单
        int c_idx = list_idx - 5;
        return (e->list_idx == c_idx);
    }
    return false;
}

static int get_list_uncompleted_count(const CalendarState* state, int list_idx) {
    if (!state) return 0;
    int count = 0;
    for (int i = 0; i < state->event_count; i++) {
        const CalendarEvent* e = &state->events[i];
        if (!e->is_completed && is_task_in_list(state, e, list_idx)) {
            count++;
        }
    }
    return count;
}

static CalendarEvent* get_selected_event(CalendarState* state) {
    if (!state || state->selected_event_id == 0) return NULL;
    for (int i = 0; i < state->event_count; i++) {
        if (state->events[i].id == state->selected_event_id) {
            return &state->events[i];
        }
    }
    return NULL;
}

// 纯 C 过程式加号图标
static void cal_draw_plus_icon(RifeCore* core, float cx, float cy, float span, float thickness, uint32_t color) {
    float half = span * 0.5f;
    rife_draw_line(core, cx, cy - half, cx, cy + half, thickness, color);
    rife_draw_line(core, cx - half, cy, cx + half, cy, thickness, color);
}

// 纯 C 绘制复选框圆圈 (Checkbox Circle with Priority Border)
static void draw_task_checkbox(RifeCore* core, float cx, float cy, float r, int priority, bool is_completed, bool is_hover) {
    uint32_t border_col = s_priority_colors[priority & 3];
    if (is_completed) {
        // 完成状态：实心翠绿或电光蓝圆盘，内嵌白色纯正对勾
        rife_draw_circle(core, cx, cy, r, 0x10B981FF, 0x10B981FF);
        rife_draw_line(core, cx - r * 0.45f, cy, cx - r * 0.1f, cy + r * 0.35f, 1.8f, 0xFFFFFFFF);
        rife_draw_line(core, cx - r * 0.1f, cy + r * 0.35f, cx + r * 0.5f, cy - r * 0.35f, 1.8f, 0xFFFFFFFF);
    } else {
        // 未完成状态：空心圆，边框颜色反映优先级
        uint32_t fill_col = is_hover ? (border_col & 0xFFFFFF33) : 0x00000000;
        rife_draw_circle(core, cx, cy, r, fill_col, border_col);
    }
}

// -------------------------------------------------------------
// 模块 1：待办清单三栏式工作台渲染 (Tasks 3-Column Flow)
// -------------------------------------------------------------

static void render_tasks_three_column(CalendarState* state, RifeCore* core, float cx, float cy, float cw, float ch) {
    bool is_dark = (rife_get_system_config()->palette == PALETTE_OBSIDIAN);
    bool is_zh   = (rife_get_system_config()->language == LANG_ZH_CN);

    uint32_t col_card_bg  = is_dark ? 0x14151DFF : 0xFFFFFFFF;
    uint32_t col_card_bd  = is_dark ? 0x1E202BFF : 0xE2E8F0FF;
    uint32_t col_div      = is_dark ? 0x1E202BFF : 0xE2E8F0FF;
    uint32_t txt_title    = is_dark ? 0xF8FAFCFF : 0x0F172AFF;
    uint32_t txt_body     = is_dark ? 0xCBD5E1FF : 0x334155FF;
    uint32_t txt_muted    = is_dark ? 0x94A3B8FF : 0x64748BFF;
    uint32_t accent_pri   = is_dark ? 0x6366F1FF : 0x4F46E5FF; // Electric Indigo

    float col1_w = 180.0f;
    float col3_w = (state->selected_event_id > 0) ? 340.0f : 0.0f;
    float col2_w = cw - col1_w - col3_w;

    // =========================================================
    // 栏 1：清单与智能分类侧边栏 (Lists Sidebar)
    // =========================================================
    rife_draw_rect(core, cx, cy, col1_w, ch, is_dark ? 0x101116FF : 0xF8FAFCFF);
    rife_draw_rect(core, cx + col1_w - 1.0f, cy, 1.0f, ch, col_div);

    float list_y = cy + 14.0f;
    rife_draw_text_font(core, cx + 16.0f, list_y, is_zh ? "智能清单" : "Smart Lists", txt_muted, 4);
    list_y += 24.0f;

    const char* smart_names_zh[6] = { "收集箱", "今天", "明天", "最近7天", "全部任务", "已完成" };
    const char* smart_names_en[6] = { "Inbox", "Today", "Tomorrow", "Next 7 Days", "All Tasks", "Completed" };
    const char* smart_icons[6]    = { "📥", "☀️", "📅", "🗓️", "📋", "✅" };

    for (int i = 0; i < 6; i++) {
        bool is_act = (state->active_list_idx == i);
        float item_y = list_y;
        float item_h = 32.0f;
        float item_w = col1_w - 20.0f;
        float item_x = cx + 10.0f;

        if (is_act) {
            rife_draw_round_rect(core, item_x, item_y, item_w, item_h, 6.0f, is_dark ? 0x1A1C28FF : 0xEEF2FFFF, is_dark ? 0x2A2E42FF : 0xC7D2FEFF);
            rife_draw_round_rect(core, item_x + 1.5f, item_y + 6.0f, 3.0f, item_h - 12.0f, 1.5f, accent_pri, 0);
        }

        rife_draw_text_font(core, item_x + 10.0f, item_y + 6.0f, smart_icons[i], is_act ? accent_pri : txt_body, 3);
        rife_draw_text_font(core, item_x + 32.0f, item_y + 7.0f, is_zh ? smart_names_zh[i] : smart_names_en[i], is_act ? txt_title : txt_body, is_act ? 5 : 0);

        // 任务计数 Badge
        int uncompleted_cnt = get_list_uncompleted_count(state, i);
        if (uncompleted_cnt > 0) {
            char cnt_str[16];
            snprintf(cnt_str, sizeof(cnt_str), "%d", uncompleted_cnt);
            float badge_w = 22.0f;
            float badge_x = item_x + item_w - badge_w - 6.0f;
            uint32_t b_col = (i == 1) ? 0xEF4444FF : (is_dark ? 0x222432FF : 0xE2E8F0FF);
            rife_draw_round_rect(core, badge_x, item_y + 7.0f, badge_w, 18.0f, 4.0f, b_col, 0);
            rife_draw_text_rect(core, badge_x, item_y + 7.0f, badge_w, 18.0f, cnt_str, (i == 1) ? 0xFFFFFFFF : txt_muted, 4, 0);
        }

        list_y += 34.0f;
    }

    // 分割线
    list_y += 8.0f;
    rife_draw_rect(core, cx + 16.0f, list_y, col1_w - 32.0f, 1.0f, col_div);
    list_y += 12.0f;

    rife_draw_text_font(core, cx + 16.0f, list_y, is_zh ? "我的清单" : "My Lists", txt_muted, 4);
    list_y += 24.0f;

    const char* custom_list_icons[4] = { "📥", "💼", "🏠", "📚" };
    for (int c = 1; c < state->list_count && c < 4; c++) {
        int list_global_idx = 5 + c;
        bool is_act = (state->active_list_idx == list_global_idx);
        float item_y = list_y;
        float item_h = 32.0f;
        float item_w = col1_w - 20.0f;
        float item_x = cx + 10.0f;

        if (is_act) {
            rife_draw_round_rect(core, item_x, item_y, item_w, item_h, 6.0f, is_dark ? 0x1A1C28FF : 0xEEF2FFFF, is_dark ? 0x2A2E42FF : 0xC7D2FEFF);
            rife_draw_round_rect(core, item_x + 1.5f, item_y + 6.0f, 3.0f, item_h - 12.0f, 1.5f, state->lists[c].color, 0);
        }

        rife_draw_text_font(core, item_x + 10.0f, item_y + 6.0f, custom_list_icons[c], state->lists[c].color, 3);
        rife_draw_text_font(core, item_x + 32.0f, item_y + 7.0f, state->lists[c].name, is_act ? txt_title : txt_body, is_act ? 5 : 0);

        int cnt = get_list_uncompleted_count(state, list_global_idx);
        if (cnt > 0) {
            char cnt_str[16];
            snprintf(cnt_str, sizeof(cnt_str), "%d", cnt);
            float badge_w = 22.0f;
            float badge_x = item_x + item_w - badge_w - 6.0f;
            rife_draw_round_rect(core, badge_x, item_y + 7.0f, badge_w, 18.0f, 4.0f, is_dark ? 0x222432FF : 0xE2E8F0FF, 0);
            rife_draw_text_rect(core, badge_x, item_y + 7.0f, badge_w, 18.0f, cnt_str, txt_muted, 4, 0);
        }

        list_y += 34.0f;
    }

    // =========================================================
    // 栏 2：中栏任务流与极速添加 (Task Stream)
    // =========================================================
    float col2_x = cx + col1_w;
    rife_draw_rect(core, col2_x, cy, col2_w, ch, is_dark ? 0x0A0B0EFF : 0xFFFFFFFF);
    if (col3_w > 0.0f) {
        rife_draw_rect(core, col2_x + col2_w - 1.0f, cy, 1.0f, ch, col_div);
    }

    // A. 顶部列表标头 (Header)
    float h_y = cy + 14.0f;
    const char* cur_list_title = (state->active_list_idx < 6) ?
        (is_zh ? smart_names_zh[state->active_list_idx] : smart_names_en[state->active_list_idx]) :
        state->lists[state->active_list_idx - 5].name;

    rife_draw_text_font(core, col2_x + 20.0f, h_y, cur_list_title, txt_title, 2);

    // 过滤选择器: [未完成] [全部] [已完成]
    float f_w = 54.0f, f_h = 24.0f;
    float f_x0 = col2_x + col2_w - (f_w * 3.0f + 24.0f);
    const char* f_labels[3] = { "未完成", "全部", "已完成" };
    for (int f = 0; f < 3; f++) {
        float fx = f_x0 + (float)f * f_w;
        bool is_cur_f = (state->task_filter == (f == 0 ? 1 : (f == 1 ? 0 : 2)));
        if (is_cur_f) {
            rife_draw_round_rect(core, fx, h_y + 2.0f, f_w - 4.0f, f_h, 5.0f, is_dark ? 0x1E202BFF : 0xE2E8F0FF, accent_pri);
            rife_draw_text_rect(core, fx, h_y + 2.0f, f_w - 4.0f, f_h, f_labels[f], accent_pri, 4, 0);
        } else {
            rife_draw_text_rect(core, fx, h_y + 2.0f, f_w - 4.0f, f_h, f_labels[f], txt_muted, 4, 0);
        }
    }

    // B. 极速添加输入框 (Quick Add Input Box)
    float q_y = cy + 50.0f;
    float q_w = col2_w - 40.0f;
    float q_x = col2_x + 20.0f;
    float q_h = 40.0f;

    bool q_focused = (state->active_field == 1);
    uint32_t q_bd = q_focused ? accent_pri : col_card_bd;
    rife_draw_round_rect(core, q_x, q_y, q_w, q_h, 7.0f, is_dark ? 0x14151DFF : 0xF8FAFCFF, q_bd);

    // 左侧加号图标
    cal_draw_plus_icon(core, q_x + 20.0f, q_y + q_h * 0.5f, 11.0f, 1.8f, accent_pri);

    // 输入文本或占位符
    if (state->quick_add_title[0] != '\0') {
        rife_draw_text_font(core, q_x + 36.0f, q_y + 11.0f, state->quick_add_title, txt_title, 0);
        if (q_focused && ((int)(state->cursor_blink_t * 2.0f) % 2 == 0)) {
            // 绘制闪烁光标
            float text_w = (float)strlen(state->quick_add_title) * 8.0f; // 估算宽度
            rife_draw_rect(core, q_x + 36.0f + text_w, q_y + 11.0f, 1.8f, 18.0f, accent_pri);
        }
    } else {
        const char* hint = is_zh ? "+ 添加待办事项，回车快速保存..." : "+ Add a task, press Enter to save...";
        rife_draw_text_font(core, q_x + 36.0f, q_y + 11.0f, hint, txt_muted, 0);
        if (q_focused && ((int)(state->cursor_blink_t * 2.0f) % 2 == 0)) {
            rife_draw_rect(core, q_x + 36.0f, q_y + 11.0f, 1.8f, 18.0f, accent_pri);
        }
    }

    // 右侧优先级切换旗帜按钮
    float p_flag_x = q_x + q_w - 32.0f;
    float p_flag_y = q_y + 10.0f;
    uint32_t p_col = s_priority_colors[state->quick_add_priority & 3];
    rife_draw_text_rect(core, p_flag_x, p_flag_y, 22.0f, 20.0f, "🚩", p_col, 4, 0);

    // C. 任务卡片流 (Task Card Stream)
    float card_stream_y = q_y + q_h + 12.0f;
    float card_h = 52.0f;
    float card_gap = 6.0f;
    float curr_y = card_stream_y + state->scroll_tasks;

    int rendered_tasks = 0;
    for (int i = 0; i < state->event_count; i++) {
        CalendarEvent* e = &state->events[i];
        if (!is_task_in_list(state, e, state->active_list_idx)) continue;
        if (state->task_filter == 1 && e->is_completed) continue; // 仅未完成
        if (state->task_filter == 2 && !e->is_completed) continue; // 仅已完成

        float card_x = q_x;
        float card_w = q_w;
        float card_y = curr_y;

        if (card_y + card_h >= card_stream_y && card_y <= cy + ch) {
            bool is_sel = (state->selected_event_id == e->id);
            uint32_t c_bg = is_sel ? (is_dark ? 0x1A1C28FF : 0xEEF2FFFF) : col_card_bg;
            uint32_t c_bd = is_sel ? accent_pri : col_card_bd;

            rife_draw_round_rect(core, card_x, card_y, card_w, card_h, 8.0f, c_bg, c_bd);

            // 1. 复选框圆圈 (Checkbox Circle)
            float chk_cx = card_x + 22.0f;
            float chk_cy = card_y + card_h * 0.5f;
            draw_task_checkbox(core, chk_cx, chk_cy, 8.5f, e->priority, e->is_completed, false);

            // 2. 任务标题
            float title_x = card_x + 42.0f;
            float title_y = card_y + 8.0f;
            uint32_t t_col = e->is_completed ? txt_muted : txt_title;
            rife_draw_text_font(core, title_x, title_y, e->title, t_col, 0);

            // 若已完成，绘制贯穿删除线 (Strikethrough)
            if (e->is_completed) {
                float tw = (float)strlen(e->title) * 7.5f;
                rife_draw_line(core, title_x, title_y + 9.0f, title_x + tw, title_y + 9.0f, 1.2f, txt_muted);
            }

            // 3. 次级徽标行 (日期、标签、子任务进度)
            float badge_y = card_y + 28.0f;
            float badge_x = title_x;

            // 到期日徽标
            if (e->has_date) {
                char date_buf[32];
                if (e->year == state->cur_year && e->month == state->cur_month && e->day == state->cur_day) {
                    snprintf(date_buf, sizeof(date_buf), e->has_time ? "今天 %02d:%02d" : "今天", e->start_hour, e->start_min);
                } else {
                    snprintf(date_buf, sizeof(date_buf), "%d月%d日", e->month, e->day);
                }
                rife_draw_round_rect(core, badge_x, badge_y, 64.0f, 16.0f, 3.0f, is_dark ? 0x222432FF : 0xF1F5F9FF, 0);
                rife_draw_text_rect(core, badge_x, badge_y, 64.0f, 16.0f, date_buf, accent_pri, 4, 0);
                badge_x += 70.0f;
            }

            // 标签徽标
            if (e->tag_idx >= 0 && e->tag_idx < state->tag_count) {
                char tag_buf[32];
                snprintf(tag_buf, sizeof(tag_buf), "#%s", state->tags[e->tag_idx].name);
                rife_draw_round_rect(core, badge_x, badge_y, 50.0f, 16.0f, 3.0f, is_dark ? 0x1E202BFF : 0xEEF2FFFF, 0);
                rife_draw_text_rect(core, badge_x, badge_y, 50.0f, 16.0f, tag_buf, state->tags[e->tag_idx].color_bar, 4, 0);
                badge_x += 56.0f;
            }

            // 子任务进度 (例如 ☑ 1/3)
            if (e->subtask_count > 0) {
                int done_cnt = 0;
                for (int s = 0; s < e->subtask_count; s++) {
                    if (e->subtasks[s].is_done) done_cnt++;
                }
                char sub_buf[24];
                snprintf(sub_buf, sizeof(sub_buf), "☑ %d/%d", done_cnt, e->subtask_count);
                rife_draw_round_rect(core, badge_x, badge_y, 46.0f, 16.0f, 3.0f, is_dark ? 0x1E202BFF : 0xF1F5F9FF, 0);
                rife_draw_text_rect(core, badge_x, badge_y, 46.0f, 16.0f, sub_buf, (done_cnt == e->subtask_count) ? 0x10B981FF : txt_muted, 4, 0);
            }

            // 右侧优先级旗帜
            if (e->priority > 0) {
                rife_draw_text_rect(core, card_x + card_w - 30.0f, card_y + 16.0f, 20.0f, 20.0f, "🚩", s_priority_colors[e->priority & 3], 4, 0);
            }
        }

        curr_y += (card_h + card_gap);
        rendered_tasks++;
    }

    if (rendered_tasks == 0) {
        // 空状态 (Empty State)
        float empty_y = cy + ch * 0.4f;
        rife_draw_text_font(core, col2_x + col2_w * 0.5f - 80.0f, empty_y, is_zh ? "✨ 暂无待办事项" : "✨ No tasks here", txt_title, 1);
        rife_draw_text_font(core, col2_x + col2_w * 0.5f - 90.0f, empty_y + 24.0f, is_zh ? "点击上方输入框极速添加" : "Add one using the input above", txt_muted, 0);
    }

    // =========================================================
    // 栏 3：右栏任务深度详情面板 (Task Inspector)
    // =========================================================
    if (col3_w > 0.0f) {
        float col3_x = cx + cw - col3_w;
        rife_draw_rect(core, col3_x, cy, col3_w, ch, is_dark ? 0x101118FF : 0xF8FAFCFF);

        CalendarEvent* cur_e = get_selected_event(state);
        if (cur_e) {
            float ins_y = cy + 16.0f;
            float ins_x = col3_x + 18.0f;
            float ins_w = col3_w - 36.0f;

            // 1. 顶栏：大复选圆圈 + 标题编辑框 + 关闭按钮 [×]
            draw_task_checkbox(core, ins_x + 10.0f, ins_y + 14.0f, 10.0f, cur_e->priority, cur_e->is_completed, false);
            rife_draw_round_rect(core, ins_x + 30.0f, ins_y, ins_w - 60.0f, 32.0f, 6.0f, is_dark ? 0x181A24FF : 0xFFFFFFFF, (state->active_field == 2) ? accent_pri : col_card_bd);
            rife_draw_text_font(core, ins_x + 38.0f, ins_y + 6.0f, state->inspector_title[0] ? state->inspector_title : cur_e->title, txt_title, 1);

            // 右上角关闭抽屉 [×]
            float close_x = col3_x + col3_w - 34.0f;
            rife_draw_text_rect(core, close_x, ins_y + 6.0f, 20.0f, 20.0f, "×", txt_muted, 1, 0);
            ins_y += 44.0f;

            // 2. 截止日期与时间设置
            rife_draw_text_font(core, ins_x, ins_y, is_zh ? "截止日期" : "Due Date", txt_muted, 4);
            ins_y += 20.0f;

            float pill_w = 66.0f;
            float pill_h = 26.0f;
            const char* d_pills[4] = { "今天", "明天", "下周", "清除" };
            for (int p = 0; p < 4; p++) {
                float px = ins_x + (float)p * (pill_w + 6.0f);
                rife_draw_round_rect(core, px, ins_y, pill_w, pill_h, 5.0f, is_dark ? 0x181A24FF : 0xFFFFFFFF, col_card_bd);
                rife_draw_text_rect(core, px, ins_y, pill_w, pill_h, d_pills[p], (p == 3) ? 0xEF4444FF : txt_body, 4, 0);
            }
            ins_y += 36.0f;

            // 3. 优先级旗帜选择器
            rife_draw_text_font(core, ins_x, ins_y, is_zh ? "优先级" : "Priority", txt_muted, 4);
            ins_y += 20.0f;

            const char* pri_labels[4] = { "🏳️ 无", "🚩 低", "🚩 中", "🚩 高" };
            for (int p = 0; p < 4; p++) {
                float px = ins_x + (float)p * (pill_w + 6.0f);
                bool is_cur_p = (cur_e->priority == p);
                uint32_t bd_c = is_cur_p ? s_priority_colors[p] : col_card_bd;
                rife_draw_round_rect(core, px, ins_y, pill_w, pill_h, 5.0f, is_dark ? 0x181A24FF : 0xFFFFFFFF, bd_c);
                rife_draw_text_rect(core, px, ins_y, pill_w, pill_h, pri_labels[p], s_priority_colors[p], 4, 0);
            }
            ins_y += 36.0f;

            // 4. 所属清单选择
            rife_draw_text_font(core, ins_x, ins_y, is_zh ? "所属清单" : "List", txt_muted, 4);
            ins_y += 20.0f;
            for (int l = 0; l < state->list_count && l < 4; l++) {
                float lx = ins_x + (float)l * (pill_w + 6.0f);
                bool is_cur_l = (cur_e->list_idx == l);
                uint32_t bd_c = is_cur_l ? accent_pri : col_card_bd;
                rife_draw_round_rect(core, lx, ins_y, pill_w, pill_h, 5.0f, is_dark ? 0x181A24FF : 0xFFFFFFFF, bd_c);
                rife_draw_text_rect(core, lx, ins_y, pill_w, pill_h, state->lists[l].name, is_cur_l ? accent_pri : txt_body, 4, 0);
            }
            ins_y += 40.0f;

            // 5. Checklist 子任务清单
            char sub_head[64];
            snprintf(sub_head, sizeof(sub_head), is_zh ? "子任务清单 (%d 项)" : "Subtasks (%d)", cur_e->subtask_count);
            rife_draw_text_font(core, ins_x, ins_y, sub_head, txt_muted, 4);
            ins_y += 22.0f;

            for (int s = 0; s < cur_e->subtask_count && s < TICK_MAX_SUBTASKS; s++) {
                TickSubtask* sub = &cur_e->subtasks[s];
                float sub_item_y = ins_y;
                rife_draw_round_rect(core, ins_x, sub_item_y, ins_w, 28.0f, 5.0f, is_dark ? 0x14151DFF : 0xFFFFFFFF, col_card_bd);

                // 子任务复选圆圈
                draw_task_checkbox(core, ins_x + 14.0f, sub_item_y + 14.0f, 6.5f, 0, sub->is_done, false);

                // 子任务文字
                rife_draw_text_font(core, ins_x + 30.0f, sub_item_y + 5.0f, sub->title, sub->is_done ? txt_muted : txt_body, 4);
                if (sub->is_done) {
                    float tw = (float)strlen(sub->title) * 6.5f;
                    rife_draw_line(core, ins_x + 30.0f, sub_item_y + 14.0f, ins_x + 30.0f + tw, sub_item_y + 14.0f, 1.0f, txt_muted);
                }

                // 右侧删除子任务按钮 [×]
                rife_draw_text_rect(core, ins_x + ins_w - 24.0f, sub_item_y + 4.0f, 18.0f, 20.0f, "×", txt_muted, 4, 0);
                ins_y += 32.0f;
            }

            // 添加子任务输入框
            if (cur_e->subtask_count < TICK_MAX_SUBTASKS) {
                bool sub_focused = (state->active_field == 4);
                rife_draw_round_rect(core, ins_x, ins_y, ins_w, 28.0f, 5.0f, is_dark ? 0x14151DFF : 0xFFFFFFFF, sub_focused ? accent_pri : col_card_bd);
                if (state->inspector_subtask[0] != '\0') {
                    rife_draw_text_font(core, ins_x + 10.0f, ins_y + 5.0f, state->inspector_subtask, txt_title, 4);
                } else {
                    rife_draw_text_font(core, ins_x + 10.0f, ins_y + 5.0f, is_zh ? "+ 添加子任务，回车确认" : "+ Add subtask, press Enter", txt_muted, 4);
                }
                ins_y += 36.0f;
            }

            // 6. 详细备注 (Notes)
            ins_y += 6.0f;
            rife_draw_text_font(core, ins_x, ins_y, is_zh ? "详细备注" : "Notes", txt_muted, 4);
            ins_y += 20.0f;
            float note_h = 70.0f;
            bool desc_focused = (state->active_field == 3);
            rife_draw_round_rect(core, ins_x, ins_y, ins_w, note_h, 6.0f, is_dark ? 0x14151DFF : 0xFFFFFFFF, desc_focused ? accent_pri : col_card_bd);
            const char* desc_show = state->inspector_desc[0] ? state->inspector_desc : (cur_e->desc[0] ? cur_e->desc : "输入任务详细备注...");
            rife_draw_text_font(core, ins_x + 10.0f, ins_y + 8.0f, desc_show, cur_e->desc[0] ? txt_body : txt_muted, 0);

            // 7. 底部操作栏：开始番茄专注 与 删除
            float act_y = cy + ch - 54.0f;
            float pomo_w = ins_w - 90.0f;
            rife_draw_round_rect(core, ins_x, act_y, pomo_w, 36.0f, 6.0f, accent_pri, 0);
            rife_draw_text_rect(core, ins_x, act_y, pomo_w, 36.0f, is_zh ? "🍅 开始番茄专注 25:00" : "🍅 Start Pomodoro", 0xFFFFFFFF, 5, 0);

            float del_x = ins_x + pomo_w + 10.0f;
            float del_w = 80.0f;
            rife_draw_round_rect(core, del_x, act_y, del_w, 36.0f, 6.0f, is_dark ? 0x221518FF : 0xFEF2F2FF, 0xEF444488);
            rife_draw_text_rect(core, del_x, act_y, del_w, 36.0f, is_zh ? "删除" : "Delete", 0xEF4444FF, 5, 0);
        }
    }
}

// -------------------------------------------------------------
// 模块 2：艾森豪威尔四象限看板渲染 (Eisenhower Matrix View)
// -------------------------------------------------------------

static void render_matrix_quadrants(CalendarState* state, RifeCore* core, float cx, float cy, float cw, float ch) {
    bool is_dark = (rife_get_system_config()->palette == PALETTE_OBSIDIAN);
    bool is_zh   = (rife_get_system_config()->language == LANG_ZH_CN);

    uint32_t col_card_bg  = is_dark ? 0x14151DFF : 0xFFFFFFFF;
    uint32_t txt_title    = is_dark ? 0xF8FAFCFF : 0x0F172AFF;
    uint32_t txt_body     = is_dark ? 0xCBD5E1FF : 0x334155FF;
    uint32_t txt_muted    = is_dark ? 0x94A3B8FF : 0x64748BFF;
    (void)txt_body;

    float pad = 16.0f;
    float gap = 12.0f;
    float qw = (cw - pad * 2.0f - gap) * 0.5f;
    float qh = (ch - pad * 2.0f - gap) * 0.5f;

    const char* q_titles_zh[4] = { "🔴 象限 I · 重要且紧急", "🟡 象限 II · 重要不紧急", "🔵 象限 III · 紧急不重要", "⚪ 象限 IV · 不重要不紧急" };
    const char* q_sub_zh[4]    = { "立即执行 · 核心攻坚", "制定计划 · 价值投资", "快速授权 · 琐事批处理", "尽量减少 · 休闲娱乐" };
    uint32_t q_accents[4]      = { 0xEF4444FF, 0xF59E0BFF, 0x06B6D4FF, 0x64748BFF };

    for (int q = 0; q < 4; q++) {
        float qx = cx + pad + (float)(q % 2) * (qw + gap);
        float qy = cy + pad + (float)(q / 2) * (qh + gap);

        // 象限底板
        rife_draw_round_rect(core, qx, qy, qw, qh, 8.0f, col_card_bg, q_accents[q] & 0xFFFFFF66);
        // 顶部高光指示条
        rife_draw_round_rect(core, qx + 12.0f, qy + 1.5f, qw - 24.0f, 2.0f, 1.0f, q_accents[q], 0);

        // 标题与理念
        rife_draw_text_font(core, qx + 16.0f, qy + 12.0f, is_zh ? q_titles_zh[q] : "Quadrant", q_accents[q], 5);
        rife_draw_text_font(core, qx + 16.0f, qy + 32.0f, is_zh ? q_sub_zh[q] : "Action Plan", txt_muted, 4);

        // 右上角 [+] 快速添加
        float add_btn_x = qx + qw - 36.0f;
        float add_btn_y = qy + 12.0f;
        rife_draw_round_rect(core, add_btn_x, add_btn_y, 22.0f, 22.0f, 4.0f, is_dark ? 0x1E202BFF : 0xF1F5F9FF, q_accents[q]);
        cal_draw_plus_icon(core, add_btn_x + 11.0f, add_btn_y + 11.0f, 9.0f, 1.5f, q_accents[q]);

        // 任务列表
        float task_y = qy + 54.0f;
        int q_target = q + 1; // 1, 2, 3, 4
        int count_in_q = 0;

        for (int i = 0; i < state->event_count; i++) {
            CalendarEvent* e = &state->events[i];
            if (e->quadrant != q_target && !(e->quadrant == 0 && (3 - e->priority + 1) == q_target)) continue;

            float t_h = 36.0f;
            if (task_y + t_h <= qy + qh - 10.0f) {
                // 单任务小卡片
                rife_draw_round_rect(core, qx + 12.0f, task_y, qw - 24.0f, t_h, 6.0f, is_dark ? 0x101118FF : 0xF8FAFCFF, is_dark ? 0x1E202BFF : 0xE2E8F0FF);

                // 复选框
                draw_task_checkbox(core, qx + 24.0f, task_y + t_h * 0.5f, 7.5f, e->priority, e->is_completed, false);

                // 文字
                rife_draw_text_font(core, qx + 38.0f, task_y + 9.0f, e->title, e->is_completed ? txt_muted : txt_title, 4);
                if (e->is_completed) {
                    float tw = (float)strlen(e->title) * 6.5f;
                    rife_draw_line(core, qx + 38.0f, task_y + 17.0f, qx + 38.0f + tw, task_y + 17.0f, 1.0f, txt_muted);
                }

                task_y += (t_h + 6.0f);
                count_in_q++;
            }
        }

        if (count_in_q == 0) {
            rife_draw_text_font(core, qx + 20.0f, qy + qh * 0.5f - 8.0f, is_zh ? "点击右上角 [+] 添加事项" : "Click [+] to add", txt_muted, 4);
        }
    }
}

// -------------------------------------------------------------
// 模块 3：日历多维时间轴网格渲染 (Calendar Timeline Grid)
// -------------------------------------------------------------

static void render_calendar_timeline(CalendarState* state, RifeCore* core, float cx, float cy, float cw, float ch) {
    bool is_dark = (rife_get_system_config()->palette == PALETTE_OBSIDIAN);
    bool is_zh   = (rife_get_system_config()->language == LANG_ZH_CN);

    uint32_t col_line   = is_dark ? 0x1A1C28FF : 0xF1F5F9FF;
    uint32_t txt_title  = is_dark ? 0xF8FAFCFF : 0x0F172AFF;
    uint32_t txt_muted  = is_dark ? 0x94A3B8FF : 0x64748BFF;
    uint32_t accent_pri = is_dark ? 0x6366F1FF : 0x4F46E5FF;

    // A. 顶部周表头
    float head_h = 56.0f;
    rife_draw_rect(core, cx, cy, cw, head_h, is_dark ? 0x101118FF : 0xF8FAFCFF);
    rife_draw_rect(core, cx, cy + head_h - 1.0f, cw, 1.0f, is_dark ? 0x1E202BFF : 0xE2E8F0FF);

    float scale_w = 54.0f;
    float col_w = (cw - scale_w) / 7.0f;

    int sun_y, sun_m, sun_d;
    get_week_sunday(state->view_year, state->view_month, state->view_day, &sun_y, &sun_m, &sun_d);

    const char* week_names[7] = { "周日", "周一", "周二", "周三", "周四", "周五", "周六" };
    for (int col = 0; col < 7; col++) {
        float col_x = cx + scale_w + (float)col * col_w;
        int dy, dm, dd;
        add_days(sun_y, sun_m, sun_d, col, &dy, &dm, &dd);
        bool is_today = (dy == state->cur_year && dm == state->cur_month && dd == state->cur_day);

        rife_draw_text_rect(core, col_x, cy + 8.0f, col_w, 16.0f, is_zh ? week_names[col] : "Day", is_today ? accent_pri : txt_muted, 4, 0);

        char d_str[16];
        snprintf(d_str, sizeof(d_str), "%d", dd);
        if (is_today) {
            float badge_w = 26.0f;
            rife_draw_round_rect(core, col_x + (col_w - badge_w) * 0.5f, cy + 26.0f, badge_w, 22.0f, 6.0f, accent_pri, 0);
            rife_draw_text_rect(core, col_x, cy + 26.0f, col_w, 22.0f, d_str, 0xFFFFFFFF, 5, 0);
        } else {
            rife_draw_text_rect(core, col_x, cy + 26.0f, col_w, 22.0f, d_str, txt_title, 5, 0);
        }

        rife_draw_rect(core, col_x, cy, 1.0f, head_h, is_dark ? 0x1E202BFF : 0xE2E8F0FF);
    }

    // B. 时间网格与事项卡片 (24小时)
    float grid_y0 = cy + head_h;
    float grid_h = ch - head_h;
    float hour_h = 60.0f; // 1小时 = 60px

    rife_push_scissor_round(core, cx, grid_y0, cw, grid_h, 0.0f);

    float scroll_y = state->scroll_y;
    for (int h = 0; h < 24; h++) {
        float line_y = grid_y0 + (float)h * hour_h + scroll_y;
        if (line_y >= grid_y0 - hour_h && line_y <= cy + ch) {
            // 时间标尺文本
            char scale_str[16];
            snprintf(scale_str, sizeof(scale_str), "%02d:00", h);
            rife_draw_text_font(core, cx + 8.0f, line_y - 8.0f, scale_str, txt_muted, 4);
            // 水平网格线
            rife_draw_rect(core, cx + scale_w, line_y, cw - scale_w, 1.0f, col_line);
        }
    }

    // 垂直列网格线
    for (int col = 0; col < 7; col++) {
        float col_x = cx + scale_w + (float)col * col_w;
        rife_draw_rect(core, col_x, grid_y0, 1.0f, grid_h, col_line);
    }

    // 绘制该周日程事项
    for (int i = 0; i < state->event_count; i++) {
        CalendarEvent* e = &state->events[i];
        if (!e->has_date || !e->has_time) continue;

        // 匹配属于哪一天
        for (int col = 0; col < 7; col++) {
            int dy, dm, dd;
            add_days(sun_y, sun_m, sun_d, col, &dy, &dm, &dd);
            if (e->year == dy && e->month == dm && e->day == dd) {
                float col_x = cx + scale_w + (float)col * col_w;
                float start_m = (float)(e->start_hour * 60 + e->start_min);
                float end_m   = (float)(e->end_hour * 60 + e->end_min);
                if (end_m <= start_m) end_m = start_m + 30.0f;

                float ey = grid_y0 + (start_m / 60.0f) * hour_h + scroll_y;
                float eh = ((end_m - start_m) / 60.0f) * hour_h;
                if (eh < 22.0f) eh = 22.0f;

                uint32_t p_col = s_priority_colors[e->priority & 3];
                rife_draw_round_rect(core, col_x + 3.0f, ey, col_w - 6.0f, eh, 4.0f, is_dark ? 0x1A1C28EE : 0xEEF2FFEE, p_col);
                rife_draw_rect(core, col_x + 3.0f, ey, 3.0f, eh, p_col);

                rife_draw_text_font(core, col_x + 9.0f, ey + 4.0f, e->title, is_dark ? 0xF8FAFCFF : 0x0F172AFF, 4);
                break;
            }
        }
    }

    // 当前时间激光指示红线
    float cur_total_m = (float)(state->cur_hour * 60 + state->cur_min);
    float now_y = grid_y0 + (cur_total_m / 60.0f) * hour_h + scroll_y;
    if (now_y >= grid_y0 && now_y <= cy + ch) {
        rife_draw_rect(core, cx + scale_w, now_y, cw - scale_w, 1.5f, 0xEF4444FF);
        rife_draw_circle(core, cx + scale_w, now_y + 0.75f, 3.5f, 0xEF4444FF, 0xEF4444FF);
    }

    rife_pop_scissor(core);
}

// -------------------------------------------------------------
// 插件生命周期：创建、更新、销毁与渲染 (Plugin Lifecycle)
// -------------------------------------------------------------

static void* calendar_create(RifeCore* core) {
    (void)core;
    CalendarState* state = (CalendarState*)malloc(sizeof(CalendarState));
    if (!state) return NULL;
    memset(state, 0, sizeof(CalendarState));

    sync_system_clock(state);
    state->view_year = state->cur_year;
    state->view_month = state->cur_month;
    state->view_day = state->cur_day;

    state->view_mode = CAL_VIEW_TASKS; // 默认展开滴答三栏流
    state->active_list_idx = 1;        // 默认进入 "今天"
    state->task_filter = 1;            // 默认展示 "未完成"
    state->time_scale = 60.0f;
    state->scroll_y = -((float)state->cur_hour * 60.0f - 120.0f);
    if (state->scroll_y > 0.0f) state->scroll_y = 0.0f;

    load_storage_to_state(state);
    return state;
}

static void calendar_destroy(void* inst) {
    CalendarState* state = (CalendarState*)inst;
    if (state) {
        save_state_to_storage(state);
        free(state);
    }
}

static void calendar_update(void* inst, RifeCore* core, const RifeInput* input, float client_w, float client_h) {
    CalendarState* state = (CalendarState*)inst;
    if (!state || !input) return;
    (void)core;

    sync_system_clock(state);
    state->cursor_blink_t += 0.05f;

    float mx = input->mouse_x;
    float my = input->mouse_y;

    // 1. 鼠标滚轮滚动
    if (fabsf(input->scroll_delta) > 0.01f) {
        if (state->view_mode == CAL_VIEW_TASKS) {
            state->scroll_tasks += input->scroll_delta * 30.0f;
            if (state->scroll_tasks > 0.0f) state->scroll_tasks = 0.0f;
        } else if (state->view_mode >= CAL_VIEW_WEEK) {
            state->scroll_y += input->scroll_delta * 40.0f;
            if (state->scroll_y > 0.0f) state->scroll_y = 0.0f;
        }
        rife_request_redraw(core);
    }

    // 2. 键盘字符键入处理 (Text Input Handling)
    if (input->text_input[0] != '\0' && state->active_field > 0) {
        if (state->active_field == 1) { // 中栏极速添加框
            size_t cl = strlen(state->quick_add_title);
            size_t al = strlen(input->text_input);
            if (cl + al < sizeof(state->quick_add_title) - 1) {
                strcat(state->quick_add_title, input->text_input);
                rife_request_redraw(core);
            }
        } else if (state->active_field == 2) { // Inspector 标题编辑
            size_t cl = strlen(state->inspector_title);
            size_t al = strlen(input->text_input);
            if (cl + al < sizeof(state->inspector_title) - 1) {
                strcat(state->inspector_title, input->text_input);
                CalendarEvent* cur_e = get_selected_event(state);
                if (cur_e) snprintf(cur_e->title, sizeof(cur_e->title), "%s", state->inspector_title);
                rife_request_redraw(core);
            }
        } else if (state->active_field == 3) { // Inspector 备注编辑
            size_t cl = strlen(state->inspector_desc);
            size_t al = strlen(input->text_input);
            if (cl + al < sizeof(state->inspector_desc) - 1) {
                strcat(state->inspector_desc, input->text_input);
                CalendarEvent* cur_e = get_selected_event(state);
                if (cur_e) snprintf(cur_e->desc, sizeof(cur_e->desc), "%s", state->inspector_desc);
                rife_request_redraw(core);
            }
        } else if (state->active_field == 4) { // 子任务输入
            size_t cl = strlen(state->inspector_subtask);
            size_t al = strlen(input->text_input);
            if (cl + al < sizeof(state->inspector_subtask) - 1) {
                strcat(state->inspector_subtask, input->text_input);
                rife_request_redraw(core);
            }
        }
    }

    // 3. 键盘按键逻辑 (Backspace / Enter / Esc)
    if (input->key_pressed[VK_BACK] && state->active_field > 0) {
        if (state->active_field == 1) utf8_pop_back(state->quick_add_title);
        else if (state->active_field == 2) {
            utf8_pop_back(state->inspector_title);
            CalendarEvent* cur_e = get_selected_event(state);
            if (cur_e) snprintf(cur_e->title, sizeof(cur_e->title), "%s", state->inspector_title);
        }
        else if (state->active_field == 3) {
            utf8_pop_back(state->inspector_desc);
            CalendarEvent* cur_e = get_selected_event(state);
            if (cur_e) snprintf(cur_e->desc, sizeof(cur_e->desc), "%s", state->inspector_desc);
        }
        else if (state->active_field == 4) utf8_pop_back(state->inspector_subtask);
        rife_request_redraw(core);
    }

    if (input->key_pressed[VK_RETURN]) {
        if (state->active_field == 1 && state->quick_add_title[0] != '\0') {
            // 回车提交极速添加
            if (state->event_count < CAL_MAX_EVENTS) {
                CalendarEvent* e = &state->events[state->event_count++];
                memset(e, 0, sizeof(CalendarEvent));
                e->id = 2000 + (uint32_t)state->event_count;
                snprintf(e->title, sizeof(e->title), "%s", state->quick_add_title);
                e->priority = state->quick_add_priority;
                e->tag_idx = -1;

                if (state->active_list_idx == 0) {
                    e->list_idx = 0; // 收集箱
                } else if (state->active_list_idx == 1) { // 今天
                    e->list_idx = 1;
                    e->has_date = true;
                    e->year = state->cur_year; e->month = state->cur_month; e->day = state->cur_day;
                } else if (state->active_list_idx == 2) { // 明天
                    e->list_idx = 1;
                    e->has_date = true;
                    add_days(state->cur_year, state->cur_month, state->cur_day, 1, &e->year, &e->month, &e->day);
                } else if (state->active_list_idx >= 6) {
                    e->list_idx = state->active_list_idx - 5;
                }

                // 默认四象限归属
                if (e->priority == TICK_PRIORITY_HIGH) e->quadrant = 1;
                else if (e->priority == TICK_PRIORITY_MEDIUM) e->quadrant = 2;
                else if (e->priority == TICK_PRIORITY_LOW) e->quadrant = 3;
                else e->quadrant = 4;

                state->selected_event_id = e->id;
                snprintf(state->inspector_title, sizeof(state->inspector_title), "%s", e->title);
                state->inspector_desc[0] = '\0';
                save_state_to_storage(state);
            }
            state->quick_add_title[0] = '\0';
            rife_request_redraw(core);
        } else if (state->active_field == 4 && state->inspector_subtask[0] != '\0') {
            // 回车添加子任务
            CalendarEvent* cur_e = get_selected_event(state);
            if (cur_e && cur_e->subtask_count < TICK_MAX_SUBTASKS) {
                TickSubtask* s = &cur_e->subtasks[cur_e->subtask_count++];
                snprintf(s->title, sizeof(s->title), "%s", state->inspector_subtask);
                s->is_done = false;
                state->inspector_subtask[0] = '\0';
                save_state_to_storage(state);
                rife_request_redraw(core);
            }
        } else if (state->active_field == 2 || state->active_field == 3) {
            state->active_field = 0;
            save_state_to_storage(state);
            rife_request_redraw(core);
        }
    }

    if (input->key_pressed[VK_ESCAPE]) {
        state->active_field = 0;
        rife_request_redraw(core);
    }

    // 4. 鼠标点击响应 (Click Routing)
    if (input->mouse_pressed[0]) {
        if (state->view_mode == CAL_VIEW_TASKS) {
            float col1_w = 180.0f;
            float col3_w = (state->selected_event_id > 0) ? 340.0f : 0.0f;
            float col2_w = client_w - col1_w - col3_w;

            // A. 左栏清单点击
            if (mx >= 0.0f && mx <= col1_w) {
                float list_y = 38.0f;
                for (int i = 0; i < 6; i++) {
                    if (my >= list_y && my <= list_y + 32.0f) {
                        state->active_list_idx = i;
                        state->active_field = 0;
                        save_state_to_storage(state);
                        rife_request_redraw(core);
                        return;
                    }
                    list_y += 34.0f;
                }
                list_y += 44.0f;
                for (int c = 1; c < state->list_count && c < 4; c++) {
                    if (my >= list_y && my <= list_y + 32.0f) {
                        state->active_list_idx = 5 + c;
                        state->active_field = 0;
                        save_state_to_storage(state);
                        rife_request_redraw(core);
                        return;
                    }
                    list_y += 34.0f;
                }
            }

            // B. 中栏点击
            if (mx > col1_w && mx <= col1_w + col2_w) {
                float col2_x = col1_w;

                // 过滤器切换
                float f_w = 54.0f, f_h = 24.0f;
                (void)f_h;
                float f_x0 = col2_x + col2_w - (f_w * 3.0f + 24.0f);
                if (my >= 14.0f && my <= 38.0f) {
                    for (int f = 0; f < 3; f++) {
                        float fx = f_x0 + (float)f * f_w;
                        if (mx >= fx && mx <= fx + f_w) {
                            state->task_filter = (f == 0 ? 1 : (f == 1 ? 0 : 2));
                            rife_request_redraw(core);
                            return;
                        }
                    }
                }

                // 极速添加输入框
                float q_y = 50.0f;
                float q_w = col2_w - 40.0f;
                float q_x = col2_x + 20.0f;
                float q_h = 40.0f;
                if (mx >= q_x && mx <= q_x + q_w && my >= q_y && my <= q_y + q_h) {
                    // 右侧优先级切换旗帜
                    if (mx >= q_x + q_w - 36.0f) {
                        state->quick_add_priority = (state->quick_add_priority + 1) % 4;
                    } else {
                        state->active_field = 1;
                    }
                    rife_request_redraw(core);
                    return;
                }

                // 任务卡片点击
                float card_stream_y = q_y + q_h + 12.0f;
                float card_h = 52.0f;
                float card_gap = 6.0f;
                float curr_y = card_stream_y + state->scroll_tasks;

                for (int i = 0; i < state->event_count; i++) {
                    CalendarEvent* e = &state->events[i];
                    if (!is_task_in_list(state, e, state->active_list_idx)) continue;
                    if (state->task_filter == 1 && e->is_completed) continue;
                    if (state->task_filter == 2 && !e->is_completed) continue;

                    if (my >= curr_y && my <= curr_y + card_h && mx >= q_x && mx <= q_x + q_w) {
                        // 检查是否点在复选框圆圈内
                        float chk_cx = q_x + 22.0f;
                        float chk_cy = curr_y + card_h * 0.5f;
                        float dx = mx - chk_cx, dy = my - chk_cy;
                        if (dx * dx + dy * dy <= 16.0f * 16.0f) {
                            e->is_completed = !e->is_completed;
                            save_state_to_storage(state);
                            rife_request_redraw(core);
                            return;
                        }

                        // 否则选中任务打开 Inspector
                        state->selected_event_id = e->id;
                        snprintf(state->inspector_title, sizeof(state->inspector_title), "%s", e->title);
                        snprintf(state->inspector_desc, sizeof(state->inspector_desc), "%s", e->desc);
                        state->active_field = 0;
                        rife_request_redraw(core);
                        return;
                    }
                    curr_y += (card_h + card_gap);
                }
            }

            // C. 右栏 Inspector 点击
            if (col3_w > 0.0f && mx > col1_w + col2_w) {
                float col3_x = col1_w + col2_w;
                float ins_x = col3_x + 18.0f;
                float ins_w = col3_w - 36.0f;

                CalendarEvent* cur_e = get_selected_event(state);
                if (cur_e) {
                    // 关闭按钮 [×]
                    float close_x = col3_x + col3_w - 34.0f;
                    if (mx >= close_x && mx <= close_x + 24.0f && my >= 16.0f && my <= 40.0f) {
                        state->selected_event_id = 0;
                        state->active_field = 0;
                        rife_request_redraw(core);
                        return;
                    }

                    // 完成复选框
                    if (mx >= ins_x && mx <= ins_x + 24.0f && my >= 16.0f && my <= 44.0f) {
                        cur_e->is_completed = !cur_e->is_completed;
                        save_state_to_storage(state);
                        rife_request_redraw(core);
                        return;
                    }

                    // 标题编辑框
                    if (mx >= ins_x + 30.0f && mx <= ins_x + ins_w - 40.0f && my >= 16.0f && my <= 48.0f) {
                        state->active_field = 2;
                        rife_request_redraw(core);
                        return;
                    }

                    // 截止日期快捷项
                    float pill_w = 66.0f;
                    float d_y = 16.0f + 64.0f;
                    if (my >= d_y && my <= d_y + 26.0f) {
                        for (int p = 0; p < 4; p++) {
                            float px = ins_x + (float)p * (pill_w + 6.0f);
                            if (mx >= px && mx <= px + pill_w) {
                                if (p == 0) { // 今天
                                    cur_e->has_date = true;
                                    cur_e->year = state->cur_year; cur_e->month = state->cur_month; cur_e->day = state->cur_day;
                                } else if (p == 1) { // 明天
                                    cur_e->has_date = true;
                                    add_days(state->cur_year, state->cur_month, state->cur_day, 1, &cur_e->year, &cur_e->month, &cur_e->day);
                                } else if (p == 2) { // 下周
                                    cur_e->has_date = true;
                                    add_days(state->cur_year, state->cur_month, state->cur_day, 7, &cur_e->year, &cur_e->month, &cur_e->day);
                                } else { // 清除
                                    cur_e->has_date = false;
                                }
                                save_state_to_storage(state);
                                rife_request_redraw(core);
                                return;
                            }
                        }
                    }

                    // 优先级旗帜
                    float pri_y = d_y + 56.0f;
                    if (my >= pri_y && my <= pri_y + 26.0f) {
                        for (int p = 0; p < 4; p++) {
                            float px = ins_x + (float)p * (pill_w + 6.0f);
                            if (mx >= px && mx <= px + pill_w) {
                                cur_e->priority = p;
                                // 联动象限
                                if (p == 3) cur_e->quadrant = 1;
                                else if (p == 2) cur_e->quadrant = 2;
                                else if (p == 1) cur_e->quadrant = 3;
                                else cur_e->quadrant = 4;
                                save_state_to_storage(state);
                                rife_request_redraw(core);
                                return;
                            }
                        }
                    }

                    // 所属清单选择
                    float list_p_y = pri_y + 56.0f;
                    if (my >= list_p_y && my <= list_p_y + 26.0f) {
                        for (int l = 0; l < state->list_count && l < 4; l++) {
                            float lx = ins_x + (float)l * (pill_w + 6.0f);
                            if (mx >= lx && mx <= lx + pill_w) {
                                cur_e->list_idx = l;
                                save_state_to_storage(state);
                                rife_request_redraw(core);
                                return;
                            }
                        }
                    }

                    // 子任务项点击 (完成或删除)
                    float sub_y = list_p_y + 60.0f;
                    for (int s = 0; s < cur_e->subtask_count && s < TICK_MAX_SUBTASKS; s++) {
                        if (my >= sub_y && my <= sub_y + 28.0f) {
                            if (mx >= ins_x + ins_w - 28.0f) {
                                // 删除该子任务
                                for (int k = s; k < cur_e->subtask_count - 1; k++) {
                                    cur_e->subtasks[k] = cur_e->subtasks[k + 1];
                                }
                                cur_e->subtask_count--;
                            } else {
                                // 切换完成
                                cur_e->subtasks[s].is_done = !cur_e->subtasks[s].is_done;
                            }
                            save_state_to_storage(state);
                            rife_request_redraw(core);
                            return;
                        }
                        sub_y += 32.0f;
                    }

                    // 添加子任务输入框点击
                    if (cur_e->subtask_count < TICK_MAX_SUBTASKS && my >= sub_y && my <= sub_y + 28.0f) {
                        state->active_field = 4;
                        rife_request_redraw(core);
                        return;
                    }

                    // 详细备注输入框点击
                    float desc_y = sub_y + 58.0f;
                    if (my >= desc_y && my <= desc_y + 70.0f) {
                        state->active_field = 3;
                        rife_request_redraw(core);
                        return;
                    }

                    // 底部操作栏：开始番茄专注 与 删除
                    float act_y = client_h - 54.0f;
                    float pomo_w = ins_w - 90.0f;
                    if (my >= act_y && my <= act_y + 36.0f) {
                        if (mx >= ins_x && mx <= ins_x + pomo_w) {
                            // 🍅 一键联动启动番茄专注
                            state->active_focus_task_id = cur_e->id;
                            snprintf(state->active_focus_title, sizeof(state->active_focus_title), "%s", cur_e->title);
                            save_state_to_storage(state);
                            rife_open_app_by_id("clock");
                            return;
                        }
                        if (mx >= ins_x + pomo_w + 10.0f && mx <= ins_x + ins_w) {
                            // 🗑️ 删除待办事项
                            for (int k = 0; k < state->event_count; k++) {
                                if (state->events[k].id == cur_e->id) {
                                    for (int j = k; j < state->event_count - 1; j++) {
                                        state->events[j] = state->events[j + 1];
                                    }
                                    state->event_count--;
                                    break;
                                }
                            }
                            state->selected_event_id = 0;
                            save_state_to_storage(state);
                            rife_request_redraw(core);
                            return;
                        }
                    }
                }
            }
        } else if (state->view_mode == CAL_VIEW_MATRIX) {
            // 四象限交互
            float pad = 16.0f, gap = 12.0f;
            float qw = (client_w - pad * 2.0f - gap) * 0.5f;
            float qh = (client_h - pad * 2.0f - gap) * 0.5f;

            for (int q = 0; q < 4; q++) {
                float qx = pad + (float)(q % 2) * (qw + gap);
                float qy = pad + (float)(q / 2) * (qh + gap);

                // 右上角 [+] 快速添加
                float add_btn_x = qx + qw - 36.0f;
                float add_btn_y = qy + 12.0f;
                if (mx >= add_btn_x && mx <= add_btn_x + 24.0f && my >= add_btn_y && my <= add_btn_y + 24.0f) {
                    if (state->event_count < CAL_MAX_EVENTS) {
                        CalendarEvent* e = &state->events[state->event_count++];
                        memset(e, 0, sizeof(CalendarEvent));
                        e->id = 3000 + (uint32_t)state->event_count;
                        snprintf(e->title, sizeof(e->title), "象限 %d 攻坚新待办", q + 1);
                        e->quadrant = q + 1;
                        e->priority = (q == 0) ? TICK_PRIORITY_HIGH : ((q == 1) ? TICK_PRIORITY_MEDIUM : ((q == 2) ? TICK_PRIORITY_LOW : TICK_PRIORITY_NONE));
                        state->selected_event_id = e->id;
                        state->view_mode = CAL_VIEW_TASKS;
                        snprintf(state->inspector_title, sizeof(state->inspector_title), "%s", e->title);
                        save_state_to_storage(state);
                        rife_request_redraw(core);
                        return;
                    }
                }

                // 任务卡片点击 (完成勾选或选中)
                float task_y = qy + 54.0f;
                int q_target = q + 1;
                for (int i = 0; i < state->event_count; i++) {
                    CalendarEvent* e = &state->events[i];
                    if (e->quadrant != q_target && !(e->quadrant == 0 && (3 - e->priority + 1) == q_target)) continue;

                    float t_h = 36.0f;
                    if (my >= task_y && my <= task_y + t_h && mx >= qx + 12.0f && mx <= qx + qw - 12.0f) {
                        float chk_cx = qx + 24.0f;
                        float chk_cy = task_y + t_h * 0.5f;
                        float dx = mx - chk_cx, dy = my - chk_cy;
                        if (dx * dx + dy * dy <= 14.0f * 14.0f) {
                            e->is_completed = !e->is_completed;
                        } else {
                            state->selected_event_id = e->id;
                            state->view_mode = CAL_VIEW_TASKS;
                            snprintf(state->inspector_title, sizeof(state->inspector_title), "%s", e->title);
                        }
                        save_state_to_storage(state);
                        rife_request_redraw(core);
                        return;
                    }
                    task_y += (t_h + 6.0f);
                }
            }
        }
    }
}

static void calendar_render(void* inst, RifeCore* core, float cx, float cy, float cw, float ch) {
    CalendarState* state = (CalendarState*)inst;
    if (!state || !core) return;

    if (state->view_mode == CAL_VIEW_TASKS) {
        render_tasks_three_column(state, core, cx, cy, cw, ch);
    } else if (state->view_mode == CAL_VIEW_MATRIX) {
        render_matrix_quadrants(state, core, cx, cy, cw, ch);
    } else {
        render_calendar_timeline(state, core, cx, cy, cw, ch);
    }
}

// -------------------------------------------------------------
// 插件应用注册导出 (Plugin App Export)
// -------------------------------------------------------------

const RifePluginApp g_calendar_plugin_app = {
    .app_id = 2002,
    .id = "rtodo",
    .name_zh = "滴答清单",
    .name_en = "TickTick",
    .glyph = "Tk",
    .color_top = 0x6366F1FF, // Electric Indigo
    .color_bot = 0x4338CAFF,
    .default_w = 1100.0f,
    .default_h = 700.0f,
    .pin_to_dock = true,
    .create = calendar_create,
    .destroy = calendar_destroy,
    .update = calendar_update,
    .render = calendar_render
};

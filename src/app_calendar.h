#pragma once
#ifndef APP_CALENDAR_H
#define APP_CALENDAR_H

#include "rife_core.h"
#include "rife_app_api.h"

#define CAL_MAX_CUSTOM_TAGS 8
#define TICK_MAX_LISTS      8
#define TICK_MAX_SUBTASKS   8
#define CAL_MAX_EVENTS      128

typedef enum {
    TICK_PRIORITY_NONE = 0,   // 灰色 (无优先级)
    TICK_PRIORITY_LOW = 1,    // 蓝色 (低优先级)
    TICK_PRIORITY_MEDIUM = 2, // 黄色 (中优先级)
    TICK_PRIORITY_HIGH = 3    // 红色 (高优先级)
} TickPriority;

typedef struct {
    char title[48];
    bool is_done;
} TickSubtask;

typedef struct {
    char name[24];          // 标签名（如“重要”、“紧急”、“学习”等）
    uint32_t color_bar;     // 强调色 (例如 0x3370FFFF)
    uint32_t color_bg;      // 背景微透底色
    uint32_t color_border;  // 边框色
    uint32_t color_text;    // 字体颜色
    bool is_enabled;        // 过滤开关
} CustomTag;

typedef struct {
    char name[32];          // 清单名（如“收集箱”、“工作”、“生活”、“学习”）
    uint32_t color;         // 清单主题色
    int icon_id;            // 图标 ID
} TickList;

typedef enum {
    CAL_VIEW_TASKS = 0,    // ✅ 滴答三栏待办工作台
    CAL_VIEW_MATRIX,       // 📊 艾森豪威尔四象限看板
    CAL_VIEW_WEEK,         // 📅 周时间网格
    CAL_VIEW_DAY,          // 📅 日时间网格
    CAL_VIEW_MONTH,        // 📅 月视图
    CAL_VIEW_AGENDA        // 日程列表
} CalendarViewMode;

typedef struct {
    uint32_t id;
    char title[64];
    char location[32];
    char desc[128];
    int tag_idx;            // 关联标签 (-1 为无)
    int list_idx;           // 关联清单 (0: 收集箱, 1: 工作, 2: 生活, 3: 学习)
    int priority;           // 0: 无, 1: 低, 2: 中, 3: 高
    int quadrant;           // 1: 重要紧急, 2: 重要不紧急, 3: 紧急不重要, 4: 不重要不紧急
    
    bool has_date;
    int year;
    int month;   // 1 - 12
    int day;     // 1 - 31
    bool has_time;
    int start_hour; // 0 - 23
    int start_min;  // 0 - 59
    int end_hour;
    int end_min;

    bool is_completed;
    int pomodoro_count;    // 已累计专注番茄数

    int subtask_count;
    TickSubtask subtasks[TICK_MAX_SUBTASKS];
} CalendarEvent;

#define RTODO_MAGIC 0x5449434B // "TICK"
#define RTODO_VERSION 3

typedef struct {
    uint32_t magic;
    uint32_t version;
    int tag_count;
    CustomTag tags[CAL_MAX_CUSTOM_TAGS];
    int list_count;
    TickList lists[TICK_MAX_LISTS];
    int event_count;
    CalendarEvent events[CAL_MAX_EVENTS];

    // 全局番茄与专注统计
    int total_pomodoros;
    int total_focus_mins;
    uint32_t active_focus_task_id;
    char active_focus_title[64];
} RtodoStorage;

void rtodo_get_storage_path(char* out_path, size_t max_len);
bool rtodo_load_storage(RtodoStorage* out_storage);
bool rtodo_save_storage(const RtodoStorage* in_storage);
void rtodo_open_create_modal(void* inst);
void rtodo_set_active_view(void* inst, int view_idx);
uint32_t rtodo_get_active_focus_task(void* inst, char* out_title, size_t max_title);
void rtodo_add_pomodoro_to_active_task(void* inst);

extern const RifePluginApp g_calendar_plugin_app;

#endif

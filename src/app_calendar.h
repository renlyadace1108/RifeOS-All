#pragma once
#ifndef APP_CALENDAR_H
#define APP_CALENDAR_H

#include "rife_core.h"
#include "rife_app_api.h"

#define CAL_MAX_CUSTOM_TAGS 8

typedef struct {
    char name[24];          // 用户自定义标签名（如“工作”、“生活”、“项目A”等）
    uint32_t color_bar;     // 标签主强调色 (例如 0x3370FFFF)
    uint32_t color_bg;      // 背景微透底色
    uint32_t color_border;  // 边框色
    uint32_t color_text;    // 字体颜色
    bool is_enabled;        // 侧边栏过滤开关
} CustomTag;

typedef enum {
    CAL_VIEW_WEEK = 0,     // 周视图 (默认)
    CAL_VIEW_DAY,          // 日视图
    CAL_VIEW_MONTH,        // 月视图
    CAL_VIEW_AGENDA        // 日程列表
} CalendarViewMode;

#define CAL_MAX_EVENTS 64

typedef struct {
    uint32_t id;
    char title[48];
    char location[32];
    char desc[64];
    int tag_idx;            // 关联的自定义标签索引
    int year;
    int month;   // 1 - 12
    int day;     // 1 - 31
    int start_hour; // 0 - 23
    int start_min;  // 0 - 59
    int end_hour;
    int end_min;
    bool is_completed;
} CalendarEvent;

#define RTODO_MAGIC 0x544F444F // "TODO"
#define RTODO_VERSION 2

typedef struct {
    uint32_t magic;
    uint32_t version;
    int tag_count;
    CustomTag tags[CAL_MAX_CUSTOM_TAGS];
    int event_count;
    CalendarEvent events[CAL_MAX_EVENTS];
} RtodoStorage;

void rtodo_get_storage_path(char* out_path, size_t max_len);
bool rtodo_load_storage(RtodoStorage* out_storage);
bool rtodo_save_storage(const RtodoStorage* in_storage);

extern const RifePluginApp g_calendar_plugin_app;

#endif


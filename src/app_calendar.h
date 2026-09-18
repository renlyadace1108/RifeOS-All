#pragma once
#ifndef APP_CALENDAR_H
#define APP_CALENDAR_H

#include "rife_core.h"
#include "rife_app_api.h"

typedef enum {
    CAL_CAT_WORK = 0,      // 工作协同 (Feishu Blue)
    CAL_CAT_REVIEW,        // 架构评审 (Violet)
    CAL_CAT_PERSONAL,      // 个人聚焦 (Emerald)
    CAL_CAT_MILESTONE,     // 关键里程碑 (Sunset Amber)
    CAL_CAT_COUNT
} CalendarCategory;

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
    CalendarCategory category;
    int year;
    int month;   // 1 - 12
    int day;     // 1 - 31
    int start_hour; // 0 - 23
    int start_min;  // 0 - 59
    int end_hour;
    int end_min;
    bool is_completed;
} CalendarEvent;

extern const RifePluginApp g_calendar_plugin_app;

#endif

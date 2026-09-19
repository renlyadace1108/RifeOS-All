#pragma once
#ifndef APP_MANIFEST_H
#define APP_MANIFEST_H

#include "rife_app_api.h"

extern const RifePluginApp* g_installed_apps[];
extern const size_t g_installed_app_count;

RifeSystemConfig* rife_get_system_config(void);
void rife_save_system_config(void);
void rife_load_system_config(void);
void rife_open_app_by_id(const char* app_id);

#endif
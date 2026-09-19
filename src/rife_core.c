#define _CRT_SECURE_NO_WARNINGS
#include "rife_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
uint64_t rife_time_now_ns(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (uint64_t)(((double)counter.QuadPart / (double)freq.QuadPart) * 1000000000.0);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}
void rife_sleep_ns(uint64_t ns) {
#ifdef _WIN32
    DWORD ms = (DWORD)(ns / 1000000ULL);
    if (ms > 0) {
        Sleep(ms);
    }
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(ns / 1000000000ULL);
    ts.tv_nsec = (long)(ns % 1000000000ULL);
    nanosleep(&ts, NULL);
#endif
}
Arena arena_create(size_t capacity) {
    Arena arena;
    arena.capacity = capacity;
    arena.offset = 0;
    arena.buffer = (uint8_t*)malloc(capacity);
    return arena;
}
void* arena_alloc(Arena* arena, size_t size) {
    if (!arena || !arena->buffer || size == 0) {
        return NULL;
    }
    size_t aligned = (size + 7) & ~7;
    if (aligned > arena->capacity - arena->offset) {
        return NULL;
    }
    void* ptr = &arena->buffer[arena->offset];
    arena->offset += aligned;
    return ptr;
}
void arena_reset(Arena* arena) {
    if (arena) {
        arena->offset = 0;
    }
}
void arena_destroy(Arena* arena) {
    if (arena && arena->buffer) {
        free(arena->buffer);
        arena->buffer = NULL;
        arena->capacity = 0;
        arena->offset = 0;
    }
}
uint32_t rife_calc_checksum(const void* data, size_t len) {
    if (!data || len == 0) {
        return 0;
    }
    const uint8_t* p = (const uint8_t*)data;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        hash ^= p[i];
        hash *= 16777619u;
    }
    return hash;
}
float rife_measure_text_width(const char* text, float char_width) {
    if (!text) {
        return 0.0f;
    }
    float width = 0.0f;
    const unsigned char* p = (const unsigned char*)text;
    while (*p) {
        if (*p < 0x80) {
            width += char_width;
            p++;
        }
        else if ((*p & 0xE0) == 0xC0) {
            width += char_width * 2.0f;
            p += (p[1] != '\0') ? 2 : 1;
        }
        else if ((*p & 0xF0) == 0xE0) {
            width += char_width * 2.0f;
            size_t step = 1;
            if (p[1] != '\0') {
                step++;
                if (p[2] != '\0') {
                    step++;
                }
            }
            p += step;
        }
        else {
            width += char_width * 2.0f;
            size_t step = 1;
            if (p[1] != '\0') {
                step++;
                if (p[2] != '\0') {
                    step++;
                    if (p[3] != '\0') {
                        step++;
                    }
                }
            }
            p += step;
        }
    }
    return width;
}
void rife_request_redraw(RifeCore* core) {
    if (core) {
        core->needs_redraw = true;
    }
}
void rife_set_active_app(RifeCore* core, uint16_t app_id) {
    if (core) {
        core->active_app_id = app_id;
        rife_request_redraw(core);
    }
}
bool rife_storage_append_record(uint16_t app_id, uint32_t data_type, const void* payload, uint32_t payload_size) {
    char filename[64];
    snprintf(filename, sizeof(filename), "app_%04u.bin", app_id);
    FILE* fp = fopen(filename, "ab");
    if (!fp) {
        return false;
    }
    RifeRecordHeader header;
    header.app_id = app_id;
    header.reserved = 0;
    header.data_type = data_type;
    header.total_size = (uint32_t)(sizeof(RifeRecordHeader) + payload_size);
    header.checksum = rife_calc_checksum(payload, payload_size);
    if (fwrite(&header, sizeof(RifeRecordHeader), 1, fp) != 1) {
        fclose(fp);
        return false;
    }
    if (payload_size > 0 && fwrite(payload, payload_size, 1, fp) != 1) {
        fclose(fp);
        return false;
    }
    fclose(fp);
    return true;
}
bool rife_storage_scan_records(uint16_t app_id, Arena* scratch, RifeRecordIterFn callback, void* user_data) {
    if (!scratch) {
        return false;
    }
    char filename[64];
    snprintf(filename, sizeof(filename), "app_%04u.bin", app_id);
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        return false;
    }
    RifeRecordHeader header;
    while (fread(&header, sizeof(RifeRecordHeader), 1, fp) == 1) {
        if (header.app_id != app_id || header.total_size < sizeof(RifeRecordHeader)) {
            fclose(fp);
            return false;
        }
        uint32_t payload_size = header.total_size - (uint32_t)sizeof(RifeRecordHeader);
        void* payload = NULL;
        size_t scratch_mark = scratch->offset;
        if (payload_size > 0) {
            payload = arena_alloc(scratch, payload_size);
            if (!payload) {
                fclose(fp);
                return false;
            }
            if (fread(payload, payload_size, 1, fp) != 1) {
                scratch->offset = scratch_mark;
                fclose(fp);
                return false;
            }
            if (header.checksum != rife_calc_checksum(payload, payload_size)) {
                scratch->offset = scratch_mark;
                fclose(fp);
                return false;
            }
        }
        if (callback) {
            callback(&header, payload, user_data);
        }
        scratch->offset = scratch_mark;
    }
    fclose(fp);
    return true;
}
bool rife_storage_compact(uint16_t app_id, const uint32_t* data_types, const void** payloads, const uint32_t* payload_sizes, size_t count) {
    char filename[64];
    char tempname[64];
    snprintf(filename, sizeof(filename), "app_%04u.bin", app_id);
    snprintf(tempname, sizeof(tempname), "app_%04u.bin.tmp", app_id);
    FILE* fp = fopen(tempname, "wb");
    if (!fp) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        RifeRecordHeader header;
        header.app_id = app_id;
        header.reserved = 0;
        header.data_type = data_types[i];
        header.total_size = (uint32_t)(sizeof(RifeRecordHeader) + payload_sizes[i]);
        header.checksum = rife_calc_checksum(payloads[i], payload_sizes[i]);
        if (fwrite(&header, sizeof(RifeRecordHeader), 1, fp) != 1) {
            fclose(fp);
            remove(tempname);
            return false;
        }
        if (payload_sizes[i] > 0 && fwrite(payloads[i], payload_sizes[i], 1, fp) != 1) {
            fclose(fp);
            remove(tempname);
            return false;
        }
    }
    fclose(fp);
    remove(filename);
    return rename(tempname, filename) == 0;
}
RenderCmd* rife_cmd_push(RifeCore* core, RenderCmdType type) {
    RenderCmd* cmd = (RenderCmd*)arena_alloc(&core->frame_arena, sizeof(RenderCmd));
    if (!cmd) {
        return NULL;
    }
    cmd->type = type;
    cmd->next = NULL;
    if (!core->render_head) {
        core->render_head = cmd;
        core->render_tail = cmd;
    }
    else {
        core->render_tail->next = cmd;
        core->render_tail = cmd;
    }
    core->render_cmd_count++;
    return cmd;
}
void rife_draw_rect(RifeCore* core, float x, float y, float w, float h, uint32_t color) {
    RenderCmd* cmd = rife_cmd_push(core, CMD_RECT);
    if (!cmd) {
        return;
    }
    cmd->x = x;
    cmd->y = y;
    cmd->w = w;
    cmd->h = h;
    cmd->radius = 0.0f;
    cmd->color = color;
    cmd->border_color = color;
    cmd->font_id = 0;
    cmd->text[0] = '\0';
}
void rife_draw_round_rect(RifeCore* core, float x, float y, float w, float h, float radius, uint32_t bg_color, uint32_t border_color) {
    RenderCmd* cmd = rife_cmd_push(core, CMD_ROUND_RECT);
    if (!cmd) {
        return;
    }
    cmd->x = x;
    cmd->y = y;
    cmd->w = w;
    cmd->h = h;
    cmd->radius = radius;
    cmd->color = bg_color;
    cmd->border_color = border_color;
    cmd->font_id = 0;
    cmd->text[0] = '\0';
}
void rife_draw_text_font(RifeCore* core, float x, float y, const char* text, uint32_t color, uint8_t font_id) {
    if (!text) {
        return;
    }
    RenderCmd* cmd = rife_cmd_push(core, CMD_TEXT);
    if (!cmd) {
        return;
    }
    cmd->x = x;
    cmd->y = y;
    cmd->w = rife_measure_text_width(text, 8.0f);
    cmd->h = 16.0f;
    cmd->radius = 0.0f;
    cmd->color = color;
    cmd->border_color = 0;
    cmd->font_id = font_id;
    strncpy(cmd->text, text, sizeof(cmd->text) - 1);
    cmd->text[sizeof(cmd->text) - 1] = '\0';
}
void rife_draw_text(RifeCore* core, float x, float y, const char* text, uint32_t color) {
    rife_draw_text_font(core, x, y, text, color, 0);
}
void rife_push_scissor(RifeCore* core, float x, float y, float w, float h) {
    RenderCmd* cmd = rife_cmd_push(core, CMD_SCISSOR_PUSH);
    if (!cmd) {
        return;
    }
    cmd->x = x;
    cmd->y = y;
    cmd->w = w;
    cmd->h = h;
    cmd->radius = 0.0f;
}
void rife_push_scissor_round(RifeCore* core, float x, float y, float w, float h, float radius) {
    RenderCmd* cmd = rife_cmd_push(core, CMD_SCISSOR_PUSH);
    if (!cmd) {
        return;
    }
    cmd->x = x;
    cmd->y = y;
    cmd->w = w;
    cmd->h = h;
    cmd->radius = radius;
}
void rife_pop_scissor(RifeCore* core) {
    rife_cmd_push(core, CMD_SCISSOR_POP);
}
bool rife_emit_event(RifeCore* core, uint32_t type, uint16_t src, uint16_t dst, uint64_t payload) {
    if (!core || core->event_count >= RIFE_MAX_EVENTS) {
        return false;
    }
    RifeEvent* evt = &core->event_queue[core->event_count++];
    evt->type = type;
    evt->src_app = src;
    evt->dst_app = dst;
    evt->payload = payload;
    rife_request_redraw(core);
    return true;
}
void rife_dispatch_events(RifeCore* core) {
    if (!core || core->event_count == 0) {
        return;
    }
    RifeEvent batch[RIFE_MAX_EVENTS];
    size_t count = core->event_count;
    memcpy(batch, core->event_queue, sizeof(RifeEvent) * count);
    core->event_count = 0;
    for (size_t i = 0; i < count; i++) {
        RifeEvent* evt = &batch[i];
        for (size_t a = 0; a < core->app_count; a++) {
            if (evt->dst_app == 0 || evt->dst_app == core->apps[a].app_id) {
                if (core->apps[a].on_event) {
                    core->apps[a].on_event(&core->apps[a], evt);
                }
            }
        }
    }
}
bool rife_core_init(RifeCore* core, size_t persistent_size, size_t frame_size, uint32_t target_fps) {
    if (!core) {
        return false;
    }
    core->persistent_arena = arena_create(persistent_size);
    core->frame_arena = arena_create(frame_size);
    if (!core->persistent_arena.buffer || !core->frame_arena.buffer) {
        arena_destroy(&core->persistent_arena);
        arena_destroy(&core->frame_arena);
        return false;
    }
    core->app_count = 0;
    core->active_app_id = 0;
    core->event_count = 0;
    core->render_head = NULL;
    core->render_tail = NULL;
    core->render_cmd_count = 0;
    core->needs_redraw = true;
    core->running = true;
    core->platform_data = NULL;
    memset(&core->input, 0, sizeof(RifeInput));
    memset(core->prev_mouse_down, 0, sizeof(core->prev_mouse_down));
    memset(core->prev_key_down, 0, sizeof(core->prev_key_down));
    core->target_frame_ns = target_fps > 0 ? (1000000000ULL / target_fps) : 16666666ULL;
    core->last_tick_ns = rife_time_now_ns();
    return true;
}
bool rife_register_app(RifeCore* core, RifeApp app) {
    if (!core || core->app_count >= RIFE_MAX_APPS) {
        return false;
    }
    core->apps[core->app_count] = app;
    if (core->apps[core->app_count].init) {
        if (!core->apps[core->app_count].init(&core->apps[core->app_count], core)) {
            return false;
        }
    }
    if (core->app_count == 0) {
        core->active_app_id = app.app_id;
    }
    core->app_count++;
    rife_request_redraw(core);
    return true;
}
void rife_core_tick(RifeCore* core) {
    if (!core || !core->running) {
        return;
    }
    uint64_t now_ns = rife_time_now_ns();
    uint64_t dt_ns = now_ns - core->last_tick_ns;
    core->last_tick_ns = now_ns;
    for (int i = 0; i < 3; i++) {
        core->input.mouse_pressed[i] = (core->input.mouse_down[i] && !core->prev_mouse_down[i]) ? 1 : 0;
        core->input.mouse_released[i] = (!core->input.mouse_down[i] && core->prev_mouse_down[i]) ? 1 : 0;
        core->prev_mouse_down[i] = core->input.mouse_down[i];
    }
    for (int i = 0; i < 256; i++) {
        core->input.key_pressed[i] = (core->input.key_down[i] && !core->prev_key_down[i]) ? 1 : 0;
        core->input.key_released[i] = (!core->input.key_down[i] && core->prev_key_down[i]) ? 1 : 0;
        core->prev_key_down[i] = core->input.key_down[i];
    }
    arena_reset(&core->frame_arena);
    core->render_head = NULL;
    core->render_tail = NULL;
    core->render_cmd_count = 0;
    rife_dispatch_events(core);
    for (size_t i = 0; i < core->app_count; i++) {
        if (core->apps[i].update) {
            bool is_focused = (core->apps[i].app_id == core->active_app_id);
            core->apps[i].update(&core->apps[i], core, &core->input, is_focused, dt_ns);
        }
    }
    if (core->needs_redraw) {
        for (size_t i = 0; i < core->app_count; i++) {
            if (core->apps[i].is_visible && core->apps[i].render) {
                core->apps[i].render(&core->apps[i], core);
            }
        }
        rife_render_flush(core);
        core->needs_redraw = false;
    }
    core->input.text_input[0] = '\0';
    core->input.scroll_delta = 0.0f;
    uint64_t work_done_ns = rife_time_now_ns() - now_ns;
    if (work_done_ns < core->target_frame_ns) {
        rife_sleep_ns(core->target_frame_ns - work_done_ns);
    }
}
void rife_core_shutdown(RifeCore* core) {
    if (!core) {
        return;
    }
    for (size_t i = 0; i < core->app_count; i++) {
        if (core->apps[i].shutdown) {
            core->apps[i].shutdown(&core->apps[i], core);
        }
    }
    arena_destroy(&core->persistent_arena);
    arena_destroy(&core->frame_arena);
    core->app_count = 0;
    core->event_count = 0;
    core->render_head = NULL;
    core->render_tail = NULL;
    core->render_cmd_count = 0;
    core->running = false;
}
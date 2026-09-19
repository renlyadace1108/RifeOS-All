#ifndef RIFE_CORE_H
#define RIFE_CORE_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define RIFE_MAX_APPS 16
#define RIFE_MAX_EVENTS 64
typedef struct {
    uint8_t* buffer;
    size_t capacity;
    size_t offset;
} Arena;
Arena arena_create(size_t capacity);
void* arena_alloc(Arena* arena, size_t size);
void arena_reset(Arena* arena);
void arena_destroy(Arena* arena);
uint32_t rife_calc_checksum(const void* data, size_t len);
uint64_t rife_time_now_ns(void);
void rife_sleep_ns(uint64_t ns);
typedef struct {
    uint16_t app_id;
    uint16_t reserved;
    uint32_t data_type;
    uint32_t total_size;
    uint32_t checksum;
} RifeRecordHeader;
typedef struct {
    float mouse_x;
    float mouse_y;
    float scroll_delta;
    uint8_t mouse_down[3];
    uint8_t mouse_pressed[3];
    uint8_t mouse_released[3];
    uint8_t key_down[256];
    uint8_t key_pressed[256];
    uint8_t key_released[256];
    char text_input[128];
} RifeInput;
typedef enum {
    CMD_NONE = 0,
    CMD_RECT = 1,
    CMD_TEXT = 2,
    CMD_SCISSOR_PUSH = 3,
    CMD_SCISSOR_POP = 4,
    CMD_ROUND_RECT = 5,
    CMD_TEXT_RECT = 6
} RenderCmdType;
typedef struct RenderCmd {
    RenderCmdType type;
    float x;
    float y;
    float w;
    float h;
    float radius;
    uint32_t color;
    uint32_t border_color;
    uint8_t font_id;
    char text[64];
    struct RenderCmd* next;
} RenderCmd;
typedef struct {
    uint32_t type;
    uint16_t src_app;
    uint16_t dst_app;
    uint64_t payload;
} RifeEvent;
struct RifeCore;
typedef struct RifeApp {
    uint16_t app_id;
    char name[16];
    bool is_visible;
    bool (*init)(struct RifeApp* self, struct RifeCore* core);
    void (*update)(struct RifeApp* self, struct RifeCore* core, const RifeInput* input, bool is_focused, uint64_t dt_ns);
    void (*render)(struct RifeApp* self, struct RifeCore* core);
    void (*on_event)(struct RifeApp* self, const RifeEvent* event);
    void (*shutdown)(struct RifeApp* self, struct RifeCore* core);
    void* user_data;
} RifeApp;
typedef struct RifeCore {
    Arena persistent_arena;
    Arena frame_arena;
    RifeApp apps[RIFE_MAX_APPS];
    size_t app_count;
    uint16_t active_app_id;
    RifeEvent event_queue[RIFE_MAX_EVENTS];
    size_t event_count;
    RenderCmd* render_head;
    RenderCmd* render_tail;
    size_t render_cmd_count;
    RifeInput input;
    uint8_t prev_mouse_down[3];
    uint8_t prev_key_down[256];
    bool needs_redraw;
    bool running;
    uint64_t target_frame_ns;
    uint64_t last_tick_ns;
    void* platform_data;
} RifeCore;
float rife_measure_text_width(const char* text, float char_width);
void rife_request_redraw(RifeCore* core);
void rife_set_active_app(RifeCore* core, uint16_t app_id);
bool rife_storage_append_record(uint16_t app_id, uint32_t data_type, const void* payload, uint32_t payload_size);
typedef void (*RifeRecordIterFn)(const RifeRecordHeader* header, const void* payload, void* user_data);
bool rife_storage_scan_records(uint16_t app_id, Arena* scratch, RifeRecordIterFn callback, void* user_data);
bool rife_storage_compact(uint16_t app_id, const uint32_t* data_types, const void** payloads, const uint32_t* payload_sizes, size_t count);
RenderCmd* rife_cmd_push(RifeCore* core, RenderCmdType type);
void rife_draw_rect(RifeCore* core, float x, float y, float w, float h, uint32_t color);
void rife_draw_round_rect(RifeCore* core, float x, float y, float w, float h, float radius, uint32_t bg_color, uint32_t border_color);
void rife_draw_text(RifeCore* core, float x, float y, const char* text, uint32_t color);
void rife_draw_text_font(RifeCore* core, float x, float y, const char* text, uint32_t color, uint8_t font_id);
void rife_draw_text_rect(RifeCore* core, float x, float y, float w, float h, const char* text, uint32_t color, uint8_t font_id, uint32_t align_flags);
void rife_push_scissor(RifeCore* core, float x, float y, float w, float h);
void rife_push_scissor_round(RifeCore* core, float x, float y, float w, float h, float radius);
void rife_pop_scissor(RifeCore* core);
bool rife_emit_event(RifeCore* core, uint32_t type, uint16_t src, uint16_t dst, uint64_t payload);
void rife_dispatch_events(RifeCore* core);
bool rife_core_init(RifeCore* core, size_t persistent_size, size_t frame_size, uint32_t target_fps);
bool rife_register_app(RifeCore* core, RifeApp app);
void rife_core_tick(RifeCore* core);
void rife_core_shutdown(RifeCore* core);
void rife_render_flush(RifeCore* core);
#endif
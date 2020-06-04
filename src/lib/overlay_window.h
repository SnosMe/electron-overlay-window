#ifndef ADDON_SRC_OVERLAY_WINDOW_H_
#define ADDON_SRC_OVERLAY_WINDOW_H_

#include <stdint.h>
#include <uv.h>

struct ow_window_bounds {
  int32_t x;
  int32_t y;
  uint32_t width;
  uint32_t height;
};

enum ow_event_type {
  OW_ATTACH = 1,
  OW_FOCUS,
  OW_BLUR,
  OW_DETACH
};

struct ow_event_attach {
  uint32_t pid;
  int has_access;
};

struct ow_event {
  enum ow_event_type type;
  union {
    struct ow_event_attach attach;
  } data;
};

static uv_thread_t hook_tid = NULL;

void ow_start_hook(char* target_window_title, void* overlay_window_id);

void ow_activate_overlay();

void ow_emit_event(struct ow_event* event);

#endif

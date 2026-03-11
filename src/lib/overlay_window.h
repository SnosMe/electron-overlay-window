#ifndef ADDON_SRC_OVERLAY_WINDOW_H_
#define ADDON_SRC_OVERLAY_WINDOW_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <uv.h>

enum ow_event_type {
  // target window is found
  OW_ATTACH = 1,
  // target window is active/foreground
  OW_FOCUS,
  // target window lost focus
  OW_BLUR,
  // target window is destroyed
  OW_DETACH,
  // target window fullscreen changed
  // only emitted on X11 and Mac backend
  OW_FULLSCREEN,
  // target window changed position or resized
  OW_MOVERESIZE,
};

struct ow_window_bounds {
  int32_t x;
  int32_t y;
  uint32_t width;
  uint32_t height;
};

struct ow_event_attach {
  // defined only on Windows
  int has_access;
  // defined only on Linux, only if changed
  int is_fullscreen;
  //
  struct ow_window_bounds bounds;
};

struct ow_event_fullscreen {
  bool is_fullscreen;
};

struct ow_event_moveresize {
  struct ow_window_bounds bounds;
};

struct ow_event {
  enum ow_event_type type;
  union {
    struct ow_event_attach attach;
    struct ow_event_fullscreen fullscreen;
    struct ow_event_moveresize moveresize;
  } data;
};

static uv_thread_t hook_tid;

// Passed the title and a pointer to the platform-specific window ID.
// Window ID format depends on platform, see
// https://www.electronjs.org/docs/api/browser-window#wingetnativewindowhandle
void ow_start_hook(char* target_window_title, void* overlay_window_id);

void ow_activate_overlay();

void ow_focus_target();

void ow_emit_event(struct ow_event* event);

void ow_screenshot(uint8_t* out, uint32_t width, uint32_t height);

// Layout must match xcb_rectangle_t; verified by static assertion in x11.c
struct ow_input_rect {
  int16_t x;
  int16_t y;
  uint16_t width;
  uint16_t height;
};

/* Sets the X11 input shape for the overlay window to the given rectangles.
 * Only the listed regions will receive mouse input; all other areas pass
 * clicks through to the window below.
 * Pass count == 0 (rects may be NULL) to remove the input shape and
 * restore full-window input. No-op on non-Linux platforms. */
void ow_set_input_regions(struct ow_input_rect* rects, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif

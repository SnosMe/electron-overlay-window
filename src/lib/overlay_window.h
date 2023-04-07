#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <node_api.h>
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

bool ow_emit_async_event(struct ow_event event, napi_threadsafe_function tsfn);

enum ow_worker_action {
  OW_TRACK = 1,
  OW_CANCEL_TRACKING,
  OW_FOCUS_TARGET,
  OW_FOCUS_APP,
  OW_SCREENSHOT,
};

struct ow_task_ionut {
  enum ow_worker_action action;
  union {
    struct {
      char* target_window_title;
      void* app_window_id;
      napi_threadsafe_function tsfn;
    } track;
    struct {
      uint8_t* out;
      uint32_t width;
      uint32_t height;
    } screenshot;
  };
  uint32_t handle;
};

void ow_worker_exec_sync(struct ow_task_ionut* data);

#ifdef __cplusplus
}
#endif

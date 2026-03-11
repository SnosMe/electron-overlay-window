#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <pthread.h>
#include <xcb/xcb.h>
#include <xcb/shape.h>
#include "overlay_window.h"

_Static_assert(sizeof(struct ow_input_rect) == sizeof(xcb_rectangle_t),
  "ow_input_rect must be layout-compatible with xcb_rectangle_t");
_Static_assert(offsetof(struct ow_input_rect, x) == offsetof(xcb_rectangle_t, x),
  "ow_input_rect.x offset mismatch");
_Static_assert(offsetof(struct ow_input_rect, y) == offsetof(xcb_rectangle_t, y),
  "ow_input_rect.y offset mismatch");
_Static_assert(offsetof(struct ow_input_rect, width) == offsetof(xcb_rectangle_t, width),
  "ow_input_rect.width offset mismatch");
_Static_assert(offsetof(struct ow_input_rect, height) == offsetof(xcb_rectangle_t, height),
  "ow_input_rect.height offset mismatch");

static xcb_connection_t* x_conn;
static pthread_mutex_t x_conn_mutex = PTHREAD_MUTEX_INITIALIZER;
static xcb_window_t root;
static xcb_atom_t ATOM_NET_ACTIVE_WINDOW;
static xcb_atom_t ATOM_NET_WM_NAME;
static xcb_atom_t ATOM_UTF8_STRING;
static xcb_atom_t ATOM_NET_WM_STATE;
static xcb_atom_t ATOM_NET_WM_STATE_FULLSCREEN;

struct ow_target_window
{
  char* title;
  xcb_window_t window_id;
  bool is_focused;
  bool is_destroyed;
  bool is_fullscreen;
};

struct ow_overlay_window
{
  xcb_window_t window_id;
};

static xcb_window_t active_window = XCB_WINDOW_NONE;

static bool geometry_debug_enabled() {
  static int cached = -1;
  if (cached == -1) {
    const char* env_value = getenv("OVERLAY_WINDOW_DEBUG_GEOMETRY");
    cached = (env_value != NULL && env_value[0] != '\0') ? 1 : 0;
  }
  return cached == 1;
}

static void log_geometry_bounds(const char* stage, const struct ow_window_bounds* bounds) {
  if (!geometry_debug_enabled()) return;

  fprintf(stderr,
    "[overlay-window:x11] stage=%s source=authoritative-x11 "
    "units=physical-virtual-desktop-pixels bounds={x=%d,y=%d,width=%u,height=%u}\n",
    stage, bounds->x, bounds->y, bounds->width, bounds->height);
}

static struct ow_target_window target_info = {
  .title = NULL,
  .window_id = XCB_WINDOW_NONE,
  .is_focused = false,
  .is_destroyed = false,
  .is_fullscreen = false // initial state of *overlay* window
};

static struct ow_overlay_window overlay_info = {
  .window_id = XCB_WINDOW_NONE
};

static xcb_window_t get_active_window() {
  xcb_get_property_reply_t* prop_reply = xcb_get_property_reply(x_conn, xcb_get_property(x_conn, 0, root, ATOM_NET_ACTIVE_WINDOW, XCB_ATOM_WINDOW, 0, 1), NULL);
  if (prop_reply == NULL) {
    return XCB_WINDOW_NONE;
  }
  xcb_window_t active_window = *((xcb_window_t*)xcb_get_property_value(prop_reply));
  free(prop_reply);
  return active_window;
}

static bool get_title(xcb_window_t wid, char** title) {
  if (wid == XCB_WINDOW_NONE) {
    *title = NULL;
    return true;
  }
  xcb_get_property_reply_t* prop_reply = xcb_get_property_reply(x_conn, xcb_get_property(x_conn, 0, wid, ATOM_NET_WM_NAME, ATOM_UTF8_STRING, 0, 100000), NULL);
  if (prop_reply == NULL) {
    return false;
  }
  int buffLenUtf8 = xcb_get_property_value_length(prop_reply);
  if (buffLenUtf8 == 0) {
    *title = NULL;
    free(prop_reply);
    return true;
  }
  *title = malloc(buffLenUtf8 + 1);
  memcpy(*title, xcb_get_property_value(prop_reply), buffLenUtf8);
  (*title)[buffLenUtf8] = '\0';
  free(prop_reply);
  return true;
}

static bool get_content_bounds(xcb_window_t wid, struct ow_window_bounds* bounds) {
  // Linux/X11 contract: exported bounds are authoritative X11 virtual-desktop
  // coordinates in physical pixels (integer origin + integer size), suitable
  // for direct comparison with global mouse hooks (e.g. uiohook).
  xcb_get_geometry_reply_t* geometry = xcb_get_geometry_reply(x_conn, xcb_get_geometry(x_conn, wid), NULL);
  if (geometry == NULL) {
    return false;
  }
  xcb_translate_coordinates_reply_t* translated = xcb_translate_coordinates_reply(x_conn, xcb_translate_coordinates(x_conn, wid, root, 0, 0), NULL);
  if (translated == NULL) {
    free(geometry);
    return false;
  }

  bounds->x = translated->dst_x;
  bounds->y = translated->dst_y;
  bounds->width = geometry->width;
  bounds->height = geometry->height;
  free(translated);
  free(geometry);
  return true;
}

static bool is_fullscreen_window(xcb_window_t wid, bool* is_fullscreen) {
  xcb_get_property_reply_t* prop_reply = xcb_get_property_reply(x_conn, xcb_get_property(x_conn, 0, wid, ATOM_NET_WM_STATE, XCB_ATOM_ATOM, 0, 100000), NULL);
  if (prop_reply == NULL) {
    return false;
  }
  *is_fullscreen = false;
  xcb_atom_t* wm_state = (xcb_atom_t*)xcb_get_property_value(prop_reply);
  for (unsigned i = 0; i < prop_reply->value_len; ++i) {
    if (wm_state[i] == ATOM_NET_WM_STATE_FULLSCREEN) {
      *is_fullscreen = true;
    }
  }
  free(prop_reply);
  return true;
}

static void handle_moveresize_xevent(struct ow_target_window* target_info) {
  struct ow_window_bounds bounds;
  if (get_content_bounds(target_info->window_id, &bounds)) {
    log_geometry_bounds("moveresize-export", &bounds);

    struct ow_event e = {
      .type = OW_MOVERESIZE,
      .data.moveresize = {
        .bounds = bounds
      }
    };
    ow_emit_event(&e);
  }
}

static void handle_fullscreen_xevent(struct ow_target_window* target_info) {
  bool is_fullscreen;
  if (is_fullscreen_window(target_info->window_id, &is_fullscreen)) {
    if (is_fullscreen != target_info->is_fullscreen) {
      target_info->is_fullscreen = is_fullscreen;
      struct ow_event e = {
        .type = OW_FULLSCREEN,
        .data.fullscreen = {
          .is_fullscreen = target_info->is_fullscreen
        }
      };
      ow_emit_event(&e);
    }
  }
}

static void check_and_handle_window(xcb_window_t wid, struct ow_target_window* target_info) {
  if (target_info->window_id != XCB_WINDOW_NONE) {
    if (target_info->window_id != wid) {
      if (target_info->is_focused) {
        target_info->is_focused = false;
        struct ow_event e = { .type = OW_BLUR };
        ow_emit_event(&e);
      }

      if (target_info->is_destroyed) {
        target_info->window_id = XCB_WINDOW_NONE;

        target_info->is_destroyed = false;
        struct ow_event e = { .type = OW_DETACH };
        ow_emit_event(&e);
      }
    }
    else if (target_info->window_id == wid) {
      if (!target_info->is_focused) {
        target_info->is_focused = true;
        struct ow_event e = { .type = OW_FOCUS };
        ow_emit_event(&e);
      }
      return;
    }
  }

  char* title = NULL;
  if (!get_title(wid, &title) || title == NULL) {
    return;
  }
  bool is_equal = (strcmp(title, target_info->title) == 0);
  free(title);
  if (!is_equal) {
    return;
  }

  if (target_info->window_id != XCB_WINDOW_NONE) {
    uint32_t mask[] = { XCB_EVENT_MASK_NO_EVENT };
    xcb_change_window_attributes(x_conn, target_info->window_id, XCB_CW_EVENT_MASK, mask);
  }

  target_info->window_id = wid;

  // listen for `_NET_WM_STATE` fullscreen and window move/resize/destroy
  uint32_t mask[] = { XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY };
  xcb_change_window_attributes(x_conn, target_info->window_id, XCB_CW_EVENT_MASK, mask);

  struct ow_event e = {
    .type = OW_ATTACH,
    .data.attach = {
      .has_access = -1,
      .is_fullscreen = -1
    }
  };
  bool is_fullscreen;
  if (
    is_fullscreen_window(target_info->window_id, &is_fullscreen) &&
    get_content_bounds(target_info->window_id, &e.data.attach.bounds)
  ) {
    log_geometry_bounds("attach-export", &e.data.attach.bounds);

    if (is_fullscreen != target_info->is_fullscreen) {
      target_info->is_fullscreen = is_fullscreen;
      e.data.attach.is_fullscreen = is_fullscreen;
    }
    // emit OW_ATTACH
    ow_emit_event(&e);

    target_info->is_focused = true;
    e.type = OW_FOCUS;
    ow_emit_event(&e);
  } else {
    // something went wrong, did the target window die right after becoming active?
    target_info->window_id = XCB_WINDOW_NONE;
  }
}

static void hook_proc(xcb_generic_event_t* generic_event) {
  if (generic_event->response_type == XCB_DESTROY_NOTIFY) {
    xcb_destroy_notify_event_t* event = (xcb_destroy_notify_event_t*)generic_event;
    if (event->window == target_info.window_id) {
      target_info.is_destroyed = true;
      check_and_handle_window(XCB_WINDOW_NONE, &target_info);
    }
    return;
  }
  if (generic_event->response_type == XCB_CONFIGURE_NOTIFY) {
    xcb_configure_notify_event_t* event = (xcb_configure_notify_event_t*)generic_event;
    if (event->window == target_info.window_id) {
      handle_moveresize_xevent(&target_info);
    }
    return;
  }
  if (generic_event->response_type == XCB_PROPERTY_NOTIFY) {
    xcb_property_notify_event_t* event = (xcb_property_notify_event_t*)generic_event;
    if (event->window == root && event->atom == ATOM_NET_ACTIVE_WINDOW) {
      xcb_window_t old_active = active_window;
      active_window = get_active_window();

      if (old_active != target_info.window_id) {
        uint32_t mask[] = { XCB_EVENT_MASK_NO_EVENT };
        xcb_change_window_attributes(x_conn, old_active, XCB_CW_EVENT_MASK, mask);
      }
      if (active_window != XCB_WINDOW_NONE && active_window != target_info.window_id) {
        // listen for `_NET_WM_NAME`
        uint32_t mask[] = { XCB_EVENT_MASK_PROPERTY_CHANGE };
        xcb_change_window_attributes(x_conn, active_window, XCB_CW_EVENT_MASK, mask);
      }
      check_and_handle_window(active_window, &target_info);
    } else if (event->window == target_info.window_id && event->atom == ATOM_NET_WM_STATE) {
      handle_fullscreen_xevent(&target_info);
    } else if (event->window == active_window && event->atom == ATOM_NET_WM_NAME) {
      check_and_handle_window(active_window, &target_info);
    }
    return;
  }
}

static void hook_thread(void* _arg) {
  x_conn = xcb_connect(NULL, NULL);
  xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(x_conn)).data;
  root = screen->root;

  xcb_intern_atom_reply_t* atom_reply;
  atom_reply = xcb_intern_atom_reply(x_conn, xcb_intern_atom(x_conn, 0, strlen("_NET_ACTIVE_WINDOW"), "_NET_ACTIVE_WINDOW"), NULL);
  ATOM_NET_ACTIVE_WINDOW = atom_reply->atom;
  free(atom_reply);
  atom_reply = xcb_intern_atom_reply(x_conn, xcb_intern_atom(x_conn, 0, strlen("_NET_WM_NAME"), "_NET_WM_NAME"), NULL);
  ATOM_NET_WM_NAME = atom_reply->atom;
  free(atom_reply);
  atom_reply = xcb_intern_atom_reply(x_conn, xcb_intern_atom(x_conn, 0, strlen("UTF8_STRING"), "UTF8_STRING"), NULL);
  ATOM_UTF8_STRING = atom_reply->atom;
  free(atom_reply);
  atom_reply = xcb_intern_atom_reply(x_conn, xcb_intern_atom(x_conn, 0, strlen("_NET_WM_STATE"), "_NET_WM_STATE"), NULL);
  ATOM_NET_WM_STATE = atom_reply->atom;
  free(atom_reply);
  atom_reply = xcb_intern_atom_reply(x_conn, xcb_intern_atom(x_conn, 0, strlen("_NET_WM_STATE_FULLSCREEN"), "_NET_WM_STATE_FULLSCREEN"), NULL);
  ATOM_NET_WM_STATE_FULLSCREEN = atom_reply->atom;
  free(atom_reply);

  if (overlay_info.window_id != XCB_WINDOW_NONE) {
    // Electron window is created with `show: false`,
    // this override-redirect is being set before window is mapped.
    uint32_t values[] = {1};
    xcb_change_window_attributes(x_conn, overlay_info.window_id, XCB_CW_OVERRIDE_REDIRECT, values);
  }

  // listen for `_NET_ACTIVE_WINDOW` changes
  uint32_t mask[] = { XCB_EVENT_MASK_PROPERTY_CHANGE };
  xcb_change_window_attributes(x_conn, root, XCB_CW_EVENT_MASK, mask);

  active_window = get_active_window();
  if (active_window != XCB_WINDOW_NONE) {
    // listen for `_NET_WM_NAME`
    uint32_t mask[] = { XCB_EVENT_MASK_PROPERTY_CHANGE };
    xcb_change_window_attributes(x_conn, active_window, XCB_CW_EVENT_MASK, mask);
    check_and_handle_window(active_window, &target_info);
  }
  xcb_flush(x_conn);

  xcb_generic_event_t* event;
  while ((event = xcb_wait_for_event(x_conn))) {
    pthread_mutex_lock(&x_conn_mutex);
    event->response_type = event->response_type & ~0x80;
    hook_proc(event);
    xcb_flush(x_conn);
    pthread_mutex_unlock(&x_conn_mutex);
    free(event);
  }
}

void ow_start_hook(char* target_window_title, void* overlay_window_id) {
  target_info.title = target_window_title;
  if (overlay_window_id != NULL) {
    overlay_info.window_id = *((xcb_window_t*)overlay_window_id);
  }
  if (geometry_debug_enabled()) {
    fprintf(stderr,
      "[overlay-window:x11] stage=hook-start source=authoritative-x11 "
      "units=physical-virtual-desktop-pixels overlay_window_id=%u\n",
      overlay_info.window_id);
  }
  uv_thread_create(&hook_tid, hook_thread, NULL);
}

void ow_activate_overlay() {
  if (overlay_info.window_id == XCB_WINDOW_NONE) return;

  pthread_mutex_lock(&x_conn_mutex);

  // Send _NET_ACTIVE_WINDOW to ask the WM to activate our overlay.
  // Even though the overlay is override-redirect (unmanaged), the WM
  // typically processes this far enough to send FocusOut to the
  // currently active window. This causes SDL2/Wine to release any
  // active XGrabPointer, allowing the overlay to receive mouse clicks.
  //
  // data32[1] (timestamp) is intentionally 0: no user-event timestamp
  // is available at this call site. data32[2] (requestor's current
  // active window) is intentionally 0: we don't track this here.
  // If a WM rejects the message due to focus-steal prevention,
  // xcb_set_input_focus below still handles keyboard focus (no regression).
  xcb_client_message_event_t msg = {0};
  msg.response_type = XCB_CLIENT_MESSAGE;
  msg.type = ATOM_NET_ACTIVE_WINDOW;
  msg.window = overlay_info.window_id;
  msg.format = 32;
  msg.data.data32[0] = 2; // source indication: pager

  xcb_send_event(x_conn, 0, root,
    XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
    (const char*)&msg);

  xcb_set_input_focus(x_conn, XCB_INPUT_FOCUS_PARENT, overlay_info.window_id, XCB_CURRENT_TIME);
  xcb_flush(x_conn);

  pthread_mutex_unlock(&x_conn_mutex);
}

void ow_focus_target() {
  if (target_info.window_id == XCB_WINDOW_NONE) return;

  pthread_mutex_lock(&x_conn_mutex);
  xcb_set_input_focus(x_conn, XCB_INPUT_FOCUS_PARENT, target_info.window_id, XCB_CURRENT_TIME);
  xcb_flush(x_conn);
  pthread_mutex_unlock(&x_conn_mutex);
}

void ow_set_input_regions(struct ow_input_rect* rects, uint32_t count) {
  // x_conn is initialized by hook_thread before overlay_info.window_id
  // is ever set, so the window_id guard below is a sufficient proxy for
  // "x_conn is ready". Do not call this function before OW_ATTACH is received.
  // Specifically: hook_thread sets x_conn via xcb_connect() at its very
  // first line, then flushes before any event is processed. The JS layer
  // only receives OW_ATTACH after hook_thread has completed setup, so
  // the invariant (x_conn != NULL) iff (window_id != XCB_WINDOW_NONE) holds.
  if (overlay_info.window_id == XCB_WINDOW_NONE) return;

  pthread_mutex_lock(&x_conn_mutex);
  /*
   * Set the input shape to exactly the given rectangles. When count == 0
   * this sets an empty shape, meaning the window receives no input at all
   * and all clicks pass through to the window below. This is the correct
   * behavior for an overlay with no visible widgets.
   *
   * Note: xcb_shape_mask with XCB_PIXMAP_NONE would *remove* the input
   * shape entirely, restoring full-window input — the opposite of what
   * we want. We intentionally always use xcb_shape_rectangles so that
   * an empty list means "accept nothing" rather than "accept everything".
   */
  xcb_shape_rectangles(x_conn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_INPUT,
    XCB_CLIP_ORDERING_UNSORTED, overlay_info.window_id, 0, 0,
    count, (xcb_rectangle_t*)rects);
  xcb_flush(x_conn);
  pthread_mutex_unlock(&x_conn_mutex);
}

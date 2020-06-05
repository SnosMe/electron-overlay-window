#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <xcb/xcb.h>
#include "overlay_window.h"

static xcb_connection_t* x_conn;
static xcb_window_t root;
static xcb_window_t active_window = XCB_WINDOW_NONE;
static xcb_atom_t ATOM_NET_ACTIVE_WINDOW;
static xcb_atom_t ATOM_NET_WM_NAME;
static xcb_atom_t ATOM_UTF8_STRING;

struct ow_target_window
{
  char* title;
  xcb_window_t window_id;
  bool is_focused;
};

static struct ow_target_window target_info = {
  .title = NULL,
  .window_id = XCB_WINDOW_NONE,
  .is_focused = false
};

static bool get_title(xcb_window_t wid, char** title) {
  xcb_get_property_reply_t* active_win_reply = xcb_get_property_reply(x_conn, xcb_get_property(x_conn, 0, wid, ATOM_NET_WM_NAME, ATOM_UTF8_STRING, 0, 100000), NULL);
  if (active_win_reply == NULL) {
    return false;
  }
  int buffLenUtf8 = xcb_get_property_value_length(active_win_reply);
  if (buffLenUtf8 == 0) {
    // @TODO: fallback to WM_NAME ?
    *title = NULL;
    free(active_win_reply);
    return true;
  }
  *title = malloc(buffLenUtf8 + 1);
  memcpy(*title, xcb_get_property_value(active_win_reply), buffLenUtf8);
  (*title)[buffLenUtf8] = '\0';
  free(active_win_reply);
  return true;
}

static void check_and_handle_window(xcb_window_t wid, struct ow_target_window* target_info) {
  if (target_info->window_id != XCB_WINDOW_NONE) {
    if (target_info->window_id != wid) {
      if (target_info->is_focused) {
        target_info->is_focused = false;
        struct ow_event e = { .type = OW_BLUR };
        ow_emit_event(&e);
      }

      // @TODO: test if closed
      bool targetIsClosed = false;
      if (targetIsClosed) {
        // @TODO set _NET_WM_STATE_HIDDEN
        // xcb_flush(x_conn);
        target_info->window_id = XCB_WINDOW_NONE;
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
    printf("untitled=%x\n", wid);
    return;
  }
  printf("title=\"%s\"\n", title);
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

  // listen for `_NET_WM_STATE` fullscreen and window move/resize
  uint32_t mask[] = { XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY };
  xcb_change_window_attributes(x_conn, target_info->window_id, XCB_CW_EVENT_MASK, mask);
}

static xcb_window_t get_active_window() {
  xcb_get_property_reply_t* active_win_reply = xcb_get_property_reply(x_conn, xcb_get_property(x_conn, 0, root, ATOM_NET_ACTIVE_WINDOW, XCB_ATOM_WINDOW, 0, 1), NULL);
  if (active_win_reply == NULL) {
    return XCB_WINDOW_NONE;
  }
  xcb_window_t active_window = *((xcb_window_t*)xcb_get_property_value(active_win_reply));
  free(active_win_reply);
  return active_window;
}

static void hook_proc(xcb_generic_event_t* event) {
  if (event->response_type & XCB_PROPERTY_NOTIFY) {
    xcb_property_notify_event_t* prop = (xcb_property_notify_event_t*)event;
    if (prop->window == root && prop->atom == ATOM_NET_ACTIVE_WINDOW) {
      check_and_handle_window(get_active_window(), &target_info);
    }
    return;
  }
  printf("[0] Unhanled Event\n");
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

  // listen for `_NET_ACTIVE_WINDOW` changes
  uint32_t mask[] = { XCB_EVENT_MASK_PROPERTY_CHANGE };
  xcb_change_window_attributes(x_conn, root, XCB_CW_EVENT_MASK, mask);

  check_and_handle_window(get_active_window(), &target_info);

  xcb_generic_event_t* event;
  while ((event = xcb_wait_for_event(x_conn))) {
    hook_proc(event);
    free(event);
  }
}

void ow_start_hook(char* target_window_title, void* overlay_window_id) {
  uv_thread_create(&hook_tid, hook_thread, NULL);
  if (hook_tid == 0) {
    //3
  }
}

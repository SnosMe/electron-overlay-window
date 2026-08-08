#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <node_api.h>
#include "napi_helpers.h"
#include "overlay_window.h"

static napi_threadsafe_function threadsafe_fn = NULL;
static struct ow_window_bounds last_reported_bounds = {0, 0, 0, 0};

// Delivering a window event is never important enough to kill the whole
// process over. `napi_define_properties` (and friends) can legitimately fail
// with `napi_pending_exception` when a JS exception is still pending or when
// the environment can no longer run JS -- aborting there takes the entire app
// down. Drop the event instead.
//
// Reported only once: these failures arrive in bursts (one per window event),
// and the first one carries the diagnostic value.
static bool napi_failure_reported = false;

static void ow_report_napi_failure(napi_env env, const char* location, const char* message, napi_status status) {
  if (napi_failure_reported) return;
  napi_failure_reported = true;

  const napi_extended_error_info* err_info = NULL;
  napi_get_last_error_info(env, &err_info);
  fprintf(stderr, "[overlay-window] %s: %s failed (napi_status=%d%s%s). "
                  "Dropping this and any further affected events.\n",
          location, message, (int)status,
          (err_info != NULL && err_info->error_message != NULL) ? ": " : "",
          (err_info != NULL && err_info->error_message != NULL) ? err_info->error_message : "");
}

#define OW_BAIL_IF_FAILED(env, status, location, message)      \
  do {                                                         \
    if ((status) != napi_ok) {                                 \
      ow_report_napi_failure((env), (location), (message), (status)); \
      return NULL;                                             \
    }                                                          \
  } while (0)

void ow_emit_event(struct ow_event* event) {
  if (threadsafe_fn == NULL) return;

  struct ow_event* copied_event = malloc(sizeof(struct ow_event));
  if (copied_event == NULL) return;
  memcpy(copied_event, event, sizeof(struct ow_event));

  napi_status status = napi_call_threadsafe_function(threadsafe_fn, copied_event, napi_tsfn_nonblocking);
  if (status != napi_ok) {
    // `napi_closing` on teardown, `napi_queue_full` under an event storm.
    // Neither warrants aborting the process.
    if (status == napi_closing) {
      threadsafe_fn = NULL;
    }
    free(copied_event);
    return;
  }
}

napi_value ow_event_to_js_object(napi_env env, struct ow_event* event) {
  napi_status status;

  napi_value event_obj;
  status = napi_create_object(env, &event_obj);
  OW_BAIL_IF_FAILED(env, status, "ow_event_to_js_object", "napi_create_object");

  napi_value e_type;
  status = napi_create_uint32(env, event->type, &e_type);
  OW_BAIL_IF_FAILED(env, status, "ow_event_to_js_object", "napi_create_uint32");

  if (event->type == OW_ATTACH) {
    napi_value e_has_access;
    if (event->data.attach.has_access == -1) {
      status = napi_get_undefined(env, &e_has_access);
      OW_BAIL_IF_FAILED(env, status, "ow_event_to_js_object","napi_get_undefined");
    }
    else {
      status = napi_get_boolean(env, event->data.attach.has_access == 1, &e_has_access);
      OW_BAIL_IF_FAILED(env, status, "ow_event_to_js_object","napi_get_boolean");
    }

    napi_value e_is_fullscreen;
    if (event->data.attach.is_fullscreen == -1) {
      status = napi_get_undefined(env, &e_is_fullscreen);
      OW_BAIL_IF_FAILED(env, status, "ow_event_to_js_object","napi_get_undefined");
    }
    else {
      status = napi_get_boolean(env, event->data.attach.is_fullscreen == 1, &e_is_fullscreen);
      OW_BAIL_IF_FAILED(env, status, "ow_event_to_js_object","napi_get_boolean");
    }

    napi_value e_x;
    status = napi_create_int32(env, event->data.attach.bounds.x, &e_x);
    OW_BAIL_IF_FAILED(env, status, "ow_event_to_js_object","napi_create_int32");

    napi_value e_y;
    status = napi_create_int32(env, event->data.attach.bounds.y, &e_y);
    OW_BAIL_IF_FAILED(env, status, "ow_event_to_js_object","napi_create_int32");

    napi_value e_width;
    status = napi_create_uint32(env, event->data.attach.bounds.width, &e_width);
    OW_BAIL_IF_FAILED(env, status, "ow_event_to_js_object","napi_create_uint32");

    napi_value e_height;
    status = napi_create_uint32(env, event->data.attach.bounds.height, &e_height);
    OW_BAIL_IF_FAILED(env, status, "ow_event_to_js_object","napi_create_uint32");

    napi_property_descriptor descriptors[] = {
      { "type",         NULL, NULL, NULL, NULL, e_type,          napi_enumerable, NULL },
      { "hasAccess",    NULL, NULL, NULL, NULL, e_has_access,    napi_enumerable, NULL },
      { "isFullscreen", NULL, NULL, NULL, NULL, e_is_fullscreen, napi_enumerable, NULL },
      { "x",            NULL, NULL, NULL, NULL, e_x,             napi_enumerable, NULL },
      { "y",            NULL, NULL, NULL, NULL, e_y,             napi_enumerable, NULL },
      { "width",        NULL, NULL, NULL, NULL, e_width,         napi_enumerable, NULL },
      { "height",       NULL, NULL, NULL, NULL, e_height,        napi_enumerable, NULL },
    };
    status = napi_define_properties(env, event_obj, sizeof(descriptors) / sizeof(descriptors[0]), descriptors);
    OW_BAIL_IF_FAILED(env, status, "ow_event_to_js_object","napi_define_properties");
    return event_obj;
  }
  else if (event->type == OW_FULLSCREEN) {
    napi_value e_is_fullscreen;
    status = napi_get_boolean(env, event->data.fullscreen.is_fullscreen, &e_is_fullscreen);
    OW_BAIL_IF_FAILED(env, status, "ow_event_to_js_object","napi_get_boolean");

    napi_property_descriptor descriptors[] = {
      { "type",         NULL, NULL, NULL, NULL, e_type,          napi_enumerable, NULL },
      { "isFullscreen", NULL, NULL, NULL, NULL, e_is_fullscreen, napi_enumerable, NULL },
    };
    status = napi_define_properties(env, event_obj, sizeof(descriptors) / sizeof(descriptors[0]), descriptors);
    OW_BAIL_IF_FAILED(env, status, "ow_event_to_js_object","napi_define_properties");
    return event_obj;
  }
  else if (event->type == OW_MOVERESIZE) {
    napi_value e_x;
    status = napi_create_int32(env, event->data.moveresize.bounds.x, &e_x);
    OW_BAIL_IF_FAILED(env, status, "ow_event_to_js_object","napi_create_int32");

    napi_value e_y;
    status = napi_create_int32(env, event->data.moveresize.bounds.y, &e_y);
    OW_BAIL_IF_FAILED(env, status, "ow_event_to_js_object","napi_create_int32");

    napi_value e_width;
    status = napi_create_uint32(env, event->data.moveresize.bounds.width, &e_width);
    OW_BAIL_IF_FAILED(env, status, "ow_event_to_js_object","napi_create_uint32");

    napi_value e_height;
    status = napi_create_uint32(env, event->data.moveresize.bounds.height, &e_height);
    OW_BAIL_IF_FAILED(env, status, "ow_event_to_js_object","napi_create_uint32");

    napi_property_descriptor descriptors[] = {
      { "type",   NULL, NULL, NULL, NULL, e_type,   napi_enumerable, NULL },
      { "x",      NULL, NULL, NULL, NULL, e_x,      napi_enumerable, NULL },
      { "y",      NULL, NULL, NULL, NULL, e_y,      napi_enumerable, NULL },
      { "width",  NULL, NULL, NULL, NULL, e_width,  napi_enumerable, NULL },
      { "height", NULL, NULL, NULL, NULL, e_height, napi_enumerable, NULL },
    };
    status = napi_define_properties(env, event_obj, sizeof(descriptors) / sizeof(descriptors[0]), descriptors);
    OW_BAIL_IF_FAILED(env, status, "ow_event_to_js_object","napi_define_properties");
    return event_obj;
  }
  else {
    napi_property_descriptor descriptors[] = {
      { "type", NULL, NULL, NULL, NULL, e_type, napi_enumerable, NULL },
    };
    status = napi_define_properties(env, event_obj, sizeof(descriptors) / sizeof(descriptors[0]), descriptors);
    OW_BAIL_IF_FAILED(env, status, "ow_event_to_js_object","napi_define_properties");
    return event_obj;
  }
}

void tsfn_to_js_proxy(napi_env env, napi_value js_callback, void* context, void* _event) {
  struct ow_event* event = (struct ow_event*)_event;
  if (event->type == OW_MOVERESIZE) {
    last_reported_bounds = event->data.moveresize.bounds;
  } else if (event->type == OW_ATTACH) {
    last_reported_bounds = event->data.attach.bounds;
  }

  napi_status status;

  napi_value event_obj = ow_event_to_js_object(env, event);
  if (event_obj == NULL) {
    free(event);
    return;
  }

  napi_value global;
  status = napi_get_global(env, &global);
  if (status != napi_ok) {
    ow_report_napi_failure(env, "tsfn_to_js_proxy", "napi_get_global", status);
    free(event);
    return;
  }

  status = napi_call_function(env, global, js_callback, 1, &event_obj, NULL);
  if (status == napi_pending_exception) {
    // The JS listener threw. Surface it, then clear it -- leaving the
    // exception pending makes every later napi call fail, which is what used
    // to turn a single bad listener call into a hard abort of the process.
    napi_value exception;
    if (napi_get_and_clear_last_exception(env, &exception) == napi_ok) {
      napi_value stack;
      char buf[1024];
      size_t len = 0;
      if (napi_get_named_property(env, exception, "stack", &stack) == napi_ok &&
          napi_get_value_string_utf8(env, stack, buf, sizeof(buf), &len) == napi_ok) {
        fprintf(stderr, "[overlay-window] exception in event listener: %.*s\n", (int)len, buf);
      } else {
        fprintf(stderr, "[overlay-window] exception in event listener (no stack available)\n");
      }
    }
  } else if (status != napi_ok) {
    ow_report_napi_failure(env, "tsfn_to_js_proxy", "napi_call_function", status);
  }

  free(event);
}

napi_value AddonStart(napi_env env, napi_callback_info info) {
  napi_status status;

  size_t info_argc = 3;
  napi_value info_argv[3];
  status = napi_get_cb_info(env, info, &info_argc, info_argv, NULL, NULL);
  NAPI_THROW_IF_FAILED(env, status, NULL);

  // [0] Overlay Window ID
  void* overlay_window_id = NULL;
  bool has_window_id;
  status = napi_is_buffer(env, info_argv[0], &has_window_id);
  NAPI_THROW_IF_FAILED(env, status, NULL);
  if (has_window_id) {
    status = napi_get_buffer_info(env, info_argv[0], &overlay_window_id, NULL);
    NAPI_THROW_IF_FAILED(env, status, NULL);
  }

  // [1] Target Window title
  size_t target_window_title_length;
  status = napi_get_value_string_utf8(env, info_argv[1], NULL, 0, &target_window_title_length);
  NAPI_THROW_IF_FAILED(env, status, NULL);
  char* target_window_title = malloc(sizeof(char) * target_window_title_length + 1);
  status = napi_get_value_string_utf8(env, info_argv[1], target_window_title, target_window_title_length + 1, NULL);
  NAPI_THROW_IF_FAILED(env, status, NULL);

  // [2] Event callback
  napi_value async_resource_name;
  status = napi_create_string_utf8(env, "OVERLAY_WINDOW", NAPI_AUTO_LENGTH, &async_resource_name);
  NAPI_THROW_IF_FAILED(env, status, NULL);
  status = napi_create_threadsafe_function(env, info_argv[2], NULL, async_resource_name, 0, 1, NULL, NULL, NULL, tsfn_to_js_proxy, &threadsafe_fn);
  NAPI_THROW_IF_FAILED(env, status, NULL);

  // printf("start(window=%x, title=\"%s\")\n", *((int*)overlay_window_id), target_window_title);
  ow_start_hook(target_window_title, overlay_window_id);

  return NULL;
}

napi_value AddonActivateOverlay(napi_env _env, napi_callback_info _info) {
  ow_activate_overlay();
  return NULL;
}

napi_value AddonFocusTarget(napi_env env, napi_callback_info info) {
  ow_focus_target();
  return NULL;
}

napi_value AddonScreenshot(napi_env env, napi_callback_info info) {
  napi_status status;

  napi_value img_buffer;
  uint8_t* img_data;
  size_t size = last_reported_bounds.width * last_reported_bounds.height * 4;
  status = napi_create_buffer(env, size, (void **)&img_data, &img_buffer);
  NAPI_FATAL_IF_FAILED(status, "AddonScreenshot", "napi_create_buffer");

#ifdef _WIN32
  ow_screenshot(img_data, last_reported_bounds.width, last_reported_bounds.height);
#endif

  return img_buffer;
}

void AddonCleanUp(void* arg) {
  // @TODO
  // UnhookWinEvent(win_event_hhook);
}

NAPI_MODULE_INIT() {
  napi_status status;
  napi_value export_fn;

  status = napi_create_function(env, NULL, 0, AddonStart, NULL, &export_fn);
  NAPI_FATAL_IF_FAILED(status, "NAPI_MODULE_INIT", "napi_create_function");
  status = napi_set_named_property(env, exports, "start", export_fn);
  NAPI_FATAL_IF_FAILED(status, "NAPI_MODULE_INIT", "napi_set_named_property");

  status = napi_create_function(env, NULL, 0, AddonActivateOverlay, NULL, &export_fn);
  NAPI_FATAL_IF_FAILED(status, "NAPI_MODULE_INIT", "napi_create_function");
  status = napi_set_named_property(env, exports, "activateOverlay", export_fn);
  NAPI_FATAL_IF_FAILED(status, "NAPI_MODULE_INIT", "napi_set_named_property");

  status = napi_create_function(env, NULL, 0, AddonFocusTarget, NULL, &export_fn);
  NAPI_FATAL_IF_FAILED(status, "NAPI_MODULE_INIT", "napi_create_function");
  status = napi_set_named_property(env, exports, "focusTarget", export_fn);
  NAPI_FATAL_IF_FAILED(status, "NAPI_MODULE_INIT", "napi_set_named_property");

  status = napi_create_function(env, NULL, 0, AddonScreenshot, NULL, &export_fn);
  NAPI_FATAL_IF_FAILED(status, "NAPI_MODULE_INIT", "napi_create_function");
  status = napi_set_named_property(env, exports, "screenshot", export_fn);
  NAPI_FATAL_IF_FAILED(status, "NAPI_MODULE_INIT", "napi_set_named_property");

  status = napi_add_env_cleanup_hook(env, AddonCleanUp, NULL);
  NAPI_FATAL_IF_FAILED(status, "NAPI_MODULE_INIT", "napi_add_env_cleanup_hook");

  return exports;
}

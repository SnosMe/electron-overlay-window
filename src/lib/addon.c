#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <node_api.h>
#include "napi_helpers.h"
#include "overlay_window.h"

bool ow_emit_async_event(struct ow_event event, napi_threadsafe_function threadsafe_fn) {
  struct ow_event* copied_event = malloc(sizeof(struct ow_event));
  memcpy(copied_event, &event, sizeof(struct ow_event));

  napi_status status = napi_call_threadsafe_function(threadsafe_fn, copied_event, napi_tsfn_nonblocking);
  if (status == napi_closing) {
    free(copied_event);
    return false;
  }
  NAPI_FATAL_IF_FAILED(status, "ow_emit_async_event", "napi_call_threadsafe_function");
  return true;
}

napi_value ow_event_to_js_object(napi_env env, struct ow_event* event) {
  napi_status status;

  napi_value event_obj;
  status = napi_create_object(env, &event_obj);
  NAPI_FATAL_IF_FAILED(status, "ow_event_to_js_object", "napi_create_object");

  napi_value e_type;
  status = napi_create_uint32(env, event->type, &e_type);
  NAPI_FATAL_IF_FAILED(status, "ow_event_to_js_object", "napi_create_uint32");

  if (event->type == OW_ATTACH) {
    napi_value e_has_access;
    if (event->data.attach.has_access == -1) {
      status = napi_get_undefined(env, &e_has_access);
      NAPI_FATAL_IF_FAILED(status, "ow_event_to_js_object", "napi_get_undefined");
    }
    else {
      status = napi_get_boolean(env, event->data.attach.has_access == 1, &e_has_access);
      NAPI_FATAL_IF_FAILED(status, "ow_event_to_js_object", "napi_get_boolean");
    }

    napi_value e_is_fullscreen;
    if (event->data.attach.is_fullscreen == -1) {
      status = napi_get_undefined(env, &e_is_fullscreen);
      NAPI_FATAL_IF_FAILED(status, "ow_event_to_js_object", "napi_get_undefined");
    }
    else {
      status = napi_get_boolean(env, event->data.attach.is_fullscreen == 1, &e_is_fullscreen);
      NAPI_FATAL_IF_FAILED(status, "ow_event_to_js_object", "napi_get_boolean");
    }

    napi_value e_x;
    status = napi_create_int32(env, event->data.attach.bounds.x, &e_x);
    NAPI_FATAL_IF_FAILED(status, "ow_event_to_js_object", "napi_create_int32");

    napi_value e_y;
    status = napi_create_int32(env, event->data.attach.bounds.y, &e_y);
    NAPI_FATAL_IF_FAILED(status, "ow_event_to_js_object", "napi_create_int32");

    napi_value e_width;
    status = napi_create_uint32(env, event->data.attach.bounds.width, &e_width);
    NAPI_FATAL_IF_FAILED(status, "ow_event_to_js_object", "napi_create_uint32");

    napi_value e_height;
    status = napi_create_uint32(env, event->data.attach.bounds.height, &e_height);
    NAPI_FATAL_IF_FAILED(status, "ow_event_to_js_object", "napi_create_uint32");

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
    NAPI_FATAL_IF_FAILED(status, "ow_event_to_js_object", "napi_define_properties");
    return event_obj;
  }
  else if (event->type == OW_FULLSCREEN) {
    napi_value e_is_fullscreen;
    status = napi_get_boolean(env, event->data.fullscreen.is_fullscreen, &e_is_fullscreen);
    NAPI_FATAL_IF_FAILED(status, "ow_event_to_js_object", "napi_get_boolean");

    napi_property_descriptor descriptors[] = {
      { "type",         NULL, NULL, NULL, NULL, e_type,          napi_enumerable, NULL },
      { "isFullscreen", NULL, NULL, NULL, NULL, e_is_fullscreen, napi_enumerable, NULL },
    };
    status = napi_define_properties(env, event_obj, sizeof(descriptors) / sizeof(descriptors[0]), descriptors);
    NAPI_FATAL_IF_FAILED(status, "ow_event_to_js_object", "napi_define_properties");
    return event_obj;
  }
  else if (event->type == OW_MOVERESIZE) {
    napi_value e_x;
    status = napi_create_int32(env, event->data.moveresize.bounds.x, &e_x);
    NAPI_FATAL_IF_FAILED(status, "ow_event_to_js_object", "napi_create_int32");

    napi_value e_y;
    status = napi_create_int32(env, event->data.moveresize.bounds.y, &e_y);
    NAPI_FATAL_IF_FAILED(status, "ow_event_to_js_object", "napi_create_int32");

    napi_value e_width;
    status = napi_create_uint32(env, event->data.moveresize.bounds.width, &e_width);
    NAPI_FATAL_IF_FAILED(status, "ow_event_to_js_object", "napi_create_uint32");

    napi_value e_height;
    status = napi_create_uint32(env, event->data.moveresize.bounds.height, &e_height);
    NAPI_FATAL_IF_FAILED(status, "ow_event_to_js_object", "napi_create_uint32");

    napi_property_descriptor descriptors[] = {
      { "type",   NULL, NULL, NULL, NULL, e_type,   napi_enumerable, NULL },
      { "x",      NULL, NULL, NULL, NULL, e_x,      napi_enumerable, NULL },
      { "y",      NULL, NULL, NULL, NULL, e_y,      napi_enumerable, NULL },
      { "width",  NULL, NULL, NULL, NULL, e_width,  napi_enumerable, NULL },
      { "height", NULL, NULL, NULL, NULL, e_height, napi_enumerable, NULL },
    };
    status = napi_define_properties(env, event_obj, sizeof(descriptors) / sizeof(descriptors[0]), descriptors);
    NAPI_FATAL_IF_FAILED(status, "ow_event_to_js_object", "napi_define_properties");
    return event_obj;
  }
  else {
    napi_property_descriptor descriptors[] = {
      { "type", NULL, NULL, NULL, NULL, e_type, napi_enumerable, NULL },
    };
    status = napi_define_properties(env, event_obj, sizeof(descriptors) / sizeof(descriptors[0]), descriptors);
    NAPI_FATAL_IF_FAILED(status, "ow_event_to_js_object", "napi_define_properties");
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

  napi_value global;
  status = napi_get_global(env, &global);
  NAPI_FATAL_IF_FAILED(status, "tsfn_to_js_proxy", "napi_get_global");

  status = napi_call_function(env, global, js_callback, 1, &event_obj, NULL);
  NAPI_FATAL_IF_FAILED(status, "tsfn_to_js_proxy", "napi_call_function");

  free(event);
}

napi_value AddonStart(napi_env env, napi_callback_info info) {
  napi_status status;

  size_t info_argc = 3;
  napi_value info_argv[3];
  status = napi_get_cb_info(env, info, &info_argc, info_argv, NULL, NULL);
  NAPI_THROW_IF_FAILED(env, status, NULL);

  // [0] Overlay Window ID
  void* overlay_window_id;
  status = napi_get_buffer_info(env, info_argv[0], &overlay_window_id, NULL);
  NAPI_THROW_IF_FAILED(env, status, NULL);

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
  napi_threadsafe_function threadsafe_fn = NULL;
  status = napi_create_threadsafe_function(env, info_argv[2], NULL, async_resource_name, 0, 1, NULL, NULL, NULL, tsfn_to_js_proxy, &threadsafe_fn);
  NAPI_THROW_IF_FAILED(env, status, NULL);

  struct ow_task_ionut inout = {
    .action = OW_TRACK,
    .track = {
      .app_window_id = overlay_window_id,
      .target_window_title = target_window_title,
      .tsfn = threadsafe_fn
    }
  };
  ow_worker_exec_sync(&inout);

  napi_value handle;
  status = napi_create_uint32(env, inout.handle, &handle);
  NAPI_THROW_IF_FAILED(env, status, NULL);

  return handle;
}

napi_value AddonActivateOverlay(napi_env env, napi_callback_info info) {
  napi_status status;

  size_t info_argc = 1;
  napi_value info_argv[1];
  status = napi_get_cb_info(env, info, &info_argc, info_argv, NULL, NULL);
  NAPI_THROW_IF_FAILED(env, status, NULL);

  // [0] handle
  uint32_t handle;
  status = napi_get_value_uint32(env, info_argv[0], &handle);
  NAPI_THROW_IF_FAILED(env, status, NULL);

  struct ow_task_ionut inout = {
    .action = OW_FOCUS_APP,
    .handle = handle
  };
  ow_worker_exec_sync(&inout);
  return NULL;
}

napi_value AddonStop(napi_env env, napi_callback_info info) {
  napi_status status;

  size_t info_argc = 1;
  napi_value info_argv[1];
  status = napi_get_cb_info(env, info, &info_argc, info_argv, NULL, NULL);
  NAPI_THROW_IF_FAILED(env, status, NULL);

  // [0] handle
  uint32_t handle;
  status = napi_get_value_uint32(env, info_argv[0], &handle);
  NAPI_THROW_IF_FAILED(env, status, NULL);

  struct ow_task_ionut inout = {
    .action = OW_CANCEL_TRACKING,
    .handle = handle
  };
  ow_worker_exec_sync(&inout);

  status = napi_release_threadsafe_function(inout.track.tsfn, napi_tsfn_abort);
  NAPI_THROW_IF_FAILED(env, status, NULL);

  return NULL;
}

napi_value AddonFocusTarget(napi_env env, napi_callback_info info) {
  napi_status status;

  size_t info_argc = 1;
  napi_value info_argv[1];
  status = napi_get_cb_info(env, info, &info_argc, info_argv, NULL, NULL);
  NAPI_THROW_IF_FAILED(env, status, NULL);

  // [0] handle
  uint32_t handle;
  status = napi_get_value_uint32(env, info_argv[0], &handle);
  NAPI_THROW_IF_FAILED(env, status, NULL);

  struct ow_task_ionut inout = {
    .action = OW_FOCUS_TARGET,
    .handle = handle
  };
  ow_worker_exec_sync(&inout);
  return NULL;
}

napi_value AddonScreenshot(napi_env env, napi_callback_info info) {
  napi_status status;

  size_t info_argc = 3;
  napi_value info_argv[3];
  status = napi_get_cb_info(env, info, &info_argc, info_argv, NULL, NULL);
  NAPI_THROW_IF_FAILED(env, status, NULL);

  // [0] handle
  uint32_t handle;
  status = napi_get_value_uint32(env, info_argv[0], &handle);
  NAPI_THROW_IF_FAILED(env, status, NULL);

  // [1] width
  uint32_t last_reported_width;
  status = napi_get_value_uint32(env, info_argv[1], &last_reported_width);
  NAPI_THROW_IF_FAILED(env, status, NULL);

  // [2] height
  uint32_t last_reported_height;
  status = napi_get_value_uint32(env, info_argv[2], &last_reported_height);
  NAPI_THROW_IF_FAILED(env, status, NULL);

  napi_value img_buffer;
  uint8_t* img_data;
  size_t size = last_reported_width * last_reported_height * 4;
  status = napi_create_buffer(env, size, &img_data, &img_buffer);
  NAPI_THROW_IF_FAILED(env, status, NULL);

  struct ow_task_ionut inout = {
    .action = OW_TRACK,
    .screenshot = {
      .out = img_data,
      .width = last_reported_width,
      .height = last_reported_height
    },
    .handle = handle
  };
  ow_worker_exec_sync(&inout);

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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <node_api.h>
#include "napi_helpers.h"
#include "overlay_window.h"

static napi_threadsafe_function threadsafe_fn = NULL;
static struct ow_window_bounds last_reported_bounds = {0, 0, 0, 0};

void ow_emit_event(struct ow_event* event) {
  if (threadsafe_fn == NULL) return;

  struct ow_event* copied_event = malloc(sizeof(struct ow_event));
  memcpy(copied_event, event, sizeof(struct ow_event));

  napi_status status = napi_call_threadsafe_function(threadsafe_fn, copied_event, napi_tsfn_nonblocking);
  if (status == napi_closing) {
    threadsafe_fn = NULL;
    free(copied_event);
    return;
  }
  NAPI_FATAL_IF_FAILED(status, "ow_emit_event", "napi_call_threadsafe_function");
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
  /*
   * Linux/X11 attach/moveresize bounds passed through this boundary are
   * already authoritative X11 virtual-desktop physical pixels (integers).
   * They are exported to JS as-is; no CSS/DIP conversion is applied here.
   */
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
  void* img_data;
  size_t size = last_reported_bounds.width * last_reported_bounds.height * 4;
  status = napi_create_buffer(env, size, &img_data, &img_buffer);
  NAPI_FATAL_IF_FAILED(status, "AddonScreenshot", "napi_create_buffer");

#ifdef _WIN32
  ow_screenshot((uint8_t*)img_data, last_reported_bounds.width, last_reported_bounds.height);
#endif

  return img_buffer;
}

/*
 * Rectangles up to this count are allocated on the stack to avoid a
 * heap allocation in the common case. 50 is a generous upper bound for
 * typical overlay UIs (HUD elements, buttons, panels). Larger arrays
 * fall back to malloc below.
 */
#define OW_INPUT_REGIONS_STACK_MAX 50

/*
 * N-API entry point for setInputRegions(). Validates that the argument is
 * an array of {x, y, width, height} objects, narrows coordinates to the
 * ranges required by xcb_rectangle_t, and forwards to ow_set_input_regions().
 *
 * Rectangles are built on the stack for arrays up to OW_INPUT_REGIONS_STACK_MAX
 * elements; larger arrays use a heap allocation freed before return.
 */
napi_value AddonSetInputRegions(napi_env env, napi_callback_info info) {
  napi_status status;

  size_t info_argc = 1;
  napi_value info_argv[1];
  status = napi_get_cb_info(env, info, &info_argc, info_argv, NULL, NULL);
  NAPI_THROW_IF_FAILED(env, status, NULL);

  bool is_array;
  status = napi_is_array(env, info_argv[0], &is_array);
  NAPI_THROW_IF_FAILED(env, status, NULL);
  if (!is_array) {
    NAPI_THROW(env, NULL, "setInputRegions: argument must be an array", NULL);
  }

  uint32_t count;
  status = napi_get_array_length(env, info_argv[0], &count);
  NAPI_THROW_IF_FAILED(env, status, NULL);

  if (count == 0) {
    ow_set_input_regions(NULL, 0);
    return NULL;
  }

  struct ow_input_rect stack_rects[OW_INPUT_REGIONS_STACK_MAX];
  struct ow_input_rect* rects = stack_rects;
  if (count > OW_INPUT_REGIONS_STACK_MAX) {
    rects = malloc(count * sizeof(struct ow_input_rect));
    if (rects == NULL) {
      NAPI_THROW(env, NULL, "setInputRegions: failed to allocate memory", NULL);
    }
  }

  for (uint32_t i = 0; i < count; i++) {
    napi_value element;
    status = napi_get_element(env, info_argv[0], i, &element);
    if (status != napi_ok) goto cleanup;

    napi_valuetype elem_type;
    status = napi_typeof(env, element, &elem_type);
    if (status != napi_ok) goto cleanup;
    if (elem_type != napi_object) {
      if (rects != stack_rects) free(rects);
      NAPI_THROW(env, NULL, "setInputRegions: each region must be an object", NULL);
    }

    napi_value prop_val;
    int32_t raw_x, raw_y;
    uint32_t raw_w, raw_h;

    status = napi_get_named_property(env, element, "x", &prop_val);
    if (status != napi_ok) goto cleanup;
    status = napi_get_value_int32(env, prop_val, &raw_x);
    if (status != napi_ok) goto cleanup;

    status = napi_get_named_property(env, element, "y", &prop_val);
    if (status != napi_ok) goto cleanup;
    status = napi_get_value_int32(env, prop_val, &raw_y);
    if (status != napi_ok) goto cleanup;

    status = napi_get_named_property(env, element, "width", &prop_val);
    if (status != napi_ok) goto cleanup;
    status = napi_get_value_uint32(env, prop_val, &raw_w);
    if (status != napi_ok) goto cleanup;

    status = napi_get_named_property(env, element, "height", &prop_val);
    if (status != napi_ok) goto cleanup;
    status = napi_get_value_uint32(env, prop_val, &raw_h);
    if (status != napi_ok) goto cleanup;

    /* xcb_rectangle_t fields are int16_t (x, y) and uint16_t (width, height).
     * JS passes int32/uint32, so range-check before narrowing to avoid UB. */
    if (raw_x < INT16_MIN || raw_x > INT16_MAX ||
        raw_y < INT16_MIN || raw_y > INT16_MAX ||
        raw_w > UINT16_MAX || raw_h > UINT16_MAX) {
      if (rects != stack_rects) free(rects);
      NAPI_THROW(env, NULL, "setInputRegions: coordinate value out of int16/uint16 range", NULL);
    }

    rects[i].x = (int16_t)raw_x;
    rects[i].y = (int16_t)raw_y;
    rects[i].width = (uint16_t)raw_w;
    rects[i].height = (uint16_t)raw_h;
  }

  ow_set_input_regions(rects, count);
  if (rects != stack_rects) free(rects);
  return NULL;

cleanup:
  if (rects != stack_rects) free(rects);
  NAPI_THROW_IF_FAILED(env, status, NULL);
  return NULL;
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

  status = napi_create_function(env, NULL, 0, AddonSetInputRegions, NULL, &export_fn);
  NAPI_FATAL_IF_FAILED(status, "NAPI_MODULE_INIT", "napi_create_function");
  status = napi_set_named_property(env, exports, "setInputRegions", export_fn);
  NAPI_FATAL_IF_FAILED(status, "NAPI_MODULE_INIT", "napi_set_named_property");

  status = napi_add_env_cleanup_hook(env, AddonCleanUp, NULL);
  NAPI_FATAL_IF_FAILED(status, "NAPI_MODULE_INIT", "napi_add_env_cleanup_hook");

  return exports;
}

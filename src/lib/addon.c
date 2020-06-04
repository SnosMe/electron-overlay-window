#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <node_api.h>
#include <uv.h>
#include <Windows.h>
#include <stdint.h>
#include "napi_helpers.h"

static napi_threadsafe_function threadsafe_fn = NULL;

struct ow_target_window
{
  char* title;
  HWND hwnd;
  HWINEVENTHOOK location_hook;
  bool is_focused;
};

struct ow_overlay_window
{
  HWND hwnd;
  bool is_focused;
};

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

static struct ow_target_window target_info = {
  .title = NULL,
  .hwnd = NULL,
  .location_hook = NULL,
  .is_focused = false
};

static struct ow_overlay_window overlay_info = {
  .hwnd = NULL,
  .is_focused = false
};

VOID CALLBACK MY_Wineventproc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);

HWINEVENTHOOK register_global_event(DWORD event_id) {
  return SetWinEventHook(
    event_id, event_id,
    NULL,
    MY_Wineventproc,
    0, 0,
    WINEVENT_OUTOFCONTEXT);
}

HWINEVENTHOOK register_window_event(DWORD event_id, DWORD pid) {
  return SetWinEventHook(
    event_id, event_id,
    NULL,
    MY_Wineventproc,
    pid, 0,
    WINEVENT_OUTOFCONTEXT);
}

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

bool getTitle(HWND hwnd, char** title) {
  SetLastError(0);
  int titleLength = GetWindowTextLengthW(hwnd);
  if (titleLength == 0) {
    if (GetLastError() != 0) {
      return false;
    }
    else {
      *title = NULL;
      return true;
    }
  }

  LPWSTR titleUtf16 = malloc(sizeof(WCHAR) * ((size_t)titleLength + 1));
  if (GetWindowTextW(hwnd, titleUtf16, titleLength + 1) == FALSE) {
    free(titleUtf16);
    return false;
  }
  int buffLenUtf8 = WideCharToMultiByte(CP_UTF8, 0, titleUtf16, -1, NULL, 0, NULL, NULL);
  if (buffLenUtf8 == FALSE) {
    free(titleUtf16);
    return false;
  }
  *title = malloc(buffLenUtf8);
  if (WideCharToMultiByte(CP_UTF8, 0, titleUtf16, -1, *title, buffLenUtf8, NULL, NULL) == FALSE) {
    free(titleUtf16);
    free(*title);
    return false;
  }
  return true;
}

bool get_content_bounds(HWND hwnd, struct ow_window_bounds* bounds) {
  RECT rect;
  if (GetClientRect(hwnd, &rect) == FALSE) {
    return false;
  }

  POINT ptClientUL = {
    .x = rect.left,
    .y = rect.top
  };
  if (ClientToScreen(hwnd, &ptClientUL) == FALSE) {
    return false;
  }

  bounds->x = ptClientUL.x;
  bounds->y = ptClientUL.y;
  bounds->width = rect.right;
  bounds->height = rect.bottom;
  return true;
}

bool has_all_process_access(DWORD pid, int* has_access) {
  HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, false, pid);

  if (hProc == NULL) {
    if (GetLastError() == ERROR_ACCESS_DENIED) {
      *has_access = 0;
      return true;
    }
    return false;
  }
  else {
    CloseHandle(hProc);
    *has_access = 1;
    return true;
  }
}

bool adjust_window_size(HWND source_hwnd, HWND target_hwnd) {
  struct ow_window_bounds bounds;
  if (!get_content_bounds(source_hwnd, &bounds)) {
    return false;
  }

  if (bounds.width == 0 || bounds.height == 0) return false;
  // printf("Bounds x=%d y=%d w=%d h=%d\n", bounds.x, bounds.y, bounds.width, bounds.height);

  if (SetWindowPos(
    target_hwnd, 0,
    bounds.x, bounds.y, bounds.width, bounds.height,
    SWP_NOZORDER
  ) == FALSE) {
    return false;
  }

  return true;
}

void check_and_handle_window(HWND hwnd, struct ow_target_window* target_info) {
  if (target_info->hwnd != NULL) {
    if (target_info->hwnd != hwnd) {
      if (hwnd == overlay_info.hwnd) {
        // emit blur, is_overlay_focus: true
        overlay_info.is_focused = true;
      }
      else {
        // emit blur, is_overlay_focus: false, hide overlay
        overlay_info.is_focused = false;
      }

      if (target_info->is_focused) {
        target_info->is_focused = false;
        struct ow_event e = { .type = OW_BLUR };
        ow_emit_event(&e);
      }

      bool targetIsClosed = (IsWindow(target_info->hwnd) == FALSE);
      if (targetIsClosed) {
        SetWindowPos(overlay_info.hwnd, 0, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_HIDEWINDOW);
        // ^^^^^^^^^^^
        target_info->hwnd = NULL;
        struct ow_event e = { .type = OW_DETACH };
        ow_emit_event(&e);
      }
    }
    else if (target_info->hwnd == hwnd) {
      if (!target_info->is_focused) {
        target_info->is_focused = true;
        struct ow_event e = { .type = OW_FOCUS };
        ow_emit_event(&e);
      }
      return;
    }
  }

  char* title = NULL;
  if (!getTitle(hwnd, &title) || title == NULL) {
    return;
  }
  bool is_equal = (strcmp(title, target_info->title) == 0);
  free(title);
  if (!is_equal) {
    return;
  }

  target_info->hwnd = hwnd;
  // https://github.com/electron/electron/blob/8de06f0c571bc24e4230063e3ef0428390df773e/shell/browser/native_window_views.cc#L1065
  SetWindowLongPtrW(overlay_info.hwnd, GWLP_HWNDPARENT, (LONG_PTR)target_info->hwnd);
  // bool failed_to_set_parent = (GetLastError() == ERROR_INVALID_PARAMETER); // elevated target window

  DWORD pid = 0;
  GetWindowThreadProcessId(target_info->hwnd, &pid);

  int has_access = -1;
  if (pid != 0) {
    has_all_process_access(pid, &has_access);
  }

  if (target_info->location_hook != NULL) {
    UnhookWinEvent(target_info->location_hook);
  }
  target_info->location_hook = register_window_event(EVENT_OBJECT_LOCATIONCHANGE, pid);
  adjust_window_size(target_info->hwnd, overlay_info.hwnd);

  SetWindowPos(overlay_info.hwnd, 0, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
  BringWindowToTop(overlay_info.hwnd);
  
  AttachThreadInput(GetWindowThreadProcessId(target_info->hwnd, NULL), GetWindowThreadProcessId(overlay_info.hwnd, NULL), FALSE);

  // LONG ex_style = GetWindowLong(overlay_info.hwnd, GWL_EXSTYLE);
  // if (ex_style & WS_EX_LAYERED) {
  //   SetWindowLong(overlay_info.hwnd, GWL_EXSTYLE, ex_style & ~WS_EX_LAYERED);
  //   SetWindowLong(overlay_info.hwnd, GWL_EXSTYLE, ex_style);
  // }

  struct ow_event e = {
    .type = OW_ATTACH,
    .data.attach = {
      .pid = pid,
      .has_access = has_access
    }
  };
  ow_emit_event(&e);
  target_info->is_focused = true;
  e.type = OW_FOCUS;
  ow_emit_event(&e);
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
    napi_value e_pid;
    status = napi_create_uint32(env, event->data.attach.pid, &e_pid);
    NAPI_FATAL_IF_FAILED(status, "ow_event_to_js_object", "napi_create_uint32");

    napi_value e_has_access;
    if (event->data.attach.has_access == -1) {
      status = napi_get_undefined(env, &e_has_access);
      NAPI_FATAL_IF_FAILED(status, "ow_event_to_js_object", "napi_get_undefined");
    }
    else {
      status = napi_get_boolean(env, event->data.attach.has_access == 1, &e_has_access);
      NAPI_FATAL_IF_FAILED(status, "ow_event_to_js_object", "napi_get_boolean");
    }

    napi_property_descriptor descriptors[] = {
      { "type",      NULL, NULL, NULL, NULL, e_type,       napi_enumerable, NULL },
      { "pid",       NULL, NULL, NULL, NULL, e_pid,        napi_enumerable, NULL },
      { "hasAccess", NULL, NULL, NULL, NULL, e_has_access, napi_enumerable, NULL },
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

  napi_status status;

  napi_value event_obj = ow_event_to_js_object(env, event);

  napi_value global;
  status = napi_get_global(env, &global);
  NAPI_FATAL_IF_FAILED(status, "tsfn_to_js_proxy", "napi_get_global");

  status = napi_call_function(env, global, js_callback, 1, &event_obj, NULL);
  NAPI_FATAL_IF_FAILED(status, "tsfn_to_js_proxy", "napi_call_function");

  free(event);
}

VOID CALLBACK MY_Wineventproc(
  HWINEVENTHOOK hWinEventHook,
  DWORD event,
  HWND hwnd,
  LONG idObject,
  LONG idChild,
  DWORD idEventThread,
  DWORD dwmsEventTime
) {
  // window hook
  if (event == EVENT_OBJECT_LOCATIONCHANGE) {
    if (idObject == OBJID_WINDOW) {
      adjust_window_size(target_info.hwnd, overlay_info.hwnd);
    }
    return;
  }
  // system hook
  if (event == EVENT_OBJECT_NAMECHANGE) {
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    if (hwnd != GetForegroundWindow()) return;
  }
  if (
    event == EVENT_OBJECT_NAMECHANGE ||
    event == EVENT_SYSTEM_FOREGROUND ||
    event == EVENT_SYSTEM_MINIMIZEEND
    ) {
    // printf("EVENT hwnd=%p idObject=%d idChild=%d, fg=%p\n", hwnd, idObject, idChild, GetForegroundWindow());

    check_and_handle_window(hwnd, &target_info);
  }
}

static uv_thread_t tid = NULL;
void hookThread(void* arg) {
  register_global_event(EVENT_OBJECT_NAMECHANGE);
  register_global_event(EVENT_SYSTEM_FOREGROUND);
  register_global_event(EVENT_SYSTEM_MINIMIZEEND);

  check_and_handle_window(GetForegroundWindow(), &target_info);

  MSG message;
  while (GetMessageW(&message, (HWND)NULL, 0, 0) != FALSE) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
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
  status = napi_create_threadsafe_function(env, info_argv[2], NULL, async_resource_name, 0, 1, NULL, NULL, NULL, tsfn_to_js_proxy, &threadsafe_fn);
  NAPI_THROW_IF_FAILED(env, status, NULL);

  printf("start(hwnd=%x, title=\"%s\")\n", *((int*)overlay_window_id), target_window_title);
  overlay_info.hwnd = *((HWND*)overlay_window_id);
  target_info.title = target_window_title;
  uv_thread_create(&tid, hookThread, NULL);

  return NULL;
}

napi_value AddonActivateOverlay(napi_env _env, napi_callback_info _info) {
  
  DWORD lockTimeOutOld = 0;
  DWORD lockTimeOutNew = 0;
  //SystemParametersInfo(SPI_GETFOREGROUNDLOCKTIMEOUT, 0, &lockTimeOutOld, 0);
  //SystemParametersInfo(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, &lockTimeOutNew, 0);

  // so use this
  SetForegroundWindow(overlay_info.hwnd);

  //SystemParametersInfo(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, &lockTimeOutOld, 0);

  return NULL;
}

napi_value AddondFocusTarget(napi_env env, napi_callback_info info) {
  return NULL;
}

void AddonCleanUp(void* arg) {
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

  status = napi_create_function(env, NULL, 0, AddondFocusTarget, NULL, &export_fn);
  NAPI_FATAL_IF_FAILED(status, "NAPI_MODULE_INIT", "napi_create_function");
  status = napi_set_named_property(env, exports, "focusTarget", export_fn);
  NAPI_FATAL_IF_FAILED(status, "NAPI_MODULE_INIT", "napi_set_named_property");

  status = napi_add_env_cleanup_hook(env, AddonCleanUp, NULL);
  NAPI_FATAL_IF_FAILED(status, "NAPI_MODULE_INIT", "napi_add_env_cleanup_hook");

  return exports;
}

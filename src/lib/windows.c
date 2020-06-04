#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <Windows.h>
#include "overlay_window.h"

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
};

static struct ow_target_window target_info = {
  .title = NULL,
  .hwnd = NULL,
  .location_hook = NULL,
  .is_focused = false
};

static struct ow_overlay_window overlay_info = {
  .hwnd = NULL
};

VOID CALLBACK hook_proc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);

HWINEVENTHOOK register_global_event(DWORD event_id) {
  return SetWinEventHook(
    event_id, event_id,
    NULL,
    hook_proc,
    0, 0,
    WINEVENT_OUTOFCONTEXT);
}

HWINEVENTHOOK register_window_event(DWORD event_id, DWORD pid) {
  return SetWinEventHook(
    event_id, event_id,
    NULL,
    hook_proc,
    pid, 0,
    WINEVENT_OUTOFCONTEXT);
}

bool get_title(HWND hwnd, char** title) {
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
  if (!get_title(hwnd, &title) || title == NULL) {
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

VOID CALLBACK hook_proc(
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

void hook_thread(void* _arg) {
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

void ow_start_hook(char* target_window_title, void* overlay_window_id) {
  target_info.title = target_window_title;
  overlay_info.hwnd = *((HWND*)overlay_window_id);
  uv_thread_create(&hook_tid, hook_thread, NULL);
}

void ow_activate_overlay() {
  SetForegroundWindow(overlay_info.hwnd);
}

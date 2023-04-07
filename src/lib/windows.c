#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <Windows.h>
#include <oleacc.h>
#include "overlay_window.h"

#define OW_FOREGROUND_TIMER_MS 83 // 12 fps

struct ow_target_window
{
  char* title;
  HWND hwnd;
  HWINEVENTHOOK location_hook;
  HWINEVENTHOOK destroy_hook;
  bool is_focused;
  bool is_destroyed;
};

struct ow_overlay_window
{
  HWND hwnd;
};

static HWND foreground_window = NULL;
static HWINEVENTHOOK fg_window_namechange_hook = NULL;
static UINT WM_OVERLAY_UIPI_TEST = WM_NULL;

static struct ow_target_window target_info = {
  .title = NULL,
  .hwnd = NULL,
  .location_hook = NULL,
  .destroy_hook = NULL,
  .is_focused = false,
  .is_destroyed = false
};

static struct ow_overlay_window overlay_info = {
  .hwnd = NULL
};

static VOID CALLBACK hook_proc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);

static bool has_uipi_access(HWND hwnd) {
  SetLastError(ERROR_SUCCESS);
  PostMessage(hwnd, WM_OVERLAY_UIPI_TEST, 0, 0);
  return GetLastError() != ERROR_ACCESS_DENIED;
}

static bool get_title(HWND hwnd, char** title) {
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

static bool get_content_bounds(HWND hwnd, struct ow_window_bounds* bounds) {
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

static bool MSAA_check_window_focused_state(HWND hwnd) {
  HRESULT hr;
  IAccessible* pAcc = NULL;
  VARIANT varChildSelf;
  VariantInit(&varChildSelf);
  hr = AccessibleObjectFromEvent(hwnd, OBJID_WINDOW, CHILDID_SELF, &pAcc, &varChildSelf);
  if (hr != S_OK || pAcc == NULL) {
    VariantClear(&varChildSelf);
    return false;
  }
  VARIANT varState;
  VariantInit(&varState);
  hr = pAcc->lpVtbl->get_accState(pAcc, varChildSelf, &varState);

  bool is_focused = false;
  if (hr == S_OK && varState.vt == VT_I4) {
    is_focused = (varState.lVal & STATE_SYSTEM_FOCUSED);
  }
  VariantClear(&varState);
  VariantClear(&varChildSelf);
  pAcc->lpVtbl->Release(pAcc);
  return is_focused;
}

static void handle_movesize_event(struct ow_target_window* target_info) {
  struct ow_window_bounds bounds;
  if (get_content_bounds(target_info->hwnd, &bounds)) {
    struct ow_event e = {
      .type = OW_MOVERESIZE,
      .data.moveresize = {
        .bounds = bounds
      }
    };
    ow_emit_event(&e);
  }
}

static void check_and_handle_window(HWND hwnd, struct ow_target_window* target_info) {
  // ignore fake ghost windows
  if (IsHungAppWindow(hwnd)) {
    return;
  }

  if (target_info->hwnd != NULL) {
    if (target_info->hwnd != hwnd) {
      if (target_info->is_focused) {
        target_info->is_focused = false;
        struct ow_event e = { .type = OW_BLUR };
        ow_emit_event(&e);
      }

      if (target_info->is_destroyed) {
        target_info->hwnd = NULL;
        target_info->is_destroyed = false;
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

  if (target_info->hwnd != NULL) {
    UnhookWinEvent(target_info->location_hook);
    UnhookWinEvent(target_info->destroy_hook);
  }

  target_info->hwnd = hwnd;

  DWORD pid;
  DWORD threadId = GetWindowThreadProcessId(target_info->hwnd, &pid);
  if (threadId == 0) {
    return;
  }

  target_info->location_hook = SetWinEventHook(
    EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
    NULL, hook_proc, 0, threadId,
    WINEVENT_OUTOFCONTEXT);
  target_info->destroy_hook = SetWinEventHook(
    EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY,
    NULL, hook_proc, 0, threadId,
    WINEVENT_OUTOFCONTEXT);

  struct ow_event e = {
    .type = OW_ATTACH,
    .data.attach = {
      .has_access = -1,
      .is_fullscreen = -1
    }
  };
  e.data.attach.has_access = has_uipi_access(target_info->hwnd);
  if (get_content_bounds(target_info->hwnd, &e.data.attach.bounds)) {
    // emit OW_ATTACH
    ow_emit_event(&e);

    target_info->is_focused = true;
    e.type = OW_FOCUS;
    ow_emit_event(&e);
  }
  else {
    // something went wrong, did the target window die right after becoming active?
    target_info->hwnd = NULL;
  }
}

void handle_new_foreground(HWND hwnd) {
  foreground_window = hwnd;

  if (fg_window_namechange_hook != NULL) {
    UnhookWinEvent(fg_window_namechange_hook);
    fg_window_namechange_hook = NULL;
  }
  if (foreground_window != NULL && foreground_window != target_info.hwnd) {
    fg_window_namechange_hook = SetWinEventHook(
      EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE,
      NULL, hook_proc, 0, GetWindowThreadProcessId(foreground_window, NULL),
      WINEVENT_OUTOFCONTEXT);
  }
  check_and_handle_window(foreground_window, &target_info);
}

static VOID CALLBACK hook_proc(
  HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild,
  DWORD idEventThread, DWORD dwmsEventTime
) {
  /* char* e_str =
    event == EVENT_SYSTEM_FOREGROUND ? "SYS_FOREGROUND"
    : event == EVENT_SYSTEM_MINIMIZEEND ? "SYS_MINIMIZEEND"
    : event == EVENT_OBJECT_NAMECHANGE ? "OBJ_NAMECHANGE"
    : event == EVENT_OBJECT_LOCATIONCHANGE ? "OBJ_LOCATIONCHANGE"
    : event == EVENT_OBJECT_DESTROY ? "OBJ_DESTROY"
    : "(unknown)";
  printf("[%d] %s hwnd=%p idObject=%d idChild=%d\n", dwmsEventTime, e_str, hwnd, idObject, idChild); */

  if (event == EVENT_OBJECT_DESTROY) {
    if (hwnd == target_info.hwnd && idObject == OBJID_WINDOW && idChild == CHILDID_SELF) {
      target_info.is_destroyed = true;
      check_and_handle_window(NULL, &target_info);
    }
    return;
  }
  if (event == EVENT_OBJECT_LOCATIONCHANGE) {
    if (hwnd == target_info.hwnd && idObject == OBJID_WINDOW && idChild == CHILDID_SELF) {
      handle_movesize_event(&target_info);
    }
    return;
  }
  if (event == EVENT_OBJECT_NAMECHANGE) {
    if (hwnd == foreground_window && idObject == OBJID_WINDOW && idChild == CHILDID_SELF) {
      check_and_handle_window(foreground_window, &target_info);
    }
    return;
  }
  if (event == EVENT_SYSTEM_FOREGROUND || event == EVENT_SYSTEM_MINIMIZEEND) {
    // checks if window is really gained focus
    // REASON: if multiple foreground windows switching too fast in short period,
    //         Windows sends EVENT_SYSTEM_FOREGROUND for them but MAY NOT actually
    //         focus window, so the focus is left on previous foreground window,
    //         but from the point of hook we think that focus is changed.
    if (GetForegroundWindow() == hwnd) {
      // printf("[1] EVENT_SYSTEM_FOREGROUND: OK, hwnd == GetForegroundWindow\n");
    } else {
      if (MSAA_check_window_focused_state(hwnd)) {
        // printf("[2] EVENT_SYSTEM_FOREGROUND: OK, GetForegroundWindow corrected by MSAA\n");
      } else {
        // printf("[2] EVENT_SYSTEM_FOREGROUND: FALSE POSITIVE\n");

        return;
      }
    }
    // check passed, continue normally

    handle_new_foreground(hwnd);
    return;
  }
}

static VOID CALLBACK foreground_timer_proc(HWND _hwnd, UINT msg, UINT_PTR timerId, DWORD dwmsEventTime)
{
  HWND system_foreground = GetForegroundWindow();

  if (
    foreground_window != system_foreground &&
    MSAA_check_window_focused_state(system_foreground)
  ) {
    // printf("WM_TIMER: Foreground changed\n");
    handle_new_foreground(system_foreground);
  }
}

static void hook_thread(void* _arg) {
  SetWinEventHook(
    EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
    NULL, hook_proc, 0, 0, WINEVENT_OUTOFCONTEXT);
  SetWinEventHook(
    EVENT_SYSTEM_MINIMIZEEND, EVENT_SYSTEM_MINIMIZEEND,
    NULL, hook_proc, 0, 0, WINEVENT_OUTOFCONTEXT);
  // FIXES: ForegroundLockTimeout (even when = 0); Also edge cases when apps stealing FG window.
  // NOTE:  Using timer because WH_SHELL & WH_CBT hooks require dll injection
  SetTimer(NULL, 0, OW_FOREGROUND_TIMER_MS, foreground_timer_proc);

  foreground_window = GetForegroundWindow();
  if (foreground_window != NULL) {
    fg_window_namechange_hook = SetWinEventHook(
      EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE,
      NULL, hook_proc, 0, GetWindowThreadProcessId(foreground_window, NULL),
      WINEVENT_OUTOFCONTEXT);
    check_and_handle_window(foreground_window, &target_info);
  }

  MSG message;
  while (GetMessageW(&message, (HWND)NULL, 0, 0) != FALSE) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
}

void ow_start_hook(char* target_window_title, void* overlay_window_id) {
  target_info.title = target_window_title;
  if (overlay_window_id != NULL) {
    overlay_info.hwnd = *((HWND*)overlay_window_id);
  }
  WM_OVERLAY_UIPI_TEST = RegisterWindowMessage("ELECTRON_OVERLAY_UIPI_TEST");
  uv_thread_create(&hook_tid, hook_thread, NULL);
}

void ow_activate_overlay() {
  SetForegroundWindow(overlay_info.hwnd);
}

void ow_focus_target() {
  SetForegroundWindow(target_info.hwnd);
}

void ow_screenshot(uint8_t* out, uint32_t width, uint32_t height) {
  POINT screenPos = {0, 0};
  ClientToScreen(target_info.hwnd, &screenPos);

  BITMAPINFOHEADER bi;
  bi.biSize = sizeof(BITMAPINFOHEADER);
  bi.biWidth = width;
  bi.biHeight = -((int32_t)height); // top-down DIB
  bi.biPlanes = 1;
  bi.biBitCount = 32;
  bi.biCompression = BI_RGB;
  bi.biSizeImage = (width * height * 4);

  HDC dcSrc = GetDC(GetDesktopWindow());
  HDC dcDest = CreateCompatibleDC(dcSrc);
  uint8_t* bmpData;
  HBITMAP bmp = CreateDIBSection(dcSrc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bmpData, NULL, 0);
  SelectObject(dcDest, bmp);
  BitBlt(dcDest, 0, 0, width, height, dcSrc, screenPos.x, screenPos.y, SRCCOPY);

  memcpy(out, bmpData, bi.biSizeImage);

  DeleteDC(dcDest);
  ReleaseDC(target_info.hwnd, dcSrc);
  DeleteObject(bmp);
}

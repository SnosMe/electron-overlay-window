#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <optional>
#include <algorithm>
#include <Windows.h>
#include <oleacc.h>
#include "napi_helpers.h"
#include "overlay_window.h"

#define OW_FOREGROUND_TIMER_MS 83 // 12 fps
#define WM_USER_TASK (WM_USER + 1)

static VOID CALLBACK hook_proc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
static auto has_uipi_access(HWND hwnd) -> bool;
static auto getWindowTitle(HWND hwnd) -> std::optional<LPWSTR>;
static auto getContentBounds(HWND hwnd) -> std::optional<ow_window_bounds>;
static auto MSAA_check_window_focused_state(HWND hwnd) -> bool;
static auto hook_thread(void* _arg) -> void;

struct TrackingTarget {
  LPWSTR byTitle;
  HWND hwnd = NULL;
  HWINEVENTHOOK locationHook = NULL;
  HWINEVENTHOOK destroyHook = NULL;
  bool isFocused = false;
  bool justDestroyed = false;
  HWND appHwnd;
  napi_threadsafe_function threadsafeFn;
  uint32_t id;

  auto deinit() -> void {
    if (this->hwnd != NULL) {
      UnhookWinEvent(this->locationHook);
      UnhookWinEvent(this->destroyHook);
    }
    free(this->byTitle);
  }

  auto emitAsyncEvent(ow_event e) -> void {
    ow_emit_async_event(e, this->threadsafeFn);
  }

  auto testWindow(HWND hwnd, std::optional<LPWSTR> title) -> void {
    if (this->hwnd != NULL) {
      if (this->hwnd != hwnd) {
        if (this->isFocused) {
          this->isFocused = false;
          this->emitAsyncEvent({ .type = OW_BLUR });
        }

        if (this->justDestroyed) {
          this->hwnd = NULL;
          this->justDestroyed = false;
          this->emitAsyncEvent({ .type = OW_DETACH });
        }
      }
      else if (this->hwnd == hwnd) {
        if (!this->isFocused) {
          this->isFocused = true;
          this->emitAsyncEvent({ .type = OW_FOCUS });
        }
        return;
      }
    }

    if (!title) return;
    bool titleMatches = (wcscmp(title.value(), this->byTitle) == 0);
    if (!titleMatches) return;

    if (this->hwnd != NULL) {
      UnhookWinEvent(this->locationHook);
      UnhookWinEvent(this->destroyHook);
    }

    this->hwnd = hwnd;

    DWORD pid;
    DWORD threadId = GetWindowThreadProcessId(this->hwnd, &pid);
    if (threadId == 0) {
      return;
    }

    this->locationHook = SetWinEventHook(
      EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
      NULL, hook_proc, 0, threadId,
      WINEVENT_OUTOFCONTEXT);
    this->destroyHook = SetWinEventHook(
      EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY,
      NULL, hook_proc, 0, threadId,
      WINEVENT_OUTOFCONTEXT);

    ow_event e{
      .type = OW_ATTACH,
      .data = {
        .attach = {
          .has_access = -1,
          .is_fullscreen = -1
        }
      }
    };
    e.data.attach.has_access = has_uipi_access(this->hwnd);
    auto bounds = getContentBounds(this->hwnd);
    if (bounds) {
      e.data.attach.bounds = bounds.value();
      // emit OW_ATTACH
      this->emitAsyncEvent(e);

      this->isFocused = true;
      this->emitAsyncEvent({ .type = OW_FOCUS });
    }
    else {
      // something went wrong, did the target window die right after becoming active?
      this->hwnd = NULL;
    }
  }

  auto handleMoveresize() -> void {
    auto bounds = getContentBounds(this->hwnd);
    if (bounds) {
      this->emitAsyncEvent({
        .type = OW_MOVERESIZE,
        .data = {
          .moveresize = {
            .bounds = bounds.value()
          }
        }
      });
    }
  }

  auto screenshot(uint8_t* out, uint32_t width, uint32_t height) -> void {
    POINT screenPos = {0, 0};
    ClientToScreen(this->hwnd, &screenPos);

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
    HBITMAP bmp = CreateDIBSection(dcSrc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, (void**)&bmpData, NULL, 0);
    SelectObject(dcDest, bmp);
    BitBlt(dcDest, 0, 0, width, height, dcSrc, screenPos.x, screenPos.y, SRCCOPY);

    memcpy(out, bmpData, bi.biSizeImage);

    DeleteDC(dcDest);
    ReleaseDC(this->hwnd, dcSrc);
    DeleteObject(bmp);
  }
};

struct WindowTracker {
  bool isCreated = false;
  UINT WM_OVERLAY_UIPI_TEST = WM_NULL;
  DWORD threadId;
  uv_sem_t taskSem;

  HWND foregroundWindow = NULL;
  HWINEVENTHOOK namechangeHook = NULL;

  uint32_t nextId = 1;
  std::vector<TrackingTarget> tracking = {};

  auto handleNameChange() -> void {
    auto title = getWindowTitle(this->foregroundWindow);
    for (auto& t : this->tracking) {
      t.testWindow(this->foregroundWindow, title);
    }
    if (title) {
      free(title.value());
    }
  }

  auto trackByTitle(char* title, HWND appHwnd, napi_threadsafe_function tsfn) -> uint32_t {
    LPWSTR titleUtf16;
    {
      int lenZCodeunits = MultiByteToWideChar(CP_UTF8, 0, title, -1, NULL, 0);
      if (lenZCodeunits == FALSE) NAPI_FATAL("trackByTitle", "MultiByteToWideChar1");

      titleUtf16 = (LPWSTR)malloc(sizeof(WCHAR) * lenZCodeunits);
      if (!titleUtf16) NAPI_FATAL("trackByTitle", "malloc");

      if (MultiByteToWideChar(CP_UTF8, 0, title, -1, titleUtf16, lenZCodeunits) == FALSE) {
        free(titleUtf16);
        NAPI_FATAL("trackByTitle", "MultiByteToWideChar2");
      }
    }

    auto target = TrackingTarget{
      .byTitle = titleUtf16,
      .appHwnd = appHwnd,
      .threadsafeFn = tsfn,
      .id = this->nextId
    };
    this->nextId += 1;
    this->tracking.push_back(target);
    this->handleNameChange();
    return target.id;
  }

  auto updateFgWindow(HWND hwnd) -> void {
    // ignore fake ghost windows
    if (IsHungAppWindow(hwnd)) return;

    this->foregroundWindow = hwnd;

    if (this->namechangeHook != NULL) {
      UnhookWinEvent(this->namechangeHook);
      this->namechangeHook = NULL;
    }
    if (this->foregroundWindow) {
      this->namechangeHook = SetWinEventHook(
        EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE,
        NULL, hook_proc, 0, GetWindowThreadProcessId(this->foregroundWindow, NULL),
        WINEVENT_OUTOFCONTEXT);
    }

    this->handleNameChange();
  }

  auto findTargetByHwnd(HWND hwnd) -> std::optional<TrackingTarget*> {
    if (hwnd == NULL) return std::nullopt;
    for (auto& t : this->tracking) {
      if (t.hwnd == hwnd) return &t;
    }
    return std::nullopt;
  }

  auto findByHandle(uint32_t handle) -> std::optional<TrackingTarget*> {
    for (auto& t : this->tracking) {
      if (t.id == handle) return &t;
    }
    return std::nullopt;
  }

  auto cancelTracking(uint32_t handle) -> napi_threadsafe_function {
    for (auto it = this->tracking.begin(); it != this->tracking.end();) {
      if (it->id == handle) {
        auto tsfn = it->threadsafeFn;
        it->deinit();
        it = this->tracking.erase(it);
        return tsfn;
      } else {
        ++it;
      }
    }
    NAPI_FATAL("cancelTracking", "invalid handle");
  }
} tracker;

auto ow_worker_exec_sync(ow_task_ionut* data) -> void {
  if (!tracker.isCreated) {
    uv_sem_init(&tracker.taskSem, 0);
    tracker.WM_OVERLAY_UIPI_TEST = RegisterWindowMessage("ELECTRON_OVERLAY_UIPI_TEST");
    uv_thread_create(&hook_tid, hook_thread, NULL);
    uv_sem_wait(&tracker.taskSem);
    tracker.isCreated = true;
  }

  PostThreadMessage(tracker.threadId, WM_USER_TASK, (WPARAM)data, (LPARAM)NULL);
  uv_sem_wait(&tracker.taskSem);
}

static auto has_uipi_access(HWND hwnd) -> bool {
  SetLastError(ERROR_SUCCESS);
  PostMessage(hwnd, tracker.WM_OVERLAY_UIPI_TEST, 0, 0);
  return GetLastError() != ERROR_ACCESS_DENIED;
}

static auto getWindowTitle(HWND hwnd) -> std::optional<LPWSTR> {
  SetLastError(ERROR_SUCCESS);
  int lenCodeunits = GetWindowTextLengthW(hwnd);
  if (lenCodeunits == 0) {
    if (GetLastError() != ERROR_SUCCESS) return std::nullopt;
    // return NULL;
    return std::nullopt;
  }
  size_t lenZCodeunits = (size_t)lenCodeunits + 1;
  auto title = (LPWSTR)malloc(sizeof(WCHAR) * lenZCodeunits);
  if (!title) return std::nullopt;

  if (GetWindowTextW(hwnd, title, lenZCodeunits) == FALSE) {
    free(title);
    return std::nullopt;
  }
  return title;
}

static auto getContentBounds(HWND hwnd) -> std::optional<ow_window_bounds> {
  RECT rect;
  if (GetClientRect(hwnd, &rect) == FALSE) {
    return std::nullopt;
  }

  POINT ptClientUL = {
    .x = rect.left,
    .y = rect.top
  };
  if (ClientToScreen(hwnd, &ptClientUL) == FALSE) {
    return std::nullopt;
  }

  return ow_window_bounds{
    .x = ptClientUL.x,
    .y = ptClientUL.y,
    .width = (uint32_t)rect.right,
    .height = (uint32_t)rect.bottom
  };
}

static auto MSAA_check_window_focused_state(HWND hwnd) -> bool {
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
  hr = pAcc->get_accState(varChildSelf, &varState);

  bool is_focused = false;
  if (hr == S_OK && varState.vt == VT_I4) {
    is_focused = (varState.lVal & STATE_SYSTEM_FOCUSED);
  }
  VariantClear(&varState);
  VariantClear(&varChildSelf);
  pAcc->Release();
  return is_focused;
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
    tracker.updateFgWindow(hwnd);
    return;
  }

  if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) {
    return;
  }

  if (event == EVENT_OBJECT_NAMECHANGE) {
    if (hwnd == tracker.foregroundWindow) {
      tracker.handleNameChange();
    }
    return;
  }

  if (event == EVENT_OBJECT_DESTROY) {
    auto target = tracker.findTargetByHwnd(hwnd);
    if (target) {
      target.value()->justDestroyed = true;
      target.value()->testWindow(NULL, std::nullopt);
    }
  } else if (event == EVENT_OBJECT_LOCATIONCHANGE) {
    auto target = tracker.findTargetByHwnd(hwnd);
    if (target) {
      target.value()->handleMoveresize();
    }
  }
}

static void CALLBACK foreground_timer_proc(HWND _hwnd, UINT _msg, UINT_PTR _timerId, DWORD _dwmsEventTime)
{
  HWND realForeground = GetForegroundWindow();
  if (
    tracker.foregroundWindow != realForeground &&
    MSAA_check_window_focused_state(realForeground)
  ) {
    // printf("WM_TIMER: Foreground changed\n");
    tracker.updateFgWindow(realForeground);
  }
}

static auto hook_thread(void* _arg) -> void {
  // trigger creation of message queue for this thread
  GetQueueStatus(QS_POSTMESSAGE);
  tracker.threadId = GetThreadId(GetCurrentThread());
  // now we can post events to it
  uv_sem_post(&tracker.taskSem);

  SetWinEventHook(
    EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
    NULL, hook_proc, 0, 0, WINEVENT_OUTOFCONTEXT);
  SetWinEventHook(
    EVENT_SYSTEM_MINIMIZEEND, EVENT_SYSTEM_MINIMIZEEND,
    NULL, hook_proc, 0, 0, WINEVENT_OUTOFCONTEXT);
  // FIXES: ForegroundLockTimeout (even when = 0); Also edge cases when apps stealing FG window.
  // NOTE:  Using timer because WH_SHELL & WH_CBT hooks require dll injection
  SetTimer(NULL, 0, OW_FOREGROUND_TIMER_MS, foreground_timer_proc);

  MSG message;
  while (GetMessageW(&message, (HWND)NULL, 0, 0) != FALSE) {
    if (message.message == WM_USER_TASK) {
      // TODO some logic
      auto inout = (ow_task_ionut*)message.wParam;
      if (inout->action == OW_TRACK) {
        inout->handle = tracker.trackByTitle(
          inout->track.target_window_title,
          *((HWND*)inout->track.app_window_id),
          inout->track.tsfn
        );
        free(inout->track.target_window_title);
      } else if (inout->action == OW_CANCEL_TRACKING) {
        inout->track.tsfn = tracker.cancelTracking(inout->handle);
      } else {
        auto target = tracker.findByHandle(inout->handle);
        if (!target) NAPI_FATAL("hook_thread", "invalid handle");
        // TODO
      }

      uv_sem_post(&tracker.taskSem);
    } else {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
}

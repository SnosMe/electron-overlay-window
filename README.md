# electron-overlay-window

[![](https://img.shields.io/npm/v/electron-overlay-window/latest?color=CC3534&label=electron-overlay-window&logo=npm&labelColor=212121)](https://www.npmjs.com/package/electron-overlay-window)

Library for creating overlay windows, intended to complement Electron.

Responsible for:
  - Finding target window by title
  - Keeping position and size of overlay window with target in sync
  - Emits lifecycle events

![npm run demo:electron](https://i.imgur.com/Ej190zc.gif)

Important notes:
  - You can initialize library only once (Electron window must never die, and title by which target window is searched cannot be changed)
  - You can have only one overlay window
  - Found target window remains "valid" even if its title has changed
  - Correct behavior is guaranteed only for top-level windows *(A top-level window is a window that is not a child window, or has no parent window (which is the same as having the "desktop window" as a parent))*
  - X11: library relies on EWHM, more specifically `_NET_ACTIVE_WINDOW`, `_NET_WM_STATE_FULLSCREEN`, `_NET_WM_NAME`
  - KDE Plasma on Wayland: the overlay window itself still runs on XWayland (Electron's
    default), but the *target* window is found/tracked through KWin's D-Bus scripting
    API (`org.kde.KWin`) instead of X11/EWMH, so target apps that have no XWayland
    window at all (native-Wayland GTK4/Qt6 apps, Wine/Proton games using the Wayland
    driver, etc.) can be attached to. This only applies when running under a KDE Plasma
    Wayland session (detected via `WAYLAND_DISPLAY` + a live `org.kde.KWin` D-Bus name);
    plain X11 sessions are unaffected. Requires `dbus-1` development headers and
    `pkg-config` at build time on Linux, in addition to `libxcb`.

Supported backends:
  - Windows (7 - 10)
  - Linux (X11, and target-window tracking on KDE Plasma Wayland sessions)

Recommended dev utils
- Windows: AccEvent (accevent.exe) and Inspect Object (inspect.exe) from Windows SDK
- X11: xwininfo, xprop, xev

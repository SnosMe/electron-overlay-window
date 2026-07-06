#ifndef ADDON_SRC_KDE_WAYLAND_H_
#define ADDON_SRC_KDE_WAYLAND_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// True only when running under a KDE Plasma Wayland session where the
// KWin scripting D-Bus API (org.kde.KWin /Scripting) is reachable.
bool ow_kde_wayland_is_available(void);

// Starts a background thread that loads a persistent KWin script to find
// and track the target window (by title) via workspace signals, forwarding
// ow_event's the same way the X11 backend does.
void ow_kde_wayland_start_hook(const char* target_window_title);

// Activates/focuses the currently tracked target window, using the last
// internalId reported by the tracking script.
void ow_kde_wayland_focus_target(void);

// Best-effort cleanup of the loaded KWin script.
void ow_kde_wayland_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // !ADDON_SRC_KDE_WAYLAND_H_

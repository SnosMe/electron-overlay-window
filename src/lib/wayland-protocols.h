#ifndef WAYLAND_PROTOCOLS_H
#define WAYLAND_PROTOCOLS_H

#include <wayland-client.h>

// KDE Plasma Window Management Protocol
// Based on: https://github.com/KDE/plasma-wayland-protocols

#define ORG_KDE_PLASMA_WINDOW_STATE_ACTIVE 1
#define ORG_KDE_PLASMA_WINDOW_STATE_MINIMIZED 2
#define ORG_KDE_PLASMA_WINDOW_STATE_MAXIMIZED 4
#define ORG_KDE_PLASMA_WINDOW_STATE_FULLSCREEN 8
#define ORG_KDE_PLASMA_WINDOW_STATE_KEEP_ABOVE 16
#define ORG_KDE_PLASMA_WINDOW_STATE_KEEP_BELOW 32
#define ORG_KDE_PLASMA_WINDOW_STATE_ON_ALL_DESKTOPS 64
#define ORG_KDE_PLASMA_WINDOW_STATE_DEMANDS_ATTENTION 128
#define ORG_KDE_PLASMA_WINDOW_STATE_CLOSEABLE 256
#define ORG_KDE_PLASMA_WINDOW_STATE_MINIMIZABLE 512
#define ORG_KDE_PLASMA_WINDOW_STATE_MAXIMIZABLE 1024
#define ORG_KDE_PLASMA_WINDOW_STATE_FULLSCREENABLE 2048
#define ORG_KDE_PLASMA_WINDOW_STATE_SKIPTASKBAR 4096
#define ORG_KDE_PLASMA_WINDOW_STATE_SKIPSWITCHER 8192
#define ORG_KDE_PLASMA_WINDOW_STATE_SHADEABLE 16384
#define ORG_KDE_PLASMA_WINDOW_STATE_SHAVED 32768
#define ORG_KDE_PLASMA_WINDOW_STATE_MOVABLE 65536
#define ORG_KDE_PLASMA_WINDOW_STATE_RESIZABLE 131072
#define ORG_KDE_PLASMA_WINDOW_STATE_MAXIMIZED_VERTICALLY 262144
#define ORG_KDE_PLASMA_WINDOW_STATE_MAXIMIZED_HORIZONTALLY 524288
#define ORG_KDE_PLASMA_WINDOW_STATE_VIRTUAL_DESKTOP_CHANGEABLE 1048576

// Forward declarations
struct org_kde_plasma_window_management;
struct org_kde_plasma_window;

// Interface definitions
extern const struct wl_interface org_kde_plasma_window_management_interface;
extern const struct wl_interface org_kde_plasma_window_interface;

// Listener structures
struct org_kde_plasma_window_management_listener {
  void (*window)(void *data,
                 struct org_kde_plasma_window_management *org_kde_plasma_window_management,
                 struct org_kde_plasma_window *window);
  void (*window_with_uuid)(void *data,
                          struct org_kde_plasma_window_management *org_kde_plasma_window_management,
                          struct org_kde_plasma_window *window,
                          const char *uuid);
};

struct org_kde_plasma_window_listener {
  void (*title_changed)(void *data,
                       struct org_kde_plasma_window *org_kde_plasma_window,
                       const char *title);
  void (*state_changed)(void *data,
                       struct org_kde_plasma_window *org_kde_plasma_window,
                       uint32_t changed,
                       uint32_t set);
  void (*geometry)(void *data,
                  struct org_kde_plasma_window *org_kde_plasma_window,
                  int32_t x,
                  int32_t y,
                  uint32_t width,
                  uint32_t height);
  void (*unmapped)(void *data,
                   struct org_kde_plasma_window *org_kde_plasma_window);
  void (*mapped)(void *data,
                 struct org_kde_plasma_window *org_kde_plasma_window);
  void (*active_changed)(void *data,
                        struct org_kde_plasma_window *org_kde_plasma_window);
};

// Function declarations
void org_kde_plasma_window_management_destroy(struct org_kde_plasma_window_management *org_kde_plasma_window_management);
void org_kde_plasma_window_management_add_listener(struct org_kde_plasma_window_management *org_kde_plasma_window_management,
                                                  const struct org_kde_plasma_window_management_listener *listener,
                                                  void *data);

void org_kde_plasma_window_destroy(struct org_kde_plasma_window *org_kde_plasma_window);
void org_kde_plasma_window_add_listener(struct org_kde_plasma_window *org_kde_plasma_window,
                                       const struct org_kde_plasma_window_listener *listener,
                                       void *data);

#endif // WAYLAND_PROTOCOLS_H 
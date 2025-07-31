#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <wayland-client.h>
#include <wayland-client-protocol.h>
#include <xkbcommon/xkbcommon.h>
#include "overlay_window.h"
#include "wayland-protocols.h"

// Wayland protocols
struct wl_display *display = NULL;
struct wl_registry *registry = NULL;
struct wl_compositor *compositor = NULL;
struct wl_shell *shell = NULL;
struct wl_seat *seat = NULL;
struct wl_pointer *pointer = NULL;
struct wl_keyboard *keyboard = NULL;

// KDE Plasma specific protocols (if available)
struct org_kde_plasma_window_management *plasma_window_management = NULL;
struct org_kde_plasma_window *plasma_window = NULL;

// Store all windows for tracking
struct wayland_window {
  struct org_kde_plasma_window *window;
  char *title;
  uint32_t window_id;
  bool is_active;
  bool is_fullscreen;
  struct ow_window_bounds bounds;
};

static struct wayland_window *windows = NULL;
static size_t window_count = 0;
static size_t window_capacity = 0;

struct ow_target_window
{
  char* title;
  uint32_t window_id;
  bool is_focused;
  bool is_destroyed;
  bool is_fullscreen;
};

struct ow_overlay_window
{
  uint32_t window_id;
};

static struct ow_target_window target_info = {
  .title = NULL,
  .window_id = 0,
  .is_focused = false,
  .is_destroyed = false,
  .is_fullscreen = false
};

static struct ow_overlay_window overlay_info = {
  .window_id = 0
};

// Wayland registry listener
static void registry_handle_global(void *data, struct wl_registry *registry,
                                 uint32_t name, const char *interface, uint32_t version)
{
  if (strcmp(interface, "wl_compositor") == 0) {
    compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
  } else if (strcmp(interface, "wl_shell") == 0) {
    shell = wl_registry_bind(registry, name, &wl_shell_interface, 1);
  } else if (strcmp(interface, "wl_seat") == 0) {
    seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
  } else if (strcmp(interface, "org_kde_plasma_window_management") == 0) {
    // KDE Plasma window management protocol
    plasma_window_management = wl_registry_bind(registry, name, 
                                               &org_kde_plasma_window_management_interface, 1);
  }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry,
                                        uint32_t name)
{
  // Handle global removal
}

static const struct wl_registry_listener registry_listener = {
  .global = registry_handle_global,
  .global_remove = registry_handle_global_remove,
};

// KDE Plasma window management listeners
static void plasma_window_management_handle_window(void *data,
                                                 struct org_kde_plasma_window_management *org_kde_plasma_window_management,
                                                 struct org_kde_plasma_window *window)
{
  // Handle new window creation
  add_window(window, NULL); // Title will be set when title_changed is called
}

static void plasma_window_management_handle_window_with_uuid(void *data,
                                                           struct org_kde_plasma_window_management *org_kde_plasma_window_management,
                                                           struct org_kde_plasma_window *window,
                                                           const char *uuid)
{
  // Handle window with UUID
  add_window(window, NULL); // Title will be set when title_changed is called
}

static const struct org_kde_plasma_window_management_listener plasma_window_management_listener = {
  .window = plasma_window_management_handle_window,
  .window_with_uuid = plasma_window_management_handle_window_with_uuid,
};

// Helper functions for window management
static void add_window(struct org_kde_plasma_window *window, const char *title) {
  if (window_count >= window_capacity) {
    window_capacity = window_capacity == 0 ? 10 : window_capacity * 2;
    windows = realloc(windows, window_capacity * sizeof(struct wayland_window));
  }
  
  windows[window_count].window = window;
  windows[window_count].title = title ? strdup(title) : NULL;
  windows[window_count].window_id = window_count;
  windows[window_count].is_active = false;
  windows[window_count].is_fullscreen = false;
  windows[window_count].bounds = (struct ow_window_bounds){0, 0, 0, 0};
  
  // Add listener to the window
  org_kde_plasma_window_add_listener(window, &plasma_window_listener, &windows[window_count]);
  
  window_count++;
}

static struct wayland_window* find_window_by_title(const char *title) {
  for (size_t i = 0; i < window_count; i++) {
    if (windows[i].title && strcmp(windows[i].title, title) == 0) {
      return &windows[i];
    }
  }
  return NULL;
}

static void remove_window(struct org_kde_plasma_window *window) {
  for (size_t i = 0; i < window_count; i++) {
    if (windows[i].window == window) {
      if (windows[i].title) {
        free(windows[i].title);
      }
      // Move last window to this position
      if (i < window_count - 1) {
        windows[i] = windows[window_count - 1];
      }
      window_count--;
      break;
    }
  }
}

static void plasma_window_handle_title_changed(void *data,
                                             struct org_kde_plasma_window *org_kde_plasma_window,
                                             const char *title)
{
  struct wayland_window *window = (struct wayland_window*)data;
  
  // Update window title
  if (window->title) {
    free(window->title);
  }
  window->title = title ? strdup(title) : NULL;
  
  // Check if this is our target window
  if (target_info.title && window->title && strcmp(window->title, target_info.title) == 0) {
    // Found our target window
    target_info.window_id = window->window_id;
    target_info.is_focused = window->is_active;
    target_info.is_fullscreen = window->is_fullscreen;
    
    struct ow_event e = {
      .type = OW_ATTACH,
      .data.attach = {
        .has_access = -1, // Not applicable on Wayland
        .is_fullscreen = window->is_fullscreen ? 1 : 0,
        .bounds = window->bounds
      }
    };
    ow_emit_event(&e);
  }
}

static void plasma_window_handle_state_changed(void *data,
                                             struct org_kde_plasma_window *org_kde_plasma_window,
                                             uint32_t changed,
                                             uint32_t set)
{
  struct wayland_window *window = (struct wayland_window*)data;
  
  // Update window state
  bool was_fullscreen = window->is_fullscreen;
  bool was_active = window->is_active;
  
  window->is_fullscreen = (set & ORG_KDE_PLASMA_WINDOW_STATE_FULLSCREEN) != 0;
  window->is_active = (set & ORG_KDE_PLASMA_WINDOW_STATE_ACTIVE) != 0;
  
  // Check if this is our target window
  if (target_info.title && window->title && strcmp(window->title, target_info.title) == 0) {
    target_info.is_fullscreen = window->is_fullscreen;
    target_info.is_focused = window->is_active;
    
    // Handle fullscreen changes
    if (was_fullscreen != window->is_fullscreen) {
      struct ow_event e = {
        .type = OW_FULLSCREEN,
        .data.fullscreen = {
          .is_fullscreen = window->is_fullscreen
        }
      };
      ow_emit_event(&e);
    }
    
    // Handle focus changes
    if (was_active != window->is_active) {
      struct ow_event e = {
        .type = window->is_active ? OW_FOCUS : OW_BLUR
      };
      ow_emit_event(&e);
    }
  }
}

static void plasma_window_handle_geometry(void *data,
                                        struct org_kde_plasma_window *org_kde_plasma_window,
                                        int32_t x,
                                        int32_t y,
                                        uint32_t width,
                                        uint32_t height)
{
  struct wayland_window *window = (struct wayland_window*)data;
  
  // Update window geometry
  window->bounds.x = x;
  window->bounds.y = y;
  window->bounds.width = width;
  window->bounds.height = height;
  
  // Check if this is our target window
  if (target_info.title && window->title && strcmp(window->title, target_info.title) == 0) {
    struct ow_event e = {
      .type = OW_MOVERESIZE,
      .data.moveresize = {
        .bounds = window->bounds
      }
    };
    ow_emit_event(&e);
  }
}

static void plasma_window_handle_unmapped(void *data,
                                        struct org_kde_plasma_window *org_kde_plasma_window)
{
  struct wayland_window *window = (struct wayland_window*)data;
  
  // Check if this is our target window
  if (target_info.title && window->title && strcmp(window->title, target_info.title) == 0) {
    struct ow_event e = { .type = OW_DETACH };
    ow_emit_event(&e);
  }
  
  remove_window(org_kde_plasma_window);
}

static void plasma_window_handle_mapped(void *data,
                                      struct org_kde_plasma_window *org_kde_plasma_window)
{
  // Window mapped - no specific action needed
}

static void plasma_window_handle_active_changed(void *data,
                                              struct org_kde_plasma_window *org_kde_plasma_window)
{
  struct wayland_window *window = (struct wayland_window*)data;
  
  // Check if this is our target window
  if (target_info.title && window->title && strcmp(window->title, target_info.title) == 0) {
    struct ow_event e = {
      .type = window->is_active ? OW_FOCUS : OW_BLUR
    };
    ow_emit_event(&e);
  }
}

static const struct org_kde_plasma_window_listener plasma_window_listener = {
  .title_changed = plasma_window_handle_title_changed,
  .state_changed = plasma_window_handle_state_changed,
  .geometry = plasma_window_handle_geometry,
  .unmapped = plasma_window_handle_unmapped,
  .mapped = plasma_window_handle_mapped,
  .active_changed = plasma_window_handle_active_changed,
};

static bool detect_wayland_environment() {
  const char *wayland_display = getenv("WAYLAND_DISPLAY");
  const char *xdg_session_type = getenv("XDG_SESSION_TYPE");
  
  return (wayland_display != NULL && strlen(wayland_display) > 0) ||
         (xdg_session_type != NULL && strcmp(xdg_session_type, "wayland") == 0);
}

static void wayland_cleanup() {
  // Clean up windows
  for (size_t i = 0; i < window_count; i++) {
    if (windows[i].title) {
      free(windows[i].title);
    }
  }
  if (windows) {
    free(windows);
    windows = NULL;
  }
  window_count = 0;
  window_capacity = 0;
  
  if (plasma_window) {
    org_kde_plasma_window_destroy(plasma_window);
    plasma_window = NULL;
  }
  
  if (plasma_window_management) {
    org_kde_plasma_window_management_destroy(plasma_window_management);
    plasma_window_management = NULL;
  }
  
  if (shell) {
    wl_shell_destroy(shell);
    shell = NULL;
  }
  
  if (compositor) {
    wl_compositor_destroy(compositor);
    compositor = NULL;
  }
  
  if (registry) {
    wl_registry_destroy(registry);
    registry = NULL;
  }
  
  if (display) {
    wl_display_disconnect(display);
    display = NULL;
  }
}

static void hook_thread(void* _arg) {
  if (!detect_wayland_environment()) {
    // Not running on Wayland, exit
    return;
  }
  
  display = wl_display_connect(NULL);
  if (!display) {
    fprintf(stderr, "Failed to connect to Wayland display\n");
    return;
  }
  
  registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, NULL);
  
  wl_display_roundtrip(display);
  
  if (!compositor) {
    fprintf(stderr, "No compositor found\n");
    wayland_cleanup();
    return;
  }
  
  // Try to get KDE Plasma window management
  if (plasma_window_management) {
    org_kde_plasma_window_management_add_listener(plasma_window_management,
                                                 &plasma_window_management_listener, NULL);
  }
  
  // Main event loop
  while (wl_display_dispatch(display) != -1) {
    // Continue processing events
  }
  
  wayland_cleanup();
}

void ow_start_hook_wayland(char* target_window_title, void* overlay_window_id) {
  if (!detect_wayland_environment()) {
    // Fall back to X11 or other backend
    return;
  }
  
  target_info.title = strdup(target_window_title);
  if (overlay_window_id) {
    overlay_info.window_id = *(uint32_t*)overlay_window_id;
  }
  
  uv_thread_create(&hook_tid, hook_thread, NULL);
}

void ow_activate_overlay() {
  // Activate overlay window on Wayland
  // Implementation depends on specific window management protocol
}

void ow_focus_target() {
  // Focus target window on Wayland
  // Implementation depends on specific window management protocol
}

void ow_screenshot(uint8_t* out, uint32_t width, uint32_t height) {
  // Screenshot functionality on Wayland
  // Not implemented yet
} 
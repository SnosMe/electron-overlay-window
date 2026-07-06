#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <dbus/dbus.h>
#include <uv.h>
#include "overlay_window.h"
#include "kde_wayland.h"

#define TRACKER_IFACE "org.electron_overlay_window.Tracker"
#define TRACKER_PATH "/Tracker"

// A persistent KWin script that finds the target window by exact title
// match (mirrors x11.c's strcmp semantics), then reports attach/focus/
// blur/moveresize/fullscreen/detach as they happen via callDBus into the
// service we host below. Fields are joined with  (unit separator) so
// no escaping of numbers/ids is needed on either side.
static const char* TRACKER_SCRIPT_TEMPLATE =
  "(function() {\n"
  "  var SERVICE = '%s';\n"
  "  var PATH = '" TRACKER_PATH "';\n"
  "  var IFACE = '" TRACKER_IFACE "';\n"
  "  var TARGET_TITLE = '%s';\n"
  "  var current = null;\n"
  "  function send(parts) {\n"
  "    callDBus(SERVICE, PATH, IFACE, 'Event', parts.join('\\u001f'));\n"
  "  }\n"
  "  function geom(w) {\n"
  // x11.c's get_content_bounds() queries the X11 client window itself, not
  // the WM-decorated frame, so it excludes title bars/borders. Use
  // clientGeometry (not frameGeometry) here to match that contract -
  // otherwise the overlay ends up sized/offset by the window decoration.
  "    var g = w.clientGeometry;\n"
  // KWin scripting reports geometry in logical/DIP pixels, but the X11
  // backend (get_content_bounds in x11.c) reports physical pixels, and
  // index.ts's screenToDipPoint conversion for Linux expects the latter.
  // Scale by the window's output devicePixelRatio to match that contract.
  "    var s = (w.output && w.output.devicePixelRatio) ? w.output.devicePixelRatio : 1;\n"
  "    return [Math.round(g.x * s), Math.round(g.y * s), Math.round(g.width * s), Math.round(g.height * s)];\n"
  "  }\n"
  "  function attach(w) {\n"
  "    current = w;\n"
  "    var g = geom(w);\n"
  "    send(['attach', w.internalId, g[0], g[1], g[2], g[3], w.fullScreen ? '1' : '0']);\n"
  "    if (w.active) {\n"
  "      send(['focus', w.internalId, 0, 0, 0, 0, '0']);\n"
  "    }\n"
  "    w.clientGeometryChanged.connect(function() {\n"
  "      if (current !== w) return;\n"
  "      var g2 = geom(w);\n"
  "      send(['moveresize', w.internalId, g2[0], g2[1], g2[2], g2[3], '0']);\n"
  "    });\n"
  "    w.fullScreenChanged.connect(function() {\n"
  "      if (current !== w) return;\n"
  "      send(['fullscreen', w.internalId, 0, 0, 0, 0, w.fullScreen ? '1' : '0']);\n"
  "    });\n"
  "    w.activeChanged.connect(function() {\n"
  "      if (current !== w) return;\n"
  "      send([w.active ? 'focus' : 'blur', w.internalId, 0, 0, 0, 0, '0']);\n"
  "    });\n"
  "    w.closed.connect(function() {\n"
  "      if (current === w) {\n"
  "        current = null;\n"
  "        send(['detach', w.internalId, 0, 0, 0, 0, '0']);\n"
  "      }\n"
  "    });\n"
  "  }\n"
  "  function isMatch(w) {\n"
  "    return w.caption === TARGET_TITLE;\n"
  "  }\n"
  "  var wins = workspace.windowList();\n"
  "  for (var i = 0; i < wins.length; i++) {\n"
  "    if (isMatch(wins[i])) { attach(wins[i]); break; }\n"
  "  }\n"
  "  workspace.windowAdded.connect(function(w) {\n"
  "    if (current === null && isMatch(w)) attach(w);\n"
  "  });\n"
  "})();\n";

// One-shot script generated on demand to activate the target window by id.
static const char* ACTIVATE_SCRIPT_TEMPLATE =
  "(function() {\n"
  "  var id = '%s';\n"
  "  var wins = workspace.windowList();\n"
  "  for (var i = 0; i < wins.length; i++) {\n"
  "    if (wins[i].internalId === id) { workspace.activeWindow = wins[i]; break; }\n"
  "  }\n"
  "})();\n";

static DBusConnection* g_conn = NULL;
static uv_mutex_t g_id_mutex;
static char g_last_internal_id[128] = {0};
static char g_plugin_name[64] = {0};

static void escape_js_string(const char* in, char* out, size_t out_size) {
  size_t j = 0;
  for (size_t i = 0; in[i] != '\0' && j + 2 < out_size; i++) {
    char c = in[i];
    if (c == '\\' || c == '\'') {
      out[j++] = '\\';
      out[j++] = c;
    } else if (c == '\n') {
      out[j++] = '\\';
      out[j++] = 'n';
    } else {
      out[j++] = c;
    }
  }
  out[j] = '\0';
}

static bool write_temp_script(const char* content, char* out_path, size_t out_path_size) {
  const char* runtime_dir = getenv("XDG_RUNTIME_DIR");
  if (runtime_dir == NULL) {
    runtime_dir = "/tmp";
  }
  snprintf(out_path, out_path_size, "%s/electron-overlay-window-XXXXXX.js", runtime_dir);
  int fd = mkstemps(out_path, 3);
  if (fd < 0) {
    return false;
  }
  FILE* f = fdopen(fd, "w");
  if (f == NULL) {
    close(fd);
    return false;
  }
  fputs(content, f);
  fclose(f);
  return true;
}

static bool call_load_script(DBusConnection* conn, const char* file_path, const char* plugin_name, int32_t* out_id) {
  DBusMessage* msg = dbus_message_new_method_call("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "loadScript");
  dbus_message_append_args(msg, DBUS_TYPE_STRING, &file_path, DBUS_TYPE_STRING, &plugin_name, DBUS_TYPE_INVALID);

  DBusError err;
  dbus_error_init(&err);
  DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 2000, &err);
  dbus_message_unref(msg);
  if (reply == NULL) {
    dbus_error_free(&err);
    return false;
  }

  int32_t id = -1;
  bool ok = dbus_message_get_args(reply, &err, DBUS_TYPE_INT32, &id, DBUS_TYPE_INVALID);
  dbus_message_unref(reply);
  dbus_error_free(&err);

  if (ok && out_id != NULL) {
    *out_id = id;
  }
  return ok;
}

static bool call_script_run(DBusConnection* conn, int32_t script_id) {
  char path[64];
  snprintf(path, sizeof(path), "/Scripting/Script%d", script_id);

  DBusMessage* msg = dbus_message_new_method_call("org.kde.KWin", path, "org.kde.kwin.Script", "run");
  DBusError err;
  dbus_error_init(&err);
  DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 2000, &err);
  dbus_message_unref(msg);
  bool ok = reply != NULL;
  if (reply != NULL) {
    dbus_message_unref(reply);
  }
  dbus_error_free(&err);
  return ok;
}

static void call_unload_script(DBusConnection* conn, const char* plugin_name) {
  DBusMessage* msg = dbus_message_new_method_call("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "unloadScript");
  dbus_message_append_args(msg, DBUS_TYPE_STRING, &plugin_name, DBUS_TYPE_INVALID);
  DBusError err;
  dbus_error_init(&err);
  DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 2000, &err);
  dbus_message_unref(msg);
  if (reply != NULL) {
    dbus_message_unref(reply);
  }
  dbus_error_free(&err);
}

static void load_and_run_script(DBusConnection* conn, const char* content, const char* plugin_name, bool unload_after) {
  char script_path[256];
  if (!write_temp_script(content, script_path, sizeof(script_path))) {
    fprintf(stderr, "electron-overlay-window: failed to write temp KWin script\n");
    return;
  }

  int32_t script_id;
  if (call_load_script(conn, script_path, plugin_name, &script_id)) {
    call_script_run(conn, script_id);
    if (unload_after) {
      call_unload_script(conn, plugin_name);
    }
  } else {
    fprintf(stderr, "electron-overlay-window: failed to load KWin script\n");
  }

  unlink(script_path);
}

static void handle_event_payload(const char* payload) {
  char buf[512];
  strncpy(buf, payload, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char* type = strtok(buf, "\x1f");
  char* id = strtok(NULL, "\x1f");
  char* xs = strtok(NULL, "\x1f");
  char* ys = strtok(NULL, "\x1f");
  char* ws = strtok(NULL, "\x1f");
  char* hs = strtok(NULL, "\x1f");
  char* fs = strtok(NULL, "\x1f");
  if (type == NULL) {
    return;
  }

  int32_t x = xs != NULL ? atoi(xs) : 0;
  int32_t y = ys != NULL ? atoi(ys) : 0;
  uint32_t width = ws != NULL ? (uint32_t)atoi(ws) : 0;
  uint32_t height = hs != NULL ? (uint32_t)atoi(hs) : 0;
  bool is_fullscreen = fs != NULL && fs[0] == '1';

  if (id != NULL) {
    uv_mutex_lock(&g_id_mutex);
    strncpy(g_last_internal_id, id, sizeof(g_last_internal_id) - 1);
    g_last_internal_id[sizeof(g_last_internal_id) - 1] = '\0';
    uv_mutex_unlock(&g_id_mutex);
  }

  struct ow_event e;
  memset(&e, 0, sizeof(e));

  if (strcmp(type, "attach") == 0) {
    e.type = OW_ATTACH;
    e.data.attach.has_access = -1;
    e.data.attach.is_fullscreen = is_fullscreen ? 1 : 0;
    e.data.attach.bounds.x = x;
    e.data.attach.bounds.y = y;
    e.data.attach.bounds.width = width;
    e.data.attach.bounds.height = height;
    ow_emit_event(&e);
  } else if (strcmp(type, "focus") == 0) {
    e.type = OW_FOCUS;
    ow_emit_event(&e);
  } else if (strcmp(type, "blur") == 0) {
    e.type = OW_BLUR;
    ow_emit_event(&e);
  } else if (strcmp(type, "detach") == 0) {
    e.type = OW_DETACH;
    ow_emit_event(&e);
  } else if (strcmp(type, "moveresize") == 0) {
    e.type = OW_MOVERESIZE;
    e.data.moveresize.bounds.x = x;
    e.data.moveresize.bounds.y = y;
    e.data.moveresize.bounds.width = width;
    e.data.moveresize.bounds.height = height;
    ow_emit_event(&e);
  } else if (strcmp(type, "fullscreen") == 0) {
    e.type = OW_FULLSCREEN;
    e.data.fullscreen.is_fullscreen = is_fullscreen;
    ow_emit_event(&e);
  }
}

static DBusHandlerResult tracker_message_handler(DBusConnection* connection, DBusMessage* message, void* user_data) {
  (void)user_data;

  if (dbus_message_is_method_call(message, TRACKER_IFACE, "Event")) {
    const char* payload = NULL;
    DBusError err;
    dbus_error_init(&err);
    if (dbus_message_get_args(message, &err, DBUS_TYPE_STRING, &payload, DBUS_TYPE_INVALID)) {
      handle_event_payload(payload);
    }
    dbus_error_free(&err);

    DBusMessage* reply = dbus_message_new_method_return(message);
    if (reply != NULL) {
      dbus_connection_send(connection, reply, NULL);
      dbus_message_unref(reply);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  if (dbus_message_is_method_call(message, "org.freedesktop.DBus.Introspectable", "Introspect")) {
    const char* xml =
      "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
      "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
      "<node><interface name=\"" TRACKER_IFACE "\">"
      "<method name=\"Event\"><arg type=\"s\" direction=\"in\"/></method>"
      "</interface></node>";
    DBusMessage* reply = dbus_message_new_method_return(message);
    if (reply != NULL) {
      dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
      dbus_connection_send(connection, reply, NULL);
      dbus_message_unref(reply);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static const DBusObjectPathVTable tracker_vtable = {
  .unregister_function = NULL,
  .message_function = tracker_message_handler,
};

static void hook_thread(void* arg) {
  char* target_window_title = (char*)arg;

  DBusError err;
  dbus_error_init(&err);
  g_conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
  if (g_conn == NULL) {
    fprintf(stderr, "electron-overlay-window: failed to connect to session bus: %s\n", err.message);
    dbus_error_free(&err);
    free(target_window_title);
    return;
  }
  dbus_error_free(&err);

  int pid = (int)getpid();
  char service_name[64];
  snprintf(service_name, sizeof(service_name), "org.electron_overlay_window.Tracker%d", pid);
  snprintf(g_plugin_name, sizeof(g_plugin_name), "electron-overlay-window-%d", pid);

  dbus_error_init(&err);
  int name_result = dbus_bus_request_name(g_conn, service_name, DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
  if (name_result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
    fprintf(stderr, "electron-overlay-window: failed to acquire D-Bus name %s\n", service_name);
    dbus_error_free(&err);
    free(target_window_title);
    return;
  }
  dbus_error_free(&err);

  dbus_connection_register_object_path(g_conn, TRACKER_PATH, &tracker_vtable, NULL);

  char escaped_title[600];
  escape_js_string(target_window_title, escaped_title, sizeof(escaped_title));

  char script[8192];
  snprintf(script, sizeof(script), TRACKER_SCRIPT_TEMPLATE, service_name, escaped_title);

  load_and_run_script(g_conn, script, g_plugin_name, /* unload_after */ false);

  free(target_window_title);

  while (dbus_connection_read_write_dispatch(g_conn, -1)) {
    // keep dispatching Event calls from the KWin script
  }
}

bool ow_kde_wayland_is_available(void) {
  if (getenv("WAYLAND_DISPLAY") == NULL) {
    return false;
  }

  // The tracking hook thread blocks indefinitely in
  // dbus_connection_read_write_dispatch() on the same DBusConnection that
  // ow_kde_wayland_focus_target() later uses (from the Node.js thread) to
  // make blocking calls. libdbus only supports that concurrent use once
  // its internal locking is enabled; safe/idempotent to call repeatedly.
  dbus_threads_init_default();

  DBusError err;
  dbus_error_init(&err);
  DBusConnection* conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
  if (conn == NULL) {
    dbus_error_free(&err);
    return false;
  }

  dbus_bool_t has_owner = dbus_bus_name_has_owner(conn, "org.kde.KWin", &err);
  dbus_error_free(&err);
  dbus_connection_unref(conn);
  return has_owner;
}

void ow_kde_wayland_start_hook(const char* target_window_title) {
  uv_mutex_init(&g_id_mutex);
  char* title_copy = strdup(target_window_title);
  uv_thread_create(&hook_tid, hook_thread, title_copy);
}

// The hook thread blocks forever in dbus_connection_read_write_dispatch()
// on `g_conn`. These two entry points are called from the Node.js thread,
// so each opens its own short-lived private connection instead of racing
// the hook thread over `g_conn` (libdbus does not support a blocking call
// on one thread and an indefinite read/dispatch loop on another thread
// sharing the same connection).
static DBusConnection* open_private_session_conn(void) {
  DBusError err;
  dbus_error_init(&err);
  DBusConnection* conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
  dbus_error_free(&err);
  return conn;
}

static void close_private_conn(DBusConnection* conn) {
  dbus_connection_close(conn);
  dbus_connection_unref(conn);
}

void ow_kde_wayland_focus_target(void) {
  char id_copy[128];
  uv_mutex_lock(&g_id_mutex);
  strncpy(id_copy, g_last_internal_id, sizeof(id_copy) - 1);
  id_copy[sizeof(id_copy) - 1] = '\0';
  uv_mutex_unlock(&g_id_mutex);

  if (id_copy[0] == '\0') {
    return;
  }

  char escaped_id[160];
  escape_js_string(id_copy, escaped_id, sizeof(escaped_id));

  char script[512];
  snprintf(script, sizeof(script), ACTIVATE_SCRIPT_TEMPLATE, escaped_id);

  char plugin_name[64];
  snprintf(plugin_name, sizeof(plugin_name), "electron-overlay-window-activate-%d", (int)getpid());

  DBusConnection* conn = open_private_session_conn();
  if (conn == NULL) {
    return;
  }
  load_and_run_script(conn, script, plugin_name, /* unload_after */ true);
  close_private_conn(conn);
}

void ow_kde_wayland_cleanup(void) {
  if (g_plugin_name[0] == '\0') {
    return;
  }
  DBusConnection* conn = open_private_session_conn();
  if (conn == NULL) {
    return;
  }
  call_unload_script(conn, g_plugin_name);
  close_private_conn(conn);
}

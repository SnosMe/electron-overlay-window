## electron-overlay-window

Library for creating overlay windows, intended to complement the Electron.

Responsible for:
  - Finding target window by title
  - Making overlay window a child of target
  - Keeping position and size of overlay window with target in sync
  - Emits lifecycle events

Important notes:
  - You can initialize library only once (overlay window must never die, and title by which target window is searched cannot be changed)
  - You can have only one overlay window
  - Found target window remains "valid" even if its title has changed
  - Windows: target window must have the same or lower privilege level than overlay window
  - X11: library relies on EWHM, more specifically `_NET_ACTIVE_WINDOW`, `_NET_WM_STATE_FULLSCREEN`, `_NET_WM_NAME`

Supported backends:
  - Windows (7 - 10)
  - Linux (X11)

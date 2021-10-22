import { EventEmitter } from 'events'
import { join } from 'path'
import { throttle } from 'throttle-debounce'
import { screen } from 'electron'
import { BrowserWindow, Rectangle, BrowserWindowConstructorOptions } from 'electron'
const lib: AddonExports = require('node-gyp-build')(join(__dirname, '..'))

interface AddonExports {
  start(
    overlayWindowId: Buffer,
    targetWindowTitle: string,
    cb: (e: any) => void
  ): void

  activateOverlay(): void
  focusTarget(): void
}

enum EventType {
  EVENT_ATTACH = 1,
  EVENT_FOCUS = 2,
  EVENT_BLUR = 3,
  EVENT_DETACH = 4,
  EVENT_FULLSCREEN = 5,
  EVENT_MOVERESIZE = 6,
}

export interface AttachEvent {
  hasAccess: boolean | undefined
  isFullscreen: boolean | undefined
  x: number
  y: number
  width: number
  height: number
}

export interface FullscreenEvent {
  isFullscreen: boolean
}

export interface MoveresizeEvent {
  x: number
  y: number
  width: number
  height: number
}

export interface AttachToOptions {
  /**
   * Whether the Window has a title bar. We adjust the overlay to not cover
   * it
   */
  hasTitleBarOnMac?: boolean
}

const isMac = process.platform === 'darwin'

export class OverlayWindow extends EventEmitter {
  static #electronWindow: BrowserWindow
  /** Exposed so that apps can get the current bounds of the target */
  static bounds: Rectangle = { x: 0, y: 0, width: 0, height: 0 }
  static #isFocused = false
  static #willBeFocused: 'overlay' | 'target' | undefined
  /** The height of a title bar on a standard window. Only measured on Mac */
  static #macTitleBarHeight = 0
  static #attachToOptions: AttachToOptions = {}

  static readonly events = new EventEmitter()

  static readonly WINDOW_OPTS: BrowserWindowConstructorOptions = {
    fullscreenable: true,
    skipTaskbar: true,
    frame: false,
    show: false,
    transparent: true,
    // let Chromium to accept any size changes from OS
    resizable: true,
    // disable shadow for Mac OS
    hasShadow: false,
    // float above all windows on Mac OS
    alwaysOnTop: isMac
  }

  static {
    OverlayWindow.events.on('attach', (e: AttachEvent) => {
      OverlayWindow.#isFocused = true
      OverlayWindow.#electronWindow.setIgnoreMouseEvents(true)
      OverlayWindow.#electronWindow.showInactive()
      if (process.platform === 'linux') {
        OverlayWindow.#electronWindow.setSkipTaskbar(true)
      }
      OverlayWindow.#electronWindow.setAlwaysOnTop(true, 'screen-saver')
      if (e.isFullscreen !== undefined) {
        OverlayWindow.#handleFullscreen(e.isFullscreen)
      }
      OverlayWindow.bounds = e
      OverlayWindow.#updateOverlayBounds()
    })

    OverlayWindow.events.on('fullscreen', (e: FullscreenEvent) => {
      OverlayWindow.#handleFullscreen(e.isFullscreen)
    })

    OverlayWindow.events.on('detach', () => {
      OverlayWindow.#isFocused = false
      OverlayWindow.#electronWindow.hide()
    })

    const dispatchMoveresize = throttle(34 /* 30fps */, OverlayWindow.#updateOverlayBounds)

    OverlayWindow.events.on('moveresize', (e: MoveresizeEvent) => {
      OverlayWindow.bounds = e
      dispatchMoveresize()
    })

    OverlayWindow.events.on('blur', () => {
      OverlayWindow.#isFocused = false

      if (isMac || OverlayWindow.#willBeFocused !== 'overlay' && !OverlayWindow.#electronWindow.isFocused()) {
        OverlayWindow.#electronWindow.hide()
      }
    })

    OverlayWindow.events.on('focus', () => {
      OverlayWindow.#willBeFocused = undefined
      OverlayWindow.#isFocused = true

      OverlayWindow.#electronWindow.setIgnoreMouseEvents(true)
      if (!OverlayWindow.#electronWindow.isVisible()) {
        OverlayWindow.#electronWindow.showInactive()
        if (process.platform === 'linux') {
          OverlayWindow.#electronWindow.setSkipTaskbar(true)
        }
        OverlayWindow.#electronWindow.setAlwaysOnTop(true, 'screen-saver')
      }
    })
  }

  static async #handleFullscreen(isFullscreen: boolean) {
    if (isMac) {
      // On Mac, only a single app can be fullscreen, so we can't go
      // fullscreen. We get around it by making it display on all workspaces,
      // based on code from:
      // https://github.com/electron/electron/issues/10078#issuecomment-754105005
      OverlayWindow.#electronWindow.setVisibleOnAllWorkspaces(isFullscreen, { visibleOnFullScreen: true })
      if (isFullscreen) {
        const display = screen.getPrimaryDisplay()
        OverlayWindow.#electronWindow.setBounds(display.bounds)
      } else {
        // Set it back to `lastBounds` as set before fullscreen
        OverlayWindow.#updateOverlayBounds();
      }
    } else {
      OverlayWindow.#electronWindow.setFullScreen(isFullscreen)
    }
  }

  static #updateOverlayBounds () {
    let lastBounds = OverlayWindow.#adjustBoundsForMacTitleBar(OverlayWindow.bounds)
    if (lastBounds.width != 0 && lastBounds.height != 0) {
      if (process.platform === 'win32') {
        lastBounds = screen.screenToDipRect(OverlayWindow.#electronWindow, OverlayWindow.bounds)
      }
      OverlayWindow.#electronWindow.setBounds(lastBounds)
      if (process.platform === 'win32') {
        // if moved to screen with different DPI, 2nd call to setBounds will correctly resize window
        // dipRect must be recalculated as well
        lastBounds = screen.screenToDipRect(OverlayWindow.#electronWindow, OverlayWindow.bounds)
        OverlayWindow.#electronWindow.setBounds(lastBounds)
      }
    }
  }

  static #handler (e: unknown) {
    switch ((e as { type: EventType }).type) {
      case EventType.EVENT_ATTACH:
        OverlayWindow.events.emit('attach', e)
        break
      case EventType.EVENT_FOCUS:
        OverlayWindow.events.emit('focus', e)
        break
      case EventType.EVENT_BLUR:
        OverlayWindow.events.emit('blur', e)
        break
      case EventType.EVENT_DETACH:
        OverlayWindow.events.emit('detach', e)
        break
      case EventType.EVENT_FULLSCREEN:
        OverlayWindow.events.emit('fullscreen', e)
        break
      case EventType.EVENT_MOVERESIZE:
        OverlayWindow.events.emit('moveresize', e)
        break
    }
  }

  /**
   * Create a dummy window to calculate the title bar height on Mac. We use
   * the title bar height to adjust the size of the overlay to not overlap
   * the title bar. This helps Mac match the behaviour on Windows/Linux.
   */
  static #calculateMacTitleBarHeight () {
    const testWindow = new BrowserWindow({
      width: 400,
      height: 300,
      webPreferences: {
        nodeIntegration: true
      },
      show: false,
    })
    const fullHeight = testWindow.getSize()[1]
    const contentHeight = testWindow.getContentSize()[1]
    OverlayWindow.#macTitleBarHeight = fullHeight - contentHeight
    testWindow.close()
  }

  /** If we're on a Mac, adjust the bounds to not overlap the title bar */
  static #adjustBoundsForMacTitleBar (bounds: Rectangle) {
    if (!isMac || !OverlayWindow.#attachToOptions.hasTitleBarOnMac) {
      return bounds
    }

    const newBounds: Rectangle = {
      ...bounds,
      y: bounds.y + OverlayWindow.#macTitleBarHeight,
      height: bounds.height - OverlayWindow.#macTitleBarHeight
    }
    return newBounds
  }

  static activateOverlay () {
    OverlayWindow.#willBeFocused = 'overlay'
    OverlayWindow.#electronWindow.setIgnoreMouseEvents(false)
    OverlayWindow.#electronWindow.focus()
  }

  static focusTarget () {
    OverlayWindow.#willBeFocused = 'target'
    OverlayWindow.#electronWindow.setIgnoreMouseEvents(true)
    lib.focusTarget()
  }

  static attachTo (overlayWindow: BrowserWindow, targetWindowTitle: string, options: AttachToOptions = {}) {
    if (OverlayWindow.#electronWindow) {
      throw new Error('Library can be initialized only once.')
    } else {
      OverlayWindow.#electronWindow = overlayWindow
    }

    OverlayWindow.#electronWindow.on('blur', () => {
      if (!OverlayWindow.#isFocused &&
          OverlayWindow.#willBeFocused !== 'target') {
        OverlayWindow.#electronWindow.hide()
      }
    })

    OverlayWindow.#electronWindow.on('focus', () => {
      OverlayWindow.#willBeFocused = undefined
    })

    OverlayWindow.#attachToOptions = options
    if (isMac) {
      OverlayWindow.#calculateMacTitleBarHeight()
    }

    lib.start(OverlayWindow.#electronWindow.getNativeWindowHandle(), targetWindowTitle, OverlayWindow.#handler)
  }
}

import { EventEmitter } from 'events'
import { join } from 'path'
import { throttle } from 'throttle-debounce'
import { screen } from 'electron'
import type { BrowserWindow, Rectangle, BrowserWindowConstructorOptions } from 'electron'
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

export class OverlayWindow extends EventEmitter {
  static #electronWindow: BrowserWindow
  static #lastBounds: Rectangle = { x: 0, y: 0, width: 0, height: 0 }
  static #isFocused = false
  static #willBeFocused: 'overlay' | 'target' | undefined

  static readonly events = new EventEmitter()

  static readonly WINDOW_OPTS: BrowserWindowConstructorOptions = {
    fullscreenable: true,
    skipTaskbar: true,
    frame: false,
    show: false,
    transparent: true,
    // let Chromium to accept any size changes from OS
    resizable: true
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
        OverlayWindow.#electronWindow.setFullScreen(e.isFullscreen)
      }
      OverlayWindow.#lastBounds = e
      OverlayWindow.#updateOverlayBounds()
    })

    OverlayWindow.events.on('fullscreen', (e: FullscreenEvent) => {
      OverlayWindow.#electronWindow.setFullScreen(e.isFullscreen)
    })

    OverlayWindow.events.on('detach', () => {
      OverlayWindow.#isFocused = false
      OverlayWindow.#electronWindow.hide()
    })

    const dispatchMoveresize = throttle(34 /* 30fps */, OverlayWindow.#updateOverlayBounds)

    OverlayWindow.events.on('moveresize', (e: MoveresizeEvent) => {
      OverlayWindow.#lastBounds = e
      dispatchMoveresize()
    })

    OverlayWindow.events.on('blur', () => {
      OverlayWindow.#isFocused = false

      if (OverlayWindow.#willBeFocused !== 'overlay' && !OverlayWindow.#electronWindow.isFocused()) {
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

  static #updateOverlayBounds () {
    let lastBounds = OverlayWindow.#lastBounds
    if (lastBounds.width != 0 && lastBounds.height != 0) {
      if (process.platform === 'win32') {
        lastBounds = screen.screenToDipRect(OverlayWindow.#electronWindow, OverlayWindow.#lastBounds)
      }
      OverlayWindow.#electronWindow.setBounds(lastBounds)
      if (process.platform === 'win32') {
        // if moved to screen with different DPI, 2nd call to setBounds will correctly resize window
        // dipRect must be recalculated as well
        lastBounds = screen.screenToDipRect(OverlayWindow.#electronWindow, OverlayWindow.#lastBounds)
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

  static activateOverlay () {
    OverlayWindow.#willBeFocused = 'overlay'
    OverlayWindow.#electronWindow.setIgnoreMouseEvents(false)
    OverlayWindow.#electronWindow.focus()
  }

  static focusTarget () {
    OverlayWindow.#willBeFocused = 'target'
    lib.focusTarget()
  }

  static attachTo (overlayWindow: BrowserWindow, targetWindowTitle: string) {
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

    lib.start(OverlayWindow.#electronWindow.getNativeWindowHandle(), targetWindowTitle, OverlayWindow.#handler)
  }
}

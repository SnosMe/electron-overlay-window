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

export interface OverlayWindow {
  on(event: 'attach', listener: (e: AttachEvent) => void): this
  on(event: 'focus', listener: () => void): this
  on(event: 'blur', listener: () => void): this
  on(event: 'detach', listener: () => void): this
  on(event: 'fullscreen', listener: (e: FullscreenEvent) => void): this
  on(event: 'moveresize', listener: (e: MoveresizeEvent) => void): this
}

export class OverlayWindow extends EventEmitter {
  private electronWindow: BrowserWindow
  private lastBounds: Rectangle = { x: 0, y: 0, width: 0, height: 0 }
  private isFocused = false
  private willBeFocused: 'overlay' | 'target' | undefined
  private static isInitialized = false

  static readonly WINDOW_OPTS: BrowserWindowConstructorOptions = {
    fullscreenable: true,
    skipTaskbar: true,
    frame: false,
    show: false,
    transparent: true,
    // let Chromium to accept any size changes from OS
    resizable: true
  }

  constructor (overlayWindow: BrowserWindow, targetWindowTitle: string) {
    super()

    if (OverlayWindow.isInitialized) {
      throw new Error('Library can be initialized only once.')
    } else {
      OverlayWindow.isInitialized = true
    }

    this.electronWindow = overlayWindow

    this.electronWindow.on('blur', () => {
      if (!this.isFocused && this.willBeFocused !== 'target') {
        this.electronWindow.hide()
      }
    })

    this.electronWindow.on('focus', () => {
      this.willBeFocused = undefined
    })

    this.on('attach', (e) => {
      this.isFocused = true
      this.electronWindow.setIgnoreMouseEvents(true)
      this.electronWindow.showInactive()
      if (process.platform === 'linux') {
        this.electronWindow.setSkipTaskbar(true)
      }
      this.electronWindow.setAlwaysOnTop(true, 'screen-saver')
      if (e.isFullscreen !== undefined) {
        this.electronWindow.setFullScreen(e.isFullscreen)
      }
      this.lastBounds = e
      this.updateOverlayBounds()
    })

    this.on('fullscreen', (e) => {
      this.electronWindow.setFullScreen(e.isFullscreen)
    })

    this.on('detach', () => {
      this.isFocused = false
      this.electronWindow.hide()
    })

    const dispatchMoveresize = throttle(34 /* 30fps */, this.updateOverlayBounds.bind(this))

    this.on('moveresize', (e) => {
      this.lastBounds = e
      dispatchMoveresize()
    })

    this.on('blur', () => {
      this.isFocused = false

      if (this.willBeFocused !== 'overlay' && !this.electronWindow.isFocused()) {
        this.electronWindow.hide()
      }
    })

    this.on('focus', () => {
      this.willBeFocused = undefined
      this.isFocused = true

      this.electronWindow.setIgnoreMouseEvents(true)
      if (!this.electronWindow.isVisible()) {
        this.electronWindow.showInactive()
        if (process.platform === 'linux') {
          this.electronWindow.setSkipTaskbar(true)
        }
        this.electronWindow.setAlwaysOnTop(true, 'screen-saver')
      }
    })

    lib.start(this.electronWindow.getNativeWindowHandle(), targetWindowTitle, this.handler.bind(this))
  }

  private updateOverlayBounds () {
    let lastBounds = this.lastBounds
    if (lastBounds.width != 0 && lastBounds.height != 0) {
      if (process.platform === 'win32') {
        lastBounds = screen.screenToDipRect(this.electronWindow, this.lastBounds)
      }
      this.electronWindow.setBounds(lastBounds)
      if (process.platform === 'win32') {
        // if moved to screen with different DPI, 2nd call to setBounds will correctly resize window
        // dipRect must be recalculated as well
        lastBounds = screen.screenToDipRect(this.electronWindow, this.lastBounds)
        this.electronWindow.setBounds(lastBounds)
      }
    }
  }

  private handler (e: unknown) {
    switch ((e as { type: EventType }).type) {
      case EventType.EVENT_ATTACH:
        this.emit('attach', e)
        break
      case EventType.EVENT_FOCUS:
        this.emit('focus', e)
        break
      case EventType.EVENT_BLUR:
        this.emit('blur', e)
        break
      case EventType.EVENT_DETACH:
        this.emit('detach', e)
        break
      case EventType.EVENT_FULLSCREEN:
        this.emit('fullscreen', e)
        break
      case EventType.EVENT_MOVERESIZE:
        this.emit('moveresize', e)
        break
    }
  }

  public activateOverlay () {
    this.willBeFocused = 'overlay'
    this.electronWindow.setIgnoreMouseEvents(false)
    this.electronWindow.focus()
  }

  public focusTarget () {
    this.willBeFocused = 'target'
    lib.focusTarget()
  }
}

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

declare interface OverlayWindow {
  on(event: 'attach', listener: (e: AttachEvent) => void): this
  on(event: 'focus', listener: () => void): this
  on(event: 'blur', listener: () => void): this
  on(event: 'detach', listener: () => void): this
  on(event: 'fullscreen', listener: (e: FullscreenEvent) => void): this
  on(event: 'moveresize', listener: (e: MoveresizeEvent) => void): this
}

class OverlayWindow extends EventEmitter {
  private _overlayWindow!: BrowserWindow
  public defaultBehavior = true
  private lastBounds: Rectangle = { x: 0, y: 0, width: 0, height: 0 }
  private isFocused = false
  private willBeFocused: 'overlay' | 'target' | undefined

  readonly WINDOW_OPTS: BrowserWindowConstructorOptions = {
    fullscreenable: true,
    skipTaskbar: true,
    frame: false,
    show: false,
    transparent: true,
    // let Chromium to accept any size changes from OS
    resizable: true
  } as const
  
  constructor () {
    super()

    this.on('attach', (e) => {
      this.isFocused = true
      if (this.defaultBehavior) {
        this._overlayWindow.showInactive()
        if (process.platform === 'linux') {
          this._overlayWindow.setSkipTaskbar(true)
        }
        this._overlayWindow.setAlwaysOnTop(true, 'screen-saver')
        if (e.isFullscreen !== undefined) {
          this._overlayWindow.setFullScreen(e.isFullscreen)
        }
        this.lastBounds = e
        this.updateOverlayBounds()
      }
    })

    this.on('fullscreen', (e) => {
      if (this.defaultBehavior) {
        this._overlayWindow.setFullScreen(e.isFullscreen)
      }
    })

    this.on('detach', () => {
      this.isFocused = false

      if (this.defaultBehavior) {
        this._overlayWindow.hide()
      }
    })

    const dispatchMoveresize = throttle(34 /* 30fps */, this.updateOverlayBounds.bind(this))

    this.on('moveresize', (e) => {
      this.lastBounds = e
      dispatchMoveresize()
    })

    this.on('blur', () => {
      this.isFocused = false

      if (this.defaultBehavior) {

        if (this.willBeFocused !== 'overlay' && !this._overlayWindow.isFocused()) {
          console.log('hide overlay 2')

          this._overlayWindow.hide()
        }
      }
    })

    this.on('focus', () => {
      this.willBeFocused = undefined
      if (this.defaultBehavior) {
        this.isFocused = true

        if (!this._overlayWindow.isVisible()) {
          console.log('show overlay')
          this._overlayWindow.showInactive()
          if (process.platform === 'linux') {
            this._overlayWindow.setSkipTaskbar(true)
          }
          this._overlayWindow.setAlwaysOnTop(true, 'screen-saver')
        }
      }
    })
  }

  private updateOverlayBounds () {
    let lastBounds = this.lastBounds
    if (lastBounds.width != 0 && lastBounds.height != 0) {
      if (process.platform === 'win32') {
        lastBounds = screen.screenToDipRect(this._overlayWindow, this.lastBounds)
      }
      this._overlayWindow.setBounds(lastBounds)
      if (process.platform === 'win32') {
        // if moved to screen with different DPI, 2nd call to setBounds will correctly resize window
        // dipRect must be recalculated as well
        lastBounds = screen.screenToDipRect(this._overlayWindow, this.lastBounds)
        this._overlayWindow.setBounds(lastBounds)
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

  activateOverlay() {
    this.willBeFocused = 'overlay'
    this._overlayWindow.focus()
  }

  focusTarget() {
    this.willBeFocused = 'target'
    lib.focusTarget()
  }

  attachTo (overlayWindow: BrowserWindow, targetWindowTitle: string) {
    if (this._overlayWindow) {
      throw new Error('Library can be initialized only once.')
    }
    this._overlayWindow = overlayWindow

    overlayWindow.on('blur', () => {
      if (!this.isFocused && this.willBeFocused !== 'target') {
          console.log('hide overlay 1')
          this._overlayWindow.hide()
      }
    })

    overlayWindow.on('focus', () => {
      this.willBeFocused = undefined
    })

    lib.start(overlayWindow.getNativeWindowHandle(), targetWindowTitle, this.handler.bind(this))
  }
}

export const overlayWindow = new OverlayWindow()

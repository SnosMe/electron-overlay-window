import { EventEmitter } from 'events'
const lib: AddonExports = require('../build/Release/overlay_window.node')

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
  EVENT_DETACH = 4
}

declare interface OverlayWindow {
  on(event: 'attach', listener: (e: { pid: number, hasAccess?: boolean }) => void): this
  on(event: 'focus', listener: () => void): this
  on(event: 'blur', listener: () => void): this
  on(event: 'detach', listener: () => void): this
}

class OverlayWindow extends EventEmitter {
  private isRunning = false

  private handler (e: { type: EventType }) {
    console.log(e)
    switch (e.type) {
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
    }
  }

  activateOverlay() {
    lib.activateOverlay()
  }

  focusTarget() {
    lib.focusTarget()
  }

  attachTo (overlayWindowId: Buffer, targetWindowTitle: string) {
    if (this.isRunning) {
      throw new Error("Library can be initialized only once.")
    }
    lib.start(overlayWindowId, targetWindowTitle, this.handler.bind(this))
    this.isRunning = true
  }
}

export const overlayWindow = new OverlayWindow()

// ;(function main () {
//   const buff = Buffer.alloc(4)
//   buff.writeUInt32LE(0xABC, 0)

//   overlayWindow.attachTo(buff, 'Path of Exile')
// })()

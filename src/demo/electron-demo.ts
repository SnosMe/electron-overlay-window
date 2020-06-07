import { app, BrowserWindow } from 'electron'
import { overlayWindow } from '../'

let window: BrowserWindow

function createWindow () {
  window = new BrowserWindow({
    width: 400,
    height: 300,
    ...overlayWindow.WINDOW_OPTS
  })

  window.loadURL(`data:text/html;charset=utf-8,
    <head>
      <title>overlay-demo</title>
    </head>
    <body style="padding: 0; margin: 0;">
      <div style="position: absolute; width: 100%; height: 100%; border: 4px solid red; background: rgba(255,255,255,0.1); box-sizing: border-box;"></div>
      <div style="padding-top: 50vh; text-align: center;">
        <span style="padding: 16px; border-radius: 8px; background: rgb(255,255,255); border: 4px solid red;">Overlay Window</span>
      </div>
    </body>
  `)

  window.setIgnoreMouseEvents(true)

  overlayWindow.attachTo(window, 'Calculator')
}

app.on('ready', () => {
  setTimeout(
    createWindow,
    process.platform === 'linux' ? 1000 : 0
  )
})

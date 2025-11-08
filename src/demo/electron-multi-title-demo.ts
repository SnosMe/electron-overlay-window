import { app, BrowserWindow, globalShortcut } from 'electron'
import { OverlayController, OVERLAY_WINDOW_OPTS } from '../'

// https://github.com/electron/electron/issues/25153
app.disableHardwareAcceleration()

let window: BrowserWindow

const toggleMouseKey = 'CmdOrCtrl + J'
const toggleShowKey = 'CmdOrCtrl + K'
const switchModeKey = 'CmdOrCtrl + M'

let isMultiTitleMode = false

function createWindow () {
  window = new BrowserWindow({
    width: 500,
    height: 400,
    webPreferences: {
      nodeIntegration: true,
      contextIsolation: false
    },
    ...OVERLAY_WINDOW_OPTS
  })

  window.loadURL(`data:text/html;charset=utf-8,
    <head>
      <title>overlay-multi-title-demo</title>
    </head>
    <body style="padding: 0; margin: 0; font-family: Arial, sans-serif;">
      <div style="position: absolute; width: 100%; height: 100%; border: 4px solid blue; background: rgba(255,255,255,0.1); box-sizing: border-box; pointer-events: none;"></div>
      <div style="padding-top: 50vh; text-align: center;">
        <div style="padding: 16px; border-radius: 8px; background: rgb(255,255,255); border: 4px solid blue; display: inline-block;">
          <span id="mode-text">Multi-Title Overlay Window</span>
          <span id="text1"></span>
          <br><span id="matched-title" style="color: blue; font-weight: bold;"></span>
          <br><br>
          <span><b>${toggleMouseKey}</b> to toggle setIgnoreMouseEvents</span>
          <br><span><b>${toggleShowKey}</b> to "hide" overlay using CSS</span>
          <br><span><b>${switchModeKey}</b> to switch between single/multi-title mode</span>
          <br><br>
          <div id="target-list" style="text-align: left; font-size: 12px; margin-top: 10px;">
            <strong>Target Windows:</strong>
            <br>Windows: Notepad, WordPad
            <br>macOS: Untitled, TextEdit
            <br>Linux: Text Editor, gedit
          </div>
        </div>
      </div>
      <script>
        const electron = require('electron');

        electron.ipcRenderer.on('focus-change', (e, state) => {
          document.getElementById('text1').textContent = (state) ? ' (overlay is clickable) ' : 'clicks go through overlay'
        });

        electron.ipcRenderer.on('visibility-change', (e, state) => {
          if (document.body.style.display) {
            document.body.style.display = null
          } else {
            document.body.style.display = 'none'
          }
        });

        electron.ipcRenderer.on('mode-change', (e, isMulti) => {
          document.getElementById('mode-text').textContent = isMulti ? 'Multi-Title Overlay Window' : 'Single-Title Overlay Window'
          document.getElementById('mode-text').style.color = isMulti ? 'blue' : 'red'
        });

        electron.ipcRenderer.on('attach-event', (e, data) => {
          if (data.matchedTitle) {
            document.getElementById('matched-title').textContent = 'Attached to: ' + data.matchedTitle
          }
        });

        electron.ipcRenderer.on('detach-event', (e) => {
          document.getElementById('matched-title').textContent = 'Detached from window'
        });
      </script>
    </body>
  `)

  // NOTE: if you close Dev Tools overlay window will lose transparency
  window.webContents.openDevTools({ mode: 'detach', activate: false })

  makeDemoInteractive()

  // 初始使用多标题模式
  initializeMultiTitleMode()
}

function initializeMultiTitleMode() {
  isMultiTitleMode = true
  
  // 根据平台设置目标窗口标题
  let targetTitles: string[] = []
  if (process.platform === 'darwin') {
    targetTitles = ['Untitled', 'TextEdit']
  } else if (process.platform === 'linux') {
    targetTitles = ['Text Editor', 'gedit']
  } else {
    targetTitles = ['electron-overlay-window', 'node_modules']
  }

  OverlayController.attachByTitles(
    window,
    targetTitles,
    { hasTitleBarOnMac: true }
  )

  window.webContents.send('mode-change', true)
}

function initializeSingleTitleMode() {
  isMultiTitleMode = false
  
  // 根据平台设置目标窗口标题
  let targetTitle: string = ''
  if (process.platform === 'darwin') {
    targetTitle = 'Untitled'
  } else if (process.platform === 'linux') {
    targetTitle = 'Text Editor'
  } else {
    targetTitle = 'Notepad'
  }

  OverlayController.attachByTitle(
    window,
    targetTitle,
    { hasTitleBarOnMac: true }
  )

  window.webContents.send('mode-change', false)
}

function makeDemoInteractive () {
  let isInteractable = false

  function toggleOverlayState () {
    if (isInteractable) {
      isInteractable = false
      OverlayController.focusTarget()
      window.webContents.send('focus-change', false)
    } else {
      isInteractable = true
      OverlayController.activateOverlay()
      window.webContents.send('focus-change', true)
    }
  }

  function switchMode() {
    // 重新初始化库以切换模式
    if (isMultiTitleMode) {
      initializeSingleTitleMode()
    } else {
      initializeMultiTitleMode()
    }
  }

  window.on('blur', () => {
    isInteractable = false
    window.webContents.send('focus-change', false)
  })

  globalShortcut.register(toggleMouseKey, toggleOverlayState)
  globalShortcut.register(toggleShowKey, () => {
    window.webContents.send('visibility-change', false)
  })
  globalShortcut.register(switchModeKey, switchMode)
}

// 监听覆盖窗口事件
OverlayController.events.on('attach', (e) => {
  console.log('Attached to window:', e.matchedTitle || 'Unknown')
  window.webContents.send('attach-event', e)
})

OverlayController.events.on('detach', () => {
  console.log('Detached from window')
  window.webContents.send('detach-event')
})

OverlayController.events.on('focus', () => {
  console.log('Target window focused')
})

OverlayController.events.on('blur', () => {
  console.log('Target window lost focus')
})

OverlayController.events.on('moveresize', (e) => {
  console.log('Window moved/resized:', e.x, e.y, e.width, e.height)
})

app.on('ready', () => {
  setTimeout(
    createWindow,
    process.platform === 'linux' ? 1000 : 0 // https://github.com/electron/electron/issues/16809
  )
})

app.on('will-quit', () => {
  globalShortcut.unregisterAll()
})
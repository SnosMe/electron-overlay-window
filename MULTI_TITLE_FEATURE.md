# 多标题匹配功能 (Multi-Title Matching Feature)

## 概述

这个功能扩展了 electron-overlay-window 库，使其能够同时匹配多个窗口标题，而不仅仅是单个标题。当用户在多个匹配的窗口之间切换时，覆盖窗口会自动跟随当前活动的窗口。

## 新增功能

### 1. 新的 API 方法

```typescript
attachByTitles(
  electronWindow: BrowserWindow | undefined, 
  targetWindowTitles: string[], 
  options: AttachOptions = {}
): void
```

### 2. 增强的事件数据

```typescript
export interface AttachEvent {
  hasAccess: boolean | undefined
  isFullscreen: boolean | undefined
  x: number
  y: number
  width: number
  height: number
  matchedTitle?: string  // 新增：当前匹配的窗口标题
  windowId?: number      // 新增：窗口ID（用于调试）
}
```

### 3. 辅助方法

```typescript
// 获取当前匹配的窗口标题
getCurrentMatchedTitle(): string | undefined

// 获取所有目标窗口标题
getTargetWindowTitles(): string[]

// 检查是否为多标题模式
isMultiTitleModeEnabled(): boolean
```

## 使用示例

### 基本用法

```typescript
import { OverlayController } from 'electron-overlay-window'

// 多标题匹配
OverlayController.attachByTitles(window, [
  'Notepad', 
  'WordPad', 
  'Text Editor'
])

// 监听事件
OverlayController.events.on('attach', (event) => {
  console.log(`附加到窗口: ${event.matchedTitle}`)
  console.log(`窗口位置: ${event.x}, ${event.y}`)
})
```

### 高级用法

```typescript
// 带选项的多标题匹配
OverlayController.attachByTitles(
  window, 
  ['Chrome', 'Firefox', 'Edge'], 
  { hasTitleBarOnMac: true }
)

// 检查当前状态
if (OverlayController.isMultiTitleModeEnabled()) {
  const titles = OverlayController.getTargetWindowTitles()
  const currentTitle = OverlayController.getCurrentMatchedTitle()
  console.log(`目标窗口: ${titles.join(', ')}`)
  console.log(`当前匹配: ${currentTitle}`)
}
```

## 行为说明

### 1. 窗口切换逻辑

- 当用户在多个匹配的窗口之间切换时，覆盖窗口会自动跟随当前活动的窗口
- 切换过程会触发相应的事件序列：DETACH（旧窗口）→ ATTACH（新窗口）
- 覆盖窗口的位置和大小会自动调整以匹配新的目标窗口

### 2. 优先级策略

- 始终选择当前具有焦点的匹配窗口
- 如果多个窗口同时匹配，优先选择当前活动的窗口
- 窗口标题匹配是精确匹配（区分大小写）

### 3. 边界情况处理

- **无匹配窗口**：如果没有窗口匹配任一标题，保持当前状态
- **所有匹配窗口关闭**：触发 DETACH 事件，隐藏覆盖窗口
- **新窗口创建**：当新创建的窗口匹配任一标题并获得焦点时，自动附加
- **窗口标题更改**：如果窗口标题更改为匹配列表中的标题，自动附加

## 演示程序

项目包含一个新的演示程序，展示多标题匹配功能：

```bash
# 运行多标题演示
yarn demo:multi-title

# 运行原始演示
yarn demo:electron
```

### 演示程序功能

- 支持单标题和多标题模式切换（Ctrl+M）
- 显示当前匹配的窗口标题
- 跨平台支持（Windows、macOS、Linux）
- 交互式控制（点击穿透切换、显示/隐藏等）

## 平台特定说明

### Windows

- 支持 Windows 7-10
- 使用 WinEventHook 监听窗口事件
- 通过 MSAA 检查窗口焦点状态

### Linux

- 支持 X11 窗口系统
- 依赖 EWHM 规范
- 监听 `_NET_ACTIVE_WINDOW` 等属性

### macOS

- 支持 macOS 10.12+
- 使用辅助功能 API
- 需要辅助功能权限

## 向后兼容性

- 现有的 `attachByTitle` 方法保持不变
- 所有现有的事件和行为保持一致
- 内部实现复用多标题匹配的逻辑

## 性能考虑

- 标题缓存：缓存窗口标题以减少重复查询
- 事件过滤：只处理与目标窗口相关的事件
- 延迟处理：使用防抖机制处理快速窗口切换

## 故障排除

### 常见问题

1. **权限问题**（macOS）
   - 确保应用具有辅助功能权限
   - 在系统偏好设置 > 安全性与隐私 > 辅助功能中添加应用

2. **窗口不匹配**
   - 检查窗口标题是否完全匹配（区分大小写）
   - 确保目标窗口是顶级窗口

3. **性能问题**
   - 避免使用过多的标题（建议不超过 10 个）
   - 定期检查内存使用情况

### 调试技巧

```typescript
// 启用详细日志
OverlayController.events.on('attach', (e) => {
  console.log('Attach event:', {
    title: e.matchedTitle,
    bounds: { x: e.x, y: e.y, width: e.width, height: e.height },
    windowId: e.windowId
  })
})

// 监听所有事件类型
OverlayController.events.on('focus', () => console.log('Focus'))
OverlayController.events.on('blur', () => console.log('Blur'))
OverlayController.events.on('detach', () => console.log('Detach'))
OverlayController.events.on('moveresize', (e) => console.log('Move/Resize:', e))
```

## 贡献

如果您发现问题或有改进建议，请：

1. 检查现有的 issue
2. 创建详细的 bug 报告或功能请求
3. 提交 pull request

## 许可证

MIT License - 与原项目保持一致。
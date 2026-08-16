# Deskflow Changelog

## 1.26.6.3 (2026-08-16) — 稳定性修复

### 修复：core 启动崩溃（TIS/TSM 跨线程调用）

`deskflow-core` 启动后立即崩溃（`SIGABRT`）并反复重启，导致连接断断续续、光标自动跳回 Mac、修饰键失效：

- **根因**：`OSXScreen` 构造时（worker 线程）调用 `TISCreateInputSourceList` 枚举键盘布局，与 Qt 主线程的 TIS 调用并发，macOS 直接 `abort()` 整个进程
- **修复**：`AppUtilUnix::getKeyboardLayoutList` 的 macOS 枚举通过 `dispatch_sync` 调度到主线程执行

### 修复：大文本剪贴板导致 Windows 鼠标卡死

在 Mac 复制超大文本（如 8MB）后切到 Windows，Windows 端鼠标完全卡死：

- **根因**：Windows 客户端将 UTF-8 文本转 UTF-16 写入剪贴板（数据量 ×2），多 MB 文本阻塞 desk 线程
- **修复**：推送前按格式检查——**文本/HTML 上限 1MB**（超限跳过不推送、不断开），**位图保持 10MB**（截图共享不受影响）

### 修复：剪贴板超限导致 Windows 断开（自动跳回）

- **根因**：切屏推送剪贴板超过 Windows 接收上限时，Windows 端 `requestDisconnect` 直接断开整个连接
- **缓解**：Mac 端按格式限制后不再推送超限数据（结合上一项）

## 1.26.6.2 (2026-08-16) — 截图剪贴板共享

### 新功能：截图剪贴板双向共享

Mac 或 Windows 上复制的截图/图片，另一端可直接粘贴：

- **新增 `OSXClipboardTIFFConverter`**：通过 ImageIO/CoreGraphics 桥接 macOS 的 `public.tiff` 与协议/Windows 使用的 DIB 位图格式——macOS 截图（TIFF）现在能被读取并同步到 Windows；Windows 发来的位图也以 TIFF 写入剪贴板，macOS 应用可直接粘贴
- **剪贴板上限提升至 10MB**（原 3MB）：全屏截图 DIB 约 8MB，之前会被丢弃

### 说明：边缘箭头切屏已回退

上一版加入的"边缘停留 → 箭头 → 推鼠标切屏"导致 macOS 端无法越界切换到 Windows（光标卡在服务器屏幕），本版已**完整回退**该功能：

- 恢复原有"鼠标直接越过屏幕边界即切换"的行为
- 移除 `PrimaryScreenEdgePush` 事件、dwell 状态机、`EdgeArrowOverlay` 及配置项
- 该交互方案待重新设计后再行加入

## 1.26.6.1 (2026-08-16) — 截图剪贴板共享 + 边缘停留箭头切屏

### 新功能 1：截图剪贴板双向共享

Mac 或 Windows 上复制的截图/图片，另一端可直接粘贴：

- **新增 `OSXClipboardTIFFConverter`**：通过 ImageIO/CoreGraphics 桥接 macOS 的 `public.tiff` 与协议/Windows 使用的 DIB 位图格式——macOS 截图（TIFF）现在能被读取并同步到 Windows；Windows 发来的位图也以 TIFF 写入剪贴板，macOS 应用可直接粘贴
- **剪贴板上限提升至 10MB**（原 3MB）：全屏截图 DIB 约 8MB，之前会被丢弃

### 新功能 2：边缘停留箭头 → 推鼠标切屏（类似 macOS 多设备协同）

光标在屏幕边缘**停留 350ms** → 屏幕边缘出现**半透明箭头提示** → 继续推动鼠标 → 切换到相邻屏幕：

- 新事件 `PrimaryScreenEdgePush`：macOS 光标被边缘 clamp 时，从原始 CGEvent delta 检测"继续推"
- Server 端 dwell 状态机：停留计时 → armed → 箭头显示 → 方向匹配的推挤完成切换（复用 `isSwitchOkay`/`switchScreen` 守卫）
- `EdgeArrowOverlay`：core 主线程上的无边框置顶透明箭头窗口
- **开关**：配置服务器 → 计算机 页面新增"边缘停留显示箭头后推鼠标切屏"复选框（`server/enableSwitchGesture`，默认开）；关闭时恢复"直接越界切换"
- 移除了上一版"三指滑到边缘切屏"（与新交互冲突）

## 1.26.0-gesture (2026-08-15) — macOS 触控板三指手势控制 Windows

### 新功能：用 MacBook 触控板三指手势操作 Windows

在服务器端（macOS）通过触控板三指手势，向已连接的 Windows 客户端注入系统快捷键：

| 手势 | 触发的 Windows 操作 | 注入的快捷键 |
|---|---|---|
| 三指上滑 | 任务视图 | `Win + Tab` |
| 三指下滑 | 显示桌面 | `Win + D` |
| 三指左滑 | 切换到上一个任务 | `Alt + Tab` + `←` |
| 三指右滑 | 切换到下一个任务 | `Alt + Tab` + `→` |
| **三指持续左右滑** | **保持 Alt+Tab 切换器打开并持续切换** | `Alt` 保持 + `Tab`/`方向键` 节流触发 |

> 前提：鼠标光标需停留在 Windows 屏幕上（光标离开服务器屏幕时手势才会转发到客户端）。
>
> 持续左右滑：首次滑动打开应用切换器（Alt 按住），继续同方向滑动按节流间隔逐项切换，三指抬起后确认切换。切换速度由 Mac 端 180ms 节流 + Windows 端 80ms 防御间隔双重控制，防止切换过快。

### 实现方式

完整的实现跨越三层，全部在本次版本中落地：

**1. macOS 端手势捕获（核心改动）**

`src/lib/server/OSXMTGestureCapture.h/.mm` —— 在 `deskflow-core` 进程内通过 macOS 私有框架 **MultitouchSupport（MTDevice API）** 直接监听触控板：

- 不依赖 AppKit `NSEvent`，因此 **Deskflow 不在前台（后台运行）时依然有效**；
- 直接从 HID 层读取每根手指的归一化坐标，**滑动方向可靠**；
- 以质心位移累计判断一次完整滑动，带冷却时间防止重复触发。

> 为什么不用原来的 NSEvent 方案：macOS 14 上 `NSEventTypeSwipe` 分类事件要么只投递给前台应用（后台收不到）、要么 `delta` 恒为 0（无方向信号），导致手势完全无法工作。MTDevice 是系统级捕获，不受前台限制。

**2. 服务端转发（复用现有管线）**

捕获到的手势被包装为 `GestureEvent`，注入现有的 `PrimaryScreenGesture` 事件流（与 GUI IPC 手势同一通道）：

- `Server::onGesture` 判断鼠标是否停留在客户端屏幕且为三指，转发给当前活动客户端；
- 协议消息 `kMsgDGesture`（协议 v1.9）通过 `ClientProxy1_9::gesture` 序列化发送。

**3. Windows 端接收与注入（原有代码 + 修复）**

- Windows 客户端 `ServerProxy::gesture` 解析消息后，`MSWindowsDesks::injectGesture` 通过 `SendInput` 注入 Win+Tab / Win+D / Alt+Tab 快捷键；
- **修复**：`ServerProxy::gesture` 原先用 `int32_t` 接收 `%1i/%2i` 字节格式的数据，高位未初始化垃圾导致范围校验几乎必然失败，客户端收到手势消息即抛 `BadClientException` 断开连接（表现为光标跳回服务器屏幕）。已改为与编码宽度匹配的 `uint8_t/int16_t/uint32_t` 类型。

### 提交记录

```
c2ba58148 merge remote master
296f2e3e4 feat: sustain Alt+Tab switcher during horizontal three-finger swipes
ecaa9feb8 fix(windows): keep installer build compatible with latest sources
a1aeb2137 fix(mac): correct swipe up/down direction mapping
620bd03b3 fix(windows): read gesture fields with matching byte widths
a6af26f13 feat(mac): capture trackpad gestures via MultitouchSupport in core
07b498e32 i18n: add gesture diagnostics strings to translations
3884d5390 fix(mac): avoid GUI gesture logging crash
998a09bbb fix(mac): capture gestures in GUI process
072bd6685 fix(mac): show gesture diagnostics log
858ee397f fix(mac): expose preferences from tray menu
f9e868e74 feat: add gesture diagnostics switch
1847fc772 fix(macos): relay classified trackpad swipe events
fff9b15d8 feat: map macOS three-finger gestures to Windows shortcuts
```

### 调试开关

配置文件中 `[log]` 段添加 `gestureDiagnostics=true` 可开启手势逐级日志（捕获 / 服务端转发 / 协议发送 / Windows 注入），便于排查各环节是否打通。

### 已知说明

- Windows 客户端需为包含 `kMsgDGesture`（协议 v1.9）的构建，否则收到手势消息会断开连接；
- macOS 系统设置中需授予 Deskflow「输入监控」权限（TCC）；MTDevice 捕获在核心进程运行于后台时无需额外权限；
- 若与系统三指手势冲突，可关闭「触控板 → 更多手势」中的系统三指手势（MTDevice 捕获不受系统手势开关影响）。

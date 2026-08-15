# Deskflow Changelog

## 1.26.0-gesture (2026-08-15) — macOS 触控板三指手势控制 Windows

### 新功能：用 MacBook 触控板三指手势操作 Windows

在服务器端（macOS）通过触控板三指手势，向已连接的 Windows 客户端注入系统快捷键：

| 手势 | 触发的 Windows 操作 | 注入的快捷键 |
|---|---|---|
| 三指上滑 | 任务视图 | `Win + Tab` |
| 三指下滑 | 显示桌面 | `Win + D` |
| 三指左滑 | 切换到上一个任务 | `Alt + Tab` + `←` |
| 三指右滑 | 切换到下一个任务 | `Alt + Tab` + `→` |

> 前提：鼠标光标需停留在 Windows 屏幕上（光标离开服务器屏幕时手势才会转发到客户端）。

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

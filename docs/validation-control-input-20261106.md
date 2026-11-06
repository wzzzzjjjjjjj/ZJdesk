# Linux 控制 Windows 输入积压排查（2026-11-06）

## 真实报告证据

Windows 报告 `F:\zjdesk-events.jsonl`、Linux 报告 `E:\zjdesk-events.jsonl`，应用版本 0.4.5、Qt 6.8.3。以下时间为报告中的 UTC，不以两机绝对时间差计算延迟：

| 同一序号 | Linux 发送 | Windows 注入 | 结果 |
| --- | --- | --- | --- |
| 47，左键按下 | 07:01:04.060 | 07:01:04.927 | accepted=1，injectUs=14939；Linux 收到回执，RTT 43058 µs |
| 48，左键松开 | 07:01:04.164 | 07:01:36.206 | accepted=1，injectUs=970 |

Linux 按下 / 松开相隔 104 ms，Windows 却相隔 31.279 s；之后序号 196～575 的多次点击集中执行，均为 accepted=1。该证据确认输入积压，不能将现象简单归因于 SendInput 拒绝或本地光标显示。旧报告未记录普通移动事件，不能逐个确认移动的注入结果。

原调用链将 GStreamer 回调经媒体线程、UI 线程交给 SessionCoordinator，再同步调用 SendInput。Windows 原生标题栏移动 / 缩放循环是能够阻塞 Qt 排队事件的一条风险路径；报告本身没有窗口消息栈，尚不能证明当时具体进入了哪一种原生循环。

## 修复

- ScreenShare 的控制消息 / 通道状态从媒体线程转发，让 QObject 接收者分别排队到各自所属线程。
- InputReceiver 在独立线程接收并注入。协调层只在已授权、准备发送 grant 时启用共享代次 / 控制代次；输入线程校验递增序号，撤销时清除授权并释放按键，队列中的旧授权事件不会恢复生效。
- 执行回执直接交给线程安全的 ScreenShare 发送入口；UI 仅接收日志和状态，不参与每次输入的执行。输入线程在媒体通道对象之前退出。
- 普通移动每秒最多采样一次回执；回执超过两秒未返回显示提示，后续成功回执清除提示。Windows 失败回执包含 SendInput 接受数量与 Win32 错误，错误码本身不被当作 UIPI 根因证明。

## 验证边界

Windows Release 构建及部署成功。完整五组非 desktop 回归首轮通过；随后调整输入队列代次保护与销毁顺序，最终版本复核 core（65 项通过）、file-clipboard、signaling、windows-runtime-integrity 均通过。媒体模块完整回归为调整前结果，没有用它替代真实跨机验证。

Linux 隔离目录 `/home/orbbec/ZJdesk/control-input-20261106` 使用 Qt 6.8.3 / GCC 11.4.0 / GStreamer 1.26.11 构建成功，补审前四组非 desktop 回归全部通过（core、file-clipboard、signaling、media，共 135.07 s）。补审后两端重新构建、执行 core 并执行以下五项 UI 专项，均为 7 passed（含 init / cleanup）；Linux 使用现有桌面 DISPLAY=:1，未使用 offscreen：

- `motionInputReportsRejectionTimeoutAndRecovery`
- `inputExecutionDoesNotWaitForHostUiEvents`
- `inputReceiverReleasesEmptyStateWithoutUiEvents`
- `rejectedHostInputExplainsFailureWithoutNativeInput`
- `inputProbesKeepOverlappingAcknowledgementsAndTraceClicks`

专项使用不支持的测试输入、空按键状态释放验证派发 / 拒绝 / 回执，或向隐藏 VideoSurface 投递 Qt 鼠标事件，不向操作系统注入真实键鼠。阻塞界面线程的回归使用信号量等待，期间不处理 Qt 事件；它验证执行不依赖 UI 队列，不冒充原生标题栏拖动验收。

## 集中补审

检查了本轮 diff、输入派发 / 授权 / 撤销调用方、ScreenShare 跨线程发送、序号与代次校验、原生输入释放和对象销毁顺序。修正两处问题：SendInput 返回后立即保存 Win32 错误码，避免字符串格式化覆盖诊断；Linux XTest 的 Qt 屏幕坐标初始化在输入线程启动前完成，后台重试禁止访问 Qt 屏幕对象。新增空状态释放专项，确认后台执行不需要 UI 处理事件；调整阻塞测试的信号量声明顺序，确保失败退出时信号量仍活到输入线程结束。

补审后 Windows Release 部署同步更新，core 65 项通过；Windows 与 Linux 五项 UI 专项均通过，Linux core 再次通过。补审没有重复媒体回归，也未进行真实跨机操作验收。

尚需在真实 Linux 控制 Windows 会话中复测：移动 Windows 指针、点击与松开、拖动 Windows 本程序标题栏，以及退出 / 撤销后不再接收输入。现有正在运行的客户端没有重启或替换。

## 复测入口

先退出旧 Windows 客户端，再从本工作树启动；Linux 控制端可先继续使用原版本，现有 Linux 信令服务器不需要重启：

```powershell
& 'C:\Users\29822\.codex\worktrees\zjdesk-1\ZJdesk\run.ps1' -Preset windows-release -ServerUrl 'ws://10.11.42.4:8765'
```

Windows 部署目录为当前工作树的 `dist\windows-release\bin`；直接运行其中的 zjdesk.exe 时需同时传入 `--server ws://10.11.42.4:8765`，否则默认连接本机服务器。

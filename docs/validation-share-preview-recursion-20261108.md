# Windows 会议共享预览递归修复验证（2026-11-08）

用户报告 Windows 放大共享画面后不断出现多层嵌套。桌面采集包含主窗口，而主窗口持续显示桌面帧，形成反馈循环；放大后该窗口占满桌面，使循环尤其明显。

主窗口在本机会议桌面共享时设置原生 `WDA_EXCLUDEFROMCAPTURE`（`0x00000011`），让 Windows 采集窗口后面的桌面内容，避免用 `WDA_MONITOR` 得到黑色全屏。共享状态改变及原生窗口句柄 / 显示 / 窗口状态改变时核对设置；停止共享、离会后恢复 `WDA_NONE`。观看端与非桌面测试源不启用排除。主窗口析构前移除应用事件过滤器，避免原生窗口销毁事件访问已析构的会话成员。该修改不涉及媒体管线、协议或远控输入。

## 红绿回归

新增 `windowsSharedDesktopExcludesOwnPreview`，通过真实协调层、独立本地信令及 WebRTC 桌面采集运行，使用 `GetWindowDisplayAffinity` 检查原生设置。

修复前失败：`Actual (affinity(window)): 0`，`Expected (DWORD(0x00000011)): 17`。日志：`build/windows-release/test-results/background-capture-exclusion-red.txt`。

修复后同用例通过，验证初始 / 合成源可采集、桌面源排除、接收端不排除、连续两轮全屏与退出、全屏期间继续收到至少三帧、全屏内停止共享后恢复，以及重新共享再离会后的恢复。

## 结果与边界

- Windows 11 / Qt 6.8.3 / GStreamer 1.26.11 / MSVC 2022 Release：原生独立桌面与真实 OpenGL，8 通过 / 0 失败，23.286 秒，含初始化 / 清理。除新增用例外，覆盖 `annotationToolsStayAlignedWithSharedFrame`、`meetingAnnotationsReachEveryViewerAndLateJoiner`、`meetingFourVideoTiles`、`meetingEmptyExpansionRestoresOnLeave`、`expandedVideoKeepsSurfaceAndRestoresWindow`。日志：`build/windows-release/test-results/background-capture-exclusion-green.txt`。
- Ubuntu 22.04 / Qt 6.8.3 / GStreamer 1.26.11 / GCC 11 Release：独立树 `/home/orbbec/ZJdesk/meeting-annotations-20261108`，真实 X11 `:1`，同组选例 7 通过 / 0 失败 / 1 Windows 专项跳过，5.539 秒。日志：该树 `build/annotation-final-ui.txt`。未改变 Linux 捕获行为。
- Windows `build.ps1 -Preset windows-release -Deploy -GuiTests -SkipTests` 构建部署成功；该命令自身跳过测试，以上测试另行执行。构建 / 部署 EXE SHA-256 均为 `94C3A65C9751B5127176C370E8325B490E8A6B92350FC433BCFD8519D9D9ECAB`。既有客户端未重启。

本轮确认 Windows 原生采集排除设置及共享收帧 / 窗口生命周期，没有在用户活动主桌面人工查看最终视频，也没有重跑双机异网或完整媒体套件。独立桌面的原生标记验证不等于已人工验收活动主桌面合成效果。重启新版 Windows 客户端后，在会议中共享主屏幕、放大 / Esc / 停止共享，确认对端显示正常且无递归；本程序主窗口本身不会出现在共享视频中。

## 本轮集中审查复核

读取主窗口 / 头文件 / GUI 测试的当前 diff，并结合本轮记录，将采集排除修复与先前未提交的多人标注改动分开核对。追踪 `isDesktopShare()` 的会议身份 / 共享所有权条件、共享启动及清理的信号顺序、信令失败 / 离会调用方、全屏布局与原生窗口事件，以及主窗口和会话的析构顺序。未发现本轮修复中新的明确逻辑问题，未追加功能代码。

审查后 Windows Release 原生独立桌面复跑 `windowsSharedDesktopExcludesOwnPreview`、`meetingAnnotationsReachEveryViewerAndLateJoiner`、`expandedVideoKeepsSurfaceAndRestoresWindow`：含初始化 / 清理共 5 通过 / 0 失败，17.380 秒。日志：`build/windows-release/test-results/background-capture-exclusion-review.txt`。`git diff --check` 通过。现有不接受焦点窗口的 Qt `requestActivate()` 警告不计为测试失败。活动主桌面实际合成效果、原生 API 被系统拒绝时的行为及本轮双机异网未重新验收；调用失败会记录 Win32 错误，不能将该情况下的排除效果视为已确认。

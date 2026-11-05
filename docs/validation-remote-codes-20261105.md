# 9 位数字校验码远控验证（2026-11-05）

实现位置：`C:\Users\29822\.codex\worktrees\033f\ZJdesk`，`codex/ZJdesk-1`。Linux 构建使用隔离的 `/home/orbbec/ZJdesk/codex-033f-control-20261103`，未覆盖原始 Linux 检出。

## 行为

- 应用启动自动连接配置的信令服务，在线后显示本机 9 位数字码。由服务系统随机生成、避免在线重复；退出、信令重连或切换会议时注销旧码。
- 输入对方码并点击连接后自动发起远控请求。自己码 / 不完整 / 非 ASCII 数字不能连接；错误、离线和忙碌可重试，不注销本机码。连接查询最多一个未完成项，8 秒超时。
- 二次验证默认开启，被控方需点击允许；本机设置关闭后，校验码配对的请求自动走同一个 grantControl 流程。旧协议不会借此跳过允许。设置仅作用于未来请求，授权包括原有文件复制。
- 停止控制仍可在当前连接重试；断开连接清除双方授权、文件和媒体状态并恢复在线，在线码保留。媒体清理不向已拆除的中继组发送 stop，迟到的 not_joined 不使在线注册失效。
- 会议仍通过会议房间码加入，保持四人能力；空闲远控可切换会议，离会后重新注册。客户端、服务端需要一起更新。

## 验证

Windows 11 / MSVC2022 x64 与 Ubuntu 22.04 / GCC 11.4，Qt 6.8.3 / GStreamer 1.26.11。

- Windows `build.ps1 -Preset windows-release -Deploy`：core、file-clipboard、signaling、media、windows-runtime-integrity 5/5，144.79 秒。
- Linux `build.sh`：core、file-clipboard、signaling、media 4/4，133.10 秒；GUI 目标另用 `--gui-tests --skip-tests` 编译，常规套件未运行 desktop。
- `remoteCodesPairWithoutJoiningRoomsAndRecoverAfterDisconnect` 真实本机 WebSocket 测试：三端注册与独立码、非法 / 自己 / 离线码、忙碌、重复查询、断开后重新配对、退出后旧码失效。Windows 初次专项含初始化 / 清理共 3 项通过，随后纳入双方完整 signaling 套件。
- 隐藏原生 Qt 用例：`remoteCodeConnectionAutomaticallyRequestsConsent`、`remoteCodeSettingsPersistWithoutRoomEntry`、`remoteCodeApprovalCanBeAutomaticWithoutDesktopInput`、`roomEntriesAreMutuallyExclusive`、`inputProbesKeepOverlappingAcknowledgementsAndTraceClicks`。双方各 7 项通过，Windows 5729 ms、Linux 588 ms。
- 自动请求测试使用真实本机信令与原生输入后端能力检查，确认等待允许期间未启动采集。自动批准测试使用既有假流，不采集、不注入；设置测试使用独立临时 QSettings 目录，不修改用户设置。未显示测试窗口、未发送系统键鼠输入。
- 原窗口 / 双机回归夹具已适配新界面；原有后端专项仍可直接使用 legacy join 设置测试会话。旧界面互斥用例最初仍期待“离开房间”，更新为远控在线和会议切换的真实新行为后通过。

日志：`build/remote-code-build-windows.log`、`build/remote-code-regression-windows.log`、`build/remote-code-regression-linux.log`、`build/remote-code-signaling-windows.txt`、`build/remote-code-ui-windows-final.txt`、`build/remote-code-ui-linux.txt`。全部为生成物，不纳入版本管理。

## 复核与边界

集中复核注册 / 注销、在线码唯一性、内部中继组隔离、发送者绑定、授权设置、代次 / 输入回执共存与停止清理。同步清理配对后残留的媒体消息，避免断开时 not_joined 导致在线码丢失；二次验证关闭不改变旧协议的授权门禁。

集中补审发现，自动远控在线后，会议按钮已允许切换，但会议房间输入框仍被禁用。新增断言先复现：`roomEntriesAreMutuallyExclusive() 'meetingRoom->isEnabled()' returned FALSE.`。将会议房间输入框在空闲远控在线状态下也启用，并复核同一事件中的会议切换、注册注销和输入回执清理；未发现其他明确逻辑问题。

会议输入框修正后，两端重新构建部署；Windows core / signaling 2/2（10.60 秒），Linux core / signaling 2/2（5.35 秒）；含会议输入框启用及本机码显示断言的专项两端各 7 项通过、0 跳过（Windows 5590 ms，Linux 595 ms）。此时界面修正未触及媒体实现，未重复上述完整媒体回归。该阶段 Windows 构建 / 部署客户端 SHA-256 均为 `DB10B2017933D131A7793CFB38FF33BDA4E7966C980C61F57CD35167A72EFB7A`，服务端均为 `7DCCEFFF0109095F612E3BF5C493FBB23663CE07ECE85762DB4DAE9F2A0AFA2F`。

补审日志为 `build/remote-code-meeting-red.txt`、`build/remote-code-final-check-{windows,linux}.log`、`build/remote-code-ui-windows-final.txt`、`build/remote-code-ui-linux-final.txt`。未重跑真实 Windows ↔ Linux 的校验码远控、原生输入、文件粘贴、完整可见界面或 OpenGL 呈现验收；原有点击迟缓问题仍待用户事件记录。普通权限应用与 UAC / UIPI 的边界保持原状。

### 自动补审：快速重连与异步停止

读取最终源码及调用方后，发现旧桌面流处于 Stopping 时马上配对，初次 capabilities 到达后 canRequestControl 返回 false；原实现仅在收到能力时自动尝试，之后停止完成没有再次请求，双方会保持 Idle。增加 `remoteCodeWaitsForPreviousStreamsToStop`，用真实本机信令和模拟停止状态分别验证控制方先停、被控方先停；旧实现两行均失败，实际 Idle、预期 Pending，日志 `build/remote-code-stop-order-red.txt`。

最小修复：校验码模式的被控端清理期间不声明可控，收到当前代次 stopped 后重新发布能力；控制方通过 `requestRemoteControlWhenReady()` 在能力到达和自身停止完成时继续尝试，仅发送成功才清除待请求标记。复核无能力回传循环、没有更改旧房间授权策略，停止 / 离线仍会清除待请求状态。测试不采集或注入，等待允许前仍无桌面流。

最终两端 Release 构建部署通过；Windows core / signaling 2/2（10.59 秒），Linux core / signaling 2/2（5.37 秒）。含两种停止顺序与此前五项专项的两端各 9 项通过、0 跳过（Windows 5801 ms，Linux 1004 ms）。Windows 客户端构建 / 部署 SHA-256 均为 `81450BF9C192BAD24A2E23BFCF5FB8D1C106B64E451BEECCE3CB02A091058DB2`。新日志为 `build/remote-code-stop-order-{build,check}-{windows,linux}.log`、`build/remote-code-stop-order-green-{windows,linux}.txt`。

该修复限协调层的就绪通告与请求触发，未修改媒体实现，未重复完整媒体套件；真实双机快速重连、系统输入与 OpenGL 呈现仍未验收。原始 Linux 检出与运行客户端均未改动或重启。

### 远控放大画面隐藏本机码与缩小按钮

新增断言先复现放大后本机码仍可见；随后将本机码加入原有全屏隐藏 / 恢复列表，并移除协作画面的缩小按钮创建及其连接，保留 Esc 恢复。会议画面的放大 / 缩小逻辑未改动。

Windows 与 Ubuntu 22.04 X11 均完成 Release 构建；三个界面专项（远控 Esc 恢复、全屏尺寸 / 像素 / 归一化坐标 / 覆盖控件隐藏、四人会议放大恢复）各 5 项通过，含初始化 / 清理，分别 11482 ms / 8074 ms。使用原生桌面与 OpenGL，未用 offscreen；合成画面不代表真实双机远控验收。Linux 的全屏用例保留普通窗口，避免 GNOME 将测试专用 Tool 窗口限制在普通尺寸。

日志为 `build/packaging-validation/fullscreen-before.txt`、`build/packaging-validation/fullscreen-final.txt` 与 `build/packaging-validation/linux-fullscreen-final.txt`。Windows 原构建 EXE 正在运行，链接因文件占用失败，故在 `build/fullscreen-release-source` 隔离构建并更新部署 / 发布包；保留原运行进程，用户需启动新版包才会看到改动。未操作系统输入或重跑真实音视频。

### Linux 设置页设备文字重叠

按用户截图在 Ubuntu 22.04 X11 原生桌面复现：设置窗口的初始高度不足以容纳样式应用后的完整控件，下拉框与下方“入会后默认开启”复选框相交。新增 `meetingSettingsDeviceRowsDoNotOverlap`，修复前设备间距断言失败。只增加设备卡片的最小高度仍会挤压相邻卡片，因此最终在设置窗口完成样式和字体初始化后，用整个窗口的内容建议高度作为最小高度。默认开关、设备选择和设置持久化逻辑未改。

回归将窗口请求高度压到 1，检查系统按最小高度扩展后，三列设备下拉框与复选框间距足够、文字容纳完整、设置各区域均在窗口内且互不相交。与入会默认开关保存、小窗口布局、远控 Esc 恢复、远控全屏像素 / 输入坐标及四人会议放大恢复一起运行，两端各 6 个用例通过（含初始化 / 清理各 8 项）：Windows 15550 ms，Ubuntu 5475 ms。Ubuntu 另以 `QT_SCALE_FACTOR=1.5` 和 `2` 各运行布局用例，均通过；已目视检查原生窗口截图。

最终日志为 `build/packaging-validation/ui-settings-fullscreen-final-windows.txt`、`build/packaging-validation/ui-settings-fullscreen-final-linux.txt`、`build/packaging-validation/settings-final-{1.5x,2x}.txt`。截图为 `build/packaging-validation/ui-settings-device-rows-{1x,1.5x,2x}.png`。两端重新构建 Release 并更新 Windows EXE / ZIP 和 Linux AppImage，未主动结束或重启用户客户端；新功能需启动新版包。本轮使用原生桌面 / OpenGL 和合成画面，未重复真实双机远控、系统输入或音视频验收。

最终 Windows EXE 清除 SDK 路径且不指定缓存目录执行 `--version`，退出 0、标准错误为空；AppImage 清除 SDK 路径后 `--version` 与所需媒体插件检查均退出 0。启动冒烟检查不代表新增实机媒体验收。

### 本轮界面集中补审

逐项复核本轮差异、设置创建与设备刷新、样式 / 字体初始化顺序、远控全屏进入与 Esc 恢复、校验码状态刷新以及 `VideoSurface` 按钮调用方。未发现新的明确逻辑问题：校验码刷新仅更新文字，不会重新显示全屏隐藏的控件；远控画布不再创建缩小按钮，会议控件仍使用原有独立按钮；设置约束不修改设备和默认开关状态。

补充设置窗口最小宽度的数据行，覆盖默认宽度和最小宽度下各区域的边界 / 间距。Windows 两行通过（含初始化 / 清理共 4 项，5510 ms），Ubuntu 普通缩放、150% 和 200% 各两行通过（分别 243 / 253 / 176 ms），并目视检查 200% 最小宽度截图。日志为 `build/packaging-validation/settings-review-windows.txt`、`build/packaging-validation/settings-review-width.txt`、`build/packaging-validation/settings-review-{1.5x,2x}.txt`。本次补审仅增加测试覆盖和记录，应用代码及已交付包未变，未重复真实双机或音视频验收。

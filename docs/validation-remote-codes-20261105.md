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

# 校验码远控生命周期验证（2026-11-06）

## 行为与实现

- 关闭本机二次确认时，控制方配对后自动申请，受控方沿用已有自动授权。默认开启确认的策略及旧协作房间审批不变。
- 校验码模式任一端结束 / 拒绝控制，`stopControl()` 调用 `disconnectRemote()`；本端立即清除桌面、成员、授权和待拨号标记，信令服务拆除配对组，对端收到 `remote_ready` 后清理。正常结束时两端保留在线注册码，不自动重连；断开请求发送失败时关闭本端信令并进入失败状态，服务端通过断线清理对端配对。
- 配对成功后清空输入。下次需再次输入或选择历史码并点击连接；历史选择仅填入，不拨号。
- `remote_paired.peers` 新增公开 `code`，协调层发出 `remotePeerConnected(code, name)`，UI 保存最近 10 条成功配对，以服务 URL / 码去重，显示首字头像、昵称、码。当前服务只展示自己的记录，不缓存授权。历史码仍可能过期或离线。

## 环境与常规回归

- Windows：MSVC2022 x64、Qt 6.8.3、GStreamer 1.26.11，`build.ps1 -Preset windows-release` 构建；core、file-clipboard、signaling（22 项）及 runtime-integrity 通过。
- Windows 首次媒体回归 `slowReceiverChangesFrameRateAndResolutionWithoutRestart` 失败：`lastFrameSize == QSize(640, 480)` 的 45000 ms 超时，报告 49950 ms 才满足。未修改媒体逻辑或放宽超时；完整媒体套件单独复跑 33 通过、0 失败、5 跳过，134.351 秒。跳过项包括非本平台桌面和未启用的物理摄像头测试。
- Ubuntu 22.04：GCC 11.4.0、Qt 6.8.3、GStreamer 1.26.11，在隔离目录 `/home/orbbec/ZJdesk/remote-lifecycle-20261106` Release 构建；`ctest --preset linux-release -E '^desktop$'` 的 core、file-clipboard、signaling、media 四套件通过，136.01 秒。未执行会操作实体桌面输入的 desktop 测试。

## 专项覆盖

- 信令集成新增 `endingRemoteControlDisconnectsBothPeers`，分别由控制端、受控端结束：双方回在线等待、组清空、授权空闲、注册码保留、不会自动重拨，空码不能连接，显式提交码后重新配对。
- 原生 Qt 专项覆盖双方旧媒体停止的两种顺序、默认等待同意、禁用二次确认、结束按钮真正解除双方配对、历史码 / 昵称 / 图标、重开保留、选择仅填入、改名去重、10 条上限、服务隔离。
- 新增 `remoteCodeAutomaticApprovalCreatesNewAuthorizationAfterReconnect`：自动授权后分别由两端结束，旧 peer / epoch 输入被拒绝，输入序号和请求清空；重连产生更高 epoch 的新授权。使用模拟已建桌面流，不进行捕获或键鼠注入。
- 集中审查新增 `remoteDisconnectDoesNotClaimOnlineWhenSendFails`：先关闭配对一端的信令再结束控制，验证本端失败而非假在线、清空注册码及拨号标记、对端回在线等待、服务端组清空。修复前在 Windows 复现 `Actual: WaitingForPeer / Expected: Failed`，修复后双平台通过。
- 审查修复后，两端重新构建部署，并复跑 core、file-clipboard、signaling 三套件：Windows 全通过（13.03 秒），Ubuntu 全通过（7.13 秒）；此次复核未重复媒体套件。
- Ubuntu 通过实际 X11 桌面 `DISPLAY=:1` 运行专项：10 通过、0 失败、0 跳过（含初始化 / 清理），包括点击最近连接箭头并截图检查窗口及列表。Windows 使用 `CreateDesktopW` 的独立原生桌面，不切换用户前台，也不使用 offscreen：最终 11 通过、0 失败、0 跳过，包含小窗口布局。
- Windows 最初额外启用弹出列表截图时，`QApplication::activePopupWidget()` 为 false，截图断言失败；隐藏桌面无法完成该焦点检查。最终 Windows 未启用可选截图，保留历史 / 授权 / 布局回归；弹出列表的实际点击与截图在 Ubuntu 可交互桌面通过。未宣称验证 Windows 前台弹出焦点。

## 验证边界与生效

现有双端程序未重启。本次未进行真实跨机桌面画面 / 键鼠控制、摄像头或长时间稳定性验收；此前实机结果不能替代新生命周期验收。启动新版客户端和信令服务后再验收，旧服务不发送对端码时不会生成新的历史记录。最近连接保存的是成功配对，不代表媒体已建立或对端仍可达。

Windows 以 `build.ps1 -Preset windows-release -SkipTests -Deploy` 完成最后界面调整后的部署并重跑专项；Ubuntu 隔离目录以 `build.sh --preset linux-release --gui-tests --skip-tests --deploy` 部署后重跑专项。日志和测试截图均保留于未跟踪的 `build/`，不进入源码。

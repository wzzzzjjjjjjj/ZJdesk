# 启动卡片收敛验证（2026-11-07）

## 需求

协作空间启动卡片去掉「服务地址」与「你的昵称」两个输入，只保留对端 IP 一个输入，按钮只留一个「连接服务」，并调整布局。原文见用户 2026-11-07 截图与指示。

## 实现

- `apps/client/main_window.h`：删除 `nameEdit_`；新增 `connectionLayout_`、`networkColumn_`、`networkPeerKey_`、`networkVerifying_`、`pendingPeerConnect_`、`networkStatus_` 与 `verifyStartupPeer()`、`invalidateNetworkSetup()`。
- `apps/client/main_window.cpp`：
  - 构造里 `serverEdit_` 只作为隐藏控件保留（内部同步、测试驱动与 `startRemoteAccess()` 读取），不再进入布局；对方校验码整列包进 `networkColumn_`，单按钮 `joinButton_`（`primaryButton`）文案「连接服务」。
  - `enableNetworkSetup()` 把校验码列换成「Linux IP / Windows IP」单输入面板，`joinButton_` 移入面板内的输入行；删除独立「验证 IP」按钮。
  - 点「连接服务」在未验证时先 `verifyStartupPeer()`；验证通过后自动 `startRemoteAccess()`，状态到达 `WaitingForPeer` 时 `pendingPeerConnect_` 触发 `connectRemoteIp()`，无需第二次点击。
  - 验证中 / 空输入的状态文案与红边、改 IP 即断开撤销验证等行为保持不变；昵称只取页头 `meetingDisplayName`，为空时用「我」。
  - 启动模式下按钮在 `Idle` 与 `WaitingForPeer` 两个状态都显示「连接服务」（对端离线被拒后可原地重试）。
- 文档：`README.md`、`docs/startup-network.md`、`docs/handoff.md` 同步更新。

## 测试

- `tests/ui/tst_window.cpp`：三个启动专项用例改为点 `joinButton_`；断言面板内只有一个按钮、IP 输入列不可见、`serverAddress` 与 `displayName` 不可见、`workspaceFitsSmallWindowAndLongNames` 改用 `meetingDisplayName`。
- 修复既有失败：第三个用例原先用 `localAddresses().first()` 做服务地址，与产品按路由选出的本机地址不一致，在多网卡 / VPN 机器上被 `configurationError` 拒绝。现用 `QUdpSocket` 连 203.0.113.9 取路由源地址，与服务端监听、URL 保持一致（该失败在改动前的 HEAD 上同样复现）。
- 验证通过后自动配对的断言：假对端 203.0.113.9 不在线，服务端回 `remote_unavailable`，用例断言状态详情出现「对端未在线」，证明自动请求确实发出。

## 验证结果

Windows 11 / MSVC 2022 / Qt 6.8.3 debug（`build.ps1 -SkipTests` + `build\run-ui-tests.cmd`，真实桌面）：

- 构建通过（仅既有 C4457 警告）。
- 启动三用例复跑 5 passed / 0 failed。
- 全量 GUI 套件 50 passed / 5 failed / 8 skipped。5 项失败为 `fileClipboardBusyDiscardsUnpublishedCache`、`screenShareFrameRates(60fps)`、`screenShareFrameRates(auto)`、`actualWindowsJoinAndLeave`（`surface->isValid()`）、`conferenceControlsAndFrames`（`session_.join` 返回 false）。用 `git stash` 回退到 HEAD 重跑同样失败，属本机环境 / 既有问题，与本次改动无关。

Ubuntu 22.04 / GCC 11.4 / Qt 6.8.3 Release（tailscale ssh `orbbec@100.87.60.58`，源码复制到 `~/ZJdesk/zjdesk-startup-card`，真实 X 会话 `:1`）：

- `build.sh --preset linux-release` 构建通过，ctest 5/5 通过（core、file-clipboard、signaling、network-setup、media）。
- GUI 全量套件 53 passed / 2 failed / 8 skipped；2 项失败同样是 `actualWindowsJoinAndLeave` 与 `conferenceControlsAndFrames`。`screenShareFrameRates` 各档在该机通过。
- 启动三用例在真实 X 下通过，卡片显示「Windows IP」单输入 + 单个「连接服务」按钮，截图 `output/ui-startup-network-linux.png`。

## 未完成

- 未做双机点击级真实互验（Windows 与 Linux 各启动真实客户端、互相填写 IP 并完成远控）——本机 X 会话没有排入自动化点击；验证通过后的自动配对是用同机模拟「对端离线」覆盖的，真实在线配对仍需人工双机验收。
- 音视频、摄像头、GStreamer 端到端未在本次改动中复测。

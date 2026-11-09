# 不同局域网低带宽远控验证（2026-11-08）

目标：当前 Windows 与 Linux 位于不同局域网时，优先能看画面、能移动和点击，不要求 60 fps 或高画质。本次在 `codex/ZJdesk-1` 工作树开发，未修改原检出、停止或重启用户正在使用的客户端。

## 当前网络与实现

- Windows：`100.122.3.99`，Linux：`100.87.60.58`（`orbbec@100.87.60.58`），Tailscale 采样为 direct，ping 约 4 ms；原 Linux 内网 `10.11.42.4` 在本次 Windows 网络不可直达。
- 沿用启动卡片「对端 IP + 连接服务」，通过现有路由选择、ping、WebSocket 互验和实际 socket IP 配对。Linux 服务负责信令，画面和控制通过 WebRTC，未增加公网信令、STUN 或 TURN。
- Tailscale 源地址自动启用低带宽屏幕共享，其他已互通路由可在启动前设置 `ZJDESK_SHARE_LOW_BANDWIDTH=1`。只影响新屏幕流，会议参数不变。
- 最高 960×540 / 15 fps，保持宽高比，不放大小屏；慢接收端可继续降到约 640×360 / 5 fps。GCC 默认预算上限 800000 bit/s，编码目标最高 640000 bit/s；拥塞时质量预算最低 128000 bit/s。VP9 profile 0 / I420 使用码率控制，Linux 保留 UDP。现有显式 GCC 码率设置仍有效。
- 800 kbps 是拥塞预算，不是网络线速硬上限；实际记录存在超过预算的短时突发，包含关键帧、重传和 FEC 等流量，不能承诺低于 1 Mbps 的链路必定流畅。

## 双机真实桌面与输入

使用新增 `zjdesk-control-probe`，连接真实 Linux `:1` 桌面（`XAUTHORITY=/run/user/1000/gdm/Xauthority`）和 Windows 交互桌面。工具走 `NetworkSetup::verifyPeer`、IP 配对与 `SessionCoordinator` 授权流程，被控端创建居中的测试窗口并自动允许本轮测试；控制端在收到桌面帧后只向该窗口中心发送一次移动、一次按下和一次松开，退出时恢复原光标位置。

| 方向 | 接收端约 40 秒结果 | 输入及被控窗口结果 |
| --- | --- | --- |
| Windows 控制 Linux | 586 帧，960×540，目标 15 fps，UDP / Tailscale 地址 | 3/3 成功回执，5.269 / 11.102 / 5.932 ms；窗口实际收到移动 1、按下 1、松开 1 |
| Linux 控制 Windows | 563 帧，960×540，目标 15 fps，UDP / Tailscale 地址 | 3/3 成功回执，7.016 / 1054.986 / 855.361 ms；窗口实际收到移动 1、按下 1、松开 1 |

Windows 原生注入耗时分别为 615 / 4759 / 2659 微秒，回执波动不能等同于 `SendInput` 执行耗时；本次未进一步证明波动来自哪层。接收帧数包含连接建立时间，不能将总帧数除以 40 当成稳定显示帧率；本工具接收 QImage，不测产品 OpenGL 绘制。

本地生成报告（不入 Git）：`build/crossnet-windows-controller2.jsonl`、`build/crossnet-windows-host.jsonl`；Linux 同步源码目录 `/home/orbbec/ZJdesk/crossnet-20261108` 下的 `build/crossnet-linux-host2.jsonl`、`build/crossnet-linux-controller.jsonl`。测试信令端口使用 18765 / 18766，临时服务按测试期限退出，产品默认端口仍为 8765。

首次工具运行遗漏 Linux 的配套 `LD_LIBRARY_PATH`，导致 `libgstwebrtcnice-1.0.so.0` 加载失败；补齐与 `run.sh` 一致的 Qt / GStreamer 环境后完成上述双向验证。单独运行新增媒体测试时也补齐了测试内的 `MediaEnvironment::inspect()` 初始化，随后通过。

## 构建与回归

- Windows Release 构建、部署通过；非桌面 CTest 6/6 通过（core、file-clipboard、signaling、network-setup、media、windows-runtime-integrity），总计 165.39 秒。
- Ubuntu 22.04 / GCC 11.4 / Qt 6.8.3 / GStreamer 1.26.11 Release 构建、部署通过；非桌面 CTest 5/5 通过（core、file-clipboard、signaling、network-setup、media），总计 139.35 秒。
- 新增低带宽档位单测覆盖尺寸 / 帧率 / 预算上限、拥塞下降、恢复与小屏不放大；媒体回归验证真实 GStreamer 低带宽帧交付、VP9 profile 0、GCC / 编码预算与控制消息传输。
- 本轮没有运行完整产品 GUI / OpenGL 套件；上述跨机探针与非桌面回归不能代替该验收。

### 集中补审与修复复核

- 核对本轮 diff、媒体启停 / 协商 / 编码预算、输入调用链和测试工具生命周期，修复低带宽模式忽略显式 `ZJDESK_WEBRTC_GCC=tcp` 的兼容性回归：默认低带宽保留 UDP 候选，显式 `tcp` 仍使 Linux 真实桌面发送仅启用 ICE-TCP。
- 新媒体回归的局部捕获改用同作用域 `QObject` 作为连接上下文，退出时取消排队回调；手动探针将被回调捕获的计数状态移到会话对象之前声明，避免会话析构发信号时访问已结束生命周期的状态。
- 修复后 Windows / Linux Release 重建、部署通过；两平台分别运行 `lowBandwidthShareDeliversFramesAndControl` 和 `gccCapConstrainsNegotiatedPacerAndEncoder`，均为 4 passed / 0 failed / 0 skipped（含初始化、清理）。探针在两平台的非法对端地址退出路径均返回预期状态 3，且会话析构日志正常写入。本次补审未重跑上列完整非桌面 CTest。
- 显式 TCP 双机复核使用临时信令端口 18768：Linux 被控端设置 `ZJDESK_WEBRTC_GCC=tcp`，统计确认 `TCP`、`VP9 profile 0`、960×540；Windows 控制端接收 360 帧，3 条输入均获得成功回执，最大回执耗时 5.408 ms。Linux 测试窗口实际收到移动 / 按下 / 释放各 1 次，两端结果均 `ok=true`。报告为 `build/crossnet-review-tcp-controller.jsonl` 与 `build/crossnet-review-tcp-host.jsonl`，测试服务已退出。
- 未发现其他明确逻辑问题；`git diff --check` 通过。以上短时工具测试仍不覆盖产品 OpenGL 呈现、拖动 / 放大、长期连接或 DERP。

## 新版启动与实际操作复测

Windows 工作树：`C:\Users\29822\.codex\worktrees\zjdesk-1\ZJdesk`。先关闭旧客户端，在 PowerShell 启动已构建的新版：

```powershell
& 'C:\Users\29822\.codex\worktrees\zjdesk-1\ZJdesk\run.ps1' -Preset windows-release -ServerUrl 'ws://100.87.60.58:8765'
```

Linux 独立源码 / 构建目录：

```bash
cd /home/orbbec/ZJdesk/crossnet-20261108
bash run.sh
```

两端 Tailscale 保持在线。Windows 填 Linux `100.87.60.58`；Linux 填 Windows `100.122.3.99`。两端分别点击「连接服务」，被控端按本机确认设置允许控制。Linux 控制 Windows 时，先验证普通窗口的移动、单击与滚动，再验证拖动、放大和退出放大。真实产品窗口的连续操作仍需用户反馈；本次未用 offscreen 或旧客户端结果代替它。

手动工具必须有真实桌面，编译目标为 `zjdesk-control-probe`。Linux 需带上与 `run.sh` 相同的环境；例如一端使用 `--host` 创建测试窗口，另一端省略 `--host`，均传入 `--server ws://100.87.60.58:18765 --peer 对方IP --output 报告路径 --seconds 40`，Linux 端可加 `--listen` 启动本次信令服务。先启动被控端；测试会实际移动并点击测试窗口，不应在没有交互桌面的环境运行。

## 未覆盖

没有验证长期稳定性、断网恢复、Tailscale DERP 中继、其他公网 NAT、窄带 / 高丢包限制、文件传输或会议音视频。当前代码未实现通用公网 NAT 穿透；本次可用性依赖现有 Tailscale 两端互通。此前放大界面点击积压问题仍需在新版产品 UI 中实际复测。

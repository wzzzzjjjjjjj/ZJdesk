# 2026-10-25 公网有线优化

## 现场条件与结论

Windows 在家庭网络，Linux 在公司内网，两端网线不构成同一局域网。
本轮 SSH 使用 Codex 中 `linux-广域网` 对应的 `orbbec@100.87.60.58`，未使用旧的 10.11.47.137。
Windows 为 Realtek 千兆以太网、192.168.3.6，Tailscale 地址 100.122.3.99；Linux 有线地址 10.11.42.96、Tailscale 地址 100.87.60.58。

- 早期连续五次 Tailscale ping 经 DERP，248–255 ms，未建立直连。
- Linux netcheck：UDP 可用、无可用 IPv6、MappingVariesByDestIP=true，最近 DERP 为香港约 21.6 ms。
- Windows netcheck：UDP 可用、无可用 IPv6、MappingVariesByDestIP=false，最近 DERP 为洛杉矶约 175.4 ms；香港约 233 ms。
- 一次有界原生 TCP 下行试验：Linux 发送 458,752 字节，Windows 收完耗时 28.6098 秒，平均 0.128 Mbps。发送循环最长 10 秒，阻塞发送及尾部排空也计入 Windows 时间；该数值是此次路径实测，不是宽带额定速率、稳定容量估计或 WebRTC UDP 测量。
- Windows netcheck 输出发现系统 HTTP 代理；仅对诊断子进程设置 NO_PROXY=* 后最近 DERP 仍为洛杉矶。没有足够证据把路径问题归因于代理，也未修改系统代理、Clash、Tailscale 服务或路由。

早期这条中继路径无法承载高画质 1080p60。随后只读排查期间已建立直连，详见下节，不能继续将 250 ms 视为当前延迟。
GStreamer 内部的 ICE transport=UDP/TCP 只描述虚拟网络上的所选 ICE 路径，不能证明底层 Tailscale 已直连。
官方说明见 [Tailscale 连接类型](https://tailscale.com/docs/reference/connection-types) 与 [性能排查](https://tailscale.com/docs/reference/troubleshooting/poor-performance-tailnet)。

## 路由器检查与直连复测

家庭华为 AX3 Pro 的 WAN IPv4 为 10.1.1.241，网关 10.1.1.1；上级网络来源暂不明确。
IPv6 开关开启，但页面未显示可用公网地址或前缀。静态端口映射及端口触发列表为空；
UPnP 已有 Tailscale UDP 7399 → 192.168.3.6:41641。只读查看，没有保存设置、增删映射或调整代理/路由。
私网 WAN 不等于无法建立直接 UDP 路径。

同日约 14:08 后 Windows 到 Linux 已直连，连续十次为 3–4 ms；Linux 反向也显示 direct。
路径为何恢复尚未定位，不能归因于本轮路由器配置修改。
随后相同有界 TCP 试验接收 32,505,856 字节 / 10.5088 秒，约 24.7456 Mbps；
这是该时段的 TCP 下行均值，不是持续容量保证，也不证明媒体长尾已消失。

同一版本、真实 Linux GNOME → Windows 原生 OpenGL、Tailscale 地址和 ICE-TCP 对照：

| 发送预算 | 结果 | 首帧 | 接收与呈现观测 |
| --- | --- | --- | --- |
| 512000 bit/s | 三次启停通过，33.149 秒 | 7255 / 13501 / 1787 ms | 短窗口交付约 60 / 49 / 60.82 fps；累计交换最大间隔 93.32 / 100.10 / 126.43 ms |
| 默认 40000000 bit/s | 已收到首帧，随后 deliveredFps > 0 断言失败，11.394 秒 | 本次未在失败前输出该统计 | Linux 累计编码均值 5.832 ms、最大 14.360 ms、输出最大间隔 28.604 ms；不能替代接收解码观测 |

512000 的实测网络短窗口仍达 14.47 Mbps，说明此 CQP / pacing 配置不能当作链路硬限速。
默认对照后期 GCC 估计降至约 19.89 Mbps，pacer 采样 101246 字节、编码前丢帧 4；
这支持继续检查启动预算、下游排队及恢复，尚不足以锁定某一个队列。
测试功能通过不代表稳定 1080p60；未进行真实 Edge 持续交互或 30 分钟验收。
后续接收用例会在 FPS 等断言前保存 JSON，避免失败时丢失关键统计。

## 本轮实现

发送端可显式设置 `ZJDESK_WEBRTC_MAX_BITRATE`，单位 bit/s，范围 128000..40000000，必须同时启用 `ZJDESK_WEBRTC_GCC=1` 或 `tcp`。
不设置时保留既有默认值。接收端忽略此发送设置；无效值在创建采集管线前明确失败。

设上限为 M 后，GCC 初值和最大值均为 M，下限为 min(M, max(128000, min(既有下限, M/4)))。
编码预算继续保留 20% 余量；软件编码器创建后的首次属性设置也使用这一预算，避免启动后又覆盖为 profile 默认值。
VA-API 路径保持既有 4:4:4 与 QP 门槛；拥塞时继续在编码之前丢完整原始帧。
`ShareStats::timingDiagnostics().congestion` 输出上下限、估计值、编码预算、pacer 字节数及编码前丢帧数。

这是发送预算控制，不是物理链路硬限速。VA-API 仍为 CQP，大帧、关键帧、重传和底层中继缓存仍可能产生突发；本轮未证明这些缓存已全部有界。
低码率下牺牲帧数的行为也不能计作稳定 60 fps 或画质验收。

仅用于诊断的进程启动示例（512000 是测试值，不能据此称为推荐画质档位）：

```bash
cd /home/orbbec/ZJdesk/zjdesk
ZJDESK_WEBRTC_LOCAL_ADDRESS=100.87.60.58 \
ZJDESK_WEBRTC_GCC=tcp ZJDESK_WEBRTC_MAX_BITRATE=512000 \
bash ./run.sh ws://100.87.60.58:8765 demo Linux
```

控制端可以连接同一信令服务：

```powershell
$env:ZJDESK_WEBRTC_LOCAL_ADDRESS='100.122.3.99'
.\run.ps1 -Preset windows-release -ServerUrl ws://100.87.60.58:8765 -Room demo -Name Windows
```

以上假定已有监听 8765 的信令服务。环境只影响该进程及其子进程；回退预算配置时移除 `ZJDESK_WEBRTC_MAX_BITRATE` 并重新建立流。
测试使用隔离的 18765 端口并清理自己的服务，不替换用户原有会话。

## 验证结果与当前阻塞

Windows `build.ps1 -Preset windows-release -Deploy` 的 core / signaling / media 三组通过，122.20 秒；
Linux 对应 Release 构建、三组回归及部署通过，116.22 秒。
新增媒体回归实际建立 WebRTC/TWCC 会话，检查 GCC 上下限、编码预算及诊断 JSON；
无效配置拒绝在采集前完成。该测试使用本地测试源，不代表公网桌面验证通过。

本轮跨公网测试使用真实 Linux GNOME 桌面与 Windows 原生 OpenGL 接收窗口、
明确绑定双方 Tailscale 地址、ICE-TCP，以及发送端 512000 bit/s 上限。
Windows `physicalDesktopAcrossMachines` 首帧断言失败，74.161 秒，2 通过 / 1 失败含初始化/清理。
Linux 发送端已经共享，最终累计编码均值 5.970 ms、最大 13.068 ms、输出间隔最大 45.344 ms；
GCC 最小/最大/当前估计为 128000/512000/128000 bit/s，编码预算 102400 bit/s，
编码前丢帧 13，pacer 字节数该次采样为 0。接收端没有显示帧。
发送端数值不能替代接收端解码/呈现统计，也不能由 pacer=0 排除下游积压。
接收端失败离开后发送端停止共享，测试下一轮启动因成员离开失败；没有完成三次启停。
临时 18765 信令监听已清理，原有服务及正常客户端未被此次测试替换。
双端 14 个相关源码、构建配置与实验脚本按 CRLF/LF 归一后 SHA256 一致；
Windows 接收失败的 TXT/XML 已归档，`git diff --check` 无空白错误。

上述首帧失败属于恢复直连之前的历史测试；恢复直连后的功能与长尾结果见前面的对照表。

## 后续顺序

1. 每轮媒体测试前后核对 direct / relay 与负载 RTT，防止路径变化混淆对照。当前已经直连，优先继续定位媒体链路；不为已恢复的路径盲改网关配置。
2. 如果直连不可用，对比合适位置的中继与实测容量；项目自身 TURN 配置/凭据、ICE 候选策略需要独立实现与回归，不能把现有 Tailscale DERP 当作已验收 TURN。
3. 在实际媒体路径记录空闲与负载 RTT、pacer/传输排队、首帧、持续显示 FPS 和长尾。码率上限依据可持续吞吐及余量设置；当前一次短测不足以选定生产档位。
4. 网络达到目标容量后继续 HEVC 4:4:4 产品协商、硬解与同内容对照。原有离线 QP12/16 结果不替代真实桌面、RTP、画质与 30 分钟稳定性验收。

证据与临时脚本保留在 `build/analysis/wan-20261025/`；不提交日志、二进制或远端凭据。

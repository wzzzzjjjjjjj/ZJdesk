# Findings & Decisions

## Requirements
- 优先排查 RTP 仍到而解码停止输出，量化其影响。测试 UI 留在后台。

## Research Findings
- Windows WLAN 10.11.48.41。Tailscale 初始 Stopped，启动后报 NoState。既有 Linux SSH 地址 10.11.42.4 和 10.11.47.137 经已知主机密钥校验均可登录同一台 Linux，可使用真实跨子网路径。
- Windows 已有原生隔离 Desktop 包装器。旧探针报告 400 ms 正向解码最大空档 1157.21 ms，本轮需逐时事件计数，区分参考链、网络断流和 UI。

## Technical Decisions
| Decision | Rationale |
|----------|-----------|

## Issues Encountered
| Issue | Resolution |
|-------|------------|

## Resources
-

- 两轮各 240 秒有效样本，Ubuntu 到 Windows 跨子网 UDP，1080p30、400 ms、sync=true。原生隔离 Desktop 中 Intel OpenGL 实际帧交换，测试窗口未在用户桌面显示。
- 首轮逐包证实 7 个解码空档都伴随普通 RTP 到达，最长 731.03 ms。410.07 ms 个例积入连续 15 包，最大收包间隔仅 38.53 ms。
- 有两次判丢后等待并请求关键帧，PTS 跳变与 1040.41 / 952.40 ms 呈现空档大小吻合。实际解码 p95 仅 1.7 ms，收到解码帧至 swap p95 3.1 ms。首轮存在一个 310.28 ms 解码离群值。
- 第二轮加录 appsink 时钟调用，未见判丢或超过 100 ms 的时钟等待，最大画面间隔 40.64 ms。首轮的具体同线程阻塞点仍需在重现时同步细分。

- 三次关键帧解码后迟至 729.77、690.21、408.81 ms 才送下一 RTP，送包时刻距离 PTS + 420 ms 的时钟推算均在 0.8 ms 内。解码、同步 appsink 与 jitterbuffer 同步推送耦合。前两次旧 PTS 距到达时间 583.23、482.11 ms，超 400 ms 触发重基准。初始到包超期无法定位到物理路径的某一端。

- 正常交互延迟主要受 appsink 的绝对播放偏移影响，恢复时相同时钟等待又会阻塞 RTP 下送。实测 ACK 中位 5.800 / 6.058 ms，平均呈现 29.623 / 29.589 FPS。关闭同步后 590 ms 发送停顿仍有 668.539 ms 帧交换间隔。只测隔离 X11 点击和跨子网 UDP，未测拖拽、键入或公网长跑。

- 产品直连 ScreenShare 使用立即交付策略，跳过 appsink 播放时钟等待。同一会话已经共享屏幕时可以直接开始远控，复用该策略无需重建管线。普通 WAN 的 400 ms 接收缓冲和关键帧恢复保留，会议共享仍按原时钟策略交付。反向台架使用合成标记，Windows 实际输入和桌面采集需另测。

- 现场新增两份日志，Windows 205 条、Linux 162 条，均能解析。按用户提供的先跨局域网后同局域网顺序，结合共享参数和双端序号划分会话。日志包含 OS 注入、回执和媒体统计。

### 现场双端日志验收（UTC+8）

- 跨局域网 13:46:18–13:47:06，Windows JSONL 13–145，Linux JSONL 13–82。Windows 读回 WebRTC / RTP jitterbuffer 均为 400 ms，sinkSync=false。Linux 软件 VP9 profile 0，UDP，1080p，13:46:46 由目标 30 降到 24。63 个 Windows send/ack 按会话和序号对应 63 个 Linux inject，accepted 全为 1，injectUs 完全匹配。ACK p50 / p95 / max = 7.253 / 22.837 / 46.769 ms，Linux 注入 p50 / p95 / max = 40 / 89.6 / 260 µs。
- Windows 最后一条跨网媒体统计 13:46:46.794，收帧到交换 p95 7.2 ms、最大 14.565 ms；appsink 到 pull p95 3.9 ms、最大 9.268 ms。764 个已采集呈现间隔最大 196.640 ms，0 个超过 200 ms；之后直到结束仅有输入记录。该快照 packetLost=0，不能代表未持续采集的时段。已确认关闭同步，现有 1080p30 目标未在本轮维持。
- 同局域网 13:47:17–13:47:43 为 Windows 导出覆盖的媒体事件，Linux 有输入至 13:48:51.844。UDP、硬件 VA-API VP9 profile 1、1080p，目标 75→60→50，前段呈现 41–42 fps，后段解码 49.85 fps。Windows 读回 200 / 200 ms、sinkSync=false。24 个 Windows send/ack 全部对应 Linux inject，ACK p50 / p95 / max = 3.582 / 7.526 / 8.150 ms。后续 45 条在 Windows 最后一条导出事件之后，Linux 全 accepted=1；无 Windows 对应区间。
- Windows 同网 13:47:41.316 和 13:47:43.320 报呈现空档 2024.904 / 1005.880 ms，快照均 presentedFps=-1，解码约 49.85 fps，累计解码输出最大间隔 144.884 ms、丢包计数 0。前段累计收帧至交换 p95 7.4–7.6 ms。呈现空档不能由解码空档解释；MainWindow 每秒按可见、非最小化、活动窗口上报，VideoSurface 从前一帧交换累计 gap，窗口变化可能计入。无法从事件日志确定当时是否前台可见、为何没有呈现。
- p95 按输入 ACK 样本线性插值。输入回执仅到 Linux 注入 API 及返回，呈现数据从 VideoSurface 收帧起，日志不关联触发输入和对应画面；不可相加成输入到画面。性能事件在变档/异常才导出，不是逐秒完整轨迹，也未给出变档原因或公网直连/中继候选信息。两份原始 JSONL 保持原状，分析未改产品。

## Phases 11–13：帧率恢复、呈现状态与缓冲对照

- 旧实现把跨网排水时的 peerFps×0.9 写进持久上限，约 30 fps 经过 27 fps 上限会落到 24 fps。跨网排水的帧率估计现限于本次排水。退出依据新鲜积压样本，连续稳定窗口可升回 30 fps，显示上限仍优先。screen-profile 记录触发与决策数据，旧日志无法回填降档原因。
- 接收窗口切换时记录 screen-presentation 状态。停用区间不计入前台 gap，切换前未上报的前台最大值随状态事件记录。
- 三档抖动缓冲各 52 次真实 XTest 应用事件与 Windows 原生 OpenGL 交换匹配。150 ms 点击和键入中位较低，400 ms 拖动中位较低。三档目标 1080p30，平均 29.631–29.665 fps，受控暂停最大空档 663.720–684.195 ms。短测仅一个低时延跨子网直连路径，默认采用 400 ms。原始记录和统计位于 build/wan-buffer-ab/report.md。

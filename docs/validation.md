# 验证记录

## 2026-10-10：最终 GCC + TCP 十分钟完整回归

当前源码使用 `ZJDESK_WEBRTC_GCC=tcp`，调度下限为 min(启动 profile 预算, 8 Mbps)。
完整跨机测试 Windows 3 项通过 / 684.616 秒，Linux 3 项通过 / 692.034 秒。
600.445 秒真实 Edge 压力中启动 / 关闭 68 次、最小化 / 还原各 204 次、调整尺寸 612 次；
平均实际交换 52.729 fps，随后 60 秒画质阶段 44.942 fps，始终 1920×1080。
压力目标曾为 60 / 50 / 45 / 40，画质阶段为 45 fps，不能报告稳定 60 fps。
整段 34,406 个新帧交换间隔 p95 39.843 ms、最大 907.258 ms、10 次 >200 ms。
彩线平均误差 0.721、最大 4；画质采样零异常 / 235 次变化。
压力后的鼠标位置确认 118 样本中位 20.996 / p95 30.864 ms；拖拽解码反馈 50 样本
中位 57.072 / p95 73.053 / 最大 79.474 ms。不是压力中同步测量，也不是物理扫描时刻。
完整功能通过，但流畅性目标未达标。

从测试约第 2.2 分钟起，TCP 进程级只读采样覆盖 539.8 秒 / 524 条：
媒体 Send-Q 中位 7,459 / p95 127,257 / 最大 1,242,227 字节，notsent 最大 1,060,283；
TCP RTT 中位 11.992 / p95 27.691 / 最大 205.665 ms。不是连续全局峰值。
长停顿时编码仍连续而解码 / 呈现同时停顿，探针额外延迟最高 718 ms；
GCC 队列为零不能排除 libnice / TCP 下游积压。另有显示交换间隔大于解码间隔的窗口，接收调度也待定位。
隔离桌面仍有 llvmpipe 软件 OpenGL 负载，编码仍为 VA-API VP9 profile 1；
测试信令临时经 SSH，媒体 / 输入断言为实际 LAN ICE-TCP，原生 GNOME 操作长测及跨网仍未验收。
报告 `build/analysis/interaction-20261010/final-soak/`；没有归档未更新的旧 input-latency JSON。
其他试验、已修复停止竞态与未通过的全 UI 布局检查见后文，不能混为本轮全部通过。

长测后最终 Windows `build.ps1 -Preset windows-release -Deploy` 构建 / 部署通过，
core / signaling / media 三组通过 / 131.15 秒；Linux 对应三组通过 / 113.86 秒。
两端 13 个相关源码 / 构建文件 SHA256 一致，Windows build / dist 可执行文件 SHA256 一致。
仅清理本轮 SSH 信令隧道和隔离测试资源，原信令服务 PID 6761 保留。
两端正常客户端已重新启动：Windows PID 33088，原生窗口句柄非零且 Responding；
Linux PID 1258181，物理 DISPLAY :1 窗口 IsViewable，进程 GCC=tcp、LOCAL_ADDRESS=10.11.47.137。
Windows 正常客户端继续使用直接 `ws://10.11.47.137:8765`，未部署 SSH 转发。
这些启动检查不等于远控连接已再次通过；两端需点击加入房间，Linux 本地批准控制后才会建立实际会话。

## 2026-10-10：Edge 卡顿优化与 GCC / UDP 对照（过程记录）

用户要求逐步优化；在原工作区保留既有未提交改动，备份在
`build/analysis/interaction-20261010/before-optimization`，Linux 同名目录有 sources.tar 和旧客户端。
新增相对 RTT 基线的拥塞反馈、媒体线程前最新位置合并、frameSwapped 计数 / 间隔，
以及进程环境 `ZJDESK_WEBRTC_GCC=1` 启用的 TWCC / 单层 rtpgccbwe pacing；默认 TCP 保留。
没有调整系统代理、路由、Tailscale、sysctl 或用户 Edge 配置；LAN 地址仍为 10.11.47.137 / 10.11.48.48。

该阶段 Windows Release 构建 / 部署通过，core / signaling / media 三组全通过，127.70 秒；
Linux Release 构建和三组基础测试全通过，113.22 秒。两端 13 个相关源码 / 构建文件 SHA256 一致。
新增回归覆盖连续延迟无丢包降预算、孤立尖峰 / 旧客户端兼容、拥塞不升档、
2000 次位置突发与按钮顺序 / 代次，以及双端 TWCC answer / transport-cc 协商。
GCC 显式协商用例另在 Linux 启用环境后通过，不能只凭创建了 GCC 元件认为接入成功。
Windows 相关真实 OpenGL 回归 6 项通过 / 15.78 秒，包含交换计数与光标独立重绘。
Linux 全 UI 轮为 16 通过、1 失败、3 跳过 / 61.84 秒：
`workspaceFitsSmallWindowAndLongNames` 的最小视频高度断言失败，相关布局本轮未改；
未独立证明为原版本故障，不报告全 UI 通过。首帧 / 实际光标 / 细字相关用例通过。

### 对照结果与测量边界

仅延迟反馈 / 位置合并的 TCP 轮完整远控用例通过，但画面曾停顿 4.065 秒，
发送端编码输出仍连续，探针额外往返延迟最高 3.671 秒。
该轮 30 秒平均显示 52.93 fps、随后 60 秒 52.46 fps；彩线均值 0.653、最大 4。
因此“测试通过”不能等于已消除卡顿。报告在 `interaction-20261010/phase1`。

首个单层 GCC / UDP 完整短测通过，60 秒画质阶段平均显示 59.92 fps，
整段画面交换间隔 p95 46.032 ms、最大 221.950 ms，1 次 >200 ms；彩线均值 0.652、最大 4。
随后发现重复设置 VA qp 会生成频繁关键帧，改为每 250 ms 检查、仅变化时更新参数。
旧候选的 DTS 改写 / clocksync / 任意积压就跳帧没有恢复，CQP 仍限 8～16。
旧短测报告在 `interaction-20261010/gcc-single-first`，不混作最终参数的成绩。

真实 Edge 压力测试运行在独立 Xephyr / Openbox 桌面，使用独立临时浏览器配置，
反复启动 / SIGTERM 关闭、最小化 / 还原和调整窗口尺寸；Windows 在未切换的原生桌面运行真实 OpenGL。
这不是用户 GNOME 桌面的逐次按钮操作验收，也不是 offscreen 渲染。
最终长测确认 Linux 隔离进程有活跃 llvmpipe 软件 OpenGL 线程，不能用该 CPU 占用代表原生 GNOME GPU 呈现。
GStreamer 仍使用 VA-API VP9 profile 1；Windows 接收进程加载 Intel OpenGL ICD，
没有读取 GL_RENDERER 或验证物理显示扫描输出。

600.349 秒媒体采样完成：平均显示 48.997 fps，每秒 FPS 第 5 百分位 43.956、最低 26.973；
保持 1920×1080，目标在 60 / 50 / 45 fps 间调整，实际 ICE-UDP。
窗口记录的画面交换最大间隔最高 504.364 ms、解码输出最大间隔最高 501.805 ms；
7 个采样窗口记录 >200 ms，不能当作精确停顿事件数或逐帧 p95。
调度队列每秒采样中位 0、p95 34,005、最高 157,420 字节，结束为 0；
不能当作连续测得的队列全局峰值。探针 RTT 中位 10、p95 49、最高 261 ms，
额外延迟 p95 44 ms，最后待答探针达 763 ms。
编码输出连续而接收解码 / 交换停顿，仍需继续定位 RTP 到达、关键帧恢复与缓冲，
不能据一秒采样区分其全部贡献。稳定 60 fps 和完全消除短卡顿未通过。

这轮在 606.971 秒失败于测试收尾：Edge 已退出，测试误调用 waitForFinished 后断言 false。
已修为仅等待仍运行的进程；这一轮未执行随后画质 / 拖拽步骤，旧 JSON 没有混入归档。
Linux 配对测试因为没有完成标志而终止，外层清理隔离桌面。
`interaction-20261010/edge-10min` 保存性能和失败报告，该轮不能称为完整用例通过。
此前出现过信令 HTTP 200、测试脚本 X11 窗口销毁竞态、QtTest 默认 5 分钟 watchdog；
前者来源仍未确定，后两者已修正脚本 / 测试超时设置，均不计作通过。

修正测试收尾后的 UDP 完整补验通过：Windows 3 项 / 253.488 秒、Linux 3 项 / 260.885 秒。
其中 Edge 启动 / 关闭 21 次、最小化 / 还原 61 次、尺寸调整 183 次；
180.165 秒压力阶段平均显示 55.24 fps，随后 60 秒画质阶段 44.95 fps；全程保持 1080p，
239 次像素采样零异常、236 次变化；1800 列彩线平均 RGB 误差 0.670、最大 4。
压力 + 画质整段新视频帧交换间隔 p95 37.571 ms、最大 468.718 ms，3 次 >200 ms。
随后光标确认 117 样本中位 22.437 / p95 45.284 ms；拖拽解码反馈 53 样本
中位 61.338 / p95 83.085 / 最大 102.238 ms；不是压力期间同步测得的延迟。
输入、文字、滚轮、拒绝 / 允许 / 撤销、旧授权及释放回归通过。归档 `interaction-20261010/edge-final`。

### 物理 GNOME 首帧复验：UDP 失败，TCP + GCC 通过

UDP 候选在物理桌面复验未通过：首帧约 35.85 秒才出现，超过 15 秒标准；
接收用例失败退出，随后发送侧下一次 startScreenShare 也失败。
不能用上面的隔离桌面通过结果推广到物理桌面，因此 UDP 不用于当前日常客户端。
报告 `physical-udp-failed.txt` / `physical-udp-sender-failed.txt`。

新增 `ZJDESK_WEBRTC_GCC=tcp` 对照，仍接入相同的 TWCC / GCC pacing，但保留 Linux ICE-TCP。
真实物理 GNOME 桌面三次停止 / 重开全通过：首帧 5349 / 2393 / 1988 ms；
Windows 3 项通过 / 18.675 秒，Linux 3 项通过 / 29.271 秒。
物理桌面流的解码均值约 15.97～19.15 ms，较隔离桌面负载更高，
不能用纯测试图案平均 FPS 代表该桌面。该短测只证明首帧 / 重启，没有验收真实 Edge 长时间操作。
报告 `background-physical-gcc-tcp.txt` / `physical-gcc-tcp-sender.txt`。

加入 tcp 模式后再次构建 / 部署并跑基础测试：Windows 三组全通过 / 130.93 秒，
Linux 三组全通过 / 114.08 秒。

GCC + TCP 的 2 Mbps 下限完整对照：Edge 启动 / 关闭 20 次，最小化 / 还原 60 次，
调整尺寸 180 次；180.135 秒压力阶段平均 59.91 fps、目标保持 60、最大窗口交换间隔 160.598 ms。
但随后 60 秒画质阶段仅 44.16 fps，整段逐帧交换最大间隔 1527.385 ms、p95 38.861 ms、
13 次 >200 ms。像素零异常，彩线均值 0.661、最大 4。
功能回归通过不代表流畅性达标，报告保留在 `interaction-20261010/edge-gcc-tcp`。
该轮画质阶段没有保存 senderMedia，不能确认 GCC 触底的完整时间线。
下一轮补录双端数据，调度下限改为 min(启动 profile 预算, 8 Mbps)，这是当前 LAN 对照参数。
再次遇到直连信令 HTTP 200 后，临时 SSH 端口转发只用于测试信令；视频 / 输入仍走真实 LAN ICE 地址，
回归单独断言信令之外的媒体地址。正常客户端未改用该转发，不能称为已修复信令问题。

### 8 Mbps 下限短测与停止竞态修复

新下限短测完整通过：60.036 秒 Edge 压力阶段平均 52.785 fps，随后 60 秒画质阶段 49.825 fps；
1920×1080 保持，彩线均值 0.703、最大 4，像素零异常 / 236 次变化。
整段逐帧交换间隔 p95 36.009 ms、最大 500.288 ms，1 次 >200 ms。
光标确认 118 样本中位 21.390 / p95 32.072 ms；拖拽解码反馈 52 样本
中位 59.723 / p95 71.232 / 最大 76.799 ms；Windows 用例 133.454 秒通过。
画质阶段 GCC 估计最低约 8.072 Mbps、队列采样最大 4431 字节、探针 RTT 最大 64 ms，
RTP lost 计数维持 20。相邻试验时长 / 网络时刻不同，不能当作严格配对的因果统计。
报告 `interaction-20261010/edge-gcc-floor`；信令经临时 SSH 转发，媒体 / 输入为真实 LAN TCP。

物理复验进一步发现正常停止竞态：ICE failed 先于 stop 信令到达，原代码清空 streamId 后
丢弃迟到 stop，发送端停留 Failed，接收端可能独自通过；该失败轮仍保留。
新增 `lateStopAfterMediaFailureMatchesOnlyTheFailedStream`：受控媒体失败后，错误 ID 的 stop
保持 Failed、匹配 ID 的 stop 收尾、新流不受旧 stop 影响。
旧代码在匹配 stop 后仍 Failed，回归失败 / 15.673 秒；修复后 3 项通过 / 0.659 秒。
控制权限在最初失败时已撤销，此修复不自动重连、不重新授权。
修复后的物理 GNOME 三轮双端通过：首帧 5247 / 3358 / 1983 ms，
Windows 3 项 / 18.730 秒，Linux 3 项 / 29.844 秒。
报告 `late-stop-before.txt` / `late-stop-final.txt` / `background-physical-gcc-floor-fixed.txt`
和 `physical-gcc-floor-fixed-sender.txt`。后续最终长测另行记录，稳定 60 fps 仍未验收。

## 2026-10-10：公司局域网路径确认与稳定性复验

用户确认两端已回公司内网，并要求先跳过跨网络。实时检查 Windows WLAN 为
`10.11.48.48/24`，Linux WLAN 为 `10.11.47.137/24`；SSH 直接使用
`orbbec@10.11.47.137` 成功，Linux 信令仍监听 `0.0.0.0:8765`。
两端在不同子网，但公司网络可互通，不等于同一广播域。

仅把信令改为 `ws://10.11.47.137:8765` 的基础控制回归通过（Windows 12.962 秒），
libnice 日志却显示媒体选中 `100.87.60.58` / `100.122.3.99`，故不计为纯 LAN 成功。
本轮增加 `ZJDESK_WEBRTC_LOCAL_ADDRESS`，只绑定本进程的候选地址；
两端分别设置 `10.11.47.137` / `10.11.48.48`，跨机回归断言 ShareStats 的实际两端地址。
无效地址在采集启动前失败，由新增 media 回归覆盖。没有更改系统代理、路由或 Tailscale。

GCC / CBR / 严格节流候选在纯 LAN 未通过：30 秒平均编码 11.35、解码 10.81、显示 10.83 fps；
彩线平均 RGB 误差 7.665、最大 66，未满足均值 <2 / 单通道 ≤24 的既有标准。
候选及原始报告保存于 `build/analysis/lan-20261010/gcc-candidate`，正式源码已撤回，
不再要求新增 rsrtp 插件，Linux 保留原 CQP 8～16 与 ICE-TCP，首帧门禁和鼠标优化保留。

恢复方案第一次纯 LAN 测量：30 秒平均编码 53.15、解码 47.22、显示 47.21 fps；
480 列彩线均值 0.667、最大 4。随后 60 秒 239 次像素采样，零异常、234 次变化，
平均显示 41.70 fps，保持 1920×1080，目标降到 40 fps；RTP lost 从 198 到 217。
本次连续画面阶段通过；后续可选拖拽视频延迟测量因为 Windows 开启标志而 Linux 未开启，
未生成对应编码图案，断言失败。此轮不能报告整个远控用例通过；保留报告于
`build/analysis/lan-20261010/baseline-quality-first`，修正两端运行参数后另行复跑。
上述 FPS 不是稳定 60 fps 的证据。

### 最终纯 LAN 复验

两端 Release 构建 / 部署完成。Windows core / signaling / media 三组全通过，129.73 秒；
Linux 三组全通过，112.50 秒，包含无效媒体绑定地址回归。
Linux 隐藏真实 X11 / OpenGL 回归：新首帧 3 项通过 / 2.139 秒、鼠标与实际光标
7 项通过 / 1.116 秒、小字与全屏 4 项通过 / 2.424 秒；后者小字均值 0.981、最大 5。

首次最终复跑出现一次入会失败：Qt 报 HTTP 200，媒体尚未启动。
独立直接 TCP WebSocket 升级检查随后得到 HTTP 101，原服务仍监听且未重启；
没有更改代理或路由，后续同一地址重试成功。200 响应来源尚未确定，不能称为已修复。
失败报告 `build/windows-release/test-results/background-lan-final-quality.txt`。

正确匹配两端运行参数后的完整远控 + 画质用例通过：
Windows `background-lan-final-quality-retry.txt`，3 项通过、102.604 秒；
Linux `build/lan-final-quality-linux.txt`，3 项通过、110.103 秒。
媒体为 `10.11.47.137` ↔ `10.11.48.48` 的 ICE-TCP，地址断言通过；
全部图形测试在未切换的隐藏桌面 / 未映射父窗口下运行，没有显示测试窗口。

| 测量 | 实际结果 |
| --- | --- |
| 前 30 秒平均编码 / 解码 / 显示 | 60.05 / 60.04 / 60.05 fps |
| 前 30 秒 RTP lost | 0；最大解码输出间隔 107.36 ms |
| 单像素彩线 480 列 | 平均误差 0.667、最大 4 |
| 后续 60 秒连续画面 | 239 次采样，0 次像素异常、236 次变化，1920×1080 保持 |
| 后续 60 秒平均显示 | 58.21 fps，末段目标从 60 降到 50 fps |
| 后续 60 秒 RTP lost | 0 → 33 |
| 持续传输后光标回传 | 73 样本，中位 296.13 ms、p95 727.26 ms |

原始 JSON 与报告快照保存在 `build/analysis/lan-20261010/final-quality`。
随后另起连接、两端均开启拖拽编码图案，完整加载动画远控用例再次通过：
Windows `background-lan-final-drag.txt`，3 项通过、13.798 秒；Linux
`build/lan-final-drag-linux.txt`，3 项通过、20.983 秒。实际滑块更新 80 次，最大间隔 47 ms；
107 个光标确认样本中位 28.96 ms、p95 77.88 ms；58 个输入到解码图案样本
中位 78.14 ms、p95 167.08 ms、最大 302.22 ms。图案近似关联滑块值，不是物理显示扫描延迟。
两轮包含点击、文字、滚轮、拖动、拒绝 / 允许、撤销、修饰键释放与退会重入。

Linux 物理桌面（非隐藏测试场景）另行连续三次共享 / 停止 / 重开也通过：
Windows `background-lan-final-physical.txt`，3 项通过、18.493 秒；Linux
`test-results/lan-physical-sender.txt`，3 项通过、29.046 秒。实际选中双方 LAN 地址与 TCP，
三次首帧分别 5083 / 2648 / 1886 ms，均为 1920×1080，真实 OpenGL 呈现断言通过。
这项不打开 Linux 测试窗口，Windows 接收窗口隐藏。三次末采样解码约 54 / 55 / 55.8 fps，
第二次 RTP lost 计数 399，其余末采样为 0；短采样不能代替持续性能验收。

最终 Windows 构建 / 部署 EXE SHA256 相同：
`F01D038D6B1955D8E40645180E29511B6AFC6B05CBA4B4D5A1CF5D2CEE9EB577`；
Linux 构建 / 部署 ELF Build ID 同为 `d433570b08103a357869449b9570f0eed025ea48`。
Linux 部署工具调整 RPATH，故两个 ELF 文件 SHA256 不同，不作为部署不一致的判断。
收尾已关闭所有本轮隐藏测试 / Xephyr，原信令服务 PID 6761 保留；正常客户端已重新打开：
Windows PID 25496，窗口可见且 Responding，信令为 `ws://10.11.47.137:8765`；
Linux PID 1206046，X11 窗口 IsViewable。两端进程分别绑定当前 LAN 地址，使用 demo 房间，
由用户正常入会并在 Linux 本地允许控制；没有自动批准真实桌面的远控。

结论：本次纯 LAN 已恢复画面与操作可用性；短时画质检查通过。
不同轮次平均帧率与持续传输后的光标等待仍波动，稳定 1080p / 60 fps 与持续低延迟尚未通过。
不同网络的 Tailscale 适配按用户要求暂停。

## 2026-10-09：握手前旧帧积压回归

用户再次反馈当前 Windows 控制 Linux 后无画面。新增隐藏真实 X11 桌面回归
`desktopSharingStartsWithFreshKeyframeAfterDelayedNegotiation`：先采集红色，延迟转发协商，
切成蓝色并确认本地已采到蓝色，再建立连接，检查首次解码是否为当前蓝色。
旧实现实际首次解码为 `#fd0000`，2 项通过 / 1 项失败，2.210 秒，
报告 Linux `build/startup-before.txt`；证实存在握手前旧帧积压，不代表全部跨网失败均由此导致。

修复在编码输出 probe 丢弃通道就绪前的视频 buffer，CAPS / 事件仍正常协商；
DataChannel 打开后请求新关键帧，并丢弃此前增量帧，避免将旧视频带入连接或争用握手阶段。
发送侧 Running 也等待该就绪状态，保留有界启动失败。未降低画质预算、分辨率或性能验收标准。
同一回归修改后 3 项通过、2.198 秒，报告 Linux `build/linux-release/test-results/hidden-startup.txt`。
强制关键帧事件采用 GStreamer 的
[upstream force key unit API](https://gstreamer.freedesktop.org/documentation/video/video-event.html)。
本节本机回归不能代替当前 Tailscale 的画面 / 控制验收，后续跨机结果另记。

## 2026-10-09：Tailscale 跨局域网检查与未通过项

Windows Tailscale IPv4 为 `100.122.3.99`，Linux 为 `100.87.60.58`，两台电脑的物理 LAN 地址不同。
新 IP 的 SSH 成功；原 Linux 信令服务 PID 6761 监听 `0.0.0.0:8765`，后台 Windows 用例直接填写
`ws://100.87.60.58:8765`，双方实际入会并交换控制能力。此次没有用 SSH 信令隧道代替跨网连接。
源码 host ICE 候选未设置 LAN 网段白名单；Linux `ss -tinp` 和 Windows TCP 表实际出现
`100.122.3.99` / `100.87.60.58` 间的媒体候选 TCP 连接，不能把候选连接直接当成画面与输入成功。

`tailscale status` / `tailscale ping` 显示实际 DERP relay，未建立 direct。
Windows 到 Linux 的三次 ping 为 268 / 266 / 801 ms，Windows home DERP 为 sfo，Linux 为 hkg。
netcheck 中 Linux `MappingVariesByDestIP=true`；Windows报告仍使用既有本地系统代理。
没有修改代理、Clash、路由、防火墙或 Tailscale 配置。
两次 SSH 随机数据探针总耗时分别为 1 MiB 111.03 秒、64 KiB 9.70 秒；
前者与失败媒体测试短暂重叠，二者均包含 SSH 握手、远程进程与 Base64 开销，不作为纯链路吞吐上限。
最终 TCP 轮候选 socket 的 `ss` 平滑 RTT 约 1.1～2.0 秒并出现重传，Send-Q 当时为 0，
尚未输出实际媒体；因此该轮不能断言为视频发送队列拥堵。快照为 `build/tailscale-progress-1.txt` / `-2.txt`。

原 15 秒期限两轮均未建立远控。第二轮 trace 中 ICE completed 10.230 秒，PeerConnection / DTLS
connected 11.209 秒，首 RTP 11.368 秒；仍未得到首帧和授权。
正式改为媒体启动等待 45 秒、控制请求及本地批准后的连接等待 60 秒；本地待批准仍 30 秒。
延时版实际跨网回归仍失败：Windows ICE checking 27.675 秒后于 37.214 秒 failed；
Linux 曾选 TCP 并短暂 ICE completed，但 DTLS / 首帧 / 控制没有完成。
Windows 接收统计 processed / delivered 为 0；用例 2 项通过、1 项失败、207.963 秒。
报告：`build/windows-release/test-results/background-tailscale-final.txt` / `.xml`，
诊断日志 `build/tailscale-windows-final-gst.log` 与 Linux `build/tailscale-linux-final-gst.log`。

随后只在 Linux 测试构建临时启用 ICE-UDP、关闭 ICE-TCP，Windows 诊断构建只将预期传输改 UDP，
没有放宽任何画面或输入指标。Windows ICE connected 9.566 秒，PeerConnection connected 10.388 秒，
RTP 到达后持续 `Waiting for keyframe` / `Dropping inter-frame before intra-frame`，40.074 秒 ICE failed。
Linux 诊断统计为 UDP、关键帧 121095 字节、发送约 3.97 Mbps；这些是发送侧统计，不能视作接收吞吐。
该轮没有首帧或远控授权，点击 / 文字 / 拖动均未执行；确认 ICE failed 后终止诊断，未标记测试通过。
日志：`build/tailscale-windows-udp-gst.log`、Linux `build/tailscale-linux-udp-gst.log`，
观察快照 `build/tailscale-udp-progress.txt`。正式源码和测试源均已按备份 SHA256 恢复，
Linux 默认仍为 ICE-TCP。含 SDP 的原始调试日志仅作本地诊断，不进入版本管理或用户报告。

两端基础 core / signaling / media 均通过：Windows 137.58 秒，Linux 109.10 秒；
日志分别为 `build/tailscale-windows-build.log` 与 Linux `build/tailscale-linux-validation.log`。
Windows 后台 `controlSurfaceRejectsLetterboxAndReleasesOnFocusLoss` 另通过，3 项、1.204 秒。
所有图形测试均在未切换 Win32 桌面 / 未映射 Xephyr 父窗口中运行实际 OpenGL，未展示测试窗口。

网络检查收尾时，Windows build / dist 客户端 SHA256 均为
`5F4EDB20F267EEEDAC916C56A2F90E34DDADA74BF915CD009DF2FC94CB9D5EF2`；
Linux build / dist 客户端 ELF Build ID 均为 `34b07c8b2874a0ce9f4ed719dc08ad54aaab7a4f`。
临时 UDP 测试源恢复后，Windows 测试强制重新编译，Linux 正式 TCP 构建 / 部署完成。
正常客户端已重新打开：Windows PID 8836，命令明确使用新 IP，窗口标题正常、IsWindowVisible=true、Responding=true；
Linux PID 654557，正常物理桌面窗口 `_NET_WM_PID` 匹配且 `Map State: IsViewable`。
双方为 demo 房间待加入，仅打开正常客户端不视作跨网画面 / 输入验收通过。
本次 Xephyr 与图形测试进程已清理，原信令 PID 6761 保留，未创建新的信令隧道。

结论：地址 / 信令和 host ICE 候选可以使用 Tailnet，当前真实中继链路下 TCP、UDP 都未通过画面与控制验收。
45 / 60 秒只是有界等待兼容，不是跨网失败修复；中继抖动与现有固定 200 ms 缓冲、发送突发、
关键帧恢复和缺少完整带宽估计 / 异步 pacer 都仍需进一步适配。
不能宣称代码无需优化、仅网络有问题，也不能宣称已支持任意公网或稳定高清高帧率。
Tailscale 路径解释参照官方 [Connection types](https://tailscale.com/docs/reference/connection-types)
与 [Poor performance between tailnet devices](https://tailscale.com/docs/reference/troubleshooting/poor-performance-tailnet)。

## 2026-10-09：独立实际光标与最终鼠标回归

保留 8 ms 最新移动合并，新增 XFixes 实际光标形状 / 热点 / 位置回传与 VideoSurface 独立图层。
控制端确认支持且取得有效形状后关闭采集内嵌光标；退出控制恢复采集光标，图像获取 / 编码失败回退。
只在有效控制画面内隐藏 Windows 光标；独立光标重绘不增加视频呈现帧数。

迁移 Tailscale 前，隐藏跨机 loaded-input 用例两端通过；Windows 报告 3 项、16.836 秒：
`build/windows-release/test-results/background-cursor-loaded-input.txt`。
滑块观察 87 次更新、最大间隔 53 ms；119 个输入到被控端实际位置回传样本中位 21.519 ms、p95 31.856 ms。
拖拽输入到解码视频变化的 69 个样本中位 59.04 ms、p95 71.488 ms、最大 80.836 ms。
位置回传测量不包含最终绘制 / 屏幕扫描；视频变化按滑块量化值近似关联输入，不是精确传输时间戳。
这些均为迁移前局域网隐藏桌面结果，不能用作当前 DERP 链路的延迟或物理桌面手感保证。
小缓冲和 videoconvert 线程数对照未显示稳定收益，均未保留；默认接收缓冲仍为 200 ms。

Tailscale 优先检查完成后，最终相关图形回归：

- Windows：移动合并 / 光标恢复、独立图层像素、黑边与失焦释放、全屏恢复，6 项通过、27.495 秒，
  报告 `build/windows-release/test-results/background-cursor-final.txt` / `.xml`。
- Linux：同上并增加真实桌面采集光标关闭 / 恢复和旧代次无效检查，7 项通过、1.214 秒，
  报告 `build/linux-release/test-results/hidden-drag.txt`。

新采集回归直接使用 ScreenShare，最初遗漏 MediaEnvironment::inspect 初始化导致测试 SIGSEGV；
补齐与现有媒体集成测试相同的初始化后通过，未改产品初始化流程。
Windows 光标像素断言按实际 devicePixelRatioF 取样，避免 125% 缩放下使用逻辑坐标读取物理帧缓冲。
测试仍在未切换的 Win32 桌面 / 未映射的 Xephyr 父窗口中执行实际 OpenGL，未展示测试窗口。
两端基础测试与部署沿用上节最终正式版本；本次后续只改测试初始化与文档，客户端哈希未变化。
独立光标仍经过可靠 DataChannel，网络往返 / TCP 排队无法消除；窗口拖拽内容仍依赖视频反馈。
当前跨网连接未通过，独立光标在新路径上的实际效果与正常物理桌面反馈仍未验收。

## 2026-10-09：连续拖拽与单光标（第一阶段）

新增 `controlSurfaceCoalescesMotionAndRestoresCursor`，连续输入 380 个移动事件后检查最后位置、
按钮松开后的无残留移动、失焦 / 停止控制清理与光标恢复。旧实现实际 x=0.1890625，
期望 x=0.78125，后台 Linux 回归失败；改为保留最新位置并在 8 ms 定时器发送后通过。
相关回归同时执行 `controlSurfaceRejectsLetterboxAndReleasesOnFocusLoss` 与
`expandedVideoKeepsSurfaceAndRestoresWindow`：Windows 5 项通过、16.286 秒；Linux 5 项通过、0.505 秒。
报告分别为 `build/windows-release/test-results/background-drag-surface.txt`、
Linux `build/linux-release/test-results/hidden-drag.txt`；均使用实际 OpenGL。

跨机控制用例改为按住鼠标、分 120 步连续移动，再松开；Linux 记录滑块更新次数和最大更新间隔，
Windows 同时验证控制画面本地光标为 BlankCursor。Windows 3 项通过、11.749 秒；
Linux 3 项通过、19.112 秒。Windows 当时观测到 79 次更新，Linux 最终观察文件记录 91 次、
最终值 100，最大间隔 37 ms。这是滑块值变化间隔，不是视频呈现延迟或鼠标总事件数。
点击、文字 `zjdesk123`、滚轮、拒绝 / 允许、双方撤销、Ctrl 释放与退会 / 重入均通过。
报告为 Windows `build/windows-release/test-results/background-drag-input.txt`、
Linux `build/linux-release/test-results/hidden-fixture.txt` 与 `output/remote-observations.json`。

基础 core / signaling / media 全部通过：Windows 115.62 秒，Linux 109.22 秒。
Windows `build.ps1 -Preset windows-release -Deploy` 与 Linux `build.sh --deploy` 成功；
Linux 构建日志为 `build/drag-linux-build.log`。Windows 构建 / 部署客户端 SHA256 均为
`DA2298E0B4DA15A52F42A52C55401E8DA9EF4015A21B39ABCF38E866E3E2AF74`；
Linux 构建 / 部署客户端 ELF Build ID 均为 `f4012353629184b2ec1d269ccf9de9e21088e4d4`。
后台测试使用未切换的 Win32 桌面、未映射父窗口的 Xephyr / X11，未显示测试程序。
已重新打开两端正常客户端；用户实际桌面拖拽手感与单光标显示待复核。
光标仍来自视频捕获；本次未改变视频接收缓冲、传输策略，也未证明任意网络下无卡顿。

## 2026-10-09：正常客户端首帧空白修复

现场 Windows 已入会并显示“正在接收对方屏幕”，但没有首帧。Windows 为用户手动关闭，未发现本次客户端崩溃证据。
`build/normal-windows-media-debug.log` 确认 ICE / PeerConnection connected、媒体包到达，
随后持续 `Dropping inter-frame before intra-frame` 与大量 expired NACKS，最终 15 秒启动超时。
Linux 物理桌面为 1920×1080 / 60 Hz；只读采集探针首个 VA CQP 8 关键帧为 627988 字节，
软件编码探针为 428021 字节。探针记录在 Linux `build/physical-va-frames.log` / `physical-sw-frames.log`。
当前直连 UDP 缓冲生效值为 425984 字节；现场 UDP SndbufErrors 从之前的 376068 增至 513602。
帧大小超过缓冲本身不能单独证明每次丢包，但与持续首帧缺失和发送错误一起构成此次修复依据。

修复限定在屏幕传输：Linux 真实桌面发送在候选收集前仅启用 ICE-TCP，使用可靠传输和背压，
测试源 / Windows 发送保留原候选策略；未实现自动回退或 UDP pacer。ShareStats 与界面显示实际 TCP / UDP。
统计及接收端零帧 / 丢包反馈不再等待首帧。新增 `receiverReportsFeedbackBeforeFirstFrame`：
修复前反馈断言失败，修复后 3 项零失败、3.046 秒；报告为 Linux `first-frame-before.txt` / `first-frame-after.txt`。

双端最终 core / signaling / media 全部通过，Windows 总计 116.27 秒，Linux 109.91 秒。
media 的物理摄像头与需显式开启的物理发送器默认跳过；后者另外实际执行，不将跳过当作验收。
Windows 构建日志 `build/first-frame-final-windows-build.log`；Linux `build/first-frame-linux-ctest.log`。
Linux TCP 桌面改动后的完整后台图形回归为 12 项通过、2 项显式跨机跳过、57.596 秒；
确认双端选中 TCP、真实 OpenGL 呈现与小字误差检查通过。其后新增的物理桌面专用用例按下表单独跨机执行。

物理桌面验证使用 Linux `zjdesk-media-tests physicalDesktopSenderWithoutWindow`，实际采集物理 X11 `:1`，
不创建任何测试窗口；Windows 使用未切换的独立 Win32 桌面接收与实际 OpenGL 绘制。
信令经本轮 SSH 18765 转发，媒体经实际跨机 ICE-TCP。连续停止 / 重开三次：

| 次数 | Windows 首帧等待 | 传输 | 实际图像尺寸 |
| --- | ---: | --- | --- |
| 1 | 4937 ms | TCP | 1920×1080 |
| 2 | 6369 ms | TCP | 1920×1080 |
| 3 | 1380 ms | TCP | 1920×1080 |

两端各 3 项零失败，Windows 31.709 秒、Linux 42.642 秒。验证实际解码、交付和呈现帧数为正，
每次停止均清空画面。报告为 Windows `test-results/background-physical-tcp.txt` 与 Linux `test-results/physical-tcp-sender.txt`。
该短测存在追赶帧与约 517 ms 最大解码输出间隔，不把某次瞬时超过 60 的计数称为稳定 60 fps。
`physical-tcp-udp-before.txt` / `physical-tcp-udp-after.txt` 中 Linux UDP SndbufErrors 均为 513602，无新增。

另行以隐藏 Xephyr / Win32 桌面回归远控：Linux 17.149 秒、Windows 9.911 秒，各 3 项零失败。
通过首帧 TCP 断言、点击、文字 `zjdesk123`、滚轮、拖动、拒绝 / 允许、双方撤销、Ctrl 释放及退会 / 重入。
报告为 Windows `test-results/background-tcp-input.txt` 与 Linux `test-results/hidden-fixture.txt`。
未显示测试窗口、未改系统代理 / Clash / 路由 / sysctl / 防火墙，未绕过本地授权门禁。
当前验证解决本次 Linux 画面启动问题；TCP 拥塞延迟、任意网络、Wayland 与持续稳定 1080p / 60 fps 仍未验收。
修复版已部署并重新打开两端正常客户端；用户两端入会、Linux 共享后明确回复“已经显示画面”。
Windows 构建 / 部署客户端 SHA256 均为 `4C30D8A7D4F2D3801C2C8AF6065C58F3C51377133C3FA1A94B68C34D7EFD065E`；
Linux 构建 / 安装 ELF Build ID 均为 `9a7e9fbfec2056ec1d5bbbda37480b09c57a5c08`。
本轮隐藏测试与 SSH 18765 转发已关闭，保留正常客户端与原信令服务。

## 2026-10-09：高画质远控基础改造，稳定 60 fps 尚未验收通过

本轮在原目录 `F:\projects\ZJdesk` 完成轻微有损 VP9 4:4:4、Linux VA 编码探测 / 软件回退、
丢包预算反馈、RED / ULPFEC、会话 UDP 缓冲与 RTCP 反馈预算，以及映射帧交接。
保留任务开始时已有的 24 fps 下限和逐档尺寸策略。未保留任何阻塞发包等待试验，未实现完整 GCC / 异步 pacer。
ToDesk Windows 包的只读线索仅用于安排排查方向，不能证明其 Linux 管线、默认参数或实测表现。
静态检查报告位于 `build/analysis/todesk-static-20261009/report.md`。

### 最终构建与本机回归

Windows 使用 Qt 6.8.3 / MSVC2022 x64 / GStreamer 1.26.11；Linux 为 Ubuntu 22.04 / GCC11、
同版 Qt / GStreamer、libnice 0.1.22，Intel iHD 22.3.1 / libva 1.14。
Linux 私有 GStreamer SDK 补充构建了 VA 插件；探测实际 `vavp9lpenc` profile 1 编码与尺寸切换后才启用。

| 最终测试 | Windows | Linux |
| --- | --- | --- |
| core | 24 项，零失败，9.776 秒 | 24 项，零失败，0.014 秒 |
| signaling | 9 项，零失败，3.567 秒 | 9 项，零失败，3.407 秒 |
| media | 15 项，零失败，跳过 1 项，107.619 秒 | 15 项，零失败，跳过 1 项，102.750 秒 |
| 后台图形回归 | 5 个相关用例，共 7 项，零失败，17.171 秒 | 完整界面回归，共 14 项，零失败，跳过 2 项，56.964 秒 |
| 无 VA 插件的软件回退 | 软件编码由 Windows media 回归覆盖 | 全屏 / 恢复与文字细节共 4 项，零失败，2.188 秒 |

项数包含初始化与清理。media 跳过物理摄像头；Linux 界面跳过需显式连接另一台机器的两个专用用例。
两端非 GUI CTest 总耗时分别为 121.16 / 106.21 秒。
新回归覆盖映射图像跨三次管线重启仍有效、写图像时分离、旧档位反馈拒绝、实际编码预算变化、
硬编 QP 调整、SDP 中 NACK / RTX / RED / ULPFEC，以及不重建连接的尺寸降低与恢复。

Windows 图形测试在未切换的独立 Win32 桌面中运行，仅执行全屏恢复、黑边 / 失焦释放、入退会、
小窗口 / 长昵称和本地预览五项，不将其称为物理 Windows 桌面采集的完整验收。
Linux 使用未映射父窗口中的 Xephyr `:77`、独立 Openbox 和实际 OpenGL / X11 采集；
父窗口状态为 `IsUnMapped`。未使用 offscreen 或 Xvfb，未显示测试窗口到用户活动桌面。
软件回退检查使用私有插件目录排除 `libgstva.so`，没有卸载或修改系统插件。

Linux 本机回环结果如下，不能当作 Windows 接收端或物理桌面的性能结果：

| 模式 | 编码 fps | 解码 fps | 显示 fps | 最终目标 |
| --- | ---: | ---: | ---: | ---: |
| 固定 30 | 30.05 | 30.05 | 29.98 | 30 |
| 固定 60 | 60.11 | 51.22 | 51.08 | 60 |
| 自动 | 50.00 | 49.01 | 49.13 | 50 |

硬编文字区域 RGB 平均通道误差 0.978、最大 5；软件回退平均 1.005、最大 6。
允许的轻微有损预算为平均小于 2、最大不超过 24，来自独立量化参数对照；仍检查原生像素尺寸。
CBR 硬编在当前驱动的小字误差较大，最终使用 CQP 8～16；这不是线路码率硬上限。
Windows profile 1 仍使用软件解码，RGB 转换和 OpenGL 上传仍存在，不能称为端到端 GPU 零拷贝。

### 跨机结果与失败记录

Windows `10.11.48.34` 控制 Linux `10.11.47.137`，两端使用 WLAN；信令经仅本轮创建的 SSH 转发访问，
媒体为实际 WebRTC 直连 UDP。Linux 采集上述隐藏 X11 桌面，Windows 在隐藏 Win32 桌面实际呈现。
接收缓冲保持 200 ms，保留原生 4:4:4 像素的验收断言。

- 加入 UDP 缓冲与 FEC 后的一次完整跨机通过：30 秒平均编码 60.04、解码 60.00、显示 60.10 fps；
  累计丢包为 0，Linux UDP SndbufErrors 增量为 0。480 列单像素彩线平均误差 0.700、最大 4；
  随后 60 秒共 228 次采样，零异常、227 次更新。但后段出现 60 / 50 fps 两种目标，不能称为全程稳定 60。
  12 次点击的画面反馈中位数 63.24 ms、最大 143.08 ms。此轮尚未加入最终 RTCP 预算设置。
- 相同产品配置复跑失败：30 秒平均编码 42.52、解码 35.46、显示 34.34 fps，丢包从 106 增至 220，
  解码输出最大间隔 880 ms，随后降到 1600×900，原生尺寸断言失败。一次通过不足以证明持续稳定。
- 逐包按平均码率等待、允许 64 KiB 突发的平均码率调度、40 Mbps 峰值限制三个候选均未通过跨机高清验收，
  已全部撤回；实验代码和报告仅保存在忽略目录，产品管线没有加入 sleep 或阻塞式 pacer。
- 重传跟踪实际观察到请求至到达约 466 ms，以及更长迟到；RTCP 日志存在反馈被几百毫秒的调度间隔抑制。
  单改 `rtcp-min-interval` 在 SAVPF 中无效，已撤回；明确 64 kbps 反馈预算后，调度日志约 16～25 ms。
  这是调度改善的证据，不能当作最终重传耗时或稳定帧率的保证。
- 最终代码关闭额外 GStreamer 调试后复测仍未通过高清性能验收：30 秒平均编码 50.02、解码 47.79、
  显示 46.78 fps，目标依次出现 60 / 50 / 45；这段保持 1920×1080，累计丢包由 0 增至 15，
  解码最大输出间隔 557 ms。480 列彩线平均误差 0.682、最大 4；12 次点击反馈中位数 78.02 ms、最大 142.51 ms。
  后续持续采样时仍降到 1600×900，95.270 秒处原生尺寸断言失败，未产生本轮完整 60 秒画质报告。

Linux 独立 ICMP 探测曾出现约 909 ms 往返，1500 字节不分片探测能通过；最终跨机过程 UDP
SndbufErrors 从 376068 到 376068，增量为 0。说明发送 socket 缓冲错误已非该轮全部迟到的解释，
不能因此排除应用传输问题，也不能把所有网络波动算成编码性能问题。未改 sysctl、系统代理、Clash 或路由。
本轮未通过“持续稳定原生 1080p / 60 fps”验收，后续需要可控链路与重复实验，再验证完整拥塞控制和异步发送调度。

原始产物：`build/stability-boundary-final-build.log`、`build/windows-release/test-results/`、
`build/analysis/stability-20261009/`；Linux 对应 `build/stability-boundary-final-build.log`、
`build/linux-release/test-results/hidden-all.txt`、`hidden-software.txt`。
最终高清失败快照为 `build/analysis/stability-20261009/final-capacity-failed/`；
其中 summary 将早于本轮帧率报告的旧画质文件标记为 stale，不作为本轮通过证据。

另行关闭高清 / 延迟附加采样，计数边界修复后的跨机功能回归两端各 3 项通过，Windows 20.231 秒、Linux 26.927 秒。
实际验证点击、文字输入、滚轮、拖动、拒绝 / 允许、双方撤销、Ctrl 释放及退会 / 重入；
这次通过用于证明远控功能和清理流程，不作为稳定 60 fps 或持续高清的证据。
报告为 `build/windows-release/test-results/background-boundary-final-input.xml` 及 Linux `hidden-fixture.txt`。

收尾用 UBSan 复现首次 RTP 计数为 qint64 上限时与初始 -1 相减的有符号溢出，
已将初始差计算的基数限制为非负，并扩充现有计数复位用例；修复后同一 UBSan 探针零报错。
该探针与修复前 / 后输出在 `build/analysis/stability-20261009/counter-boundary*`，不改变普通 RTP 计数下的档位策略。
图形与性能采样在该极端计数保护修复前完成；修复后重新完整构建、部署并通过双端 core / signaling / media。

### 部署与剩余范围

Windows 已通过 `build.ps1 -Preset windows-release -Deploy` 更新 `dist/windows-release`，
构建 / 部署客户端 SHA256 均为 `D1E616754A3DD1078B4A5D22CC9EECCBC1E64785DFE80BF37D9A93BD2F212254`。
Linux 已安装到原项目 `dist/linux-release`；构建 / 安装 ELF Build ID 均为
`4442beca990a6147019443603e26794d70aa35ff`，安装时改变 RPATH，两个整文件 SHA256 因此不同。
旧 Linux 部署客户端备份为 `build/backups/stability-20261009/zjdesk-before-final-install`。
没有启动或显示正常客户端。安装成功不代表干净电脑运行、物理桌面采集、任意 GPU / 网络或长期稳定性已验收。
公网 / TURN、Wayland、硬件解码、全程 GPU 纹理传递、物理摄像头与声音仍未验收。
本轮测试进程、Xephyr / Openbox 与 SSH 18765 转发已清理；已有信令服务保留，
最后观测为远控未开启、共享已停止、Ctrl 未按住。

## 0.4.5：2026-10-07 同步回主目录

按用户要求，后续工作使用 `F:\projects\ZJdesk`（分支 `ZJ-desk`）。同步前两目录 HEAD 相同，
主目录无未提交修改；同步的 8 个文件逐一核对 SHA256，原文件、校验清单及补丁保存于
`build/backups/ui-before-sync-20261007-231915`。修改保留为未提交状态，可在 VS Code 源代码管理中查看。

- 主目录执行 `build.ps1 -Preset windows-release -Deploy`，core / signaling / media 三组通过，
  总耗时 88.42 秒；对应 JUnit 为 22 / 9 / 13 项，零失败 / 错误，media 跳过 1 项物理摄像头用例。
- 在真实桌面运行小窗口与长昵称、两窗口入退会、全屏恢复、本地预览四项 GUI 回归，
  `build/windows-release/test-results/ui-sync.xml` 为 6 项（含初始化与清理），零失败 / 错误 / 跳过，9.593 秒。
- 从主目录执行 `run.ps1 -Preset windows-release -Name Windows`，客户端窗口可见且响应正常。
  构建与部署客户端 SHA256 一致：`23A98554E82147D8C1B835CFC804AF808DA89395EFF5D8E74E0608F97FEB67C7`。

下面的完整 desktop 与 125% 缩放结果来自 `C:\Users\29822\.codex\worktrees\9fba\ZJdesk`；
同步回主目录后只复跑上述四项 GUI 回归，未重新验收跨机或 Ubuntu 部署。

## 0.4.5：2026-10-07 工作台界面优化

本次在 Windows 独立工作区优化 Qt Widgets 布局与样式，保留原有会话、媒体和远控逻辑。
构建环境为 Qt 6.8.3 / MSVC2022 x64 / GStreamer 1.26.11，使用真实 Windows 桌面和 OpenGL，未使用 offscreen。

- 修改前 Debug 的 core / signaling / media 三组通过，零失败。
- 修改后 `build.ps1` 的 Debug 三组通过；相关七个 GUI 用例通过，报告为
  `build/windows-debug/test-results/ui-design.xml`（9 项含初始化与清理，零失败 / 错误 / 跳过）。
- 最终执行 `build.ps1 -Preset windows-release -GstRoot <本机 SDK> -GuiTests -Deploy`，
  core / signaling / media / desktop 四组全部通过，总耗时 175.65 秒，完成 `dist/windows-release` 部署。

Release JUnit 记录如下，项数包括初始化和清理：

| 报告 | 项数 | 失败 / 错误 | 跳过 | 用例耗时 |
| --- | ---: | --- | ---: | ---: |
| core.xml | 22 | 0 / 0 | 0 | 18.117 秒 |
| signaling.xml | 9 | 0 / 0 | 0 | 3.638 秒 |
| media.xml | 13 | 0 / 0 | 1 | 84.088 秒 |
| desktop.xml | 14 | 0 / 0 | 2 | 69.145 秒 |

新增 GUI 回归覆盖 1040×720 小窗口、40 字中文昵称、右侧滚动访问操作、错误后重新入会、
1280×820 布局，以及键盘切换诊断页。已有用例继续验证同一画面控件的全屏放大 / Esc 恢复、
键鼠黑边与失焦释放、本地动态测试源、真实桌面共享及单像素文字细节。
普通 QWidget 抓图曾出现 OpenGL 组合截图异常，新增截图改用 `QScreen::grabWindow`；实际原生窗口和 framebuffer 检查正常。

以 `QT_SCALE_FACTOR=1.25` 重新运行 Release 的小窗口 / 长昵称及全屏恢复两项用例，全部通过。
报告为 `build/windows-release/test-results/ui-scale125.xml`（4 项含初始化 / 清理，零失败 / 错误 / 跳过）。
全屏像素断言先统一 QImage 的 DPR 元数据，再逐像素比较，避免把 1.25 与 1 的元数据差异当作像素错误。
本次检查了工作台、诊断页、错误状态、本地预览和 125% 缩放的真实截图。

通过 `run.ps1 -Preset windows-release -Name Windows` 冷启动新版客户端，原生窗口可见、位于前台且响应正常；
最终保留一个正常客户端及脚本创建的本地信令服务，未替用户加入房间或授权远控。
截图为 `build/windows-release/output/ui-client-startup.png`；其他截图在同目录及 `scale125/output/`。
部署与构建客户端 SHA256 一致：`20CF60985247E7448530FAE25573E892A4FA88FCD8B4B1B348376F3221FD7CAF`。

跳过项为显式跨机远控的两个专用用例及物理摄像头释放用例。本次未重新验收 Ubuntu / 跨机远控、
物理摄像头、扬声器或干净电脑部署，也未同步远端 Linux。Release 部署生成与本机启动不代表这些场景已通过。

## 0.4.5：2026-09-30 远控延迟和 WebRTC 缓冲对照

Windows 11 控制 `192.168.3.13` 上的 Ubuntu 22.04 / X11；Linux 仍发送原始 1920×1080 VP9 4:4:4 零量化画面。
测试将按钮背景在绿 / 紫之间切换。Windows 在发送 DataChannel 输入时开始计时，Linux XTest 注入后回执，
Windows 同时记录首次包含新颜色的解码帧与 OpenGL 画面交换。每档 12 次点击，时间单位为 ms：

| 接收缓冲 | 画面反馈中位数 | 最慢画面反馈 | 输入回执中位数 | 结果 |
| --- | ---: | ---: | ---: | --- |
| 30 | 78.2 | 3507.2 | 11.3 | 启动阶段长停顿 |
| 50 | 64.5 | 6686.9 | 11.2 | 启动阶段长停顿 |
| 100 | 76.3 | 1282.4 | 10.8 | 长测启动阶段长停顿；稳态清晰 |
| 150 | 58.2 | 3829.1 | 11.3 | 累计丢包上升并降到 1280×720，单像素断言失败 |
| 200 | 64.9 | 106.0 | 12.0 | 首轮全套通过 |

100 和 200 ms 的完整对照各持续约 30 秒帧率采样加 60 秒动态画面检查：
平均显示 57.8 / 57.9 fps，235 次画面采样均零颜色异常、234 次更新；原始 1080p 单像素彩线的
平均通道误差均为 0.556、最大为 2。100 ms 启动前 12 次点击的累计丢包由 352 增至 895；
200 ms 对应为 533 保持不变。150 ms 由 215 增至 1811，30 秒内降分辨率并导致细节误差均值 84.829。
输入回执保持约 11～12 ms，长停顿发生在画面传输 / 解码链路，不能用缩短输入处理时间解决。
本次在这对电脑及局域网保留 200 ms 默认值；不把 WebRTC 统计中的零 RTT 当作有效测量值。
最终 0.4.5 构建部署后的 200 ms 跨机复跑也通过：12 次反馈中位 78.0 ms、最慢 686.3 ms，
回执中位 11.1 ms；启动阶段累计丢包从 388 增至 504，之后 30 秒平均显示 57.8 fps，
60 秒画面采样 235 次零颜色异常、234 次更新，累计丢包保持 504，单像素最大误差 2。
因此 200 ms 也不能保证每次启动都无卡顿，但明显避开了较小缓冲在本轮出现的多秒停顿和降分辨率。
Windows、Ubuntu 的最终 Release 四组回归各零失败（Windows 155.88 秒，Linux 136.79 秒），
`build` 和 `dist` 已部署 0.4.5；最终跨机 Windows / Linux 专用用例各 3 项通过、零失败。
专用测试窗口均已关闭；正常启动后 Windows 客户端窗口响应正常，Linux X11 显示一个 1200×780 客户端窗口，
信令服务监听 8765。两端预填 `demo`，本次没有替用户点击入会或授权。
报告为 `build/windows-release/output/cross-input-latency-*ms.json`、
`cross-video-performance-{100,200}ms.json`、`cross-video-quality-{100,200}ms.json` 与
`cross-text-detail-{100,200}ms.json`；最终复跑的持续画面报告另有 `-200ms-final.json` 后缀。
这组数据不代表其他电脑或公网性能。

## 0.4.4：2026-09-29 文字细节与原始像素全屏

用户反馈远程 Linux 的文字和字母粗细不均，Linux 地址更新为 192.168.3.13。
共享改为 VP9 / Y444 / 零量化；编码及解码显式传递完整范围 BT.709。
全屏取消留白和底部控件，退出按钮覆盖在画面上；保留同一个 VideoSurface 与远控连接。
低分辨率稳定后优先恢复上一分辨率的 5 fps，再提高帧率。

Windows / Ubuntu 22.04 最终 Release 与真实桌面回归均通过并完成本机安装：

| 测试组 | Windows | Linux |
| --- | --- | --- |
| core | 22 项，零失败 | 同左 |
| signaling | 9 项，零失败 | 同左 |
| media | 13 项，零失败，物理摄像头跳过 1 项 | 同左 |
| desktop | 13 项，零失败，跨机专用用例跳过 2 项 | 同左 |

原始画面与真实 RTP 解码画面对比小字及彩色单像素细线，RGB 通道平均绝对误差：
Windows 0.962、最大 3；Linux 0.952、最大 2（每通道 0～255）。
旧 VP8 同样场景平均误差 13.495、最大 255；新编码保留细节，但 RGB 色彩转换仍有舍入误差。
全屏回归直接对比原始屏幕尺寸下的单像素黑白线，验证像素对应与 Esc 后窗口恢复。

第一次完整回归暴露 VP9 默认缓存 25 帧，在恢复到 5 fps 时延迟超过稳定期，随后错误降回低分辨率。
统计轨迹确认问题，显式设 lag-in-frames=0 后，两端在线恢复回归均约 49 秒通过，完整媒体组也通过。
没有通过提高测试超时掩盖问题。UI 自动帧率测量等待启动稳定期；Linux 全屏尺寸检查等待窗口管理器完成配置。
初轮完整日志为 build/text-clarity-windows-final2.log 与 Linux build/text-clarity-linux-final2.log。
补充跨机复跑时发现接收窗口被终端覆盖，显示仅约 4 fps；旧逻辑将此误判为性能不足，
30 秒内降至 1280×720，单像素线误差增至平均 84.829、最大 195，细节断言失败。
UI 现在将后台 / 最小化 / 不可见的显示测量标为不可用，仍反馈真实解码与交付速度；
返回前台清空旧计数基线。媒体回归增加启动后 6.5 秒无绘制的后台阶段及恢复显示，防止重新引入误降档。
恢复后的显示统计异步更新，最初固定等待 2 秒的断言提前检查了旧读数；改为等待实际统计回调，
后台不降档与恢复后超过 50 fps 的性能断言保持不变。最终各组报告以两端 test-results/*.xml 为准，
后台修复后的 desktop 日志为 text-clarity-*-final3.log，媒体重验日志为 text-clarity-*-media-final.log。
最终在 192.168.3.13 完成真实 Linux → Windows 画面及 Windows → Linux 键鼠验收，两端专用测试零失败。
保持 1920×1080；正常显示测量 30.036 秒，平均接收 32.173 fps、绘制 31.097 fps，最终目标 30 fps。
480 列单像素彩色线经网络与全屏绘制后平均通道误差 0.556、最大 2。
随后约 60 秒 framebuffer 压力采样共 235 次，零颜色异常、234 次画面更新，最低匹配率 98.88%，
全程保持 1920×1080 / 30 fps。点击、输入、滚轮、拖动、Ctrl + Esc、双方撤销与退会释放均通过。
最终画面直接保存实际 VideoSurface framebuffer，检查键鼠输入后单像素彩线依然保持细节。
全屏退出按钮另由实际桌面回归的 ui-expanded-video.png 验证。

第一轮控制也通过，但后段整窗截图仅 1×1，绘制变为采样触发的约 4 fps，自动档位随之降低；
该轮保留为 display-unavailable 诊断报告，不用它说明正常显示性能。
跨机 Windows 测试临时保持显示活动并在结束恢复线程状态。
QWidget::grab 的另一张诊断截图出现彩色边缘，与已验证的实际 framebuffer 不一致；
最终验收统一保存 framebuffer 并校验尺寸与单像素细节，不把失败截图当作正常显示证据。
没有修改系统电源配置或网络设置。
报告为 output/cross-video-performance-0.4.4.json、cross-video-quality-0.4.4.json、cross-text-detail-0.4.4.json，
跨机日志 build/text-cross-control-final4.log 与两端 test-results/remote-control.txt。
VP9 零量化增加 CPU 与带宽开销；本次跨机稳定约 30 fps，没有宣称稳定 60 fps、硬件编码或所有电脑适配完成。

最终更新 Windows / Linux 的 build 与 dist，Windows 客户端 SHA256 一致，Linux 两份均报告 ZJdesk 0.4.4。
关闭全部本任务测试窗口及测试服务，用 run.ps1 / run.sh 重启正常入口：Windows 一个响应正常的客户端，
连接 ws://192.168.3.13:8765；Linux 一个可见 1200×780 客户端与后台 0.0.0.0:8765 服务，均预填 demo。
两端加入 demo 后 Windows 请求控制，Linux 本地允许，点击放大可一对一查看本次 1080p 桌面。
Linux dist 仍依赖本机独立 GStreamer SDK 插件，本次不是干净电脑的独立安装包验收。

## 0.4.3：2026-09-29 清晰度优先与最高可用帧率

自动桌面从主屏幕原始物理像素尺寸及刷新率开始，在同一分辨率下细化帧率档位，
直到 5 fps 仍持续不足才降低尺寸。首轮等待 10 秒稳定，后续切换等待 4 秒；
两次持续不足按均值降档，连续稳定后试探恢复。低帧率仍保留文字画面码率，
VP8 采用八个 token 分区、最大量化值 32，媒体采样及帧交付周期为 4 ms。

Windows / Ubuntu 22.04 的最终 Release 构建、真实桌面回归与本机部署均通过：

| 测试组 | Windows | Linux |
| --- | --- | --- |
| core | 20 项，零失败 | 同左 |
| signaling | 9 项，零失败 | 同左 |
| media | 13 项，零失败，物理摄像头跳过 1 项 | 同左 |
| desktop | 12 项，零失败，跨机专用用例跳过 2 项 | 同左 |

新增回归先在旧实现失败，再在修复后通过：原尺寸内选择细分帧率、保留低帧率文字码率，
按主屏幕刷新率开始、两次慢反馈取均值，启动阶段短暂慢显示不会提前降档。
媒体回归仍覆盖持续慢显示触发在线切换尺寸，以及恢复后回到原尺寸；
信令回归覆盖 5～240 整数 fps 转发，拒绝零、过小、过大及小数目标值。
构建日志：build/quality-windows-final.log、Linux build/quality-linux-final.log。

0.4.3 最终真实跨机验收也通过：原始 1920×1080，正常显示测量 30.067 秒，
平均接收 59.893 fps、绘制 52.682 fps。随后 60 秒频繁读取 framebuffer 的压力检查中，
230 次采样零颜色异常、229 次画面更新，最低颜色匹配率 97.18%；
自动目标从 60 → 50 → 40 → 30 fps，但始终保留 1920×1080。
实际点击、输入、滚轮、拖动、全屏退出、双方撤销与退会释放全部通过。
报告为 output/cross-video-performance-0.4.3.json 与 output/cross-video-quality-0.4.3.json。
一次入会先收到 HTTP 200，独立 WebSocket 升级检查为 HTTP 101，重试后成功；未宣称根治该偶发网络问题。

## 0.4.2：2026-09-29 直接请求远控与画面放大

用户要求 Windows 控制 Linux 不依赖 Linux 手动共享，并可放大接收画面。
房间信令只增加能力 / 请求 / 撤销，服务器验证 requestId、绑定发送者并拒绝 grant / input；
Linux 本地允许后自动创建桌面 WebRTC，实际授权与键鼠仍走 DataChannel。
结束控制关闭本次自动创建的桌面传输，原有手动共享功能保留。

两端完整 Release 回归通过：core 17、signaling 9、media 12、desktop 12 个测试项，零失败。
media 物理摄像头跳过 1 项，默认 desktop 跨机专用用例跳过 2 项，随后单独执行实际跨机用例。
新增信令回归覆盖未入会拒绝、发送者覆盖、拒绝经信令发送授权 / 输入，以及成员离开。
原有房间隔离、旧流丢弃、旧授权 / 重放和 NACK / RTX 回归通过。

放大使用同一个 VideoSurface，进入全屏后图像区域增加，不改变 WebRTC 连接或控件的父级。
真实桌面回归检查 framebuffer 颜色、放大后归一化坐标和退出时的窗口几何恢复。
最初发现按住 Ctrl 后 Esc 不退出，改为仅针对当前窗口放大模式处理 Escape 事件，
两端 desktop 再次零失败，最终跨机用例也验证 Ctrl + Escape 返回并释放 Ctrl。
已查看 Windows / Linux 的 output/ui-expanded-video.png：图像与退出按钮均正常显示。

实际跨机 remote-control 用例两端零失败：

- Linux 没有点击 shareButton；首次请求在未采集桌面时拒绝，再次允许后自动显示。
- Windows 放大后点击、输入 zjdesk123、滚轮 10 → 11、拖动到 100 均在 Linux 原生控件执行。
- 放大退出、Windows 停止、Linux 本地停止和退会均释放已按下的 Ctrl。
- 自动启动的桌面在停止控制后清除，可以再次直接请求，撤销后的输入没有执行。
- 60 秒 framebuffer 检查共 232 次，颜色异常 0 次，222 次相邻画面更新，最差匹配率 95.9%。

跨机报告：Windows build/windows-release/test-results/remote-control.txt，
Linux build/linux-release/test-results/remote-control.txt；画面报告为
build/windows-release/output/cross-video-quality-0.4.2.json，日志为 build/cross-video-direct-control-final.log。
已查看最终放大后的实际 Linux 桌面，最后自动档位 1280×720 / 15 fps，未承诺固定 1080p / 60 fps。
一次早期入会失败日志为 WebSocket 升级收到 HTTP 200，重新连接后成功；
独立 TCP 升级检查收到本项目服务的 HTTP 101，未确定前一次响应来源，未修改代理或路由。
没有将这次重跑通过描述为实现自动重连或根治所有网络问题。

Windows / Linux 0.4.2 Release 已重新安装，Windows build / dist 客户端 SHA256 相同。
Linux Qt 部署使用绝对安装前缀；相对路径会被 qt.conf 检查拒绝，已改为正确前缀完成安装。
最终关闭全部测试窗口并通过 run.ps1 / run.sh 重启：Windows 一个响应正常的客户端，
Linux 一个可见的 1200×780 客户端和后台 0.0.0.0:8765 服务，两端预填 demo。
启动后的使用流程：两端加入 demo → Windows 请求控制 Linux → Linux 允许 → 自动显示桌面；
点击“放大画面”进入全屏，Esc / “退出放大”返回，任一端可停止控制。

## 0.4.1：2026-09-29 跨机 VP8 花屏修复

用户截图中 Windows 已获得控制权，视频接收约 15 fps，但画面内容大面积损坏。
Linux 当前桌面正常；通过同样的 Linux → Windows WebRTC 路径和持续移动的已知颜色图案复现花屏。
接收端日志真实记录 RTP Lost timer、Discontinuity、missing first packet，不能以 FPS 正常认为画面正确。

修复只涉及共享媒体：已有 / 新建 transceiver 配置 do-nack，协商 NACK / RTX；
rtpvp8depay 启用 request-keyframe 与 wait-for-keyframe。
丢包后等待有效关键帧，避免继续输出依赖损坏参考帧的图像。
属性行为见 [GStreamer VP8 depay 文档](https://gstreamer.freedesktop.org/documentation/rtp/rtpvp8depay.html)。

新增回归使用真实两台电脑和 OpenGL framebuffer：Linux 显示持续变化的 480×280 蓝 / 白图案，
Windows 每 250 ms 检查图案区域颜色；每次至少 90% 采样点应落在已知颜色范围，持续 60 秒。
最终用缩小后的实际图像比较检查持续更新，防止冻结一张正常图片造成假通过。
此用例需要显式 ZJDESK_VIDEO_QUALITY_TEST=1；默认测试不额外采集桌面。

| 同场景 60 秒复验 | 采样次数 | 颜色异常次数 |
| --- | --- | --- |
| 修复前 | 236 | 63 |
| 首轮启用重传与关键帧恢复后 | 237 | 0 |
| 0.4.1 最终完整跨机回归 | 237 | 0 |

报告位于 Windows build/windows-release/output/cross-video-quality-before.json、
cross-video-quality-after-first.json；日志为 build/cross-video-before.log、cross-video-after.log。
已查看修复前保存的 framebuffer，形态与用户截图一致。
首轮视频已通过，后续本地撤销的独立 SSH 连接超时；验证工具改用持续连接的固定标记命令并重跑。
SSH 仍只用于读取验证结果和触发测试窗口的本地撤销，产品键鼠继续走 DataChannel。

最终报告 cross-video-quality-final.json：237 次采样，223 次相邻画面有变化，
最差颜色匹配率 92%，没有低于 90% 的异常样本；已查看实际接收画面截图，内容正常。
Windows / Linux 的 remote-control.txt 均零失败：拒绝后重新申请、点击、输入 zjdesk123、
滚轮 10 → 11、拖动到 100、Windows 撤销后不再执行、Linux 本地撤销、退会释放 Ctrl 全部通过。
过程中曾有一次入会超时，未进入视频阶段，原因未确定；重新启动验证窗口后完整用例通过。
当前产品仍需手动重新加入，不声称实现自动重连。

两端完整 Release / GUI 回归均通过：core 17、signaling 8、media 12、desktop 11 个测试项，
零失败；media 的物理摄像头用例和 desktop 的两个显式跨机用例在默认套件中按条件跳过。
随后单独运行上述跨机用例，包含新增的 60 秒画面检查。
媒体回归断言双方实际 SDP 包含 NACK 与 RTX，防止只设置属性但没有协商重传。
两端已安装 0.4.1 Release；Windows build / dist 客户端 SHA256 相同。
Linux 安装时 Qt 调整 ELF RPATH，不能直接以 build / dist 二进制哈希不同判断部署失败。
最终截图显示自动档位为 1280×720 / 15 fps，实际接收 14.9 fps、显示 15.0 fps；
本次没有承诺固定 1920×1080 / 60 fps，既有自适应仍依据实际速度选择档位。
已关闭测试窗口并通过 run.ps1 / run.sh 重新启动 0.4.1：Windows 一个可见且响应的客户端窗口，
Linux 一个 1200×780 客户端窗口和后台 0.0.0.0:8765 信令服务，默认房间 demo。
启动后需要两端手动加入，Linux 共享主屏幕，再由 Windows 请求控制、Linux 本地允许。
本次验证限于这两台电脑和上述 60 秒场景，尚未覆盖长时间运行、全部网络条件或 Wayland。

## 0.4.0：2026-09-29 Linux 部署与 Windows → Linux 远控

Linux 实机：Ubuntu 22.04.5 x64、GCC 11.4、i7-1355U / 12 逻辑线程、GNOME X11、1920×1080 主屏幕。
Qt 6.8.3 qtbase / qtwebsockets 和 GStreamer 1.26.11 在 Ubuntu 本机编译，使用用户目录独立安装前缀。
Windows 实机沿用 Qt 6.8.3 / MSVC2022 / GStreamer 1.26.11；两台电脑通过实际局域网连接。
源码路径为 Windows `F:\projects\ZJdesk`、Linux `/home/orbbec/ZJdesk/zjdesk`。

| 验证 | Windows Release | Linux Release |
| --- | --- | --- |
| core | 17 项，零失败 | 17 项，零失败 |
| signaling | 8 项，零失败 | 8 项，零失败 |
| media | 12 项，零失败，物理摄像头跳过 1 项 | 12 项，零失败，物理摄像头跳过 1 项 |
| desktop，真实桌面 / OpenGL | 11 项，零失败，跨机专用用例跳过 2 项 | 11 项，零失败，跨机专用用例跳过 2 项 |
| 单独跨机用例，含初始化 / 清理 | 3 项，零失败，9.0 秒 | 3 项，零失败，9.8 秒 |
| 本机部署目录 | dist/windows-release | dist/linux-release |

新增媒体回归直接建立两个真实 webrtcbin 端点，验证双方 DataChannel 打开、JSON 双向传输、
旧共享代次不发送。VideoSurface 回归验证未授权 / 画面黑边不发点击，中心坐标和失焦释放。
原有 ControlSession 回归仍验证错误成员、旧授权和重放序号拒绝。
本轮默认 build.ps1 / build.sh 不启动 GUI，表中 desktop 使用显式 GuiTests / --gui-tests 在物理桌面运行。

跨机服务器实际监听 Linux 0.0.0.0:8765；Windows 使用 ws://10.11.47.137:8765，房间 remote-test。
Linux 端一个 MainWindow 和临时验证窗口；Windows 端一个 MainWindow，Linux 真实 X11 桌面经 VP8 到 Windows。
Windows 的画面输入通过产品 DataChannel 和 SessionCoordinator 到 Linux XTest；
SSH 只读取验证窗口的实际结果及触发本地撤销测试，没有用于执行键鼠。

- 首次请求由 Linux 本地拒绝；第二次本地允许后 Windows 才能控制。
- Linux 实际按钮点击计数为 1，文本框收到 zjdesk123，滚轮将数值 10 改为 11，拖动滑块到 99。
- Ctrl 按下状态用 Linux queryKeyboardModifiers 读取；Windows 撤销后释放，随后 blocked 文本不执行。
- 再次授权并按下 Ctrl，Linux 本地撤销后 Windows 回到 Idle，Linux Ctrl 已释放。
- 再次授权并按下 Ctrl，Windows 退会后 Linux 撤销并释放，最终状态 Idle。
- 截图已查看：Windows output/remote-control-windows.png、Linux output/remote-input-linux.png。

回归曾因每次查询新建 SSH 连接而超时，Linux 实际输入已经发生；随后改用一个持续只读观察连接，
重跑两端专用用例全部通过。产品输入仍使用 DataChannel，没有将 SSH 当作远控实现。
新增控制栏使成员区域空间变小；收紧成员卡片间距后，单独真实 UI 入退会用例再次验证布局。
最终补足成员数字的字体高度并收紧工作区间距，两端真实窗口已查看，成员数完整显示。
Linux Qt 会提前处理 --name；客户端增加 --display-name，启动脚本使用该参数，实际 Linux 窗口昵称正确。
最终跨机用例在这些界面调整后再次通过，两端部署文件重新安装；随后关闭验证窗口和观察进程。
正式启动状态：Linux 一个客户端和一个后台服务，Windows 一个客户端，房间预填 demo，等待用户加入。
Linux 启动器当前使用 build/linux-release/bin；其 dist 安装会修改 ELF RPATH，不能用字节哈希相等验证安装。
Linux 部署仍依赖本机 GStreamer 插件前缀，未宣称可直接复制到干净电脑使用。

Linux 的真实桌面性能用例与 Windows 同类，用持续移动的图案验证画面变化，稳定后采样约 10 秒：

| Linux 本机目标 | 编码 fps | 接收 fps | 新帧绘制 fps | 测试进程 CPU |
| --- | --- | --- | --- | --- |
| 1080p / 30 | 30.0 | 30.1 | 30.0 | 9.7% |
| 1080p / 60 | 59.1 | 58.8 | 56.9 | 22.8% |
| 自动，最终 1080p / 60 | 58.8 | 59.0 | 56.5 | 22.6% |

性能表是 Linux 同一测试进程内两端编解码与显示的结果，CPU 为其占全部逻辑 CPU 的比例。
跨机专用回归验证功能、授权和释放，运行较短且 Windows 同时有媒体回归负载，未测量跨机稳定帧率。
不能以本机表格承诺网络下的 60 fps，也不能把截图的初始统计窗口当作稳定性能证据。

Linux 初期失败依次查明并修正：tar 时间戳导致测试旧对象未重编、webrtc / libnice Gst 插件未启用、
Linux 桌面路径缺 videorate。已重新构建并跑完四组回归；没有扩大 CTest 超时掩盖失败。
MediaEnvironment 增加 videorate / sctpenc / sctpdec 插件检查；脚本统一库与插件前缀。

最新 XML 位于两端 build/<preset>/test-results/；部署和完整日志位于 build/control-build-*.log。
物理摄像头、Windows 被控、Wayland、中文输入法、剪贴板、公网连接、长时间运行和干净新电脑安装仍未验收。

## 0.3.1：2026-09-29 共享 30 / 60 fps 与自适应验证

本机 Windows 11、i7-11800H（8 核 / 16 逻辑线程）、RTX 3060 Laptop，主屏幕采集 1920×1080。
Qt / MSVC / GStreamer 基线不变。采集使用 D3D11，VP8 为软件编码；没有新增 H.264 或硬件编码后端。

真实桌面测试使用持续移动的 900×650 色块 / 文字窗口，经房间信令和 WebRTC 到另一端，
在实际 OpenGL 窗口显示。运行稳定后采样约 10 秒：

| 固定目标 | 实际编码 fps | 实际接收 fps | 实际绘制 fps | 测试进程 CPU |
| --- | --- | --- | --- | --- |
| 30 fps | 29.7 | 29.8 | 29.6 | 14.9% |
| 60 fps | 37.2 | 37.2 | 36.3 | 23.6% |

两端和移动桌面都在同一测试进程，CPU 为该进程占全部逻辑 CPU 时间的比例，
包含采集、编解码、两个画面交付和测试动画；不等于单个实际客户端 CPU，也不是整机总负载。
接收计数取 VP8 解码输出，绘制计数取新帧后的 paintGL；目标 60 不表示实际达到 60。
帧数均为处理帧数，不能把静止桌面的重复画面当成每秒 60 次内容变化。
本轮其他本机回归有部分并行执行，结果不是空载实验室极限；不能据此保证所有机器的上限。

初轮旧 4 线程编码时，30 fps 接收约 29.7、60 fps 接收约 36.1；
按可用逻辑线程数选择编码线程后，60 目标仍没有稳定达到 60，因此自动策略不会强行保持该档。
自动档最终选择原尺寸 1920×1080 / 30 fps。自动报告包含先试 60 的阶段，
整段平均接收为 32.1 fps；切到 30 后的窗口约为 29～31 fps，不能将整段平均值误写为稳定档帧率。

报告：`build/windows-release/output/share-30fps.json`、`share-60fps.json`、`share-0fps.json`。
测试名为 `screenShareFrameRates:30fps` / `60fps` / `auto`，仅在显式 -GuiTests 时运行，
默认构建不显示窗口、不采集桌面。数据源、时长、采样窗口与 CPU 测量代码保存在 GUI 回归中。

| 验证 | 结果 |
| --- | --- |
| Debug core / signaling / media | 17 / 8 / 11 项，零失败，摄像头默认跳过 1 项 |
| Release core / signaling / media / desktop | 17 / 8 / 11 / 8 项，零失败，摄像头默认跳过 1 项 |
| 无 GStreamer 的 Debug 构建 | core / signaling 通过，之后已恢复媒体 ON |
| Release 本地部署 | 通过，实际客户端与构建文件一致 |
| SDK 环境隔离后的自动共享 | 3 项通过，含初始化 / 清理，选择 1080p / 30 fps |

计数包含初始化 / 清理。Release 最后缩短侧栏连接说明，完整性能读数在共享状态栏中显示，
避免侧栏文字被裁切；该 UI 改动后再跑默认回归和实际桌面共享用例，性能测试数字来自前述完整 GUI 回归。
最后单独桌面回归首次失败：部署测试产生的 Windows 防火墙提示将桌面变暗，
真实标记像素从预期高亮紫色变为 RGB 141 / 0 / 141，原先要求红蓝通道超过 200 的检查不成立。
接收 framebuffer 和主屏幕截图均包含该标记；改为检查颜色通道差异后，紫色变绿色、停止、
交换共享方和成员退出的实际桌面回归 3 项通过。没有操作防火墙提示或修改系统安全设置。
已查看此次 `build/windows-release/output/ui-screen-share.png` 的 0.3.1 界面，
侧栏连接说明完整；截图中的初始统计窗口不作为稳定帧率证据。
Debug 本次运行默认三组回归；本次新增性能 GUI 用例在 Release 上实际执行，
Debug 的 desktop XML 属于之前 0.3.0，不能说本次 Debug 也完成了这三组性能测试。

适配规则回归：

- 原尺寸先试 60 / 30 / 15；两次持续不足才降档，暂时单个慢窗口不触发。
- 显式模拟接收端显示停滞，经真实 stats 信令反馈，在线降低帧率后将 640×480 降到 480×360。
  视频持续解码，无重新加入房间；恢复实际画面交付速度并持续稳定后，在线恢复到 640×480。
- 4K、超宽、小尺寸的档位顺序、缩放比例、最低档及恢复迟滞有单元回归。
- share profile / stats 字段校验，旧流拒绝、共享启停、取消、同时开始、成员离开、服务关闭均通过。
- 接收帧、主线程交付和新帧绘制分别统计，显示真实速度，不写入每秒日志。
- 无 SDK 构建通过；恢复后 Debug / Release 媒体配置都为 ON。
- 部署目录临时加入 GUI 测试程序及 Qt Test DLL，清除 Qt / GStreamer 插件变量，
  PATH 只保留 Windows 系统路径；真实动态桌面自动档通过，临时文件已清理。

日志：`build/fps-baseline-release.log`、`fps-adaptive-release.log`、`fps-final-release.log`、
`fps-final-release-ui.log`、`fps-final-debug.log`、`fps-without-media.log`、
`fps-deployment.txt` / `.xml`、`fps-final-ui.txt` / `.xml`。
首次失败保留在 `fps-final-ui-first-failure.txt` / `.xml`；原因诊断见
`fps-marker-diagnostic.txt` / `.xml` 与 `build/windows-release/output/share-marker-*.png`。
服务停止媒体回归出现 `QSocketNotifier: Invalid socket specified` 告警；预期状态清理和用例通过，
告警根因尚未定位。另有 SDK 旧 ksvideosrc 的弃用提示，当前仍使用 Media Foundation 摄像头。

这些结果是本机双端验证和慢显示模拟，尚没有第二台实际电脑、弱电脑、4K 实屏、
真实网络限速 / 丢包、长时间升降档或 Ubuntu 22.04 的验收。
CPU 占用和适配阈值不是硬件性能保证；公网 TURN、实际 RTT / 码率、硬件编码等仍未实现。
摄像头模式策略没有变化，物理摄像头没有在此轮重复验证；语音转写和会议纪要保持暂缓。

## 0.3.0：2026-09-29 历史 Windows 验证

平台与 SDK 沿用 0.2 的 Windows 11 x64 / Qt 6.8.3 / MSVC2022 / GStreamer 1.26.11。

| 项目 | Debug | Release |
| --- | --- | --- |
| 客户端 / 服务 / 媒体构建 | 通过 | 通过 |
| core | 15 项通过 | 15 项通过 |
| signaling | 8 项通过 | 8 项通过 |
| media | 10 项，零失败，物理摄像头默认跳过 1 项 | 同左 |
| desktop（显式 -GuiTests） | 5 项通过 | 5 项通过 |
| 物理摄像头最高模式 | 单独 3 项通过，含初始化 / 清理 | 未单独运行 |
| 无媒体 SDK 构建 / core / signaling | 通过，之后已恢复启用媒体 | 未单独运行 |
| Qt + GStreamer 本地部署 | 不适用 | 通过 |
| 部署目录隔离 SDK 后真实桌面共享 | 不适用 | 单独 3 项通过，含初始化 / 清理 |

计数包含 Qt Test 初始化 / 清理，跳过摄像头不等于硬件验收。
最新默认构建只执行 core / signaling / media；desktop 报告来自显式 GUI 完整测试。
最后仅增加旧 SDP 与信令中断回归，两种配置的三组默认测试已再次通过。

新增真实验证：

- 模式选择：1920×1080 / 30 fps 优先于 640×480 / 120 fps；同分辨率按分数帧率比较，
  raw / JPEG 与范围模式测试通过；无尺寸 / 帧率的 caps 不伪造模式。
- HP Wide Vision HD Camera 公布最高模式为 NV12 1280×720 / 30 fps；实际预览 caps 与尺寸一致，
  三次打开，每次至少三帧，停止后均能重新打开。记录为 `build/camera-max.txt` / `.xml`。
- 测试源通过真实 WebSocket 协商、WebRTC / ICE / DTLS / SRTP、VP8 编解码到另一端 QImage，
  不是信令转发图片；连续三次停止 / 重开，接收画面尺寸及动态变化通过。
- 共享方交换、同时点击、开始后取消、退出清理、信令服务关闭清理均通过。
  旧流 ICE 在停止后被拒绝，旧流无效 SDP 在新流运行中也不会终止新流。
- share 动作校验、服务器覆盖伪造发送者身份的协议回归通过。
- Windows D3D11 主屏幕采集接收 1920×1080，目标采集帧率 15 fps；GUI 测试显示已知紫色标记，
  改为绿色后接收端 OpenGL framebuffer 检测到对应变化，证明收到真实桌面内容。
  停止后画面清空，换另一端共享及对方离开清理均通过。
- 已查看 `build/windows-debug/output/ui-screen-share.png` 的真实 UI；后续修正共享期间连接说明，
  共享显示目标帧率，避免误称实测实时 FPS。RTT / 实际码率统计仍未实现。
- Release 便携目录临时加入 GUI 测试程序与 Qt Test DLL；清除 Qt / GStreamer 插件环境变量，
  PATH 仅保留 Windows 系统目录；真实桌面颜色变化、停止、交换共享方通过。
  临时文件已清理，记录为 `build/deployment-share.txt` / `.xml`。

构建记录：`build/validation-0.3-debug.log`、`validation-0.3-release.log`、
`validation-0.3-release-regression.log`、`validation-0.3-without-media.log`。
默认 Debug 与 Release 的媒体配置均为 ON，SDK 路径仍在各自 CMakeCache。

首次回归发现并修复两项实际问题：对方退出后再次发送 stop 引发 peer_unavailable，
以及停止尚未确认时按钮文字已恢复导致交换共享方测试过早点击；当前回归均通过。

当前没有双向摄像头通话、麦克风 / 远端音频、系统声音共享、远控、转写或会议纪要。
Ubuntu 22.04 / X11、不同电脑之间、其他摄像头、长时间稳定性、干净电脑、公网 TURN、
Wayland、云端 CI 和正式安装包尚未验收；这些结果不能用本机双客户端替代。

## 0.2.0：2026-09-29 历史 Windows 验证

平台与 Qt / MSVC / CMake / Ninja 沿用下面的 0.1 基线；媒体为 GStreamer 1.26.11 MSVC x64。
MSI SHA256 已与同目录下载校验文件匹配，Runtime / Development 提取在同一独立前缀，
没有修改系统安装记录或全局 PATH。SDK 位置见 handoff.md。

| 项目 | Debug | Release |
| --- | --- | --- |
| 启用 GStreamer 构建客户端 / 服务 | 通过 | 通过 |
| core | 通过，15 项 | 通过，15 项 |
| signaling | 通过，7 项 | 通过，7 项 |
| media | 通过，7 项，其中物理摄像头默认跳过 1 项 | 同左 |
| desktop | 通过，4 项 | 通过，4 项 |
| 单独物理摄像头回归 | 通过，3 项含初始化 / 清理 | 未单独运行 |
| Qt + GStreamer 本地部署 | 不适用 | 通过 |

计数来自实际 JUnit，包含初始化 / 清理；默认跳过摄像头不代表硬件验收。
详细日志：`build/validation-0.2-debug.log`、`build/validation-0.2-release.log`。
另外实际运行 `build.ps1 -WithoutMedia`，构建及 core / signaling 通过；之后重新启用 SDK，
默认三组回归通过并恢复 Debug 媒体配置。日志为 `validation-0.2-without-media.log` 和
`validation-0.2-media-restored.log`。最近一次默认构建不执行 desktop，GUI XML 来自前述完整测试。

实际覆盖：

- videotestsrc 真正产出 640×480 RGB 帧，五次启动 / 停止；已交付 QImage 在管线停止后仍有效。
- 主线程暂时不消费时，交接槽始终一帧，旧帧丢弃计数增长，没有无界 Qt 帧事件队列。
- 开始后立即取消、无效设备失败、失败后启动测试源、退会时停止；旧状态和旧帧不复活预览。
- 摄像头、音频输入 / 输出由 SDK 枚举；audiotestsrc 到 fakesink 的真实静音管线启动 / 停止。
- 两个真实窗口的信令操作及 OpenGL 上下文回归；本地预览两次 framebuffer 不同，停止清空画面，
  再开始收到帧。已检查 `ui-local-preview.png` 的实际布局和画面。
- HP Wide Vision HD Camera 通过 Media Foundation 三次打开，每次至少收到三帧，停止后能重新打开；
  `build/windows-debug/test-results/camera.xml` 为零失败、零错误、零跳过。
- 初次硬件测试确实失败：旧 ksvideosrc 被排除；MF 默认格式协商没有产出帧，固定 raw 640×480 / 30 FPS 后
  原生 gst-launch 三种格式探测通过，项目摄像头回归通过。没有改摄像头隐私权限。

本地部署验证：临时把 Release GUI 测试程序及 Qt Test DLL 放入部署目录，清除 Qt / GStreamer
插件变量，PATH 仅保留 Windows 系统目录；动态预览、停止、重启用例通过，零失败、零跳过。
临时测试文件已清理，报告在 `build/deployment-preview.xml` / `.txt`。
再单独启动部署目录的真实 zjdesk.exe，创建标题为“ZJdesk · 跨平台协作工作台”的响应窗口，
正常退出码为 0；证据在 `build/deployment-app.txt`。首次检查早于窗口创建，改为等待实际窗口后通过。
这些结果证明本机部署的动态预览不依赖 SDK 路径，不代表已在其他干净电脑验收。
另外实际运行媒体配置下的 `run.ps1`，服务和 Alice / Bob 两个客户端均创建成功，
两个窗口响应；关闭两个客户端后脚本正常退出，三名子进程均清理。记录在 `build/demo-0.2.txt`。
物理扬声器听感、其他摄像头格式、Ubuntu 22.04、双人音视频、屏幕共享、远控、
语音转写、会议纪要、云端 CI、正式安装包和干净电脑均未通过验收。

## 0.1.0：2026-09-28 历史验证

日期：2026-09-28。验证平台：本机 Windows 11 x64。

实际工具：Qt 6.8.3 msvc2022_64、VS 2022 / MSVC 19.44、Windows SDK 10.0.19041.0、
CMake 3.31.6-msvc6、Ninja 1.12.1。

## 构建与测试结果

| 项目 | Debug | Release |
| --- | --- | --- |
| 客户端与信令服务构建 | 通过 | 通过 |
| core | 通过 | 通过 |
| signaling | 通过 | 通过 |
| desktop | 通过 | 通过 |

每个配置输出三份 JUnit XML：core 15 项、signaling 7 项、desktop 3 项，
共 25 项（包含各测试组的初始化和清理，实际功能 / 数据场景 19 项），失败与错误均为 0。
原始结果在 build/<preset>/test-results/，构建日志在 build/ 中。

实际验证内容：

- 两个客户端经过真实本机 WebSocket 进入同一房间，成员状态正确。
- offer / answer / ICE payload 转发，服务覆盖伪造的发送者 ID。
- 第三人被拒绝，现有成员离开后可以重新加入；空房间释放。
- 两个房间隔离，未加入房间的连接不能转发协商消息。
- 连接期间取消、连续 20 次入退会、服务停止与重新启动后的手动重新加入。
- 协议错误、大小限制、尾部换行房间号、授权撤销 / 旧代次 / 重复序号拒绝。
- 日志保持最多 300 条，JSONL 导出可再次解析。
- 两个真实 Widgets 窗口通过按钮完成入退会，OpenGL 上下文有效。
- 查看了原生窗口与工作台 / 诊断截图，实际界面清晰；截图不使用 QWidget::grab 捕获 OpenGL 合成层。

## 本地部署验证

Release 使用 CMake install 和 Qt 部署工具生成 dist/windows-release/bin/。
客户端采用 Windows GUI 子系统，双击不会附带控制台；信令服务保留控制台子系统。
移除测试进程所有 Qt 插件环境变量，PATH 只保留 Windows 系统目录后，
部署目录的客户端成功创建窗口并保持响应。
该验证证明本机启动不依赖 Qt SDK 路径，不代表其他干净电脑的全部运行依赖已经验收。

run.ps1 已通过 Windows PowerShell 5.1 实际启动验证，
创建一个服务和 Alice / Bob 两个客户端。脚本不修改系统环境，结束时清理自己创建的客户端和服务。

Ubuntu 22.04、GStreamer 启用构建、真实音视频与远控尚未验证。
云端 CI 和正式安装包尚未交付。

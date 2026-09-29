# 验证记录

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

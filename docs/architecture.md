# 架构与扩展约定

`PeerProfile` 保存单个对端别名及局域网 / Tailscale IPv4，使用 `QSettings` 的 `ZJdesk/network` 中 `peer/*` 键持久化。应用内匹配别名，不写系统 DNS。`PeerConnection` 将 `PeerRouteWorker` 放在独立 `QThread` 中，两条无代理 WebSocket 长连接并行探测对端服务端口，5 秒心跳、3 秒握手及 pong 超时，局域网优先，故障切换并在恢复后切回。每次探测重新计算源地址，地址族须匹配线路类型，配置代次过滤取消后迟到的结果。两端客户端均托管本地 `RoomServer` 以响应探测，别名模式从启动服务参数沿用端口，实际信令使用当前线路的 Linux 端地址。

只有 `SessionCoordinator` 应用已选线路，先释放旧媒体和输入授权，再更新进程级 WebRTC 地址绑定并重新注册 / 入会。5 秒重试保留会议房间、本端共享意图及远控发起意图，主动离开清除恢复意图。`remote_ready.reason` 区分传输断开 `disconnected` 与主动结束 `ended`，旧服务没有 reason 时要求重新操作，避免自动再次发起已被用户结束的控制。重连遵循现有审批设置，媒体和输入使用新的代次。UI 保存设置并展示线路和暂不可达状态，网络连接与探测由协调层和后台线程管理。线路重建期间媒体会中断，双机恢复效果以实际验收为准。

会议控制的信令撤销和 DataChannel 授权可交错到达。观看者在当前共享期间记录已结束请求，拒绝这些请求的迟到授权，并忽略当前请求的重复授权，避免重置输入序号。共享清理同时清除请求记录，旧共享消息继续由共享代次隔离。文件剪贴板启动入口限定为双人协助模式。

2026-11-10 WAN 1080p30 路径由 `ScreenShareWorker` 在新流启动时选择。`NetworkSetup` 成功验证后同时写入进程级源地址 `ZJDESK_WEBRTC_LOCAL_ADDRESS` 和对端地址 `ZJDESK_WEBRTC_PEER_ADDRESS`。源地址属于 `100.64.0.0/10`，或对端不在该源地址所属网卡的子网内时启用 WAN，也可通过 `ZJDESK_SHARE_WAN=1` 显式启用。`ShareQuality::Mode::WideArea` 保持比例、不放大源画面，最高 1920×1080 / 30 fps，GCC 最低 128 kbps、初始 4 Mbps、默认最高 12 Mbps。编码 / 对端有效帧率连续不足时沿用原有尺寸与帧率降档机制，稳定后逐步恢复。`ZJDESK_SHARE_LOW_BANDWIDTH=1` 优先选择原 960×540 / 15 fps、128～800 kbps 策略。两种模式使用软件 VP9 profile 0 / I420、CBR 与 GCC，编码目标保留 20% 余量，默认允许 UDP，显式 `ZJDESK_WEBRTC_GCC=tcp` 保留 Linux ICE-TCP 限制。显式最大码率继续按 128000～40000000 bit/s 校验，GCC 估计和当前画质档位共同约束编码目标。RTP / 修复与关键帧突发由实际网络统计观察。实测与验收边界见 [跨网 1080p30 验证](validation-wan-1080p30-20261110.md)。

客户端入口直接显示 `MainWindow`，在协作空间原校验码位置验证对端 IP：Windows 填 Linux IPv4，Linux 填 Windows IPv4。`session/network_setup.*` 用 UDP connect 选择访问对端的本机源地址（不发送数据），检查活动网卡、异步对端 ping 及信令 WebSocket，10 秒总超时，退出或编辑地址取消探测。Linux 客户端通过 `NetworkSetup` 持有的 `RoomServer` 自动监听 IPv4 端口（默认 8765），已有服务时通过后续握手验证；两端默认连接 Linux 地址，显式非回环 `--server` 参数保留。验证成功才注册远控服务、请求摄像头权限、启用远控及入会；编辑地址通过 `SessionCoordinator::leave()` 断开并撤销放行。成功的对端输入保存在独立 `network.ini`，不能代替下次验证。成功后将本机地址写入进程的 `ZJDESK_WEBRTC_LOCAL_ADDRESS`，屏幕共享和会议均绑定该地址。验证只证明地址和基础连通性，不能证明动态媒体端口、设备或实际远控可用。见 [启动网络验证](startup-network.md)。

客户端入口直接显示 `MainWindow`，在协作空间原校验码位置验证对端 IP：Windows 填 Linux IPv4，Linux 填 Windows IPv4。`session/network_setup.*` 用 UDP connect 选择访问对端的本机源地址（不发送数据），检查活动网卡、异步对端 ping 及信令 WebSocket，10 秒总超时，退出或编辑地址取消探测。Linux 客户端通过 `NetworkSetup` 持有的 `RoomServer` 自动监听 IPv4 端口（默认 8765），已有服务时通过后续握手验证；两端默认连接 Linux 地址，显式非回环 `--server` 参数保留。验证成功才注册远控服务、请求摄像头权限、启用远控及入会；此时仅保持在线，用户点击独立的「远程控制」按钮才调用协调层发起 IP 配对与控制请求；编辑地址通过 `SessionCoordinator::leave()` 断开并撤销放行。成功的对端输入保存在独立 `network.ini`，不能代替下次验证。成功后将本机地址写入进程的 `ZJDESK_WEBRTC_LOCAL_ADDRESS`，屏幕共享和会议均绑定该地址。验证只证明地址和基础连通性，不能证明动态媒体端口、设备或实际远控可用。见 [启动网络验证](startup-network.md)。
`start-both.ps1` 未传 `-LinuxSsh` 时提示查询方法并要求输入 Linux 用户名和 IPv4，不再内置开发机器的 SSH 地址；显式参数及原有网卡自动选择流程保留。

双端启动器的网络选择在 `select-network.ps1` 完成：查询 Linux 当前接口，枚举 Windows 当前可用的首选 IPv4 地址，按有线 / Wi-Fi 优先、Tailscale 兜底验证地址组合。自动选择只在两端均使用 Tailscale 时匹配 Tailscale 地址。每组均检查指定源地址与网卡的路由，并绑定该源地址探测 TCP 22。选中后将 Linux 地址用于信令目标，将两端地址分别传给已有 `ZJDESK_WEBRTC_LOCAL_ADDRESS`；媒体层及控制方向不变。显式指定地址优先且失效时停止。此逻辑只在启动时运行，不修改系统路由或实现运行中网络重连。

协作和会议使用同一个 `SessionCoordinator`，连接或入房期间禁止同时加入另一房间。信令 `join` 携带 `roomType`，服务端按房间类型和原始房间码联合隔离；未携带类型的旧客户端按协作处理。会议客户端必须在 `joined` 响应中收到 `meeting` 类型确认，避免旧服务忽略类型后混入协作房间。会议远控请求定向到当前共享者，按共享流和成员身份授权；协作远控通过服务端观测的 socket 地址和双方登记的对端地址匹配，候选不唯一则拒绝，按本机二次验证设置手动或自动授权后传输桌面。IP 匹配不提供身份认证；服务保留旧校验码协议兼容旧调用者。

会议界面启用 `setConferenceAutoConnect(true)`，使用 `MeetingMedia` 工作线程：摄像头与麦克风各采集一次，最多向三位远端分别编码 VP8 / Opus；三路远端音频经 `audiomixer` 后进入一个扬声器。每成员独立 WebRTC 管线、状态、45 秒连接期限及移除操作，一人离会或失败不会结束其余连接。两人显式呼叫 / 接听接口仍使用 `ConferenceMedia`，供原有接口及回归使用；协作房间不自动启动会议。

会议房间最多四人，协作房间最多两人。服务广播成员加入 / 离开，以 `targetPeerId` 定向转发；超过两人时未指定目标会拒绝。目标必须是同房间的其他成员，服务覆盖 `fromPeerId` 并保留消息长度与字段校验。每对成员以排序后的成员 ID 生成一致的 `sessionId`，ID 较小者发 offer；重新入会得到新成员 ID，旧协商不会进入新连接。早到协商消息有界暂存，失败后不循环重试。

`SessionCoordinator` 是成员和会议状态的唯一写入者。有任一远端实际收到音视频且 WebRTC connected 即为 Running，同时显示已连接数 / 远端成员数；单成员连接状态独立保留。隐藏状态按目标同步，入会默认值在采集前应用。最后一位远端离会、退出房间或共享采集失败会释放设备。

本机与各远端均只交接最新帧；媒体线程不向 UI 无界排队。共享本机帧的实际 UI 交付计数同时供各路编码自适应使用，保留原生摄像头采集、先降帧率再降尺寸的策略，最多每路两条编码线程。UI 仅调用协调层，预分配一个本机和三个远端 OpenGL 画面，按成员 ID 更新，不直接操作媒体或网络。

单画面全屏只修改 UI：保留原 OpenGL 控件及父级关系，临时隐藏兄弟控件、解除尺寸约束并调整布局伸展，退出时恢复窗口位置、状态和布局；不修改媒体会话。成员身份更换或画面清空时退出全屏，避免显示错人。协作画布全屏继续使用原输入坐标映射，隐藏本机校验码且不提供缩小按钮，按 Esc 恢复；会议画面的放大 / 缩小按钮保持原有行为。

Windows 全屏通过 Qt 6.8 的 `QNativeInterface::Private::QWindowsWindow::setHasBorderInFullScreen(true)` 保留一圈原生边框，以缓解 OpenGL 全屏切换引起的整屏闪黑。客户端及 UI 测试在 Windows 额外使用同版本 `Qt6::GuiPrivate` 头文件。边框约占每边 1 个物理像素，视频继续按实际客户区等比显示。后台原生桌面回归检查连续出帧、上下文复用、画面内容和窗口恢复，物理显示器的闪黑仍须在实际远控时复验。

会议屏幕共享由服务端按房间维护唯一 `streamId` / `ownerId`，首个共享请求获得共享权；同一时刻一人向其余最多三人发送。`session/meeting_share.cpp` 为每位观看者建立独立 `ScreenShare` 管线，沿用 VP9 / WebRTC 和定向协商；一人离开只移除对应链路，晚入会补建链路，共享者离开广播结束。桌面采集和编码当前按观看者各做一次，三路成本需要实机评估；摄像头的一次采集策略不变。协作远控保留原双人 `screen_` 路径。会议将远控消息放到当前控制者对应的共享 DataChannel，批准和主动切换均由共享者的 `SessionCoordinator` 修改状态，一次只授权一位成员。`InputReceiver` 在输入线程校验共享代次、成员身份、授权代次与序号；成员身份从本机通道绑定取得。撤销和切换先失效旧代次并释放键鼠，停止共享与断线复用同一清理路径。动态会议通道的输入回执排队回到通道对象，避免跨线程直接访问正在销毁的通道。标注继续使用原画布与信令，浏览 / 绘制模式释放键鼠并暂停远控输入。

`annotation` 信令由 `RoomServer` 校验当前会议、共享代次、归一化坐标、工具与画布容量，并覆盖作者 ID / 昵称 / 颜色；同房间广播，晚入会逐条节流回放当前画布。画布最多 128 条、每条 256 点，更新只能修改本人的笔画，清除全部仅共享者可执行。`SessionCoordinator` 持有客户端标注模型，UI 只提交绘制 / 清除意图。`AnnotationLayer` 在共享画面的实际显示区域叠加笔画，在右上角按作者 ID 去重显示颜色与昵称图例，笔画不单独署名；共享者的桌面透明层在绘制模式接收鼠标，退出后恢复鼠标穿透。Windows 请求将桌面标注层排除出采集；X11 会将桌面层采入视频，因此协商 `annotationsInVideo`，观看端只绘制本地草稿，避免再次叠加。画布不写入共享文件。使用与验证边界见 [视频会议](video-conference.md) 和 [多人标注验证](validation-meeting-annotations-20261108.md)。
界面从本机 `QSettings(IniFormat, UserScope, "ZJdesk", "meeting")` 读取三个入会默认开关，初始均开启，
通过 `setConferenceJoinDefaults` 交给协调层；每次新建媒体连接时取用，不改变当前通话。
媒体工作线程在管线进入 PLAYING 前应用静音和画面隐藏，远端通过当前会话的 `camera` 消息同步默认隐藏状态。

当前采用模块化单进程客户端与独立信令服务。将功能按依赖边界拆成 CMake 目标，
便于分别编译、测试和替换实际的平台实现，不引入微服务或通用插件加载器。
本文件是随代码维护的当前设计；原始产品目标保留在 original-project-plan.md。

## 当前依赖

屏幕统计通过 `media/frame_timing.h` 的有界累计直方图记录分段耗时，由 ScreenShare 经
SessionCoordinator 发布；VideoSurface 将 GUI 接收、绘制和 frameSwapped 统计经同一路径回传。
统计包含长停顿计数及实际解码器/输出格式，不引入 GUI 对 GStreamer 的调用。
当前产品仍为 VP9；HEVC 独立实验不代表产品协商或 RTP 路径已接入。
公网发送可以通过 `ZJDESK_WEBRTC_MAX_BITRATE` 配合 GCC 限定启动估计、拥塞控制上下限与编码目标；
配置只影响新流，未设置时保留既有行为。CQP 与底层中继缓存仍可能突发，不能将其视为硬性线速上限。
当前异网条件与配置边界见 [公网优化](wan-optimization-20261025.md)。
统计精度、验证边界及后续工作见 [1080p60 实施进度](1080p60-implementation-progress.md)。

```mermaid
flowchart TD
    UI[apps/client: Qt Widgets UI] --> CORE[zjdesk_client_core]
    CORE --> PROTO[zjdesk_protocol]
    SERVER[apps/signaling-server] --> SIGNAL[zjdesk_signaling]
    SIGNAL --> PROTO
    TESTS[Qt Test / CTest] --> CORE
    TESTS --> SIGNAL
```

| 目录 / 目标 | 职责 | 约束 |
| --- | --- | --- |
| protocol / zjdesk_protocol | 信封编码、版本、输入校验 | 不依赖 UI、GStreamer 或服务状态 |
| signaling / zjdesk_signaling | 房间成员、会议四人 / 协作两人限制、定向协商消息路由、心跳 | 不传输媒体和远控输入 |
| signaling / SignalingClient | WebSocket 生命周期与协议消息 | 不修改会议业务状态 |
| session / SessionCoordinator | 入退会、状态变更、成员和信令事件 | 是会议业务状态的唯一写入者 |
| diagnostics / EventLog | 有界事件记录、原子文件导出 | 不记录 SDP 内容或实际输入文本 |
| remote-control / ControlSession | 本地授权、控制代次、序号校验 | 由协调层处理请求、授权与撤销 |
| remote-control / InputInjector | Windows SendInput / Linux X11 XTest 键鼠输入、释放与真实光标读取 | 仅授权后调用；不依赖 UI Widgets |
| remote-control / InputReceiver | 独立输入线程、授权代次与序号门禁、执行结果回传 | 授权和撤销仍由协调层发起；输入与回执不等待 UI 事件循环 |
| media / MediaEnvironment | 可选 SDK 初始化、插件检查与部署环境选择 | 不推断通话连接成功 |
| media / MediaEngine | 设备枚举、内部采集回归、音频测试与有界帧交接 | Gst 指针只在媒体模块，当前不发送媒体 |
| media / ScreenShare | 桌面采集、WebRTC 协商、VP9 编解码与有界帧交接 | 独立工作线程，不直接修改会话业务状态 |
| platform / PlatformInfo | 真实平台、架构、会话信息 | 系统输入与采集适配留到对应功能实现 |
| platform / CameraPermission | Windows 桌面摄像头开关只读预检与系统设置入口 | 协调层持有预检状态，仅明确关闭时引导授权；未知不误报拒绝，实际访问由采集后端检查 |
| apps/client | 界面布局、显示状态、收集用户操作 | 不直接操作 socket、系统输入或 GStreamer 对象 |

客户端和信令服务是独立可执行文件。client_core 使用 Qt Gui 的 QImage，没有 Widgets 依赖，可以在无桌面的测试环境中验证。
当前模块较小，client_core 将业务模块放在同一静态库内；只在独立依赖、编译成本或复用需求出现时进一步拆库。

## 会话状态

```mermaid
stateDiagram-v2
    Idle --> Connecting: 加入
    Connecting --> Joining: WebSocket 建立
    Joining --> WaitingForPeer: 首位成员入会
    Joining --> PeerAvailable: 房间已有成员
    WaitingForPeer --> PeerAvailable: 对方加入
    PeerAvailable --> WaitingForPeer: 对方离开
    Connecting --> Failed: 连接错误或超时
    Joining --> Failed: 房间拒绝或超时
    WaitingForPeer --> Failed: 信令断开
    PeerAvailable --> Failed: 信令断开
    Failed --> Connecting: 手动重新加入
    Connecting --> Idle: 取消
    Joining --> Idle: 取消
    WaitingForPeer --> Idle: 离开
    PeerAvailable --> Idle: 离开
```

`PeerAvailable` 仅表示双方已入会。屏幕共享另有 Stopped / Starting / Running / Stopping / Failed
状态；Running 必须同时收到实际画面和 WebRTC connected 事件，不能由 WebSocket 连接直接判定。
媒体统计及接收端零帧 / 丢包反馈从管线启动后产生，不再依赖 Running，避免首个关键帧丢失时无法反馈。

每次连接创建一个新的 QWebSocket，回调检查当前 socket 身份。
退出时先清空当前连接标识，再断开回调与 socket，防止旧连接回调影响新会话。
信令和业务状态运行在 Qt 主线程，媒体管线与设备枚举在专用 Qt 工作线程运行。
服务器定期 WebSocket ping / pong；客户端当前依赖底层 socket 断开事件，完整的双端故障检测与恢复在媒体阶段补齐。

## 协议

统一信封：`{"version":1,"type":"join","payload":{...}}`，UTF-8 总长度最多 64 KiB。
客户端类型为 `join`、`leave`、`share`、`offer`、`answer`、`ice`、`control`。
服务响应为 `joined`、`left`、`peer_joined`、`peer_left`、`error`。
服务端根据当前连接所属房间转发协商消息，并覆盖 `fromPeerId`，客户端不能自行指定另一个房间或发送者身份。
`share` 包含 action=start/stop 和 streamId，服务验证动作和标识格式。
0.3.1 增加 action=profile/stats：profile 携带档位 revision、尺寸、目标 fps；
stats 携带同一 revision 下的接收端有效 fps。服务校验字段范围并覆盖发送者身份。
0.4.3 的 profile 接受 5～240 的整数 fps，以支持不同刷新率及更细的帧率档位。
共享 SDP 消息携带 sdp，ICE 携带 candidate / sdpMLineIndex；都携带 streamId。
接收端先校验成员身份和当前流标识，再解析 SDP 或 ICE；旧流事件被丢弃。
WebSocket 只传共享控制与协商信息，视频 RTP 由 WebRTC 的 ICE / DTLS / SRTP 通道传输。

0.4.3 默认从主屏幕物理像素尺寸及 Qt 报告的刷新率开始，最高目标 240 fps。
当前 ShareQuality 在相同分辨率下依次选择刷新率及低于它的 240 / 165 / 144 / 120 / 90 / 60 /
50 / 45 / 40 / 30 / 24 档位；达到 24 fps 仍持续落后才按比例降低尺寸。
尺寸依次受 1920×1080、1600×900、1280×720、960×540、640×360 约束，不放大源画面；
仅最低尺寸保留 20 / 15 / 10 / 5 fps。连续两次不足只降低一档，连续 30 次稳定后试探恢复。
低分辨率下优先试探上一分辨率的最低帧率，再提高帧率；不必先爬完低分辨率的帧率档位。
首轮留出 10 秒建立连接及稳定显示，切换档位后留出 4 秒，避免启动阶段的慢显示触发过早降档。
目标码率按像素数和 max(30, fps) / 15 计算，范围 2～40 Mbps；降低帧率不会继续压低文字画面码率。
2026-10-09 按高画质优先、允许轻微有损的要求保留 VP9 profile 1 / 4:4:4，软件量化范围改为 0～8。
软件编码使用 CBR、300 ms 编码缓冲、5 秒关键帧间隔，限制关键帧预算，并将强制关键帧最小间隔设为 500 ms；
这些是编码器参数，不是网络线路的码率硬上限。接收端 NACK / RTX 及默认 200 ms 缓冲保留，
transceiver 增加 RED / ULPFEC，目标冗余 20%；协商回归同时验证 VP9、RTX、RED 与 ULPFEC。
现有及后续创建的 rtpsession 设置 `rtcp-fraction=64000.0`，这里大于 1 表示绝对 bits/s，
而非比例。自动预算在启动阶段可能仅为 3.2 kbps，实测曾抑制期限内的重复 NACK；
明确反馈预算后，日志中的 RTCP 确定性调度间隔约 16～25 ms。预算不是固定发送流量，
也不能保证无线网络上的重传一定在默认 200 ms 缓冲期限内到达。
属性单位见 [GStreamer rtpsession](https://gstreamer.freedesktop.org/documentation/rtpmanager/rtpsession.html)。
RGB 转换显式使用完整范围的 BT.709；RTP 解包后的压缩 caps 同样传入颜色信息，避免解码器继承错误的默认范围。
当前属于轻微有损，RGB 转换也存在舍入误差；原生像素细字和彩线由回归测量，不能再称为无损。
VP9 的 lag-in-frames 显式设为 0，避免默认缓存 25 帧在低帧率下影响延迟与恢复判断。

Linux 优先探测 `vavp9lpenc` 的 VP9 profile 1，自动模式还验证尺寸缩小；任一失败回退 `vp9enc`。
硬编使用 VUYA / CQP 8～16、hierarchical-level=1、gf-group-size=1，禁用未来参考帧等待。
硬编前固定 RGB，使转换器在尺寸变化时重新分配 VA 缓冲，避免当前 VA 插件内部上传池保留旧尺寸。
硬编仍需 CPU 色彩转换和上传；Windows profile 1 接收仍是 `vp9dec` 软件解码，并非端到端 GPU 零拷贝。
该硬编插件是可选依赖，当前 SDK 不具备时仍可使用软件链路。
部署时用与应用相同的 SDK / 插件环境检查 `gst-inspect-1.0 vavp9lpenc`；
需要启用 VA 插件及匹配的 libva 驱动，不能仅凭系统支持 VA-API 就判定 4:4:4 编码可用。

接收端 stats 额外反馈累计 RTP 收包 / 丢包，发送端只接受当前 epoch / revision。
有效窗口至少 100 包；连续两窗口丢包率 ≥2% 将预算降至 85%，连续 15 窗口低于 0.5% 才逐步恢复。
预算下限 2 Mbps；软件更新 target-bitrate，硬编在 CQP 8～16 内提高量化值。CQP 忽略 bitrate 属性，
不能把预算当作实际码率。另增加同一 DataChannel 的有界往返探针，以最近 60 秒最小 RTT 为基线；
连续两窗口额外延迟 ≥80 ms 也降低预算，额外延迟 >20 ms 不恢复码率、≥30 ms 不升档。
探针包含两端媒体线程调度与往返传输，不能当作视频帧年龄或跨机单程延迟。
未回过探针的旧客户端不触发延迟降码率；消息由媒体层消费，不进入控制授权状态机。

2026-10-10 增加 `ZJDESK_WEBRTC_GCC=1` 对照路径：TWCC 显式加入 offer，接收端只接受 offer 中的映射；
回归检查 answer 的扩展与 transport-cc，不能用“创建了 rtpgccbwe”代替实际协商。
`request-aux-sender` 接入单个 rtpgccbwe，使用它自己的异步 pacing；不改 DTS、不追加 clocksync。
GCC 初始 / 上限 40 Mbps；下限取启动 profile 的编码预算与 8 Mbps 中较小者（profile 预算最低 2 Mbps）。
这是当前局域网高画质对照的下限，不能作为低带宽跨网保证；旧 2 Mbps 下限在后续小流量阶段仍有长停顿。
编码预算取既有预算与估计的 80% 中较小者；CQP 仍限制在 8～16。
反馈回调只覆盖最新估计；媒体线程每 250 ms 检查一次，仅在实际参数变化时更新编码器，避免重复重配置产生关键帧。
发包调度队列大于当前估计 100 ms 数据量时，仅在编码前跳过完整原始帧，不丢编码参考帧或 RTP 分片。
统计记录估计、调度队列字节和编码前跳帧计数；设为 1 时不禁用 UDP，TCP 候选仍可用。
`ZJDESK_WEBRTC_GCC=tcp` 使用相同的 TWCC / pacing，但 Linux 桌面仍强制 ICE-TCP。
UDP 候选在隔离 Edge 桌面完整短测通过，在物理 GNOME 三轮首帧复验失败，不用于日常客户端。
这是按进程显式启用的路径，不改变默认 TCP；当前 LAN / Edge 长测与补验结果见 validation.md。

当前局域网跨机实测出现 Linux UDP SndbufErrors，FEC 单独不能避免发送突发丢包。
通过 libnice 的 selected-pair 通知，在媒体线程获取本会话的已选直连 UDP GSocket，
申请 1 MiB 收发缓冲并读回实际值；不修改系统 sysctl、其他 socket 或代理。
本机 Linux 受系统上限限制实际为 425984 字节，Windows 为 1048576 字节，不能把申请值当成生效值。
2026-10-09 首帧修复：Linux 真实桌面发送在收集候选前设置 NiceAgent `ice-udp=false`、`ice-tcp=true`，
默认采用 ICE-TCP 避开该主机 UDP 大帧突发。libnice 0.1.22 的非 reliable agent 在 TCP 拥塞时仍可能拒绝 RTP，
不能保证分片必达或完整背压。测试源及 Windows 发送仍保留原候选策略；这是明确的默认策略，
不是自动 UDP→TCP 回退。未加入阻塞 sleep；这条默认路径不启用 GCC，新的显式 pacing 对照见上节。
实际选中候选的 transport 进入 ShareStats 与画面状态；TCP 没有可取得的直连 UDP socket，
其 sendBufferBytes / receiveBufferBytes 报告 -1，不代表 TCP 连接失败。
物理 Linux 桌面跨机停止 / 重开三次与远控回归已验证；TCP 拥塞可能产生队头等待，稳定 60 fps 尚未验收。
TURN 仍未验收。候选属性依据 [libnice NiceAgent](https://libnice.freedesktop.org/libnice/NiceAgent.html)。
媒体构建额外链接 SDK 已有的 libnice / GIO；其它 ICE 实现未做适配。

ScreenShare 将只读映射的 GstBuffer 交给 QImage，最后一个图像引用释放时 unmap；
写图像触发 Qt 隐式共享分离，停止 / 重开后保留的图像仍持有自身缓冲。单槽邮箱只留最新图像。
这移除交接处的一次整帧复制，RGB 转换与 QPainter / OpenGL 上传仍存在。
ShareStats 记录编解码均值 / 最大耗时、最大输出间隔、编码帧 / 关键帧大小、邮箱覆盖、实际编码器与回退原因。
encoderQuantizer 读回编码器的 qp 配置属性，没有逐帧解析 VP9 码流中的量化值。
codec 耗时只覆盖编码器 / 解码器 sink 到 src，不包含采集、网络或完整呈现延迟。
编码线程按 CPU 逻辑线程数选择；采样与交付定时器为 4 ms。NACK / RTX 和请求 / 等待关键帧仍保留。
WAN 接收端 webrtcbin 缓冲默认 400 ms，appsink 按时钟交付帧。局域网及保守档默认缓冲为 200 ms。`ZJDESK_WEBRTC_LATENCY_MS` 可选择 30、50、100、150、200 或 400 ms。WAN 和保守档 RTP 封包上限为 1100 字节，本地档为 1400 字节，为 1280 MTU 的 VPN 路径预留 RTP 扩展、修复和 SRTP 开销。
每秒读取 WebRTC RTP 收发字节数、收包数、累计丢包和抖动；远控测试输入带一次性探针，Linux 注入后经 DataChannel 应答，Windows 同时测首次解码帧与 OpenGL 画面交换。
对照测试中小于 200 ms 的缓冲在连接初期产生长停顿或降分辨率，故未降低默认值；数据见 validation.md。
这些是目标参数和自适应规则，不表示每台电脑或网络都可达到屏幕刷新率；实际接收、显示帧率单独统计。

0.4.2 在房间信令中增加 control 的 capabilities / request / revoke，
允许在尚无桌面连接时请求控制，requestId 绑定当前请求，服务覆盖发送者身份。
信令服务拒绝 control 的 grant / input 动作；授权和键鼠仍走 WebRTC DataChannel。
被控端按本机二次验证设置确认或自动批准后才能授权，每次授权生成新的 controlEpoch；
检查对端 ID、代次和单调递增序号后才接受输入。撤销或对方离开时立即清除授权。
输入执行器维护本程序按下的键与鼠标按钮，失焦、撤销、停共享、退会和析构时释放。
鼠标按钮输入自动请求执行回执，运行诊断的 `control-input` 事件记录发送序号、坐标、按下 / 松开、
注入是否被系统接受及往返 / 注入耗时。各探针独立计时，最多保留 64 个待回执项；授权更换或停止时清空。
回执表示输入 API 的接受结果，不代表目标窗口已处理事件或新画面已呈现。

## 9 位数字校验码远控

UI 不再提供协作房间码。应用启动时由协调层发送 `register_remote`，服务用系统随机源生成 9 位 ASCII 数字码。默认 `startup` 每次上线重新生成；`three_days` 和 `never` 携带本机随机身份凭据，服务将其 SHA-256 摘要对应的码与签发毫秒时间保存在 GenericConfigLocation/ZJdesk/remote-codes.ini，分别在签发满 72 小时后更新或持续保留。离线码仍被保留，但不能连接；匿名分配也避开保留码。重复使用同一固定身份的在线实例被拒绝，保存失败明确报错。客户端身份和策略保存在 remote-control 的 identity / codePolicy 设置中；更改下次上线生效，固定策略要求服务明确回传同一 codePolicy，旧服务不能静默降级为随机码。

客户端确认身份和策略写入本机配置成功后才应用选择；保存失败时保留原策略并显示提示，初次身份保存失败使用每次打开变化。

服务每 10 秒检查空闲的三天策略注册，满 72 小时后换码；配对中延后到断开，避免中断授权或媒体。协调层在在线等待时只更新本机码，不重置正在查询对方的超时或连接状态。断开信令注销在线路由，固定码预留记录保留；删除身份或服务注册数据、切换服务后不能保证恢复旧码。

`connect_remote` 校验码后，将两个已注册且空闲的客户端组成内部双人中继组，回复 `remote_paired`；内部组 ID 与校验码独立，旧版 join 无法加入此组。错误码、自己码、忙碌和离线不改变在线注册；单次查询限一个未完成请求、8 秒超时。服务仍负责覆盖实际发送者身份。

控制方收到能力后自动发送原有控制请求。`requireRemoteApproval` 默认开启，由 UI 持久化到本机 `QSettings(IniFormat, UserScope, "ZJdesk", "remote-control")` 的 `requireApproval`；关闭后仅对已配对的校验码请求自动批准，旧协作协议仍要求本地确认。授权、代次、输入序号、释放和文件复制门禁复用原流程。修改设置不追溯批准当前请求。

快速重连时，两端旧桌面流可能仍在异步停止。校验码模式在清理期间暂不声明可控，当前代次 stopped 后重新发布能力；控制方保留尚未发出的自动请求，在自身停止完成和对方能力到达两个时机尝试，发送成功才清除待请求标记。不会在等待清理时提前批准，也不要求用户额外点击请求。

`disconnect_remote` 或任一端离线清除中继组，另一端收到 `remote_ready`，停止媒体并清理授权 / 文件 / 回执，保留在线码。清理媒体时不向已拆除的组发送 stop；此前排队的 not_joined 不使在线注册失效。会议保留原来的 join 协议：空闲远控可切换到会议（注销旧码），离会后重新注册远控。现有一个协调层仍只保留一场活动协作或会议，需要同步升级客户端及服务。

校验码模式的 `stopControl()` 直接走 `disconnectRemote()`：本端立即撤销授权、清理桌面和成员，清除待自动请求标记，发送 `disconnect_remote`；对端通过 `remote_ready` 清理。两端返回在线等待，不自动重拨。UI 在配对成功时清空输入，下次必须再次输入或选择历史码并提交连接。

`remote_paired.peers` 增加对方公开在线 `code`（不包含身份凭据），协调层发出 `remotePeerConnected(code, name)`。UI 将最近 10 条成功配对按服务 URL / 校验码去重，保存在 remote-control 的 `recentConnections`，仅展示当前服务记录；列表显示首字头像、昵称、校验码。选择仅填入，仍走服务查码和本机二次验证策略，不缓存授权。服务和客户端需同步更新才能获取双方历史信息；历史码不保证持续有效。

## 0.4 Windows 控制 Linux X11

共享方创建有序可靠的 `zjdesk-control` DataChannel，接收方在 prepare-data-channel 中挂回调。
远端名称直到 DCEP 到达才赋值，因此在 on-open 与实际消息处校验名称，不能在准备阶段提前过滤。
GStreamer 回调通过已有生命周期 / 共享代次保护的队列回到媒体线程，再交给协调层。
DataChannel 的 GObject 引用跨队列持有，旧流回调不影响新连接；单条控制消息最多 4096 字节。

双方入会后通过信令交换控制能力，媒体与原生输入后端可用的 Windows / Linux X11 端声明可以被控制。
请求先进入 Pending；二次验证开启时等待本地允许，关闭时仅校验码配对模式自动调用同一个 grantControl 路径；未授权不采集桌面。如果没有桌面连接，由协调层自动启动。
DataChannel 打开后发送带 requestId / controlEpoch 的授权；控制方丢弃已取消或旧请求的授权。
校验码模式结束控制时解除双方配对并停止全部桌面传输；旧协作房间模式只结束本次自动启动的传输，手动启动的共享在控制结束后保留。
请求、待本地批准、控制中、被控制中分别是独立 RemoteState；控制请求与本地允许后的连接等待为 60 秒，被控方等待本地批准为 30 秒。
授权后每条输入携带字符串编码的 64 位控制代次与序号，ControlSession 拒绝旧授权及重放。
失去 DataChannel、停止共享或房间失败立即撤销；信令入会本身不会自动授权。

### Windows ↔ Linux 文件剪贴板

`SessionCoordinator` 持有两个 `FileClipboard`，分别处理被控方 → 控制方与控制方 → 被控方，批次独立；Windows / Linux 均可担任任一角色。
仅在远控授权及双方文件能力握手完成后激活，绑定当前 controlEpoch；反向额外通过 `fileClipboardReverse` / `file_ready.reverse` 协商，兼容旧对端的单向能力。
撤销、停止共享、断线或通道失效立即停止。UI 只展示协调层的文件状态，不直接访问剪贴板或文件传输。
通过 Qt 剪贴板读取 Windows 原生文件 URL 或 Linux 的 `text/uri-list` / `x-special/gnome-copied-files`，仅复制用户选中的本地普通文件和目录；
不会解释文本路径，剪切也只复制，不删除源文件。

文件消息通过 `_zjdeskFiles: true` 在现有可靠有序的 `zjdesk-control` DataChannel 上复用，媒体层将文件与输入消息分别分发。
普通控制消息仍限 4096 字节，文件 JSON 上限 32768 字节；每次读取 16384 字节，等待接收方请求下一块，避免文件数据无界排队。
文件与输入共享通道及带宽，低带宽网络中一个文件块仍可能增加输入延迟；反向文件消息使用 `reverse: true` 路由到独立批次，消息校验控制代次、批次、文件序号和偏移。
接收方先写独立临时目录，完整接收后一次发布本地文件 URL，在 Windows 由 Qt 提供原生 `CF_HDROP`，在 Linux 同时提供 GNOME 文件列表，因此文件管理器可直接粘贴。
发布的剪贴板带 `application/x-zjdesk-file-clipboard` 标记，发送方忽略此类导入内容，避免自动来回传输；用户在文件管理器重新复制时仍是新的操作。
完成文件保留到客户端退出，收回控制不删除已发布的文件；取消只清除未完成的缓存。本机复制新内容会取消正在接收的旧批次。
剪贴板被占用时显示失败，清理未发布的缓存，不报告已就绪。每批最多 4096 项，拒绝路径穿越、符号链接、Windows 保留名称、大小写重名和无效数据；
读写失败或 30 秒无响应终止本批。该功能需要双方升级，不启用文本或普通会议文件同步。

VideoSurface 按实际帧的等比显示区域生成归一化坐标，黑边不发送点击。
鼠标移动保留最新位置，由 8 ms 精确定时器合并发送，避免原 16 ms 限流直接丢弃末次位置；
定时器节奏不等于跨机实际更新率。按钮、滚轮与键盘事件先发送待处理移动，再发送离散事件，
保证拖拽顺序；失焦、停止控制或清空画面取消待发送移动并释放键鼠，避免旧位置延后执行。
已授权控制且指针处于有效画面内时，取得有效远端光标后以本地系统光标显示其形状和热点，按画面比例缩放并缓存形状。
本地系统即时移动光标，不等待输入往返或视频重绘；此时不重复绘制远端位置。黑边、离开画面、停止控制或无画面时恢复默认光标。
尚未取得有效形状时仍隐藏本地光标，保留采集光标回退。
新控制端确认支持独立光标后，Linux 使用 XFixes 读取实际光标形状、热点和主屏幕位置，
每 16 ms 检查变化，并在执行输入后立即检查；位置或形状有变化才经现有可靠 DataChannel 回传。
形状仅在变化时发送 PNG，限制为 128×128、2400 字节；接收端验证控制代次、递增序号、
归一化坐标、图像尺寸和热点后，更新本地光标形状；指针在画面外时仍在 VideoSurface 独立绘制被控端位置，不计入视频呈现帧数。
得到有效形状后关闭 ximagesrc 内嵌光标，停止控制时恢复；旧控制端不确认支持、形状获取或编码失败时
保留或恢复采集光标。远端形状 / 可见性确认、实际输入执行和窗口内容反馈仍受网络及视频处理影响，
本地指针即时移动不代表远端已经执行。Linux 构建额外依赖 Xfixes。
InputInjector 将坐标映射到主屏幕物理像素，
通过 XTest 执行鼠标、滚轮、常用字母 / 数字 / 功能键 / 修饰键；UI 不调用 X11 API。
Windows 使用 SendInput 执行同一套归一化坐标与 Qt 键码协议；主屏幕物理像素坐标映射到整个虚拟桌面的绝对坐标，兼容负坐标的副屏和 DPI 缩放。
键盘使用扫描码与扩展键标志，Pause 保留虚拟键路径；只记录系统已接受的按下事件，撤销时释放这些键鼠，失败的释放保留记录以便后续重试。
GetCursorInfo / GetIconInfo 读取 Windows 实际光标位置、形状与热点，复用现有授权代次、形状大小限制与采集光标回退；Windows 采集开关使用 show-cursor，X11 使用 show-pointer。
Windows 光标通过 DrawIconEx 在黑 / 白背景各绘制一次，恢复颜色与透明度，兼容本机 Qt 6.8.3 的 fromHICON 对箭头 / 手形返回空图像、文本光标丢失透明遮罩的问题；生成图像仍限制为 128×128。RGBA 无法表达背景反色，反色像素以白边黑色形状近似，避免文本光标在白底内容上消失。
Windows 仅普通交互桌面；不绕过 UIPI 权限边界，不支持 UAC 安全桌面或注入更高权限应用。Wayland 被控、文本剪贴板及中文输入法同步留待后续需求。

画面放大在同一个 MainWindow 中隐藏普通面板并进入全屏，保留同一个 VideoSurface
和实际绘制统计，不重新创建画面控件或媒体管线。0.4.4 去掉外围边距与底部控件，
退出按钮覆盖在画面上；两端物理像素尺寸一致时按一对一绘制，普通窗口使用平滑缩放。
Esc 只在放大时作为本地退出快捷键，恢复进入前的窗口状态；切换前发送按键释放。

## 0.2 本地媒体与线程

独立本地预览的界面、按钮、预览帧到会议画面的连接及诊断统计已移除。MediaEngine 与协调层的内部采集接口保留用于媒体回归，用户不能从界面启动；会议本机画面仍由独立 ConferenceMedia 提供。

为避免采集、枚举和关闭设备阻塞日常 UI，MediaEngine 通过 queued invokeMethod 将命令交给
MediaWorker。该线程用 Qt 定时器读取 GstBus 和 appsink，无需为当前预览建立 GLib MainLoop。
GStreamer 内部线程负责采集，工作线程只复制映射后的 RGB 帧，不向 UI 交付 GstBuffer 指针。

appsink 最多一帧并丢弃旧帧；互斥保护的交接槽最多一个深拷贝 QImage，两处合计最多两帧。
交接槽从空变为有帧时才排队一次通知，主线程取走前只覆盖旧帧；取消原先每 33 ms 取帧的上限，避免高帧率源丢失交接机会，同时保持通知有界。
VideoSurface 在真实 OpenGL widget 中按比例绘制会议和共享画面。内部采集、交接与丢弃计数只表示本地管线，不能当作网络丢包或窗口呈现 FPS。会议本地 / 远端帧也使用一个合并的待处理通知。

SessionCoordinator 是预览和音频状态的唯一写入者，状态分别为 Stopped / Starting / Running /
Stopping / Failed，与房间状态独立。只有第一帧到达才显示预览运行；每次启动、停止携带独立
代次，取消后的旧状态与旧帧不影响新预览。离开房间或信令失败会停止本地媒体。
摄像头无帧启动超时为 8 秒；管线错误显示真实 GstBus 信息。
停止在媒体线程将管线设为 NULL 并释放引用，返回确认后允许重启；析构等待工作线程清理。
驱动自身在关闭时挂起的硬退出保护尚未实现，不把现有回归当作所有驱动的时间上限保证。

设备由 GstDeviceMonitor 枚举并保存设备引用，刷新后旧设备 ID 失效；预览中禁止刷新或换源。
SDK 的旧 ksvideosrc 在本机摄像头失败，当前排除该接口，使用枚举出的 Media Foundation 接口。
音频设备仅枚举；audiotestsrc 可接扬声器或测试用 fakesink，尚无麦克风录制与远端音频。

## 0.3 摄像头模式与屏幕共享

CameraMode 从设备 caps 的单个结构中固定宽高与帧率，按像素数、再按分数帧率比较；
不把不同模式的宽高和帧率拼成设备未公布的组合。raw 直接转换，JPEG 先解码，再转 RGB；
摄像头输入固定为最高原生模式，在其后通过 videorate drop-only、videoscale、videoconvert 和动态 capsfilter 调整输出；分数帧率保留，不把 60000/1001 提升成 60/1。
预览与会议复用 ShareQuality，以 1 秒观测窗口评估采集 / 本地交接或编码 / 本地交接速度；自身管线跟不上时连续两个窗口低于目标 85% 即降档，只有接收端掉帧而编码正常时要求连续四个窗口，连续 15 个稳定窗口且额外延迟低于 60 ms 后恢复。区分两类慢窗口的依据是：链路瞬时抖动会同时压低对端有效帧率，而缩短尺寸并不能修复它。
摄像头清晰度优先，在同一分辨率内先降低到 15 fps，之后才降尺寸；恢复时优先尺寸。屏幕共享仍使用原来的 24 fps 分辨率降档门槛。
内部采集统计记录实时采集 / 交接帧率与目标帧率，供媒体回归读取；独立预览诊断已从界面移除。输出分辨率来自实际 caps，目标帧率不代表实际呈现帧率。会议 VP8 码率随输出档位变化，按 CPU 核心数使用 1–8 个编码线程；本次没有添加会议网络拥塞控制。

用户将屏幕共享提前，语音通话仍待实现，因此共享是独立单向视频管线，摄像头预览保持本地。
ScreenShareWorker 在专用 Qt 线程管理管线、GstBus 和 appsink；发送侧桌面 tee 分为本地预览
和 VP9 / RTP / webrtcbin，接收侧 webrtcbin / VP9 解码进入有界 QImage 交接槽。
Windows 采集使用 d3d11screencapturesrc / d3d11download，Linux X11 使用 ximagesrc；
平台差异集中在媒体采集管线入口，UI 与协调层不包含平台 API，Wayland 暂不支持。

每次共享生成新 streamId；启停带独立 epoch，回调与帧都拒绝旧代次。
媒体断开可能先于正常 stop 信令到达。失败时仍立即清理媒体和控制权限，协调层仅保留该失败流标识；
随后来自当前对端且标识匹配的 stop 可将失败收尾为已停止。错误标识不生效，开始新流或退出房间清除记录，
旧 stop 不能停止新共享。这不自动恢复传输，也不重新授予控制。
GStreamer 的 SDP promise 和 ICE 回调通过受生命周期保护的 queued invokeMethod 回到工作线程。
RTP pad 在 streaming 回调返回前同步连接预建的解码入口，避免未连接错误；不从回调修改 UI。
0.4.1 针对真实跨机花屏启用每个 RTP transceiver 的 do-nack，协商 NACK / RTX 重传。
发送侧 transceiver 可能已在 parse-launch 连接管线时创建，因此既配置已有实例，也处理 on-new-transceiver。
接收侧 rtpvp9depay 设置 request-keyframe / wait-for-keyframe，丢包后请求关键帧，
在完整关键帧到达前丢弃依赖旧参考帧的数据，避免继续显示损坏画面。
发送侧在 DataChannel 就绪前丢弃编码输出 buffer，仍传递 CAPS / 事件完成媒体协商，
避免旧 RTP 积压以及视频突发占用 ICE / DTLS / SCTP 建立阶段。通道就绪后请求新的关键帧，
丢弃它前面的增量帧，以当前完整画面开始发送；本地采集预览继续运行。
发送侧只有控制通道就绪后才进入 Running，避免未就绪时提前取消媒体启动期限。
远端描述应用完成前缓存最多 256 个 ICE 事件；连接启动超时为 45 秒，兼容高 RTT 中继握手等待。
SessionCoordinator 决定共享角色及状态；双方同时开始时按 peerId 排序确定同一共享方。
停止、成员离开或信令失败均取消管线，清空画面；对方已离开时不会再发送停止消息。
全局信令和共享连接分别清理，避免把退出期间迟到的 peer_unavailable 当作本端退会。
共享采用 host ICE，不过滤 Tailscale 的 100.64.0.0/10 地址，可经已连通的 Tailnet 跨局域网连接。
信令填写可达的 Tailscale 地址；媒体候选仍由 libnice 自动收集和协商，没有固定机器 IP。
信令地址与媒体选中网卡独立。为可重复验证局域网，`ZJDESK_WEBRTC_LOCAL_ADDRESS` 可将
本次进程的 ICE 候选绑定到一个已存在的本机地址；无效 / 非本机地址在启动采集前失败，
不回退到其他网卡而误报验证成功。未指定时行为不变。ShareStats 同时保存实际选中
localAddress / remoteAddress；跨机用例检查两端地址，不能只凭信令 URL 判定传输路径。
GCC / CBR / 严格节流候选未通过当前局域网画质与帧率检查，已撤回到忽略的 build 分析目录；
正式源码保留 CQP 策略，跨网络工作按用户要求暂停。
2026-10-10 的新 GCC 对照保留 CQP，纠正双端 TWCC 协商、重复 pacing 与过度跳帧；
与上述已撤回候选不同，是否通过分别记录，不能沿用旧候选结论。
媒体的 TCP / UDP 是 ICE 传输类型，不能据此推断 Tailscale 使用 direct 或 DERP relay。
尚无 STUN / TURN、独立公网 NAT 穿透、断线恢复或系统声音。

## 0.3.1 共享帧率与电脑适配

旧版固定 15 fps、每 33 ms 交付共享画面，无法评估较高帧率。
现在编码器 / 解码器 src pad 的 buffer probe 计数真实完成帧，每秒按单调时钟计算速度。
probe 增加计数并记录编解码耗时、输出间隔和帧大小，不复制帧、不排队 UI 事件；
管线设为 NULL 后才清理对应计数器。
共享主线程用 PreciseTimer 每 4 ms 取最新帧，有界缓存保持不变；本地摄像头预览与会议使用上述有界帧通知。
ScreenShare 向媒体线程交接连续鼠标位置 / 无新形状的光标消息时，尚未消费的同代位置只保留最新一条；
按钮、按键、释放、授权、形状和代次变化形成顺序屏障。已经交给 SCTP / TCP 的消息不能在这里撤回。
VideoSurface 对新帧完成 frameSwapped 后计数，并记录新视频帧之间的最大交换间隔；
单独更新光标不增加视频计数，清空画面会清除待交换状态。这是窗口合成交换时刻，不是物理扫描输出时间。
接收、交付、呈现分别记录；档位 / 编码参数变化与长间隔 / 高额外延迟写入有界 screen-performance 日志。
帧率表示处理帧数，不等于画面内容每秒变化次数。

ShareQuality 是供媒体线程和单元测试使用的有限档位规则，没有新增设备数据库或通用策略系统。
档位从主屏幕原尺寸开始；房间 capabilities 可选 maximumFps（5..240 整数）公布接收方主屏幕刷新率，自动帧率上限取双方较低值。
旧客户端未公布或值无效时沿用发送端刷新率，退房或对端离开清除上限，不把上一成员的限制带到下一成员；手动诊断帧率保留原行为。
在这个共同上限内使用前述 24 fps 下限与逐档尺寸规则，保持 VP9 profile 1 / 4:4:4 软件编解码作为共同路径，Linux 可选硬编仍先探测并回退；
不放大源画面，小尺寸取偶数，仅最低尺寸继续降低至 20 / 15 / 10 / 5 fps。
首档留 10 秒、后续留 4 秒稳定期；编码端连续两次低于目标的 85% 才降档，只有对方掉帧时要求连续四次。
对方有效速度取解码、交付、可用时的实际绘制速度之最小值；反馈超过 5 秒不参与本次判断。
接收窗口处于后台、最小化或画面不可见时，协调层标记显示统计不可用，仍按真实编解码和交付速度反馈；
不把系统暂停重绘当作电脑性能不足。回到前台重建显示计数基线，避免后台时间拉低帧率。
连续 30 个统计窗口稳定后试探恢复；低分辨率以目标的 85% 判稳，优先恢复上一分辨率，
原分辨率以目标的 95% 判稳并试探更高帧率。这是迟滞规则，不是所有硬件的性能保证。

媒体线程修改 videoscale 后的 capsfilter 和编码目标码率，不关闭房间或重建 WebRTC 连接。
每次调整增加 revision 并通知对方；对方等待 2 秒再反馈，旧档反馈不参与新档决策。
首次参数取 Qt 主屏幕尺寸乘 devicePixelRatio；摄像头最高模式策略不变。
软件编码线程数为可用逻辑线程数的一半并限制在 1～8；目标预算为 2～40 Mbps，
当前轻微有损编码的实际瞬时带宽仍可超过预算，硬编 CQP 尤其不能视作线路码率硬上限。
UI 无手动档位菜单，默认按钮使用自动模式；30 / 60 固定目标仅作为公开核心 API 的性能验证参数。
Ubuntu 本机及 Windows → Linux 功能与跨机性能验收见 validation.md；长时间性能与网络瓶颈仍待测量，
不能将这两台电脑和模拟慢显示回归称为所有电脑已适配验收。

## 新增需求：转写与会议纪要

每端转写自己的麦克风音频，通过已加入房间的成员身份绑定 peerId / 昵称，记录发言时间和文本。
服务端须覆盖发送者身份，不能接受客户端伪造说话人。独立音轨无需用声纹猜测房间成员；
同一麦克风附近的多名说话人属于额外的说话人分离需求，当前未承诺。
最终按时间汇总发言，生成讨论内容、决策、待办与待确认事项；不补写转写中不存在的事实。
用户已确定先保持两人，不扩展房间人数；最新要求暂缓转写与纪要，目前没有功能实现。

## 增加功能的顺序

1. 在 roadmap 中写明用户行为、边界和验收条件。
2. 改公共协议或状态前，先加入失败场景与回归测试。
3. 实现业务或平台模块，再接入 UI；新类只为实际职责而建立。
4. 跑受影响测试和本版本完整 CTest，记录真实平台验证结果。
5. 同时更新 README 的“已实现 / 未实现”和本文件的模块依赖。

不为尚未出现的需求提前添加插件机制、服务定位器、全局事件总线、通用仓储层或数据库。
架构变更记录具体触发问题和依赖变化；若需要迁移，保留可以验证旧行为的测试。

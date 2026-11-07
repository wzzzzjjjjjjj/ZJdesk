# 2026-11-08 会议多人标注与异网验证

## 实现范围

最多四人的会议支持一人向其余三人共享屏幕。所有成员均可在共享预览中用画笔、圆圈或箭头绘制，服务端按真实成员填充昵称、作者 ID 和颜色；标注同步到所有成员和共享者的真实桌面。桌面层透明、鼠标穿透、不取得远控权限；直接绘制仍在会议共享预览中进行。

每人可清除自己的标注，共享者可清除全部。晚入会回放当前画布；一位观看者离开保留其他链路，共享者离开或停止共享清空画布。每次共享最多保留 128 条、每条最多 256 点，长画笔自动分段；满量晚入会回放按发送队列节流。

主要改动：`protocol/annotation.*` 验证归一化坐标和工具，`RoomServer` 管理当前共享与规范画布，`session/meeting_share.cpp` 管理每位观看者的独立 `ScreenShare`，`apps/client/annotation_layer.*` 绘制原生透明层，`MainWindow` 仅调用协调层。协作远控沿用原双人路径。

## 测试环境

- Windows 11、Qt 6.8.3、MSVC 2022 x64、GStreamer 1.26.11；主检出 `F:\projects\ZJdesk`。
- Ubuntu 22.04 / GCC 11.4、Qt 6.8.3、GStreamer 1.26.11，真实 X11 `:1`；独立源码 `/home/orbbec/ZJdesk/meeting-annotations-20261108`。
- Windows 物理网段为 `192.168.3.6`，Ubuntu 网卡为 `10.11.42.4` / `10.11.47.137`，两台不在同一局域网。使用已有 Tailscale 地址 `100.122.3.99` / `100.87.60.58` 互通；独立信令监听 `100.87.60.58:8767`，未替换原服务或重启现有客户端。
- Windows GUI 使用 Win32 独立原生桌面及真实 OpenGL，Ubuntu GUI 使用真实 X11；未用 offscreen。测试通过协调层直接加入独立会议，没有把启动卡片 IP 验证或自动远控配对算作本轮验收。

## 红绿回归与本地结果

`meetingAnnotationsBroadcastValidateAndReplay` 在实现前失败，收到的 `annotation` 事件数为 0（期望 1）；实现后通过。覆盖四人广播、不同房间隔离、伪造作者被覆盖、越界坐标与非法工具、旧共享代次、并发共享只允许一位、清除权限、晚入会、共享者离开，以及 128 条满画布 / 每条 256 点回放。

`annotationBoundsAndTools` 覆盖工具和坐标边界。原生 `annotationToolsStayAlignedWithSharedFrame` 覆盖画笔 / 圆圈 / 箭头、缩放和黑边坐标、浏览模式穿透及不发送远控输入。`meetingAnnotationsReachEveryViewerAndLateJoiner` 用真实 WebRTC 测试源验证四个进程收帧、双作者同步、第四人晚入会、第一位观看者退出不终止其他链路、清除和结束，以及共享者透明原生桌面层。

发现并修正两项问题：单点笔画的 `QJsonArray` 初始化被误选为复制构造，实际鼠标绘制产生无效点数组；Windows 最小化主窗口会隐藏其拥有的桌面层。后者先增加 `windowHandle()->isExposed()` 断言重现失败，再将桌面层改为独立窗口并由主窗口析构释放，保留最小化回归。原四人画面测试的共享按钮断言也从禁用更新为启用。

Windows Debug 的非 GUI 六组均通过：core 8.00 秒、file-clipboard 0.24 秒、signaling 17.10 秒、network-setup 5.51 秒、media 144.12 秒、windows-runtime-integrity 0.76 秒（分批运行）。Ubuntu Release 五组均通过，共 144.30 秒，其中 media 128.16 秒。媒体回归使用合成设备，不能代替真实摄像头和音频听感。

最终 Windows Release 原生 GUI 专项 9 通过 / 0 失败，18.091 秒；Ubuntu Release 同组 9 通过 / 0 失败，6.198 秒。包含初始化 / 清理和以下七个用例：两项标注专项、`meetingEarlyStopKeepsOtherMembers`、`meetingFourVideoTiles`、`meetingEmptyExpansionRestoresOnLeave`、`remoteDisconnectDoesNotClaimOnlineWhenSendFails`、`remoteCodeConnectionAutomaticallyRequestsConsent`。该组没有重跑整套 GUI；旧交接记录中的其他 GUI 环境失败不计为本次通过。

## 两机异网真实桌面

`meetingAnnotationWanPeer` 通过环境参数分别指定 owner / viewer、同一个房间和信令地址；双方在原生窗口中实际用鼠标拖动画圈 / 箭头，等待两个不同作者与颜色同步，检查各自清除、共享者清除全部、停止共享与桌面层隐藏，并保存收到的桌面帧、整窗截图、透明层与 JSON 画布。

| 方向 | Ubuntu 结果 | Windows 结果 | 实际画面 |
| --- | --- | --- | --- |
| Ubuntu 共享 → Windows 接收 | owner：3 通过，13.747 秒，累计 168 帧 | viewer：3 通过，13.426 秒，累计 57 帧 | 1920×1080 |
| Windows 共享 → Ubuntu 接收 | viewer：3 通过，21.029 秒，累计 38 帧 | owner：3 通过，21.112 秒，累计 113 帧 | 1920×1080 |

上表为修复桌面层最小化问题后的最终双端 Release 复跑，两方向均通过；修复前 Windows Debug / Ubuntu Release 两方向也通过。上述计数包含初始化 / 清理；累计帧数不是 FPS 测量。指定共享上限 15 fps，验证的是实际桌面链路、双向标注和生命周期，未据此宣称持续帧率。

X11 会将共享者桌面透明层采入视频；协商 `annotationsInVideo` 后观看端不再重复绘制规范画布，本地草稿仍立即反馈。Windows 请求将桌面层排除出采集，由观看端叠加标注。已查看 Ubuntu 原生整窗截图、两端收到的桌面帧和透明层：圆圈与箭头及作者标签均存在。Windows 隔离桌面上的整窗 `window.grab()` 截图出现 OpenGL 合成花屏，但直接保存的接收 `QImage` 正常；当前未人工验收 Windows 活动主桌面的显示效果。

首次跨网测试因临时测试房间名超过现有 32 字符限制而在入会前失败；缩短测试参数后上述两轮均通过，未放宽产品房间名校验。

## 产物、复现和边界

Windows 调用 `build.ps1 -Preset windows-release -Deploy -GuiTests -SkipTests` 生成 Release / 部署；此命令本身不算测试。Ubuntu 使用 `build.sh --preset linux-release --gui-tests --skip-tests --deploy`。Windows 构建与部署 EXE 的 SHA-256 相同；Ubuntu 安装修改 RUNPATH，二进制 Build ID 相同（`7872a059709a80c4f422204ce39e15b922125470`）。既有客户端未自动重启，临时 8767 测试服务在验证后关闭；客户端与信令服务需同步更新。

生成日志与截图保留在 Windows `build/windows-debug/test-results`、`build/windows-release/test-results`、`build/wan-windows-owner-final`、`build/wan-windows-viewer-final`、`build/wan-linux-owner`、`build/wan-linux-viewer`，Ubuntu 独立树的 `build/annotation-*` 和 `build/wan-owner` / `build/wan-viewer`。它们均为忽略的生成物，不提交源码。

复现标注专项：`zjdesk-ui-tests annotationToolsStayAlignedWithSharedFrame meetingAnnotationsReachEveryViewerAndLateJoiner`。异网用例要求两端同时运行 `zjdesk-ui-tests meetingAnnotationWanPeer`，配置 `ZJDESK_ANNOTATION_WAN_URL`、`ZJDESK_ANNOTATION_WAN_ROOM`、`ZJDESK_ANNOTATION_WAN_ROLE=owner|viewer`、`ZJDESK_ANNOTATION_WAN_OUTPUT`，并提供真实桌面和可互通网络。

尚未覆盖四台实体机、长时间并发标注、三路真实桌面采集 / 编码的持续负载、实际会议音视频同时启用的跨网通话或 Windows 活动主桌面人工操作。无 STUN / TURN，本轮仅证明既有 Tailscale 可互通异网链路；不承诺任意公网 NAT 能直连。Wayland、直接在共享者真实桌面上绘制、窗口 / 多屏选择不在本次范围。

## 集中代码审查复核

逐项读取本轮 diff、新增标注协议 / 绘制层 / 会议共享实现，以及原有房间离开、会话失败与退出、媒体线程启停和信令队列调用方。重点核对服务器规范作者身份和清除权限、画布边界、慢速回放期间编辑 / 清除、共享代次与迟到回调、逐观看者链路释放、共享者退出以及桌面层显式销毁，未发现新的明确逻辑问题。

审查后复跑最终 Release：Windows 原生 GUI 同组 9 通过 / 0 失败，17.605 秒；Ubuntu X11 原生 GUI 同组 9 通过 / 0 失败，6.119 秒。Windows Release 完整 core / signaling 两组通过，共 24.29 秒（8.01 / 16.27 秒），包含坐标 / 点数校验、四人广播、作者伪造与越权拒绝、满画布晚入会回放及共享者退出。`git diff --check` 通过。本次复核没有修改功能代码，没有重复整套媒体或双机跨网测试；上文未验收边界继续保留。

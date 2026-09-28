# ZJdesk 项目交接

更新日期：2026-09-29。项目目录：`F:\projects\ZJdesk`。
本文是交接时的状态快照；后续接手应以最新代码、工作区和实际验证为准。

## 2026-09-29 最新状态：0.2.0

以下更新优先于本文后面的 0.1 历史快照。用户已要求推进 0.2，并新增语音转写及会议纪要需求。

### 已实现和实际边界

- SDK：官方 1.26.11 Runtime / Development MSI 的 SHA256 与下载校验文件一致；通过 MSI
  administrative extraction 提取到 `F:\gstreamer\1.26.11\PFiles64\gstreamer\1.0\msvc_x86_64`。
  没有系统安装记录，也没有改全局 PATH。不能仅查卸载列表就说 SDK 不存在。
- `MediaEngine / MediaWorker`：Qt 专用工作线程管理设备和管线，Qt 定时器读取 GstBus 与 appsink。
- 摄像头、音频输入、音频输出枚举；视频测试源 / 摄像头本地预览，640×480。
- 实际设备协商固定为 raw 640×480 / 30 FPS；SDK 旧 ksvideosrc 在本机失败，排除该接口，
  使用 Media Foundation。该格式已在 HP Wide Vision HD Camera 上验证，不承诺所有设备支持。
- audiotestsrc：用户可点击低音量播放 / 停止；自动回归接 fakesink，不播放声音。
- appsink 最多一帧 + 交接槽最多一个深拷贝 QImage；主线程定时取最新帧，不按每帧排队事件。
- 协调层独立管理预览 / 音频状态及代次；第一帧到达才判定预览运行，取消后拒绝旧帧和状态。
- 离开房间、信令失败会停止本地媒体；开始 / 停止不依赖入会。
- UI 支持选摄像头 / 测试源、开始 / 停止、刷新设备；诊断显示真实版本、设备和帧交接计数。
- Windows 部署增加 Runtime DLL、插件、扫描器、SDK 版本及随包许可，程序识别自己的媒体目录。

这仍然不是双人通话：本地预览不发送给对方，通话摄像头 / 麦克风、共享和远控仍禁用。
音频硬件目前只枚举，没有麦克风采集、录音或播放远端音轨。
关闭管线在工作线程完成，析构等待清理；驱动关闭挂起的硬退出保护还未实现。

### 构建和运行

```powershell
# 本机 Debug 已保存 SDK 配置，后续默认仍不弹 GUI 测试窗口
.\build.ps1
.\run.ps1

# 新环境或新的 preset 首次启用媒体
.\build.ps1 -GstRoot '实际 SDK 前缀'
.\build.ps1 -Preset windows-release -GstRoot '实际 SDK 前缀' -GuiTests -Deploy

# 无 SDK 时保留房间功能
.\build.ps1 -WithoutMedia
```

`-GstRoot` 保存到对应 CMakeCache；构建 / 运行脚本读取该配置，只修改子进程环境。
默认测试为 core / signaling / media（媒体启用时），GUI 测试仍须显式 `-GuiTests`。
物理摄像头默认跳过，在 SDK 运行环境设置 `ZJDESK_CAMERA_TEST=1` 并执行
`zjdesk-media-tests cameraReleasesDevice` 才是硬件回归。

### 已做验证

- 最新 Debug、Release 均启用 GStreamer，四组 CTest 通过。
- 视频测试源五次启停、取消、失败后重试、退出后旧帧、慢消费者单槽缓存均通过。
- 真实 Widgets / OpenGL 上显示变化的画面，按钮停止及重新开始通过；
  截图 `build/<preset>/output/ui-local-preview.png`。
- HP Wide Vision HD Camera 三次采集 / 停止 / 重新打开通过，单独报告
  `build/windows-debug/test-results/camera.xml`，不把默认跳过算作硬件通过。
- Release 便携依赖已部署；SDK 环境隔离后的实际启动与动态预览验证记录见 validation.md。
- Ubuntu 22.04 尚未验证；实际扬声器是否听到声音需用户人工确认；没有通话、转写或纪要通过证据。

### 新增语音转文字和会议纪要需求

用户要求：同一房间里两人或多人发言，记录说话人、转成文字，最终生成会议纪要。
已记录到 README / architecture / roadmap，尚未实现。
已向用户询问处理方式和人数；用户已答复先保持两人，后端选择还没有答复：

1. 全本地模型 / 云端 API / 本地转写加云端纪要。
2. 人数已确定：先保持两人，不扩展多人房间。

不得在未选择后端时擅自接入云端或假定已提供 API Key；也不要把模板纪要或输入文本当作实时转写。
两端分开采集自己的音轨并绑定房间成员 ID / 昵称，可以标记说话人，无需先做声纹识别。
先接真实麦克风及 0.3 通话，再验证两人交替 / 同时发言、中文识别、退会后的旧结果拒绝和纪要溯源。
原产品方案曾排除 AI 纪要；最新用户指示已把它加入范围，以本节为准。

### 接手顺序与 Git

保留当前所有未提交变更与原先脚本迁移，不自动提交、不清理未跟踪文件。
版本定义已改到 CMakeLists.txt 的 0.2.0；新增 media_engine.*、tst_media.cpp 及媒体相关改动。
下一步先处理用户待答复的转写方案，再按 0.3 推进两人的真实语音链路及转写。
并行的 Ubuntu 验收需要真实 22.04 / GCC11 / X11 环境，不以 Windows 通过替代。

## 2026-09-28 历史快照：0.1.0（以下保留作为背景）

## 1. 项目目标与用户约定

开发一个可以持续维护的 C++ / Qt 跨平台协作项目，用来展示音视频应用、网络通信、
跨平台接口、组件化、测试和诊断能力。目标是 Windows 与 Ubuntu 22.04 间的双人音视频、
屏幕共享与需要本地确认的双向远程操控。

- 工程名为 ZJdesk；旧方案中的 MeetScope 是同一产品方向，不是另一个待创建的项目。
- 用户允许根据实际问题调整架构，但需要同步设计文档，保持小步迭代和可维护性。
- 日常使用 VS Code 的 PowerShell 终端，不依赖 Qt Creator 操作。
- 根目录只保留两个 PowerShell 入口：`build.ps1` 构建，`run.ps1` 启动。
- **默认执行 `build.ps1` 不显示界面测试窗口。** 用户已明确要求关闭构建时的界面显示。
- 当前先做双人场景；多人 SFU、账户系统、无人值守远控不属于首版目标。

## 2. 先读哪些文件

先读 [AGENTS.md](../AGENTS.md)，再结合以下文件定位实际实现：

| 文件 | 用途 |
| --- | --- |
| [README.md](../README.md) | 当前能力、依赖、构建与运行命令 |
| [architecture.md](architecture.md) | 当前模块边界、状态机、协议和扩展约定 |
| [roadmap.md](roadmap.md) | 0.2～0.5 的实现顺序和验收目标 |
| [development.md](development.md) | 测试、构建环境和维护约定 |
| [validation.md](validation.md) | 已做过的 Windows 验证及其局限 |
| [original-project-plan.md](original-project-plan.md) | 原始产品方案与 A01～A10 验收目标；不能当成已实现清单 |

## 3. 当前已实现的范围

当前应用版本为 **0.1.0 基础框架**，版本统一定义在根目录 `CMakeLists.txt`。

- Qt Widgets 工作台、成员状态、诊断页面、JSONL 日志导出。
- `zjdesk-server` 独立 WebSocket 信令服务，一个房间最多两人。
- 入会、等待成员、离开、取消连接、错误反馈、手动重新入会。
- 版本化 JSON 信封、输入校验、64 KiB 消息限制、有界发送队列、房间隔离。
- offer / answer / ICE 的 JSON payload 转发，服务端覆盖发送者身份；没有真实媒体参与协商。
- 服务端 ping / pong 心跳；客户端有入会超时及 socket 断开处理。
- 远控授权核心类：申请、授权、撤销、控制代次和输入序号检查，有单元测试。
- 可选 GStreamer 初始化、实际版本读取和必要插件检查的代码。
- CMake / Ninja、Qt Test / CTest、Windows Qt 运行依赖部署。

**容易误判的地方：**

- `PeerAvailable` / “双方已入会”只表示信令房间中有两个人，不能表示音视频已连接。
- `VideoSurface` 目前绘制背景、头像和文字，没有接收或显示实际视频帧。
- 摄像头、麦克风、共享和远控按钮目前禁用，不能直接启用按钮冒充功能完成。
- `ControlSession` 只有本地授权门禁，没有授权交互、远控传输或系统输入执行。
- 诊断中的消息数量是真实信令统计；尚无真实媒体 RTT、FPS、码率或丢包率。

## 4. 代码入口与边界

| 入口 / 模块 | 当前职责 | 后续接入位置 |
| --- | --- | --- |
| `apps/client/main.cpp`、`main_window.cpp` | UI、状态展示、用户操作 | 设备选择、媒体操作、授权确认与诊断显示 |
| `apps/client/video_surface.cpp` | QOpenGLWidget 占位画面 | 实际视频帧渲染 |
| `src/session/session_coordinator.*` | 会话业务状态的唯一写入者 | 媒体协商、连接和恢复状态 |
| `src/signaling/signaling_client.*` | WebSocket 生命周期、发送与接收 | 保持与媒体业务解耦 |
| `src/signaling/room_server.*` | 两人房间、转发、隔离、心跳 | 按实际媒体需要补充 payload 校验 |
| `src/protocol/message.*` | 信封编码、版本、基础输入校验 | 明确 SDP / ICE 与远控消息格式 |
| `src/media/media_environment.*` | SDK / 插件探测 | 增加实际媒体引擎，不把管线放进 UI |
| `src/remote-control/control_session.*` | 本地授权、代次与序号校验 | 接入 DataChannel、授权 UI 和输入执行器 |
| `src/platform/platform_info.*` | 读取平台与会话信息 | 按功能需要增加 Windows / Linux 平台实现 |
| `src/diagnostics/event_log.*` | 最多 300 条事件、原子导出 | 记录真实媒体状态与异常事件 |
| `cmake/FindGStreamer.cmake` | SDK 查找、基础库链接 | 按实际 API 增加 WebRTC / SDP 等依赖 |

现有业务运行在 Qt 事件线程，尚未建立媒体工作线程。
UI 不直接操作 socket、GStreamer 对象或系统输入。
未来 GLib / 工作线程回调应通过 Qt queued signal 或 invokeMethod 进入协调层，
携带会话代次，防止退出后的旧回调修改新会话。
只在实际职责出现时拆模块，不提前添加插件框架、服务定位器、事件总线或数据库。

## 5. 未完成清单与建议顺序

| 顺序 | 未完成事项 | 当前缺口 | 完成判断 |
| --- | --- | --- | --- |
| 1 / 0.2 | GStreamer SDK 真正启用 | 默认 `ZJDESK_ENABLE_GSTREAMER=OFF`；启用构建、运行库和插件部署尚未验证 | MSVC x64 Runtime / Development 配套；启用后能构建、启动并显示真实版本及插件结果 |
| 2 / 0.2 | 设备与本地预览 | 没有设备枚举、摄像头采集、音频设备处理、测试源或媒体管线 | 先测试源，再真实摄像头；能停止并重新开始，设备可释放；帧交接有界 |
| 3 / 0.3 | 双人音视频 | 没有 webrtcbin 管线、实际 SDP / ICE 处理、采集编码、接收解码、音频播放和媒体状态 | VP8 / Opus 双向通话；真实画面和声音；连续入退会无资源残留；Windows ↔ Ubuntu 22.04 实测 |
| 4 / 0.4 | 屏幕共享 | 没有 Windows / Linux 屏幕采集，也没有摄像头与桌面切换 | 保持语音，切换视频源；按真实分辨率显示共享画面 |
| 5 / 0.4 | 实际远程操控 | 没有 DataChannel、授权确认交互、键鼠协议、坐标映射或 OS 输入注入 | Windows ↔ Linux/X11 双向控制；拒绝与撤销生效；断线释放本程序按下的键；旧代次输入被拒绝 |
| 6 / 0.5 | 真实媒体诊断与恢复 | 只有信令统计；没有自动重连、媒体故障恢复、双端完整故障检测 | 统计来自真实管线；有可复现故障、时间线及恢复验证；失败后资源清理可回归 |
| 并行平台验证 | Ubuntu 22.04 实机 | Linux presets 已有，但未在 Ubuntu 构建、启动或验证跨机通信 | Qt / SDK 在 22.04 可用；GCC11 构建、测试和与 Windows 互通通过；先 X11 |
| 7 / 0.5 | 交付与持续集成 | 没有双平台 CI、正式安装包、干净电脑测试；媒体依赖还未部署 | 固定依赖与缓存；自动测试；Qt 与 GStreamer 的 DLL、插件、扫描器完整；按 A01～A10 逐项验收 |

公网 TURN、Wayland、回声消除、多显示器、剪贴板与双路视频属于后续按需求扩展。
当前没有账号、房间密码或公网服务部署；首先按本机 / 局域网开发场景推进。

## 6. 当前环境与构建运行

| 项目 | 基线 / 本机信息 |
| --- | --- |
| 平台与语言 | Windows 11 x64；C++17；目标 Ubuntu 22.04 x64 / GCC11 |
| Qt | 已验证 6.8.3 MSVC2022 64-bit，路径 `F:\Qt6\6.8.3\msvc2022_64` |
| 编译器 | VS 2022 Community / v143，路径 `D:\Program Files\Microsoft Visual Studio\2022\Community` |
| 构建工具 | CMake >= 3.28，本机 3.31.6；Ninja 1.12.1；脚本自动定位 VS 内附工具 |
| Windows SDK / 调试 | 已验证 SDK 10.0.19041.0；CDB x64；Qt Creator 17.0.2 可选 |
| 媒体依赖 | 计划基线 GStreamer 1.26.11 MSVC x86_64，默认未启用 |

日常使用：

```powershell
cd F:\projects\ZJdesk
.\build.ps1
.\run.ps1
```

`build.ps1` 默认 Debug，自动配置环境、增量编译，并通过 `ctest -E "^desktop$"`
运行 core 与 signaling。界面测试仍可编译，但默认不会启动它。
Windows preset 中的 `ZJDESK_GUI_TESTS=ON` 表示生成测试目标，不代表默认脚本会弹窗。

完整测试与部署：

```powershell
# 会显示测试窗口，只在需要 GUI 验证时运行
.\build.ps1 -GuiTests

# Release 构建与 Qt 本地部署
.\build.ps1 -Preset windows-release -Deploy
```

两个脚本支持 `-Preset windows-release` 和 `-QtRoot '实际 Qt MSVC x64 目录'`。
`-Fresh` 重新生成 CMake 配置，不是删除整个源码或构建目录。

`run.ps1` 启动一个后台服务以及 Alice、Bob 两个客户端，默认地址 `ws://127.0.0.1:8765`，
房间 `demo`。两边分别点击“加入房间”。关闭两窗或在终端按 Ctrl+C，会清理本次脚本创建的进程。
它不执行构建；构建前先结束上次演示，避免 exe 被占用。

输出目录为 `build/windows-debug/bin/`、`build/windows-release/bin/` 和 `dist/windows-release/bin/`。
`run.ps1` 使用 build 下的程序及 Qt SDK 运行环境；部署目录下的客户端也需要单独启动信令服务。
脚本不修改系统 PATH 或现有 Qt 5 项目。

## 7. 媒体依赖接手时的注意点

已下载两个 MSI，交接时确认文件仍存在于 `F:\codex\other\output\GStreamer-1.26.11`：

- `gstreamer-1.0-msvc-x86_64-1.26.11.msi`
- `gstreamer-1.0-devel-msvc-x86_64-1.26.11.msi`

文件已下载不等于 SDK 已安装或启用。本次交接未重新检查所有安装目录；接手先确认真实安装状态，
不要直接将 README 中的示例路径视为实际路径。
Runtime 与 Development 应为相同版本、MSVC x64，安装到同一前缀。

启用构建所需 CMake 输入：`ZJDESK_ENABLE_GSTREAMER=ON` 和 `GST_ROOT=<真实 SDK 路径>`。
当前脚本没有专门的 GStreamer 参数；需要沿用现有 CMake 配置，或在接入媒体时增加必要入口。
MSVC 环境须初始化，Qt 前缀和 Ninja 可用；直接从普通终端调用裸 cmake 不一定满足这些条件。
运行需找到 SDK DLL、插件与扫描器；现有运行脚本只配置 Qt，Qt 部署脚本不会自动部署 GStreamer。
当前 FindGStreamer 只链接基础库，使用具体 WebRTC / SDP API 后应补齐实际所需头文件和链接库。

Ubuntu 22.04 需要适合该系统的 Qt 6.8.3 与媒体 SDK；不能直接用 Ubuntu 自带旧版 Qt
或在更新系统构建的预编译库替代验证。Linux 使用 pkg-config 查找 GStreamer。

## 8. 验证证据与 Git 状态

已有 Windows Debug / Release 构建、core、signaling、desktop 测试通过的记录，
详细范围见 [validation.md](validation.md)。交接时读取的 JUnit 文件全部为零失败、零错误：
core 15 项、signaling 7 项、desktop 3 项，其中包括测试组初始化 / 清理。
Debug 最近一次默认构建只执行两组无界面测试；desktop XML 来自此前完整测试，不能当作每次重新执行。

- JUnit：`build/<preset>/test-results/*.xml`。
- CTest 日志：`build/<preset>/Testing/Temporary/LastTest.log`。
- 已有 GUI 截图：`build/<preset>/output/ui-workspace.png`、`ui-diagnostics.png`。
- Release 部署客户端曾在移除 Qt SDK 环境路径后成功创建窗口，仅证明本机启动，未代表干净电脑验收。
- Ubuntu 实机、启用 GStreamer、真实音视频和真实远控尚无通过证据。
- 本次交接只核对文件、代码及已有测试结果，没有重新启动 GUI 或执行媒体测试。

交接时 Git 分支为 `main`，HEAD 为 `5465c7a`；工作区有未提交改动。
根目录 `build.ps1`、`run.ps1` 尚未跟踪，旧 `tools/build-windows.ps1`、`tools/run-demo.ps1` 已删除，
README、AGENTS 与部分 docs 有修改；另有未跟踪的 `MeetScope-项目方案.md`。
新交接文档也需要一起保留。接手先 `git status --short`，阅读工作区，不能只检出 HEAD
或清理未跟踪文件，否则会丢失当前脚本及交接内容。本次没有自动提交这些变更。

## 9. 建议下一位 AI 的第一个任务

先做 **0.2：GStreamer 依赖与设备预览**，不要一次重写整个应用。

1. 确认工作区、依赖安装与实际路径，执行默认 `build.ps1` 验证基础回归。
2. 启用 GStreamer，记录版本、插件和运行依赖，修复实际出现的构建 / 启动问题。
3. 先实现测试源画面，再做摄像头预览和设备枚举，建立有界帧交接与停止清理。
4. 在协调层接入实际媒体状态，验证反复启停和设备释放，再开放对应 UI 操作。
5. 更新 README、architecture、roadmap 与验证记录，区分真实硬件测试和测试源验证。

可复制给下一位 AI：

```text
接手 F:\projects\ZJdesk。先阅读 AGENTS.md、docs/handoff.md、README.md 和当前架构、路线文档，
检查 Git 未提交改动并保留现有内容。当前仅完成 0.1 基础框架，真实音视频、共享和远控尚未接入。
从 0.2 GStreamer 依赖与设备预览推进，小步实现并验证，不进行无依据的大规模重写。
保留根目录 build.ps1 / run.ps1；默认构建不能弹出 GUI 测试窗口，完整 GUI 测试用 -GuiTests。
Ubuntu 按 22.04 / GCC11 / X11 优先设计，不将 Windows 通过结果当作 Linux 已验证。
```

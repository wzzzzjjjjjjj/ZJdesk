# ZJdesk

C++ / Qt 跨平台协作工作台，目标平台为 Windows 11 x64 与 Ubuntu 22.04 x64。
本项目沿用 MeetScope 的产品方向，以 ZJdesk 为工程名，按可验证的小版本持续开发。

## 当前版本：0.4.5 远控延迟测量与缓冲对照

已实现：

- Qt Widgets 工作台、OpenGL 画面容器、房间成员和运行诊断页面。
- 独立 WebSocket 信令服务，一个房间最多两人。
- 入会、等待成员、对方加入 / 离开、取消连接、错误反馈与手动重新入会。
- offer / answer / ICE 转发、房间隔离、版本化 JSON 协议及消息大小限制。
- 有界事件日志、JSONL 报告导出，以及远控授权与代次校验的核心逻辑。
- CMake / Ninja 构建、Qt Test / CTest 测试和 Windows 本地部署。
- 可选启用 GStreamer 1.26.11，枚举摄像头、音频输入与输出设备。
- 视频测试源和摄像头本地预览，开始 / 停止 / 重新开始；最多两帧交接缓存。
- 音频测试源和低音量测试音，本地预览的真实采集 / 丢弃帧数。
- 摄像头按设备公布的模式选择最高分辨率，再选该分辨率最高帧率；保留原尺寸并显示实际协商格式。
- 两人房间中共享主屏幕，WebRTC / VP9 4:4:4 传输，保留细字和彩色细线；另一端自动接收并显示，可停止、重开或交换共享方。
- 屏幕共享从主屏幕原始像素尺寸和刷新率开始，依据实际编码、接收和显示速度调整帧率；原尺寸降至 5 fps 仍持续落后时才降低分辨率。
- 降低帧率时保留文字画面码率，逐级试探恢复；自动目标最高 240 fps，实际效果以两端硬件、桌面和网络测量为准。
- 显示真实编码 / 接收 / 绘制帧率，区分目标参数与实际速度。
- 跨机回归记录键鼠注入确认、画面反馈、RTP 收发 / 丢包与实际带宽；接收缓冲保留实测稳定的 200 ms 默认值。
- 启用 RTP 丢包反馈 / 重传，丢包后请求关键帧并暂停依赖损坏参考帧的解码，避免持续花屏。
- Windows / Linux 启动脚本各打开一个客户端；未指定服务器时在后台启动信令服务。
- 同房间可直接请求控制 Linux，无需 Linux 手动共享；本地允许后自动建立桌面连接。
- WebRTC DataChannel 传输授权和键鼠输入；Linux X11 本地确认后使用 XTest 执行。
- 点击“放大画面”全屏查看并控制，按 Esc 或“退出放大”恢复原窗口。
- 支持点击、移动、拖动、滚轮和常用键盘操作；拒绝、撤销、失焦和退会释放本程序按下的键鼠。

双向摄像头通话、远端音频、Windows 被控端和自动重连还没有实现。
摄像头预览不会发送给对方；两人入会后，Windows 直接请求控制，Linux 本地允许后自动连接桌面。
停止控制会结束由该次授权自动启动的桌面传输；手动启动的屏幕共享仍可独立使用。
共享帧率来自实际帧计数；实际收发码率与输入确认往返时间可用于诊断。WebRTC 自身 RTT 在本次环境未返回可靠值。
按用户最新指示，语音转写与会议纪要暂缓；房间保持两人。

## 依赖基线

| 项目 | 基线 |
| --- | --- |
| C++ | C++17 |
| Qt | 已验证基线 6.8.3；工程要求 6.8 或更高 |
| Windows | VS 2022 / v143 / amd64，Windows SDK，CDB |
| Ubuntu | 22.04，GCC / G++ 11，Qt 在 22.04 环境构建 |
| 构建 | CMake >= 3.28，Ninja；本机使用 CMake 3.31.6 / Ninja 1.12.1 |
| 媒体 | GStreamer 1.26.11，可选启用预览与 WebRTC 屏幕共享 |

Qt 组件：Core、Network、WebSockets；桌面客户端增加 Widgets、OpenGLWidgets；测试增加 Test。
WebSocket 使用 [Qt WebSockets](https://doc.qt.io/qt-6.8/qwebsocket.html)，
Windows 本地部署使用 [Qt CMake deployment](https://doc.qt.io/qt-6.8/qt-generate-deploy-app-script.html)。

## Windows 快速开始

在 VS Code 中打开本项目，选择“终端 → 新建终端”，使用 **PowerShell** 执行：

```powershell
cd F:\projects\ZJdesk
.\build.ps1
.\run.ps1
```

默认构建 Debug。构建脚本自动找到 VS 2022，加载 MSVC x64、CMake、Ninja 和 Qt 环境，
依次完成配置、编译和无界面的测试；启用媒体后增加 media 回归，任一步失败即停止。无需使用 Developer Command Prompt。
默认不运行会弹出窗口的 `desktop` 界面测试；需要完整测试时执行 `.\build.ps1 -GuiTests`。
项目只提供两个 PowerShell 入口：`build.ps1` 负责构建，`run.ps1` 负责启动。
修改代码后重新执行构建脚本即可增量构建；构建前关闭上一次演示窗口，避免可执行文件被占用。

启动脚本打开一个客户端窗口；未指定 `-ServerUrl` 时同时启动后台信令服务。
连接另一台电脑时传入 `-ServerUrl 'ws://服务机器IP:8765'`，两端使用相同房间号。
终端会保持运行；关闭客户端后脚本停止自己创建的服务。
在该终端按 `Ctrl+C` 会清理本次脚本创建的客户端和服务，不影响其他实例。
不会修改全局 PATH、系统配置或现有 Qt 5 项目。

Qt 安装位置不同时，两个脚本都可传入 `-QtRoot '实际的 Qt MSVC2022 x64 目录'`。

构建和运行 Release：

```powershell
.\build.ps1 -Preset windows-release
.\run.ps1 -Preset windows-release
```

构建输出位于 `build/windows-debug/bin/` 或 `build/windows-release/bin/`。
如果 PowerShell 提示脚本执行被策略禁止，可以用下列命令仅为本次进程运行脚本：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\run.ps1
```

### Qt Creator

1. 打开根目录 `CMakeLists.txt`。
2. 选择 **Qt 6.8.3 MSVC2022 64bit** 套件，使用 Ninja。
3. 构建后选 `zjdesk` 作为运行目标；先另外启动 `zjdesk-server`。
4. 默认服务地址为 `ws://127.0.0.1:8765`，默认房间为 `demo`。

### 可直接双击的本地部署

```powershell
.\build.ps1 -Preset windows-release -Deploy
```

输出到 `dist/windows-release/bin/`，包含客户端、信令服务和 Qt 运行依赖。
客户端启动并不自动创建服务；本机演示可先启动同目录的 `zjdesk-server.exe`。
这只是本机部署目录，还没有生成正式安装包。

### 局域网

服务默认只监听本机；在服务机器显式运行：

```powershell
.\dist\windows-release\bin\zjdesk-server.exe --listen 0.0.0.0 --port 8765
```

两端填写 `ws://服务机器局域网IP:8765` 并加入相同房间。
首版没有账号和房间密码，定位于局域网开发验证；公网部署属于后续版本。

## Ubuntu 22.04

准备在 Ubuntu 22.04 上构建的 Qt 6.8.3、GStreamer 1.26.11、CMake >= 3.28、Ninja、G++ 11
和 X11 / XTest 开发库。脚本默认使用 `~/.local/opt/zjdesk-deps/` 下的 Qt 和 GStreamer，
可通过 `QT_ROOT` / `GST_ROOT` 指定其他前缀。在 Linux 的 X11 桌面终端执行：

```bash
cd ~/ZJdesk/zjdesk
bash ./build.sh --deploy
bash ./run.sh
```

本次 Linux 项目实际位于 `~/ZJdesk/zjdesk`；依赖安装和验证记录见 [validation.md](docs/validation.md)。
默认构建不运行 GUI；需要真实桌面验证时执行 `bash ./build.sh --gui-tests --deploy`。
`run.sh` 默认后台启动服务并打开一个客户端，关闭客户端后清理自己的服务。
连接已有服务时执行 `bash ./run.sh ws://服务机器IP:8765 demo Linux`。
仅构建服务端时配置 `-DZJDESK_BUILD_CLIENT=OFF`；服务不需要桌面会话。

### Windows 控制本次 Linux 电脑

Linux 桌面运行 `bash ./run.sh`，Windows 运行：

```powershell
.\run.ps1 -Preset windows-release -ServerUrl 'ws://192.168.3.13:8765' -Room demo -Name Windows
```

两端点击“加入房间”；Windows 点击“请求控制 Linux”，Linux 点击“允许控制”，桌面自动连接。
Windows 点击收到的画面后操作键鼠；“放大画面”进入全屏，Esc / “退出放大”返回，任一端可停止控制。
当前被控端要求 X11；Wayland、Windows 被控、剪贴板和中文输入法同步尚未实现。

## 启用 GStreamer、摄像头与屏幕共享

默认 `ZJDESK_ENABLE_GSTREAMER=OFF`，因此安装媒体 SDK 前也可以维护和测试房间逻辑。
准备同版本 MSVC x64 Runtime 与 Development 的共同目录后，首次在 PowerShell 中执行：

```powershell
.\build.ps1 -GstRoot '实际的 GStreamer SDK 目录'
.\run.ps1
```

之后两个脚本从对应 preset 的 CMakeCache 读取 SDK 路径；只设置子进程的 DLL、插件、
扫描器与独立注册缓存，不修改系统 PATH。`build.ps1 -WithoutMedia` 可显式关闭媒体。
Qt Creator 使用 `ZJDESK_ENABLE_GSTREAMER=ON`、`GST_ROOT=<真实 SDK 目录>`，并配置对应运行环境。

本机 SDK 是校验官方 MSI 后提取的独立目录：
`F:\gstreamer\1.26.11\PFiles64\gstreamer\1.0\msvc_x86_64`，没有写系统安装记录。
本机首次启用可将该路径传给 `-GstRoot`；它不是其他机器的默认路径。

在“本地预览”选择视频测试源或摄像头，点击“开始预览”；停止后可以重新开始。
摄像头显示其最高模式，并按“最高分辨率优先，再取对应最高帧率”启动；不再固定 640×480。
支持设备公布的 raw / JPEG 模式；不能以软件设置超过摄像头硬件能力，驱动或权限错误会显示失败原因。
预览不需要加入房间。点击“测试声音”播放低音量测试音，再次点击停止。
诊断页显示真实版本、枚举设备和预览帧计数；默认测试不打开摄像头，也不播放测试音。

两端加入同一房间后，点击“共享主屏幕”，另一端自动显示整个主屏幕及鼠标指针。
共享从主屏幕原始像素尺寸及刷新率开始，在原尺寸下逐级选择可持续帧率，
原尺寸 5 fps 仍持续不足时才按比例缩小；不会放大原本较小的屏幕。
编码线程数按可用逻辑线程数选择（1～8），VP9 保留 4:4:4 色彩并使用零量化，优先保留文字细节。
实际带宽与 CPU 开销会增加，帧率由两端实测自动调整；不含系统声音。
全屏移除边距与底部控件，占满屏幕；远端与本机屏幕像素尺寸相同时按 1:1 显示。
退出按钮悬浮于右上角，Esc 仍可返回；普通窗口按尺寸适配时使用平滑缩放。
调整时保持 WebRTC 连接和当前共享；连续稳定后才试探恢复上一档，避免频繁升降。
接收窗口在后台或最小化时，不把暂停重绘计为性能不足；回到前台重新采样显示帧率。
原尺寸优先是为了保留桌面文字清晰度，并非保证每台电脑都能达到原尺寸 60 fps。
双方必须使用更新后的客户端和信令服务；本机 30 / 60 fps 对比及限制见 docs/validation.md。
同一时刻一人共享；停止后可由任一端重新共享，对方离开或信令中断会停止并清空共享画面。
共享会发送整个主屏幕，可点击“停止共享”结束。当前不提供窗口选择、多屏切换或公网 TURN。
Windows 使用 D3D11 桌面采集；Linux X11 使用 ximagesrc，Wayland 暂不支持。

Release 需要为自己的 preset 首次指定 `-GstRoot`：

```powershell
.\build.ps1 -Preset windows-release -GstRoot '实际的 GStreamer SDK 目录' -Deploy
```

部署包含 SDK Runtime DLL、插件、扫描器及随包许可；程序自动选择部署目录的媒体依赖。
这是本地便携部署，尚未完成干净电脑验收或正式安装包。
Linux 设置独立前缀的 `PKG_CONFIG_PATH`、`LD_LIBRARY_PATH` 后，使用
`bash ./build.sh` 默认启用媒体；缺少媒体 SDK 时可用 `--without-media` 构建房间功能。

## 项目文档

- [项目交接与未完成清单](docs/handoff.md)
- [架构与扩展约定](docs/architecture.md)
- [开发与验证流程](docs/development.md)
- [迭代路线](docs/roadmap.md)
- [本次验证记录](docs/validation.md)
- [原始产品与环境方案](docs/original-project-plan.md)

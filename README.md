# ZJdesk

C++ / Qt 跨平台协作工作台，目标平台为 Windows 11 x64 与 Ubuntu 22.04 x64。
本项目沿用 MeetScope 的产品方向，以 ZJdesk 为工程名，按可验证的小版本持续开发。

## 当前版本：0.1.0 基础框架

已实现：

- Qt Widgets 工作台、OpenGL 画面容器、房间成员和运行诊断页面。
- 独立 WebSocket 信令服务，一个房间最多两人。
- 入会、等待成员、对方加入 / 离开、取消连接、错误反馈与手动重新入会。
- offer / answer / ICE 转发、房间隔离、版本化 JSON 协议及消息大小限制。
- 有界事件日志、JSONL 报告导出，以及远控授权与代次校验的核心逻辑。
- CMake / Ninja 构建、Qt Test / CTest 测试和 Windows 本地部署。

音视频、屏幕采集、DataChannel、实际键鼠注入、自动重连还没有实现。
界面中的相关操作保持禁用；两人入会成功只表示房间信令成功。
诊断面板不填充虚构的 FPS、RTT 或码率。

## 依赖基线

| 项目 | 基线 |
| --- | --- |
| C++ | C++17 |
| Qt | 已验证基线 6.8.3；工程要求 6.8 或更高 |
| Windows | VS 2022 / v143 / amd64，Windows SDK，CDB |
| Ubuntu | 22.04，GCC / G++ 11，Qt 在 22.04 环境构建 |
| 构建 | CMake >= 3.28，Ninja；本机使用 CMake 3.31.6 / Ninja 1.12.1 |
| 媒体 | GStreamer 1.26.11，当前为可选依赖探测 |

Qt 组件：Core、Network、WebSockets；桌面客户端增加 Widgets、OpenGLWidgets；测试增加 Test。
WebSocket 使用 [Qt WebSockets](https://doc.qt.io/qt-6.8/qwebsocket.html)，
Windows 本地部署使用 [Qt CMake deployment](https://doc.qt.io/qt-6.8/qt-generate-deploy-app-script.html)。

## Windows 快速开始

在 PowerShell 中执行：

```powershell
cd F:\projects\ZJdesk
.\tools\build-windows.ps1 -Preset windows-debug
.\tools\run-demo.ps1 -Preset windows-debug
```

演示脚本启动一个本机服务与 Alice、Bob 两个窗口。在两个窗口分别点击“加入房间”，
可以观察两人成员状态、离开和重新加入。关闭两个窗口后脚本停止自己创建的服务。
不会修改全局 PATH、系统配置或现有 Qt 5 项目。

Qt 安装位置不同时，两个脚本都可传入 `-QtRoot '实际的 Qt MSVC2022 x64 目录'`。

### Qt Creator

1. 打开根目录 `CMakeLists.txt`。
2. 选择 **Qt 6.8.3 MSVC2022 64bit** 套件，使用 Ninja。
3. 构建后选 `zjdesk` 作为运行目标；先另外启动 `zjdesk-server`。
4. 默认服务地址为 `ws://127.0.0.1:8765`，默认房间为 `demo`。

### 可直接双击的本地部署

```powershell
.\tools\build-windows.ps1 -Preset windows-release -Deploy
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

准备在 Ubuntu 22.04 上构建的 Qt 6.8.3 和 CMake >= 3.28，再执行：

```bash
export QT_ROOT="$HOME/.local/opt/meetscope/qt-6.8.3"
export PATH="$QT_ROOT/bin:$PATH"
export LD_LIBRARY_PATH="$QT_ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
./build/linux-debug/bin/zjdesk-server --listen 0.0.0.0
./build/linux-debug/bin/zjdesk
```

仅构建服务端时配置 `-DZJDESK_BUILD_CLIENT=OFF`；其运行不需要桌面会话。
GUI 测试需要真实桌面或 Xvfb，显式配置 `-DZJDESK_GUI_TESTS=ON`；不使用 offscreen 验证 OpenGL。
Ubuntu 跨机运行尚待实机验收，不能以 Windows 的测试结果代替。

## 启用 GStreamer 依赖探测

默认 `ZJDESK_ENABLE_GSTREAMER=OFF`，因此安装媒体 SDK 前也可以维护和测试房间逻辑。
安装同版本的 Runtime 与 Development 到同一目录后，在 Qt Creator 的 CMake 配置中添加：

```text
ZJDESK_ENABLE_GSTREAMER:BOOL=ON
GST_ROOT:PATH=F:/gstreamer/1.0/msvc_x86_64
```

`GST_ROOT` 必须换成真实安装目录；把它的 `bin` 加到**项目运行环境** PATH。
Linux 使用该独立前缀的 `PKG_CONFIG_PATH` 和 `LD_LIBRARY_PATH`，通过 pkg-config 查找。
目前接入仅初始化运行库、记录实际版本并检查必要插件，不创建通话管线。
启用后仍需部署 GStreamer 自身的 DLL、插件和扫描器；Qt 部署工具不会自动处理它们。

## 项目文档

- [架构与扩展约定](docs/architecture.md)
- [开发与验证流程](docs/development.md)
- [迭代路线](docs/roadmap.md)
- [本次验证记录](docs/validation.md)
- [原始产品与环境方案](docs/original-project-plan.md)

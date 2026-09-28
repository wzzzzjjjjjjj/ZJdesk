# 开发与验证流程

## 构建入口

Windows 的根目录 build.ps1 自动定位本机 VS C++ 工具，初始化 amd64 环境，
然后顺序运行 configure、build、CTest。任一步失败即停止。
默认 CTest 运行 core、signaling，以及启用 GStreamer 时的 media，不弹出窗口或播放测试音；
加 `-GuiTests` 才执行包括 desktop 的完整测试。
`-GstRoot` 首次启用媒体并保存 CMakeCache，后续复用；`-WithoutMedia` 显式关闭。
使用参数 `-Fresh` 重建 CMake 配置，`-Deploy` 输出本地部署目录。
所有环境修改只在子进程中生效，不写系统环境变量。
根目录 run.ps1 启动本机信令服务与客户端演示，不执行构建。

Qt Creator 打开根 CMakeLists.txt 并选择已经配好的 Qt 6.8.3 MSVC2022 x64 Kit。
Qt / GStreamer 本机路径放在 Kit、环境变量或被忽略的 CMakeUserPresets.json，
公共 CMake 目标不写死个人电脑路径。

## 测试分层

| CTest 目标 | 验证内容 |
| --- | --- |
| core | 协议异常输入、房间校验、授权代次 / 重放、日志容量和导出、入会校验 |
| signaling | 真实本机 WebSocket、第三人拒绝、房间隔离、SDP/ICE 转发、20 次入退会、断开与重新加入 |
| media | 实际视频测试源、五次启停、有界缓存、取消 / 旧帧、失败后重试、设备枚举及静音音频测试源 |
| desktop | 两个真实 Qt 窗口、成员状态、OpenGL、本地预览动态画面及截图 |

core 和 signaling 不需要桌面。desktop 由 ZJDESK_GUI_TESTS 控制，需要可见桌面或 Xvfb，
Windows presets 已启用它；Linux presets 默认关闭，避免将 offscreen 当成真实 OpenGL 验证。
GUI 测试只向自己创建的 Qt 控件发送事件，不控制其他软件或注入系统键鼠。

CTest 输出位于 build/<preset>/Testing/Temporary/LastTest.log。
每组测试另写 JUnit XML 到 build/<preset>/test-results/，供后续 CI 收集。
desktop 输出截图到 build/<preset>/output/，这些生成文件不进入源码版本管理。
所有网络测试使用本机随机端口，不占用演示默认端口 8765。
物理摄像头用例默认跳过，必须在项目 SDK 环境中设置 `ZJDESK_CAMERA_TEST=1` 后执行
`zjdesk-media-tests cameraReleasesDevice`，单独保存结果；不能把默认跳过当作硬件通过。

## 持续集成准备

目前提供统一构建和测试入口，尚未启用云端 CI 工作流。
第一条 CI 应复用已验收的 Qt / 工具依赖缓存，先覆盖 core 与 signaling。
Windows 固定 VS 2022 x64；Linux 固定 Ubuntu 22.04 / GCC 11。
Linux Qt 6.8.3 必须在 22.04 环境构建，不把 ubuntu-latest 或不兼容的预编译 Qt 当作基线。
desktop、摄像头、屏幕采集、真实远控分别验收，不以逻辑测试代替。

## 维护约定

- UTF-8、C++17、四空格缩进；clang-format 配置仅规定格式，不自动批量改无关文件。
- 应用版本只在根 CMakeLists.txt 的 project(VERSION) 中修改，构建时生成公共版本头。
- 类型与状态使用值或明确的 QObject 所有者；不要同时给智能指针与 Qt parent 承担同一个对象的所有权。
- 所有业务状态更改通过 SessionCoordinator；UI 只读状态和发送用户意图。
- 网络、媒体和 UI 分别报告失败；错误不能被吞掉后仍显示“连接成功”。
- 为真实行为写测试，不增加仅镜像实现的测试。
- 依赖升级独立验证两端，记录版本、插件、部署方式及结果。
- 当前源码未采用第三方 UI 主题、图片、商业 SDK 或云服务。

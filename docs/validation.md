# 验证记录

## 0.2.0：2026-09-29 最新 Windows 验证

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

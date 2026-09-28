# 0.1.0 验证记录

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

tools/run-demo.ps1 已通过 Windows PowerShell 5.1 实际启动验证，
创建一个服务和 Alice / Bob 两个客户端。脚本不修改系统环境，结束时仅停止自己创建的服务。

Ubuntu 22.04、GStreamer 启用构建、真实音视频与远控尚未验证。
云端 CI 和正式安装包尚未交付。

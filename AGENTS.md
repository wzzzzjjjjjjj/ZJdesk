# 项目维护约定

- 先读 README.md、docs/architecture.md 与 docs/roadmap.md，区分已实现功能和后续计划。
- 用户的当前指示优先；改架构时同步更新文档，说明实际问题和可验证的收益。
- C++17、Qt 6.8 基线；Windows 使用 MSVC2022 x64，Ubuntu 基线为 22.04 / GCC11。
- 修改限定在用户要求涉及的模块，不做无关格式整理或推测性的抽象。
- UI 不直接调用网络、媒体或系统输入；会话状态由 SessionCoordinator 修改。
- Windows 验证入口：tools/build-windows.ps1；Release 部署加 -Preset windows-release -Deploy。
- 测试失败先修复；功能改动需要相关回归用例。GUI 测试必须有桌面，不能用 offscreen 冒充实际 OpenGL。
- build/、dist/、测试截图和日志是生成物，不进入源码版本管理。
- 只报告真实验证结果。GStreamer、音视频、远控、Ubuntu 实机与云端 CI 未完成时明确写出。

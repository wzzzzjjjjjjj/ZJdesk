# 2026-11-06 启动网络验证

本文件记录早期弹框方案及复核，已由主界面顶部单个对端 IP 输入替代。当前方案与验证见 [内嵌验证记录](validation-inline-network-20261106.md)，使用说明见 [启动网络验证](startup-network.md)。

工作树：`C:\Users\29822\.codex\worktrees\zjdesk-2\ZJdesk`，分支 `codex/ZJdesk-2`。本次未修改原始检出或其他工作树。

## 实现与验证目标

每次启动必须先验证 Windows / Linux 地址；输入记录不能绕过验证。验证失败或退出时不构造主界面，不注册远控码或请求摄像头权限。界面提供查询命令、当前本机地址、信令电脑与端口，以及失败后的修正方法。验证成功让屏幕共享和会议都使用所选本机地址；不修改系统网卡或路由。

## 已执行

环境为 Windows 11、MSVC 2022 19.44、Qt 6.8.3、GStreamer 1.26.11。

```powershell
.\build.ps1 -Preset windows-release -GstRoot F:\gstreamer\1.26.11\PFiles64\gstreamer\1.0\msvc_x86_64 -Deploy
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/scripts/test_start_both.ps1
```

- Release 构建、部署通过；部署目录为 `dist/windows-release/bin`。
- 默认 CTest 的 core、file-clipboard、signaling、network-setup、media、windows-runtime-integrity 共 6 组通过，耗时 165.85 秒。
- `network-setup` 的 19 项（含初始化 / 清理）通过：非法 / 同址 / 过期本机地址、错误服务、ping 非零退出与 Windows 不可达零退出、成功后重新失败、服务拒绝后重试、超时、取消和再次成功。
- 网络测试用 PATH 中的专用模拟 ping 覆盖成功 / 失败 / 挂起；WebSocket 实际连接本机活动 IPv4 上的测试服务。它不代表真实对端 ping 已通过。
- `media` 共 40 项，35 项通过、5 项跳过。新增检查实际 GStreamer ICE 候选只包含所选本机地址，并拒绝失效地址；合成音视频源用于回归。跳过项为两个 Linux 专用用例及三个需显式启用物理摄像头的用例。
- 启动器 44 项模拟回归全部通过，含未传 SSH 参数时要求输入、输入为空停止，以及原有选网卡、构建、锁、失败和重启分支。
- 在原生 Windows Qt 平台运行 `startupNetworkCannotSkipVerification` 与 `startupNetworkPersistsInputsButRequiresNewVerification`，含初始化 / 清理共 4 项通过（752 ms）。覆盖错误阻止进入、成功保存输入、重开仍须验证；没有使用 offscreen。
- 已查看 `output/ui-startup-network.png`，查询说明、地址字段、服务命令、错误信息和操作按钮显示完整。截图与测试日志均为忽略的生成物。

## 集中审查与修复复核

审查启动入口、地址验证、异步取消 / 重试与资源释放、会议地址绑定及启动器调用方，发现主界面仍允许编辑服务地址，可能连接未经启动验证的服务。先增加 `startupNetworkServerRemainsVerified`，确认原实现因服务地址可编辑而失败；随后将该字段设为只读，并提示修改时重启验证。会议沿用同一服务地址。

修复后 Release 构建与部署通过；重新执行三个启动界面用例，含初始化 / 清理共 5 项通过（955 ms），网络验证组再次通过（4.99 秒）。本次复核没有重跑完整媒体或桌面套件；其前述结果来自修复前的验证。复核未发现其他明确逻辑问题。

## 尚未验证

本次没有执行 Ubuntu 构建 / 桌面验证、真实 Windows / Linux 对端 ping、真实双机远控或摄像头 / 麦克风 / 扬声器验收，没有运行完整桌面 OpenGL 套件或云端 CI。启动检查不包含 WebRTC 动态端口、设备权限和真实媒体质量；更换网络后需重启并重新验证。

本次生成 Windows 部署目录，未重新生成自解压 EXE / ZIP 或 Linux AppImage 发布包。

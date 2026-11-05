# 两端有线 / 无线启动验证（2026-11-05）

## 范围与行为

修复双端启动器使用固定 Linux 有线地址的问题，并为 Windows 加入对称的有线优先、Wi-Fi 回退。只在启动时选择当前可达地址，不实现通话或远控过程中的切网重连，不修改系统路由、接口优先级或防火墙。

- Linux：通过已配置的 SSH 候选查询在线 IPv4 接口，物理有线优先，Wi-Fi 次之。
- Windows：枚举在线物理网卡的首选 IPv4 地址，有线优先，Wi-Fi 次之；跳过未连接接口、链路本地地址和自动源地址禁用的地址。
- 按 Linux 接口优先级、Windows 接口优先级逐对检查指定源地址和网卡的路由，再绑定 Windows 源地址探测 Linux TCP 22；只选择成功的组合。
- 所选地址同时用于信令目标及两端已有的媒体地址绑定，与控制方向无关。TCP 22 可达不代表媒体动态端口一定可达，需另做实际媒体验证。
- Docker / VPN 等虚拟接口不参与自动选择；显式 `-LinuxAddress` / `-WindowsAddress` 仍可使用。显式地址失效时报错，不改用其他网卡。
- 默认 SSH 候选为当前已知地址；所有候选均随 DHCP 变化后，需通过 `-LinuxSsh` 提供一个新的可登录地址。查询到主机后可以发现其他网卡的新地址。

## 回归与构建

此前的无线场景会继续启动 `ws://10.11.42.4:8765`，使用旧 Linux 有线地址；复现输出保存在 `build/network-wireless-before.log`。Windows 扩展前还复现了默认路由优先 Wi-Fi、有线不可达后未回退、只剩虚拟接口仍被自动使用等失败，见 `build/network-windows-before.log`。

42 项启动回归全部通过，覆盖两端有线 / 无线优先级、只有 Wi-Fi、断开的有线地址、无路由及不可达回退、DHCP 地址改变、虚拟接口排除、显式地址与失效地址，以及原来的缓存、源码同步、构建失败标记、锁、进程重启和端口冲突。执行入口为 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/scripts/test_start_both.ps1`，结果记录在 `build/network-both-launcher-final.log`。

Windows `build.ps1 -Preset windows-release -Deploy -SkipTests` 通过，日志 `build/network-windows-build.log`。Ubuntu 22.04 的隔离源码在 `/tmp/zjdesk-meeting-20261104.3eOqNN9R` 执行 `build.sh --preset linux-release --deploy --skip-tests` 通过，日志为该目录的 `build/network-build.log`。应用 C++ 未修改；未覆盖原 Linux 运行目录或重启用户现有客户端。

## 现场地址与有线链路

当前 Windows 有线 `10.11.42.108` / Wi-Fi `10.11.48.95`，Linux 有线 `10.11.42.4` / Wi-Fi `10.11.47.137`。自动选择结果为两端有线，记录在 `build/network-both-wired-selection.json`；显式指定两端 Wi-Fi 的路由与绑定源地址 TCP 探测也通过，记录在 `build/network-both-wireless-selection.json`。后者不等于拔线后的自动回退实测。

额外的后台验证程序仅采集真实桌面、解码及通过同一 WebRTC 数据通道双向发送探测消息和回执，不创建 OpenGL 显示窗口，不注入鼠标键盘，不读写剪贴板。验证程序、独立构建和输出均放在忽略的 `build/network-validation`，不加入产品源码或发布包；测试服务使用独立端口 18768。

两轮结束后已停止本次独立测试服务，保留用户原有的服务和客户端。

Linux 桌面发送、Windows 接收：运行 35 秒有效采样，两端均 `passed:true`，1920×1080，UDP，本地 / 远端地址匹配所选有线地址；Windows 收到 1057 帧、70 次回执，Linux 统计 1054 帧、69 次回执。末段交付约 29.91 fps，非 1080p60 验收。日志为 `build/network-validation/wired-final-{windows,linux}.log`。

Windows 桌面发送、Linux 接收：运行 35 秒有效采样，两端均 `passed:true`，1920×1080，UDP，地址匹配；Linux 收到 1072 帧、72 次回执，Windows 统计 1056 帧、70 次回执，末段交付分别为 30 / 29.88 fps。日志为 `build/network-validation/wired-windows-sender-{windows,linux}.log`。以上双向控制消息仅为数据通道探测，不代表实际键鼠注入验收。

首轮验证程序按各自启动时间结束，初始化耗时差导致 Linux 先退出，Windows 末尾缺帧而返回失败。改为首次 Running 后计 35 秒并在测量后保留连接 5 秒，上述最终轮通过；这次调整仅属于忽略目录下的验证程序。

## 联合验证与边界

用户确认可以开始后，实际执行 `start-both.ps1 -SkipBuild -ForceRestart`：自动选择 Windows `10.11.42.108`、Linux `10.11.42.4`，原有 ZJdesk 实例结束后两端客户端及 Linux 8765 服务重新启动成功。Linux 客户端环境确认绑定 `10.11.42.4`，Windows 客户端连接 `ws://10.11.42.4:8765`。日志在 `build/network-joint-wired-launch.log`。用户分别确认 Linux 控制 Windows、Windows 控制 Linux 的画面、鼠标拖动及空白编辑器键盘输入均正常。

随后用户拔掉两端网线并保持 Wi-Fi 联网，现场确认 Windows 有线接口已断开、Linux 在线地址中没有原有线 IP。相同启动命令自动选择 Windows WLAN `10.11.48.95`、Linux `wlp0s20f3` `10.11.47.137`，SSH 自动回退到 `orbbec@10.11.47.137`，两端客户端重新启动成功，日志在 `build/network-joint-wireless-launch.log`。Linux 客户端环境也确认绑定 Wi-Fi 地址。用户分别确认 Wi-Fi 下 Linux 控制 Windows、Windows 控制 Linux 的画面、鼠标拖动和空白编辑器键盘输入均正常。

| 网络条件 | 控制方向 | 用户现场确认 |
| --- | --- | --- |
| 两端有线 | Linux → Windows | 画面、鼠标、键盘均正常 |
| 两端有线 | Windows → Linux | 画面、鼠标、键盘均正常 |
| 两端拔掉网线，仅 Wi-Fi | Linux → Windows | 画面、鼠标、键盘均正常 |
| 两端拔掉网线，仅 Wi-Fi | Windows → Linux | 画面、鼠标、键盘均正常 |

本轮没有重跑原生 OpenGL 自动化、音视频设备、文件粘贴、长时间稳定性或干净系统验收。真实远控画面与键鼠由用户现场确认。启动脚本修改不改变已交付 EXE / AppImage 的应用二进制，本轮未重新打包。

## 集中补审

复核了本轮启动脚本、网络选择、调用入口、源码快照、媒体地址绑定及验证程序：未发现产品启动逻辑的明确新问题。重点检查了显式地址不回退、不可达地址组合、SSH 候选回退、虚拟网卡排除、源地址与路由匹配，以及原有锁和进程启动流程。真实回环 TCP 测试验证开放端口返回真、关闭端口和无效本地地址返回假，未影响运行中的客户端。

发现忽略目录下的验证脚本只依据 Windows 进程退出码返回，Linux 报告失败时仍可能返回成功。故障注入复现 Windows 成功、Linux `passed:false` 时退出码为 0；现增加 Linux 报告获取和结果校验。三种故障注入结果为 Linux 失败退出 1、缺少结果退出 1、成功退出 0，日志为 `build/network-validation/review-report-{before,failed,missing,passed}.log`。此前两轮有线后台验证均已人工核对两端 `passed:true`，不受此漏检影响；无需重跑相同媒体验证。

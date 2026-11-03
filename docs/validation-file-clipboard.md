# Windows ↔ Linux 文件剪贴板验证（2026-11-03）

实现位置：`C:\Users\29822\.codex\worktrees\bfbd\ZJdesk`，分支 `codex/ZJdesk-2`。
Linux 验证使用独立目录 `/home/orbbec/ZJdesk/file-clipboard-bfbd-20261103`，不覆盖原始检出。

## 使用

两端使用本次版本，Windows 请求远控，Linux 本地允许控制（授权提示包含文件复制）。
在来源电脑的文件管理器选择文件或目录并 Ctrl+C，等待远控状态显示文件已就绪，然后切换到目标电脑的文件夹 Ctrl+V。
Windows → Linux 时，可在 Windows 复制后点击远控画面中的 Linux 文件管理器粘贴。文件预先传到目标电脑缓存，客户端退出后缓存删除；停止控制后仍能粘贴已完成的批次。

支持多选、中文和空目录，每批最多 4096 个文件 / 目录。剪切按复制处理，不删除源文件。
拒绝符号链接、Windows 不兼容名称、路径穿越、重复路径与无效数据。源文件改变、传输超时、本机剪贴板改写或撤销控制会取消未完成批次。
普通共享与会议不启用文件同步。文本、Wayland 和文件拖放未实现。反向能力单独协商，旧对端保持单向；导入剪贴板带标记，避免自动传回。

## 环境与方法

Windows 11 / MSVC2022 x64，Ubuntu 22.04 / GCC 11.4.0；Qt 6.8.3、GStreamer 1.26.11。
Linux 使用真实 X11 桌面 `:1`；Windows 使用原生 `qwindows`，未使用 offscreen。
两台机器通过实际局域网连接，媒体地址分别为 Linux `10.11.42.4`、Windows `10.11.42.57`。

显式选择 `WindowTests::fileClipboardAcrossMachines`，Linux 使用 source 角色、Windows 使用 controller 角色：

- 通过真实信令与远控授权建立 WebRTC 数据通道，普通入会不会激活文件复制。
- Linux Qt/X11 剪贴板放入 GNOME 文件列表，复制包含 300004 字节二进制文件及空子目录的中文目录。
- Windows 接收完成后校验全部字节、目录和 Qt 文件 URL。
- 使用 `GetClipboardData(CF_HDROP)` / `DragQueryFileW` 验证原生文件列表与 Unicode 路径。
- 用 Windows `SHFileOperationW(FO_COPY)` 将原生剪贴板路径复制到另一个临时目录，再校验字节和空目录。
- Windows 随后复制中文目录（二进制文件、空子目录）和独立空文件；Linux 校验接收内容。
- Linux 通过独立 Python / GTK 进程读取 X11 的 `x-special/gnome-copied-files`，使用 GIO 将原生剪贴板路径复制到另一临时目录，再校验全部字节和目录。
- 断言导入文件未自动传回；停止远控、离开房间，验证两端源文件未删除，已完成的缓存仍存在。

这是原生剪贴板与系统文件复制接口验证；未用鼠标操作资源管理器 / Nautilus，也未模拟 Ctrl+V。
用例暂存原剪贴板并在结束时尝试恢复；Windows 剪贴板被占用时恢复可能失败。自动允许控制仅存在于显式选择的测试角色中，产品仍要求 Linux 用户确认。

## 双向版本回归

两端 Release 构建、常规回归与部署通过：

- Windows 5/5：core、file-clipboard、signaling、media、windows-runtime-integrity，总计 133.57 秒。
- Linux 4/4：core、file-clipboard、signaling、media，总计 122.21 秒。
- Windows 原生文件格式、本机覆盖取消、占用失败清理和双向授权门禁用例通过，含初始化 / 清理共 5 项，855 毫秒。
- Linux X11 原生文件格式、不回传与双向授权门禁用例通过，含初始化 / 清理共 4 项，48 毫秒。
- 同一远控会话先 Linux → Windows、再 Windows → Linux，连续 5/5 轮通过上述原生格式与系统复制验证；每轮两端均 3 项通过、0 失败，无外部剪贴板取消或崩溃。

覆盖旧对端的单向回退、当前授权代次、撤销后双向停止，以及接收文件不重新发送。Windows 双机夹具在原生 Shell 复制后处理 100 毫秒的本机通知，再发布反向文件列表并断言剪贴板所有权；这段等待只存在于测试。

Windows 日志：`build/file-bidi-regressions.log`、`build/file-bidi-native-final.txt`。
Linux 日志：`/tmp/zjdesk-bfbd-bidi-regressions.log`、`/tmp/zjdesk-bfbd-bidi-native.txt`。
双机日志：`build/file-bidi-settled-{1..5}.txt` 及对应 `-linux.txt`，汇总为 `build/file-bidi-settled-summary.txt`。
Windows 发布文件与构建文件 SHA-256 一致：`2EB88DF48537DD3944766AA2A045C4753999E5ED083634BE4050843241AC8A54`。

集中补审检查双向授权、代次、消息路由、剪贴板回传和撤销 / 析构清理，未发现明确的产品逻辑问题。
辅助验证脚本原先未检查 Linux 进程等待与日志读取命令的退出码，现改为超时或读取失败即终止。
修正后额外真实双机 1/1 轮通过，两端均 3 项通过并正常退出，日志为 `build/file-bidi-audit-1.txt`、对应 `-linux.txt` 和 `build/file-bidi-audit-summary.txt`。
补审未修改产品代码，没有重复上述完整构建与媒体回归；辅助脚本仍作为 `build/` 生成物，不进入源码版本管理。

## 单向版本回归

`file-clipboard` 覆盖 MIME 解析、多选、二进制与空文件 / 目录、旧授权代次、撤销、路径与名称拒绝、坏块、大小写冲突、源文件修改及批次替换。
`media` 覆盖文件与输入的消息分发、原控制消息限制、文件大小限制、过期媒体代次及现有共享 / 会议回归。
原生 GUI 用例另验证 `CF_HDROP`、本机重新复制取消传输，以及文件能力与当前授权门禁。

集中审查前的完整 Release 构建与部署均通过：

- Windows 常规回归 5/5，通过 core、file-clipboard、signaling、media 与 windows-runtime-integrity，总计 134.20 秒。
- Linux 常规回归 4/4，通过 core、file-clipboard、signaling 与 media，总计 121.87 秒。
- Windows 原生文件剪贴板与授权用例通过，含初始化 / 清理共 4 项，105 毫秒。
- 真实双机连续 12/12 轮完成上述接收、原生格式、系统 Shell 复制与退出检查；本轮无外部剪贴板取消、崩溃或失败。

Windows 日志为 `build/file-release-regressions.log`、`build/file-release-native.txt`，
双机日志为 `build/file-release-{1..12}.txt` 及对应 `-linux.txt`，汇总为 `build/file-release-summary.txt`。
Linux 回归日志为 `/tmp/zjdesk-bfbd-files-release-regressions.log`。日志与构建产物均未加入源码版本管理。

Windows 部署位于当前工作树 `dist/windows-release/bin`，已核对客户端与构建目录 SHA-256 一致。
Linux 部署位于 `/home/orbbec/ZJdesk/file-clipboard-bfbd-20261103/dist/linux-release`。

集中审查发现，剪贴板发布失败时，未发布的缓存被提前移入已完成列表，会滞留到客户端退出。
新增原生用例从另一线程占用 Windows 剪贴板，先复现缓存仍存在的失败，再将缓存保留操作移到发布成功之后，验证失败时立即清理、不发出就绪信号。
修复后 Windows 原生用例含初始化 / 清理共 5 项通过（502 毫秒），文件 / core / 信令及 Windows 运行完整性针对性回归 4/4 通过（10.38 秒），
Linux 文件 / core / 信令针对性回归 3/3 通过（4.44 秒），真实双机额外 1/1 轮传输与 Shell 复制通过；两端 Release 已重新构建部署。
补审日志为 `build/file-review-busy-before.txt`、`build/file-review-native.txt`、`build/file-review-regressions.log`、`build/file-review-1.txt` 及对应 `-linux.txt`，
Linux 回归日志为 `/tmp/zjdesk-bfbd-files-review-regressions.log`。补审只调整缓存保留时机，媒体代码未再修改，未重复上述完整媒体回归。

## 验证范围

未验证公网 / 高延迟网络、大文件长时间传输、真实用户拖放、其他 Linux 文件管理器或 Wayland。
文件和输入复用可靠有序的控制通道；每次最多一个 16 KiB 文件块等待确认，避免无界排队，低带宽时仍可能增加输入延迟。

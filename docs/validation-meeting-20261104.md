# 四人会议实现与验证（2026-11-04）

本次完成最多四人的局域网会议：成员广播、定向信令、独立媒体连接、共享设备采集、三路音频混音及四个视频画面。协作远控仍限两人。按用户本轮要求，真实设备只测摄像头；麦克风与扬声器均使用合成 / 无声设备，真实听感暂缓。

## 环境与范围

- Windows 11 x64，MSVC2022，Qt 6.8.3，GStreamer 1.26.11；有线地址 `10.11.42.46`。
- Ubuntu 22.04 x64，GCC11，Qt 6.8.3，GStreamer 1.26.11；有线地址 `10.11.42.4`，SSH 经 `10.11.47.137`。
- 信令为本次后台服务 `ws://10.11.42.4:18765`。未修改网络或防火墙配置。
- Linux 源码与构建位于隔离目录 `/tmp/zjdesk-meeting-20261104.3eOqNN9R`，未替换原工作目录或 `start-both-current` 启动标记。
- Windows Release 已构建并部署到 `dist/windows-release`；生成物、日志及截图不进入版本管理。

## 实现与覆盖

- 会议房间最多四人，拒绝第五人；协作房间仍拒绝第三人。加入 / 离开通知全体成员，超过两人的协商必须带 `targetPeerId`，服务端写入真实发送者身份。
- 每对成员独立协商、状态和 45 秒期限，一人离开、失败或超时不结束剩余连接。离会重入生成新的成员身份，隔离旧协商。
- 本机摄像头、麦克风和输出各打开一次；最多三路 VP8 / Opus WebRTC 连接，接收音频经 `audiomixer` 输出。各路帧邮箱有界，保留最高原生摄像头采集及按实际编码 / 交付速度自适应输出。
- 显示本机加三位远端的独立画面 / 昵称及隐藏状态。三人以上屏幕共享禁用，第三人加入停止原双人共享；一对多共享尚未实现。

## 自动与桌面回归

| 检查 | 结果 | 说明 |
| --- | --- | --- |
| Windows Release 构建、部署 | 通过 | `build.ps1 -Preset windows-release -Deploy -SkipTests` |
| Windows 常规 ctest（排除 desktop） | 5/5 通过，140.98 秒 | core、file-clipboard、signaling、media、windows-runtime-integrity |
| Windows 慢消费者自适应专项 | 通过，5.51 秒 | `meetingAdaptsToSlowFrameConsumer`；在上述完整测试后新增并独立运行 |
| Ubuntu Release 构建 | 通过 | 最终界面修复亦已同步并编译 |
| Ubuntu 常规 ctest | 4/4 通过，134.13 秒 | 包含四人媒体与慢消费者自适应测试 |
| Windows 真实桌面 UI 专项 | 6 项通过 | QtTest 计入初始化 / 清理后为 8 passed、0 failed，15.11 秒 |

四人媒体回归逐步建立二至四人连接，检查十二条单向视频 / 音频、三路混音输入逐个静音 / 恢复、同时发送时输出非零、扬声器静音、摄像头隐藏 / 恢复、成员离开 / 重入及设备释放。原手动双人呼叫、自动入会默认开关、并行双人共享回归保留。

UI 专项为 `meetingFourVideoTiles`、`meetingJoinDefaultsPersist`、`workspaceFitsSmallWindowAndLongNames`、`conferenceControlsAndFrames`、`localPreviewIsNotAvailable`、`meetingEntryRefreshesCameraAndHintsMissingDevice`。使用真实 Windows 桌面及原生 OpenGL，后台无激活窗口；未使用 Qt offscreen。检查 1040×720 逻辑尺寸下四个画面非空、无重叠、成员离开和单路超时不影响其他连接。截图为 `output/ui-meeting-four-small.png`。

测试中发现并修复：协作房间成员误出现在会议名册；直接实例化媒体的新增测试缺少 GStreamer 初始化。移植过程中还修复共享采集绕过原摄像头自适应的问题，并增加慢消费者回归。最终上述检查均通过。

## 集中代码补审

补审检查了本轮 diff、成员路由、会话与设备生命周期、线程回调、帧邮箱及界面调用方，另发现两项边界问题，均先用回归复现后最小修复：

- 共享采集尚未就绪时，一位成员发来的停止消息会因媒体成员集合尚为空而结束整个本机会议。现在等待采集就绪后按成员分别连接；其余成员继续通话，若所有远端均已停止则释放设备。
- 第三人加入后，迟到的双人共享开始消息仍可启动屏幕接收。现在接收端同样拒绝三人以上的共享开始消息，停止消息仍正常处理。

新增 `meetingEarlyStopKeepsOtherMembers` 覆盖启动竞态、其余成员持续连接和唯一远端停止后的设备释放；`meetingRejectsLateScreenShare` 覆盖迟到共享。修复后的 Windows 原生桌面专项共 7 项通过，QtTest 计入初始化 / 清理后为 9 passed、0 failed，15.905 秒。

修复后 Windows Release 重新构建并部署，Ubuntu 隔离目录重新构建；两端再次运行 signaling / media 回归，均为 2/2 通过，Windows 140.03 秒、Ubuntu 133.25 秒。日志为 `build/meeting-20261104/ctest-review-final.log`、`linux-ctest-review-final.log`、`ui-review-final.txt`；修复前两项失败证据为 `review-stop-before.txt`、`review-share-before.txt`。补审未发现其他明确逻辑问题，真实听感和长期稳定性仍未验证。

以下真实设备记录来自补审前的实现验证；补审修复未重复跨机摄像头实测，保留原始证据及其范围。

## 两机真实摄像头：70 秒

两端各一个真实摄像头进程，收到彼此的 1280×720 画面。Windows 在约 17.5 / 24.5 秒隐藏 / 恢复、35 / 42 秒离会 / 重入。两端最终均报告 `passed:true`、两人入会及设备释放。

末段 Windows 收到 Ubuntu 共 685 帧，最近约五秒 67 帧；Ubuntu 在 Windows 重入后的新身份下收到 545 帧，最近约五秒 125 帧。Windows 记录的 7832 ms 最大间隔包含计划离会，不能当作稳定通话帧间隔。

Ubuntu 图像接近全黑。用户已晃手，但暂无法确认隐私挡板和照明，要求先记录画面偏暗。因此只确认设备采集、跨机传帧及生命周期，实际可辨认的画面内容尚未验收。

## 两机四进程：90 秒

每台电脑一个真实摄像头成员、一个合成摄像头成员，共四人。Windows 合成成员执行隐藏 / 恢复、离会 / 重入；其余成员继续接收其他画面。四个进程最终均报告 `passed:true`、四人入会和 `released:true`。真实摄像头为 1280×720，合成摄像头为 640×480。

末段约五秒，Windows 真实成员收到 Ubuntu 真实成员 61 帧、两个合成成员分别 84 / 86 帧；Ubuntu 真实成员收到 Windows 真实成员 112 帧、两个合成成员分别 129 / 127 帧。这是后台帧交付统计，不是显示器帧率或帧率保证。合成成员自身离会造成的约十秒间隔不计为其他连接中断。

## 最终版本复验与资源采样

在恢复共享采集自适应和完成最终构建后，另做两机四进程 45 秒复验。各端配置与上轮相同，Windows 合成成员执行隐藏 / 恢复、离会 / 重入。四端均报告 `passed:true`、四人入会及 `released:true`。Windows 真实成员在末段约五秒收到 Ubuntu 真实成员 68 帧、两个合成成员各 133 帧；其非离会远端最大交付间隔为 625 / 613 ms。结果仍是短时链路验证，不等于稳定帧率验收。

约第 31–32 秒采样，CPU 以一个逻辑核满载为 100%，为启动以来平均值；内存是单次工作集 / RSS，线程数含底层库。本机同时有两个会议进程，不能直接当作四台独立电脑的硬件门槛。

| 进程 | CPU（单核=100%） | 内存 KiB | 线程 |
| --- | ---: | ---: | ---: |
| Windows 真实摄像头 | 243.4% | 304048 | 114 |
| Windows 合成摄像头 | 125.3% | 171320 | 107 |
| Ubuntu 真实摄像头 | 170% | 332708 | 93 |
| Ubuntu 合成摄像头 | 137% | 176388 | 93 |

运行期间 `fuser /dev/video0` 只列出 Ubuntu 真实摄像头进程，证实三个远端共用一次摄像头采集；退出后设备无占用。本次后台信令服务也已停止。

## Linux 依赖补齐与日志

Linux 首次真实设备探针因 SDK 缺少 `audiomixer` 失败，日志为“无组件‘audiomixer’”。已在同版本 GStreamer 1.26.11 构建中启用 `-Dgst-plugins-base:audiomixer=enabled`，编译并安装该插件到独立 SDK 前缀；没有混入系统旧插件。媒体诊断新增 `appsrc` / `audiomixer` 必需组件检查。自建 SDK 也需启用该选项。

本机证据目录：`build/meeting-20261104/`，包含 `ctest-final.log`、`adaptive-final.txt`、`ui-final.txt`、`four-media.txt`、两轮四进程日志和摄像头快照。资源记录为 `resources-windows-final.json`、`resources-linux-final.txt`；Linux完整回归另保存为 `linux-ctest-final.log`。

## 单画面放大与恢复（2026-11-04）

共享屏幕、本机及三位远端摄像头均提供“放大 / 缩小”，Esc 恢复原布局。保留原 OpenGL 控件、父级关系和媒体连接；恢复普通窗口位置或原最大化状态。成员身份更换、离开或画面清空时自动退出放大。协作远控的原全屏入口增加右上角“缩小”按钮，保留输入坐标映射。

专项检查逐个放大五个画面、刷新成员状态后持续占满窗口、摄像头继续呈现、共享测试图颜色、按钮 / Esc 恢复、最大化恢复及成员离开清理。Ubuntu 1040×720 下发现原四人视频最小高度导致约两像素重叠，已缩小多人画面的最小高度和卡片间距，并通过无重叠回归。异步布局与窗口状态使用等待断言；协作画布测试按对象名定位，避免误取隐藏会议页的摄像头控件。

- Windows：MSVC2022 / Qt 6.8.3 Release 构建和部署成功。`meetingFourVideoTiles`、`expandedVideoKeepsSurfaceAndRestoresWindow`、`workspaceFitsSmallWindowAndLongNames`、`conferenceControlsAndFrames` 共 6 passed / 0 failed（含初始化 / 清理），54.204 秒；部署运行库检查 1/1 通过。
- Ubuntu 22.04 / GCC11：隔离目录 Release 构建成功，`meetingFourVideoTiles`、`expandedVideoKeepsSurfaceAndRestoresWindow` 共 4 passed / 0 failed（含初始化 / 清理），2.427 秒。
- 两端均使用实际桌面与原生 OpenGL，未使用 offscreen；本专项摄像头为合成源，共享画面为注入测试图，未重跑双机真实摄像头或真实桌面共享。已查看 Windows 放大摄像头 / 共享图与 Ubuntu 四人布局截图。

证据位于 `build/meeting-20261104/ui-expand-delivery.txt`、`build/meeting-20261104/linux-ui-expand-delivery.txt`、`build/meeting-20261104/ctest-expand-runtime.txt`。截图为 `output/ui-meeting-expanded-camera.png`、`output/ui-meeting-expanded-share.png`、`build/meeting-20261104/linux-meeting-four-small.png`；均为生成物。

### 放大功能集中补审

检查本轮 UI 差异、画面更新调用方和窗口恢复生命周期，复现了一个边界问题：尚未收到首帧时放大占位画面，离会后仍保持全屏。原因是空帧再次清空不触发 `frameAvailabilityChanged`。现按会议成员状态退出放大，新增本机空画面离会及共享空画面断线回归；修复前用例失败，修复后两端通过。

另补充放大期间隐藏 / 恢复本机摄像头、引发画面排序变化的检查。旧小窗口布局用例在注入模拟名册前等待会议媒体停止，避免异步媒体状态刷新覆盖测试名册。

- Windows Release 部署成功，五项 UI 专项共 7 passed / 0 failed（含初始化 / 清理），21.396 秒；运行库检查 1/1 通过。构建与部署的 `zjdesk.exe` SHA256 一致。
- Ubuntu Release 构建成功，四项 UI 专项共 6 passed / 0 failed（含初始化 / 清理），5.341 秒。
- 两端使用实际桌面与原生 OpenGL；本轮未重跑真实双机摄像头或真实桌面共享。

证据为 `build/meeting-20261104/ui-expand-review-before.txt`、`build/meeting-20261104/ui-expand-review-final.txt`、`build/meeting-20261104/linux-ui-expand-review-final.txt` 和 `build/meeting-20261104/ctest-expand-review-runtime.txt`。

## 仍待验收

两机四进程能验证成员路由、并行媒体、混音和入退会，不替代三至四台实体机。后续还需确认明亮可辨认的真实画面、真实麦克风 / 耳机听感与同时说话、异常退出及至少 30 分钟稳定性。当前无 AEC、纯语音入会、会议网络拥塞自适应、STUN / TURN；三人以上一对多屏幕共享为可选后续项。

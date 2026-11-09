# 会议共享桌面控制权切换验证

工作树为 `C:\Users\29822\.codex\worktrees\zjdesk-1\ZJdesk`，分支为 `codex/ZJdesk-1`。Ubuntu 使用独立源码目录 `/home/orbbec/ZJdesk/meeting-control-20261109`。

## 使用流程与实现

观看者点击“申请控制”，共享者在成员选择框中选定申请者并点击“交给所选成员”。共享者也可直接指定或切换成员，一次一人，可拒绝、收回，观看者可取消申请或释放控制。已有控制者或待处理申请时，其他申请结束，由共享者决定下一位成员。申请 30 秒未批准会取消。

获准后工具切为“远控”。选择浏览或绘制工具会释放已按键鼠并暂停输入，保留当前授权，重新选择远控可继续操作。标注画布在切换控制成员时保留。共享结束、控制者或共享者离会、控制通道关闭时清理授权并释放键鼠，更换共享者需重新授权。

复用每位观看者的现有 `ScreenShare` DataChannel 和 `InputReceiver` 输入线程。输入校验共享代次、由本机通道绑定的成员身份、授权代次与递增序号。授权切换使旧输入失效。信令申请和撤销携带 `streamId`、`requestId`、`targetPeerId`，匹配当前共享流和当前请求。动态会议通道的输入回执排队回到通道对象，避免销毁期间的跨线程访问。

## 构建与常规回归

Windows 使用 MSVC2022 x64、Qt 6.8.3、GStreamer 1.26.11，Release 构建和部署成功。六组非 desktop CTest 全部通过，耗时 165.93 秒，包含 core、file-clipboard、signaling、network-setup、media、windows-runtime-integrity。构建与部署的 `zjdesk.exe` SHA-256 相同，为 `BCB070AB809F2C9B0455FD2F1DA04D3AF8075B38D4844115BD84AE4C5481B412`。

Ubuntu 22.04 使用 GCC 11.4.0、Qt 6.8.3、GStreamer 1.26.11，Release 构建和部署成功。五组非 desktop CTest 全部通过，耗时 146.86 秒，包含 core、file-clipboard、signaling、network-setup、media。

Windows 日志位于 `build/meeting-control-final-build.log`，Ubuntu 日志位于 `build/meeting-control-ctest.log`。完整 desktop 套件未运行。

## 专项回归

两端以下八个用例全部通过，计入初始化与清理后各为 10 通过、0 失败、0 跳过。Windows 耗时 13.679 秒，使用独立 Windows 原生桌面；Ubuntu 耗时 7.668 秒，使用真实 X11 会话 `DISPLAY=:1`。均使用原生 Qt 平台。

- `inputReceiverBindsAuthorizationToMemberAndEpoch`
- `meetingControlSwitchesMembersAndKeepsAnnotations`
- `meetingAnnotationsReachEveryViewerAndLateJoiner`
- `annotationToolsStayAlignedWithSharedFrame`
- `inputExecutionDoesNotWaitForHostUiEvents`
- `motionInputReportsRejectionTimeoutAndRecovery`
- `rejectedHostInputExplainsFailureWithoutNativeInput`
- `inputReceiverReleasesEmptyStateWithoutUiEvents`

新增会议用例通过同机四个真实成员和 GStreamer 测试画面覆盖申请与共享者按钮批准、主动切换、第三人伪造输入和撤销、繁忙请求、旧请求撤销无效、标注模式释放、标注保留、控制者离会、拒绝、超时、停止共享和更换共享者。输入探针使用不支持的输入类型，检查路由、拒绝回执与授权隔离，未执行原生鼠标点击或键盘按键。单独输入线程回归继续验证主 UI 不处理事件时输入处理仍可完成。

Windows 另运行 `workspaceFitsSmallWindowAndLongNames`、`meetingEmptyExpansionRestoresOnLeave`、`meetingFourVideoTiles`，计入初始化与清理为 5 通过、0 失败、0 跳过，耗时 13.239 秒。原生窗口及 OpenGL 由这些既有 GUI 用例覆盖，新增四成员授权用例主要检查协调层和控件事件链路。

Windows 专项记录位于 `build/meeting-control-ui-original.txt`，额外布局记录位于 `build/meeting-control-ui.txt`；Ubuntu 专项记录位于 `build/meeting-control-ui.txt`。测试画面被显式赋予桌面控制能力的夹具会产生 `GstVideoTestSrc` 缺少 `show-cursor` 属性的警告，仅出现在该合成夹具。

## 集中补审与最终部署

补审检查了本轮 diff、授权与撤销调用方、输入线程、动态通道生命周期及双人协助兼容路径。发现并修复两项问题。

- 观看者取消申请或收到收回通知后，另一通道迟到的授权仍会开启远控。补审前新增回归实际失败，状态为 `Controlling`，预期为 `Idle`。现按当前共享记录已结束请求，拒绝迟到授权，并忽略重复授权以保留输入序号。回归覆盖取消后授权、信令撤销先到、DataChannel 撤销先到、后续新授权和重复授权。
- 会议复用的 `file_ready` 处理会启动文件剪贴板，补审前新增断言实际失败。现将 `startFileClipboard()` 限定为双人协助模式。扩展原授权用例同时验证双人协助正向、反向流程继续工作，会议收到该消息后两者均不启动。

最终 Windows 与 Ubuntu Release 均重新构建部署。上述八项专项加 `fileClipboardRequiresGrantAndCurrentEpoch` 共九项全部通过，计入初始化与清理后两端各 11 通过、0 失败。Windows 耗时 14.933 秒，独立原生桌面；额外尝试的 `windowsControlHostWithoutDesktopInput` 因缺少显式跨机配置而跳过。Ubuntu 耗时 7.414 秒，原生 X11。两端另各通过 core、file-clipboard、signaling 三组 CTest，分别耗时 21.34 秒和 15.59 秒。最终修订未重跑完整媒体及 desktop 套件，前文全量结果属于补审前版本。

Windows 最终 build / dist 的 `zjdesk.exe` SHA-256 一致，为 `3EB254612C72CF83AF600149DFF41E1202D034B453E86602B9FB5B93F48B3FA1`，替代前文补审前校验值。最终记录为 `build/meeting-control-review-final-build.log`、`build/meeting-control-review-final-ui.txt` 和 `build/meeting-control-review-final-ctest.log`。Ubuntu 使用同名日志，副本在 Windows 的 `build/meeting-control-review-linux-final-*`。

Ubuntu 首次补审专项误用了运行库路径，导致 GStreamer 插件加载失败，该次测试已停止；最终记录使用项目现有 Qt / GStreamer 环境重新运行。合成桌面能力夹具仍有测试源缺少光标属性的警告。真实跨机验收边界如下。

## 启动与验收边界

Windows 新版位于当前工作树的 `dist/windows-release/bin/zjdesk.exe`，可使用当前工作树的 `直接启动.bat`。Ubuntu 在 `/home/orbbec/ZJdesk/meeting-control-20261109` 执行 `bash run.sh`。两端使用本轮客户端，验证对端 IP 后加入同一会议，由一人共享主屏幕，再申请或指定控制成员。

本轮未进行两台实体机之间的会议原生鼠标、点击、拖动与键盘验收，未测四台实体机、长时间负载、真实摄像头或音频听感。当前回归不能作为上述验收的通过结论。

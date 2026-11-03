# 2026-11-03 Linux 控制 Windows 验证

工作树：`C:\Users\29822\.codex\worktrees\033f\ZJdesk`，分支 `codex/ZJdesk-1`，本次修改未提交。

## 实现范围

- Windows 普通桌面使用 SendInput 执行鼠标、滚轮和常用 Qt 键码；扫描码覆盖扩展方向键等键，按下记录仅跟踪系统实际接受的事件，撤销 / 离会沿用既有释放流程。
- 主屏幕物理坐标映射到虚拟桌面，处理负坐标副屏；读取 Windows 真实光标、热点与可见性，使用 Windows 采集源的 show-cursor 开关。
- Linux 可在协作空间请求控制 Windows；被控方仍需本地允许，拒绝时不采集桌面。
- 自动帧率上限取双方主屏幕刷新率较低值，最高 240 fps；旧端未声明时沿用发送端上限，成员离开清除旧上限。继续沿用 VP9 profile 1 / 4:4:4、原生分辨率优先与实际编码 / 解码 / 交付 / 呈现反馈调档，网络反馈调码率；未修改 ICE / UDP / TCP 默认值。
- 高画质和高帧率受到硬件与网络的共同限制，本次没有承诺固定 1080p60 或任意设备上的全局最优。

## 构建与自动回归

Windows：Qt 6.8.3 / MSVC2022 x64，GStreamer 1.26.11。入口：

```powershell
.\build.ps1 -Preset windows-release -Deploy -GstRoot 'F:\gstreamer\1.26.11\PFiles64\gstreamer\1.0\msvc_x86_64'
```

新增单元回归覆盖共同刷新率上限及恢复不越界、Windows 扩展扫描码、主屏 / 虚拟桌面坐标、按钮按下 / 释放、滚轮及畸形事件。最终 Windows Release 构建部署和常规 CTest 4/4 通过，其中单元测试 56 项通过；其余为信令、媒体与 Windows 运行权限完整性检查。媒体回归覆盖接收方 30 fps 限制与旧客户端重新入会恢复 60 fps。未运行完整 GUI 测试套件。

Linux：Ubuntu 22.04 / GCC 11.4 / Qt 6.8.3 / GStreamer 1.26.11。为避免覆盖 Linux 原检出的未提交修改，单独建立 `/home/orbbec/ZJdesk/codex-033f-control-20261103`，通过 `bash ./build.sh` 构建；核心、信令、媒体 CTest 3/3 通过。

Windows 生成日志：`build/control-windows-final.log`；Linux 生成日志：上述隔离目录中的 `build-control-final-regression.log`。生成物不提交。

## 真实 Windows → Linux 桌面后台链路

同一办公网络：Windows `10.11.42.57`，Linux 有线 `10.11.42.4`；临时独立信令监听 Linux 18766 端口，检查结束自动关闭。

显式启用测试夹具 `WindowTests::windowsControlHostWithoutDesktopInput` 和 `MediaTests::linuxControlsWindowsWithoutDesktopInput`。Windows 使用真实 QApplication 原生桌面环境，未显示测试窗口；Linux 仅接收 / 解码，不呈现 GUI。两端测试通过：

1. Linux 发现 Windows 被控能力并请求，Windows 测试端拒绝，确认未启动桌面采集。
2. Linux 再次请求，Windows 测试端显式授权，真实 D3D11 桌面经 WebRTC 到达 Linux VP9 解码器。
3. Linux 发送一个不支持的测试事件，Windows 返回 accepted=false；这验证授权代次和输入通道，未执行鼠标移动、点击、滚轮或键盘输入。
4. Linux 撤销，Windows 退出被控并停止自动启动的共享。

最终短测：1920×1080，目标 60 fps，10 秒等待结束时采样统计解码约 57.77 fps、交付约 58.76 fps，UDP、丢包 0，未执行输入的探针往返约 10.1 ms。启动 / 清理在内两端测试各 3 项通过。测试只计数帧、不保留全部图像，避免夹具自身的内存压力影响处理速度。短测仅证明此网络的后台链路可用，不能当作持续性能结论。

生成日志：Windows `build/control-cross-windows.txt`，Linux `build/control-cross-linux.txt`。

## 集中代码审查补充

审查了本次完整 diff、协调层授权 / 停止 / 失焦释放调用链、帧率参数到媒体线程的传递、Windows 光标 GDI 资源与输入记录生命周期。

发现并复现两处特殊键问题：本机 MapVirtualKeyW 对 NumLock 返回 0x45、未含扩展前缀；PrintScreen 返回 0x54（SysRq），不适合作为普通扫描码直接发送。新增回归先得到 2 项失败，再最小修复：NumLock 显式增加 KEYEVENTF_EXTENDEDKEY，PrintScreen 与 Pause 一样使用虚拟键，由 Windows 合成特殊序列。

修复后 Windows 全部单元测试 **58 项通过**，Release 重新构建部署通过，diff 空白检查通过。未重复完整媒体 / 信令回归；修复仅影响 Windows 特殊键翻译，不改变媒体或 Linux 编译路径。未向真实桌面发送这些按键。日志：`build/control-review-red.txt`、`build/control-review-core.txt`、`build/control-review-build.log`。

## 鼠标延迟与管理员窗口反馈修正

用户从本工作树 `构建启动.bat` 启动，反馈鼠标移动明显延迟，并确认点击无效仅发生在管理员程序 / UAC 窗口。普通权限 SendInput 不能向更高完整性程序注入输入；由用户右键本工作树 `构建启动.bat`，选择“以管理员身份运行”，可重启两端并以提升权限运行 Windows 客户端，再验证普通管理员程序。UAC 安全桌面仍不支持，不修改 Windows 的 UAC 策略。

原 VideoSurface 在控制画面内隐藏本地光标，只绘制被控端回传位置，指针响应至少经过一次输入 / 位置往返。现取得有效形状后改用本地系统光标，缓存缩放后的图像与热点，指针随本地鼠标即时移动，不依赖后续远端位置或视频帧。画面内不重复绘制回传位置；黑边、离开画面、无画面和停止控制恢复默认指针。点击 / 拖拽坐标、8 ms 移动合并、授权 / 序号和离散事件顺序保持原逻辑。实际输入执行、形状变化确认与窗口内容反馈仍受网络及媒体处理影响，本次没有降低视频接收缓冲。

新增 `controlSurfaceUsesLocalCursorWithoutWaitingForHostPosition`：旧实现先在 `!surface.cursor().pixmap().isNull()` 断言失败；修复后 Windows / Ubuntu 均通过（各 3 项含初始化 / 清理）。覆盖无新远端位置 / 视频帧时使用本地系统光标、形状缓存 / 更新、缩放热点、点击释放坐标、黑边、离开后的迟到回传、远端隐藏、清空画面与停止恢复。测试不显示窗口、不调用真实鼠标移动 / 系统按键注入，不冒充 OpenGL 呈现或真实跨机手感验收。现有远端位置绘制用例明确将指针置于画面外，匹配新的显示语义；本次未运行该需要显示窗口的 OpenGL 用例。

Windows Release 构建部署通过，单元测试 58 项通过；Ubuntu 22.04 / GCC 11.4 / Qt 6.8.3 在隔离目录构建通过，单元测试 41 项通过。未重复完整信令 / 媒体回归，也未自动重启两端现有客户端。当前 Linux 运行中的启动器临时目录仍为修复前版本，需用户再次运行启动器加载修改。日志：Windows `build/mouse-latency-red.txt`、`build/mouse-latency-green.txt`、`build/mouse-latency-core.txt`、`build/mouse-latency-build.log`；Linux 隔离目录下同名 green / core / build 日志。

## 本地系统光标集中复核

复核本轮 VideoSurface diff、MainWindow 信号连接和 SessionCoordinator 光标校验 / 清理调用链，发现并用失败用例复现以下问题：

- 停止 / 清空画面后，新主机可能复用相同 serial 与尺寸，导致沿用旧光标图像。按源图像 cacheKey 与热点变化使本地缓存失效。
- 协议允许宽度为 1 的屏幕，直接按显示比例放大会分配巨大位图。保持比例并将本地光标尺寸限制在 128×128 内，热点使用同一缩放比例。
- 视频宽高比变化时，静止指针进入黑边仍显示远端形状。在帧尺寸变化时重新检查指针所在区域。
- 缩放比例变化后，图像取整尺寸可能相同而热点取整位置不同。缓存判定同步比较缩放后的热点。

新增三个隐藏窗口用例覆盖会话 / 画面重建、重复 serial、更换热点、极小屏幕、同位图尺寸下热点取整变化及静止指针跨黑边；同时回归原本地光标和移动合并 / 恢复用例。Windows / Ubuntu 的这五个用例均通过（各 7 项含初始化 / 清理），Windows Release 构建部署与 Linux 隔离目录构建通过。没有显示测试窗口、移动系统鼠标、发送原生输入或重启现有客户端；未运行完整信令 / 媒体 / OpenGL GUI 套件，真实手感与不同 DPI 仍需验收。日志：Windows `build/mouse-review-red.txt`、`build/mouse-review-hotspot-red.txt`、`build/mouse-review-green.txt`、`build/mouse-review-build.log`；Linux 隔离目录 `build/mouse-review-green.txt`、`build/mouse-review-build.log`。

## Windows 光标转换故障定位与修复

用户重启后仍反馈 Linux 画面中的指针本身移动慢。核对运行中的 Linux 启动器快照及二进制时间，确认包含最新本地光标代码，不能归因为旧版。后台只读探针调用真实 Windows GetCursorInfo / GetIconInfo：当前普通箭头可见、句柄及位图有效，但 Qt 6.8.3 `QImage::fromHICON` 返回空图像；系统手形同样为空，文本光标丢失透明区域，等待光标正常。空图像使 SessionCoordinator 无法发送形状，VideoSurface 继续隐藏本地指针，采集光标回退保留了视频延迟。

新增系统箭头 / 手形 / 文本 / 等待光标回归，旧转换先得到 3 项失败。Windows 图像转换改用 DrawIconEx 绘制黑 / 白背景，从差值恢复透明度，保留颜色 / 热点，限制尺寸 128×128，及时释放临时 DIB / DC 和 GetIconInfo 位图。补充合成单色 AND/XOR 遮罩、半透明彩色资源验证，Windows 全部 64 项单元测试通过。后台探针重新读取当前箭头为 32×32、visible=true、PNG 319 字节，满足现有 2400 字节消息上限。

Windows Release 构建部署通过；Ubuntu 隔离目录构建通过。现有隔离跨机测试增加接收真实光标形状 / 热点 / 屏幕尺寸断言，Linux 实际收到 Windows 的 32×32 光标，Windows / Linux 各 3 项通过（含初始化 / 清理）。同次短测目标 1920×1080 / 60 fps，采样解码 56 fps、交付 55 fps、UDP 丢包 0，拒绝的测试事件往返 10238 µs；仅作为后台链路记录，不代表呈现帧率或手感验收。

未显示测试窗口、移动系统指针、点击程序或重启现有客户端；原生输入、实际 Linux 呈现与主观延迟仍待用户再次运行本工作树启动器验证。没有调整接收缓冲、传输参数或授权逻辑，没有重复完整信令 / 媒体 / OpenGL 套件。生成日志：`build/cursor-shape-red.txt`、`build/cursor-shape-core.txt`、`build/cursor-shape-build.log`、`build/cursor-shape-cross-windows.txt`、`build/cursor-shape-cross-linux.txt`；Linux 构建日志位于隔离目录 `build/cursor-shape-build.log`。

## Windows 光标转换集中复核

复核本轮转换 / 单元 / 跨机夹具 diff、私有 Windows 声明及 InputInjector → SessionCoordinator → VideoSurface 调用链，检查尺寸限制、透明度计算、线程归属和 DC / DIB / GetIconInfo 位图所有权。发现背景反色像素被直接转为纯白：真实系统文本光标有 26 个可见像素，全部为白色，在白底内容上难以看清。

扩展合成单色遮罩用例，先复现反色像素实际为 `#ffffffff`、期望黑色 `#ff000000` 的失败；最小修正为黑色主体加一像素白色描边，仅填充相邻透明区域，保持图像尺寸和热点。RGBA 不能还原动态背景反色，此为静态形状近似。修复后真实文本光标读到 26 个深色主体像素和 64 个浅色边缘像素；64 项单元回归与 Windows Release 构建部署通过。连续读取当前真实光标 1000 次前后 GDI 对象数均为 6，未观察到资源增长。修改后的像素转换、描边边界和清理路径再次复核，未发现其他明确逻辑问题。

补审仅改 Windows 图像像素处理，没有重复 Linux 构建、跨机媒体或 OpenGL 呈现；源文件 / 测试 / 文档同步至 Linux 隔离目录，原始脏检出未动。真实操作手感、缩放后的可见性和不同 DPI 仍待人工验收。日志：`build/cursor-shape-review-red.txt`、`build/cursor-shape-review-core.txt`、`build/cursor-shape-review-build.log`、`build/cursor-shape-review-probe.txt`。

## 尚未完成的验收

- 真实 Linux GUI 操作 Windows：点击、按住拖拽、滚轮、组合键、失焦释放和对端停止时的实际系统效果。
- Linux OpenGL 实际呈现、文字单像素细节、连续运动画面和长时间帧间隔 / 端到端操作延迟；当前解码 / 交付数值不代表实际呈现帧率。
- 不同 DPI / 多屏、UAC / 权限切换的真实设备验证；普通 SendInput 不能绕过 UIPI 或操作 UAC 安全桌面。
- 公网 / VPN / 丢包网络、其他 GPU / 显示刷新率及持续 1080p60；云端 CI 未运行。

手动验收时分别在两端启动本次构建，加入同一个协作空间房间；Linux 点击“请求控制对方电脑”，Windows 点击“允许控制”，Linux 点击共享画面后检查上述键鼠动作，再在任一端停止控制。Windows 从本工作树运行 `.\run.ps1 -Preset windows-release`；Linux 从上述隔离目录运行 `bash ./run.sh`，避免混用旧客户端。

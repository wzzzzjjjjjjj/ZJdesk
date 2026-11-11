# 发布包使用与构建

2026-11-06 源码与 Windows 部署目录已增加主界面顶部的对端 IP 验证，Linux 客户端自动提供共用服务；构建不要求输入 IP。当前既有 EXE / ZIP / AppImage 尚未重新打包，以下既有发布文件的使用及验证记录不代表包含该功能。新版交互见 [启动网络验证](startup-network.md)。

## 应用图标（2026-11-05）

`packaging/zjdesk.png` 是以用户提供的 `D:\edgedown\ZJ创意图标设计.png` 为参考生成的圆形透明图标；`packaging/zjdesk.ico` 为 16 / 24 / 32 / 48 / 64 / 128 / 256 像素格式转换。Qt 资源用于窗口及左上角品牌图标，Windows RC 与自解压编译器嵌入 ICO，AppImage 的桌面入口及 `.DirIcon` 使用 PNG。

圆形版本的 Windows 与 Ubuntu 22.04 Release 构建及原生桌面 `workspaceFitsSmallWindowAndLongNames` 各 3 passed / 0 failed（含初始化 / 清理）；截图确认界面图标外侧透明。Windows 客户端和自解压包提取出的图标与 ICO 的 32 像素图一致且四角透明，Linux AppDir 的 PNG 与资源哈希一致。新版 ZIP 客户端 `--version` 退出 0、标准错误为空；AppImage 的 `--version` 和媒体插件检查通过。发布包哈希已核对。

清除开发 SDK 路径后，Windows ZIP 内客户端 `--version` 退出 0、标准错误为空；AppImage 在真实 X11 会话的 `--version`、`--check-media` 通过。本次 Windows 自解压 EXE 在临时目录提交阶段报 `System.IO.IOException`（目录访问被拒），工作区继承了 `Everyone / DeleteSubdirectoriesAndFiles` 拒绝权限；未修改 ACL 或启动器，不能记为默认缓存自解压启动通过。可使用已验证的 ZIP 解压启动。日志及图标提取结果位于 `build/packaging-validation/icon-*`；本轮未重跑实际跨机远控或音视频。

方形版本的集中补审读取了 Qt 初始化与窗口创建、RC 资源依赖、两端打包脚本及 AppRun / desktop 入口，未发现明确逻辑问题；圆形版本仅更新图片资源并重新构建。尚未进行干净系统验收，前述 EXE 自解压权限限制仍保留。

## 使用

发布目录为 `dist/packages/`，版本取自根目录 CMakeLists.txt，目前为 0.4.5。

Windows 11 x64：双击 `ZJdesk-0.4.5-windows-x64.exe`。这是免安装自解压启动包，首次解压到 `%LOCALAPPDATA%\ZJdesk\packages\<包内容哈希>`，之后复用同一个目录；默认目录无写权限时自动改用 EXE 旁的 `ZJdesk-cache\<包内容哈希>`。不创建安装记录或自动更新。也可完整解压 `ZJdesk-0.4.5-windows-x64-portable.zip`，运行 `bin\zjdesk.exe`。Qt、GStreamer 和 MSVC 运行库随包提供，不需要开发 SDK。删除 EXE 不会自动删除已解压的缓存；关闭客户端后可删除上述缓存目录。需要选择其他缓存位置时，可设置 `ZJDESK_PACKAGE_CACHE` 为有写权限的目录；显式指定的目录不可写时会报错，不自动改用其他位置。

Ubuntu 22.04 x64 X11：

```bash
chmod +x ZJdesk-0.4.5-linux-x86_64.AppImage
./ZJdesk-0.4.5-linux-x86_64.AppImage
```

AppImage 携带 Qt 6.8.3、GStreamer 1.26.11、libnice 和依赖库，保留宿主 glibc、显示 / 音频服务、字体及显卡驱动。它仍需要 X11 桌面和可用的图形驱动；不代表 Wayland 或其他发行版已经验收。FUSE 不可用时可以使用 `./ZJdesk-0.4.5-linux-x86_64.AppImage --appimage-extract-and-run`。

## 局域网连接

客户端不自动启动服务。选一台电脑作为信令服务端，在该电脑上运行以下任一命令，并允许局域网访问所选端口。

Windows 使用 ZIP 解压目录内的服务：

```powershell
.\bin\zjdesk-server.exe --listen 0.0.0.0 --port 8765
```

Linux 使用同一个 AppImage：

```bash
./ZJdesk-0.4.5-linux-x86_64.AppImage --server-only --listen 0.0.0.0 --port 8765
```

两端客户端填写 `ws://服务电脑的局域网IP:8765` 和相同房间码。摄像头、麦克风权限和媒体动态 TCP / UDP 互通要求与开发版本相同，见 [README](../README.md)。

## 重新打包

Windows 使用已配置的 MSVC2022 / Qt / GStreamer 开发环境和系统 .NET Framework C# 编译器。`-QtLicenseRoot` 指向与 SDK 对应的 Qt 6.8.3 源码 `qtbase/LICENSES` 目录：

完成 Release 编译部署后，可直接双击项目根目录的 `package-windows.bat`。它调用 `package-windows.ps1 -SkipBuild`，使用已有的 `dist/windows-release` 部署生成 EXE、ZIP 并更新 `dist/packages/SHA256SUMS.txt`；不重新编译应用。已有编译结果尚未部署时，先运行 `build.ps1 -Preset windows-release -Deploy -SkipTests`。

```powershell
.\package-windows.ps1 -QtLicenseRoot 'Qt源码中的LICENSES目录'
```

脚本通过 `build.ps1 -Preset windows-release -Deploy -SkipTests` 构建部署，再生成 ZIP 和嵌入相同 ZIP 的 EXE；也可在已部署后传 `-SkipBuild`。Qt 许可文本和 GStreamer 随 SDK 的许可目录会一并打包，MSVC CRT 使用 app-local DLL。此入口不运行回归，发布前仍需按项目要求单独验证。

Linux 在 Ubuntu 22.04 构建机使用现有 Qt / GStreamer SDK、Python 3、`ldd`、`dpkg-query`、`mksquashfs` 和官方 [appimagetool](https://github.com/AppImage/appimagetool)：

```bash
export APPIMAGETOOL=/path/to/appimagetool-x86_64.AppImage
bash ./package-linux.sh
```

可选设置 `APPIMAGE_RUNTIME_FILE=/path/to/runtime-x86_64`，使用预先下载的官方 type2 runtime，避免 appimagetool 构建时联网下载。

脚本调用 `build.sh --preset linux-release --skip-tests`，也可传 `--skip-build` 使用已有构建。`QT_ROOT` / `GST_ROOT` 沿用 build.sh 默认值；许可文本从 SDK 的 share/licenses 和同级 src 源码目录采集。Python 收集 ELF 依赖闭包，并补齐 NSS 通过 dlopen 加载的加密模块及对应 .chk 文件；缺少依赖立即失败，glibc 与显卡驱动栈使用宿主版本。产物和暂存目录分别在 `dist/packages/`、`build/packaging-*`，不进入 Git。

### 一键生成 EXE 与 AppImage

双击项目根目录的 `打包可执行程序.bat`，或执行 `.\package-release.ps1`。脚本默认先构建并部署 windows-release（`-ReuseWindowsBuild` 可复用已有部署），生成自解压 EXE；再通过 SSH 把当前源码同步到 Linux 构建机（默认 `orbbec@10.11.42.4`，用 `-LinuxSsh` 覆盖，需要免密登录），在远端 `~/ZJdesk-release` 执行 `package-linux.sh` 生成 AppImage，再把产物拷回本机 `dist/packages`。两端都成功后才会清理并写入项目根目录的 `可执行程序` 文件夹；该文件夹每种产物只保留最新一份（带版本号文件名），打包失败时旧文件不受影响。

Windows 不能直接生成 AppImage：`packaging/bundle-linux.py` 依赖 `ldd`、`dpkg-query`、`mksquashfs` 和 appimagetool，所以这一步交给 Linux 构建机。appimagetool 与官方 type2 runtime 缓存在 `build/packaging-tools/`（缺失时从官方 GitHub 下载），每次打包上传到远端 `build/packaging-tools/`；远端 `build/` 不在同步范围内，可增量编译。`-SkipWindows` / `-SkipLinux` 只生成单侧，`-ReuseLinuxBuild` 使用远端已有构建。

## 本次打包验证（2026-11-05）

- 应用源码基于 `12338b2`；本次修改仅增加打包脚本、启动包装与使用文档，不修改会议 / 远控实现。Windows Release 构建部署成功，Linux 对当前源码快照重新配置构建成功。
- Windows 自解压 EXE 已启动包内客户端，进程模块确认 Qt、qwindows 和 GStreamer 均来自解压目录。当前执行环境对默认用户缓存目录写入受限，因此此项使用 `ZJDESK_PACKAGE_CACHE` 指向项目内独立目录；没有把该限制当作普通用户桌面的验证结果。版本入口加载后由测试终止进程，未操作其界面。
- AppImage 在真实 Ubuntu 22.04 X11 桌面环境下清除 SDK 路径变量后，`--version` 返回 `ZJdesk 0.4.5`，服务端入口及媒体插件检查通过；ELF 依赖扫描未发现缺失或引用开发 SDK 的库路径。
- Windows 与 Ubuntu 分别使用包内运行库进行 30 秒双进程合成摄像头测试，两端均报告 `passed:true`，退出后 `released:true`；探针仅用于验证，不进入发布包。Linux 初版遗漏 NSS 动态模块，曾导致 `Could not initialize SRTP encoder`；补齐模块后重建 AppImage，通过上述媒体复验。两种系统分别在本机连接，不作为本轮跨机实测结果。
- 详细运行日志与包哈希保存在 `build/packaging-validation/`、`build/package-windows-launch.json` 和发布目录的 SHA256SUMS.txt。打包不代替干净电脑、Ubuntu 24.04、真实音视频及长期运行验收。

### 打包集中补审

- 检查了两端打包入口、Windows 启动器、Linux AppRun 和依赖收集，以及已有构建 / 部署入口。发现 Windows 解压失败留下临时目录、无标准错误句柄时错误提示被隐藏、临时目录名称过长触发 .NET Framework 路径限制三处问题。启动器已改为失败时尽力清理自己的临时目录、区分无句柄与输出重定向，并使用较短的暂存目录名称；未修改系统路径策略。
- `tests/packaging/windows_launcher.ps1` 使用损坏的嵌入 ZIP 调用实际启动入口，仅将模态提示替换为文件标记，不操作界面。补丁前退出码 1、提示标记不存在、遗留一个临时目录；补丁后退出码 1、提示标记存在、无临时目录遗留。
- 更新后的真实 Windows EXE 在清除 SDK 路径变量后，使用较深的独立缓存目录连续执行两次 `--version`：首次解压与随后复用缓存均退出 0、标准错误为空，仅保留一个完整缓存目录。通过 `QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES=1` 禁用 Qt 版本提示窗口；缓存内最长文件路径为 246 字符，旧暂存命名方式在这一位置已复现路径长度失败。
- Windows EXE 已重新生成并更新 SHA256SUMS；ZIP 内容与 AppImage 未变。本次补审未重复媒体 / 远控实机或原生 OpenGL 验收，普通用户默认缓存路径仍需在不受当前执行环境限制的桌面补验。

### 默认缓存权限修复与 BAT 入口

- 用户实际启动也遇到默认目录拒绝访问，已在不设置 `ZJDESK_PACKAGE_CACHE` 时复现；此前仅测试指定缓存目录不足以覆盖此问题。现仅在默认目录发生 `UnauthorizedAccessException` 时改用 EXE 旁的 `ZJdesk-cache`，不调整目录 ACL 或系统设置。
- 更新后的 EXE 在相同环境下不设置缓存变量，首次启动和复用缓存均退出 0。回归同时覆盖默认目录拒绝访问后的回退，以及用户显式指定目录失败时不回退；模态提示继续使用标记拦截。启动器回归脚本使用 PowerShell 7。
- `package-windows.bat` 已通过 Windows 自带 PowerShell 实际打包；修复了脚本 UTF-8 编码兼容问题，校验值改用 .NET SHA256 计算并自动更新，避免依赖当前环境缺失的 `Get-FileHash` 命令。打包不运行媒体 / OpenGL 回归。

### 缓存回退与 BAT 集中复核

- 复核了 BAT、PowerShell 打包入口、启动器及故障回归，检查默认与显式缓存的错误处理、互斥锁释放、解压暂存提交和退出码传递，未发现新的明确逻辑问题。
- 启动器故障回归通过：损坏 ZIP 退出 1、错误提示可见且无暂存目录遗留；默认目录拒绝访问会回退，显式指定目录失败保持报错。BAT 指定不存在的许可证目录后退出 1，没有误报打包成功。
- 两个真实发布 EXE 在清除 SDK 路径变量、使用同一全新缓存时同时执行 `--version`，均退出 0、标准错误为空，最终仅一个完整缓存目录，验证并发首次解压的互斥与复用。已有不指定缓存变量的两次启动结果见上一节；本次复核未重复媒体、原生 OpenGL 或干净系统验收。

## 一键打包 EXE 与 AppImage（2026-11-11）

- 新增根目录 `package-release.ps1` 和 `打包可执行程序.bat`：一次运行生成 Windows 自解压 EXE 与 Linux AppImage，成功后替换 `可执行程序` 内的旧文件；每种产物只保留最新一份，任一环节失败都不改动该目录。AppImage 由 SSH 同步到 Linux 构建机生成，因为 `packaging/bundle-linux.py` 依赖 `ldd`、`dpkg-query`、`mksquashfs` 和 appimagetool。
- Windows 路径：`.\package-release.ps1 -ReuseWindowsBuild -SkipLinux` 退出 0，`可执行程序/ZJdesk-0.4.5-windows-x64.exe` 为 135,518,208 字节，SHA256 `2e82bccc22223582ec41c73fd6295570994042aaf53a834c5922e05b01f43973`；生成的 EXE 与旧文件同名不同哈希，确认已替换。`package-windows.ps1` 的 ZIP 经 `Compress-Archive` 重建，所以每次哈希不同。
- 完整路径：`.\package-release.ps1 -ReuseWindowsBuild` 退出 0。远端 `orbbec@10.11.42.4:/home/orbbec/ZJdesk-release` 完成全量 Release 构建（90 个目标，`Tests skipped.`），`bundle-linux.py` 收集 310 个文件、96 个系统库，AppImage 为 61,221,368 字节，SHA256 `c86b59dd802c95f61a9ff24ac04e993b285540464255ee56765dfa2028a3ad83`，与拷回本机 `dist/packages` 的副本一致。
- AppImage 在构建机真实 X11（`DISPLAY=:1`）下清除 SDK 变量执行 `--version`，输出 `ZJdesk 0.4.5`、退出 0；未跑界面、媒体或远控用例。
- `打包可执行程序.bat` 以互斥参数 `-SkipWindows -SkipLinux` 调用时退出 1 并透传错误信息，确认 `%*` 参数转发和失败退出码。
- 未验证：本次没有启动新生成的 EXE（当前执行环境以 `操作已被用户取消` 拒绝创建该进程，不计为通过）；未重跑整机回归、媒体、真实双机远控或干净系统验收。AppImage 复用构建机既有 Qt 6.8.3 / GStreamer 1.26.11 SDK，仍是 Ubuntu 22.04 X11 口径。

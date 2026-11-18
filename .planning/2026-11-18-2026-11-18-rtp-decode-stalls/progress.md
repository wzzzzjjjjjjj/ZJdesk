# Progress Log

## Session: 2026-11-18

### Current Status
- **Phase:** 5 - Delivery
- **Started:** 2026-11-18

### Actions Taken
-

### Test Results
| Test | Expected | Actual | Status |
|------|----------|--------|--------|

### Errors
| Error | Resolution |
|-------|------------|

- 先通过已知 SSH 端点核对 Linux 编译缓存的匹配源文件。直连信令收到 HTTP 200 响应，改用临时 SSH 转发信令，媒体仍经跨子网 UDP 直连。
- 两轮均通过隔离后台探针完成。首轮 7 个长解码空档、6 个长画面空档，最长 1040.41 ms。复跑 0 个长空档，最长 40.64 ms。首轮性能门槛仅最长画面间隔失败，复跑全面通过。
- 首轮 Linux probe result ok=true，包裹脚本执行完成后 PowerShell 管道尾部 CR 令 SSH 返回 1。第二轮用 Python 传送 LF 脚本并正常返回 0。复核两端临时信令端口和测试进程都已退出。
- Tailscale 初始化探路后 Running / NoState，停止请求 OpenService FAILED 5，当前权限无法恢复到初始 Stopped。
- 报告及可复算日志在 build/rtp-decode-stalls-20261118/。产品源码未修改。

追查继续。两次关键帧等待附近均存在 rtp_jitter_buffer_reset_skew，需核算 PTS+pipeline latency 和流式线程阻塞长度。GStreamer 1.26.11 Linux 可通过 Python GI 加载，用于隔离合成 RTP 复现。

完成 Phase 6。后台 GStreamer 1.26.11 合成重复输入对照，sync=true 关键帧解码后等待 419.13 ms，sync=false 0.09 ms。单帧漏队列消除送包阻塞但只交付 10/90 帧。已回收 Linux 实验临时目录，产物保留在 build/rtp-decode-stalls-20261118，新增 receiver-root-cause.md。

Phase 7 完成。A2 / B2 各 44 次 XTest 点击与原生 Windows frameSwapped 同序号匹配，两端正常退出。跨子网 UDP 1080p30，固定两个 400 ms 读回值，只切换诊断副本 appsink sync。稳定期 p50 448.736 / 68.085 ms。开启时恢复窗口 406.287 ms、同线程期间到达 18 个原始 RTP、下送 0 个。Linux 临时 Xvfb、端口与目录已回收。报告位于 build/rtp-decode-stalls-20261118/real-input/report.md。

Phase 8 完成。Windows 产品 Release 库使用直连接收模式，44/44 次 XTest 点击匹配，未中断期间中位 52.965 ms，p95 61.085 ms，1080p30 平均呈现 29.646 fps。400 ms 读回、sync=false，人工发送暂停 590.894 ms 时最长帧间隔 684.180 ms。确认正向后应用到 Linux 接收端。

Phase 9 双机对照完成。Windows 发送合成画面，Linux 隔离 X11 通过 Mesa llvmpipe 完成真实 OpenGL frameSwapped。两轮各 74/74 标记、80 秒有效窗口、30.00 fps，400 ms 读回。接收模式切换后 p50 472.235 → 65.899 ms，p95 487.634 → 100.044 ms，最长帧间隔 56.056 → 72.122 ms。

Windows Release 非 GUI 7/7（229.16 秒），Linux Release 6/6（199.43 秒），均成功。已保留原始结果并清理本轮 Linux 临时构建。当前文档 13 张 Mermaid 图渲染成功，20 条相对链接有效。报告位于 build/playout-fix-20261118/report.md。

现场分析开始，读取 F:\下载\日志 下两份 JSONL。核对会话边界、收发配置、输入与呈现指标后评估是否达标。

Phase 10 完成。解析 367 条 JSONL 记录，按两段会话各自匹配 63 / 24 个已导出的 Windows 控制回执，Linux 注入时间与接受状态逐项一致。核对跨网 400 ms / sync=false、同网 200 ms / sync=false，记录跨网掉到 24 fps、同网呈现空档和统计覆盖边界。

Phases 11–13 完成。核心回归验证 1080p30→24→30、缺样不退出和显示上限 24。新增 Windows 原生 OpenGL 用例验证停用隔离并保留活动空档。三档各 52 条送出、XTest 应用与交换反馈全部匹配，约 591 ms 暂停对应 664–684 ms 空档；保留默认 400 ms。首次隔离启动失败，沿用之前验证的原生桌面路径后恢复；早期 400 ms 缺正常结束记录已排除并补跑。最终 Windows Release 7/7（228.31 秒）、Linux Release 6/6（199.44 秒）、Windows 原生呈现 3/3。远程临时目录及临时进程已清理。报告、原始日志和重算脚本位于 build/wan-buffer-ab。

集中审查：接收端集中交付积压帧时可能短时高于发送档位。新增回归证明跨网 24 fps 档在收到 300 ms 积压和 48 fps 反馈后会误升到 30 fps。修复为排水临时上限不超过进入时的发送档位，排空后仍需满足原有稳定窗口。Windows Release 重建及 CoreTests 75/75 通过。重新解析三组双机原始日志，均为 52/52 匹配、全程 1080p30；这些测量早于本次边界修复。未在最终修补后重跑 Ubuntu 或双机。

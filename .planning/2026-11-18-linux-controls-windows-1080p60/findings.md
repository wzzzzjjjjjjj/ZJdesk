# 排查发现

- 两端同一千兆有线 LAN，Windows 10.11.42.19 发往 Linux 10.11.42.4，媒体为实际 ICE-UDP。
- Windows 显示 1536×864 × 1.25，物理 1920×1080，约 144 Hz。D3D11 采集后经 VP9 Profile 1 软件编码。
- Xvfb 报 llvmpipe，仅用于探路。原生 X11 使用 Mesa Intel Graphics RPL-U。
- 原生 X11，30 档 27.39 fps，60 档 23.64 fps。60 档初期 55 fps 编码、33.90 fps 收端处理及 479 ms 积压，随后自适应降档。降档前接收端解码产出至 appsink p95 约 32 ms，解码本身 p95 约 6 ms，优先检查颜色转换。
RGB 四线程 39.68 fps，解码到 appsink p95 约 18 ms。独立的剪贴板禁用 VA 硬解复测为 39.65 fps，解码 p95 约 0.5–0.6 ms，下游仍 22–24 ms。所有有效配置未达到稳定 60 fps。

# Findings & Decisions

## Requirements
- 对比远控链路、传输与连接、编解码、键鼠/光标、弱网、缓冲/延迟、安全，形成新 MD。
- 仅文档研究，无二进制逆向、抓包、安装、真实双机横测。

## Research Findings
- ToDesk https://www.todesk.com/news/420.html（2023-07-06）：公开 RTC/RTP、delay/loss-based 拥塞策略、Jitter Buffer/HARQ；RTC 名称不能证明使用 WebRTC/libwebrtc。
- ToDesk https://www.todesk.com/faq/80.html：网络诊断、帧率与模式、完整/精简版协议差异；性能不能不分版本泛化。
- 向日葵 https://service.oray.com/question/16055.html：HSKRC、P2P优先与服务器中转。营销文章中其他产品对比不作为事实。
- 搜索结果含 uuyc163.com/.net/.cc、todesk.st/.me 等近似域名，不作为厂商官方证据。UU 需从 163.com 核实。
- ZJdesk 当前 HEAD 5bcd349：screen_share.cpp:243 跨网默认400ms、253 sync=true；VP9软件主路、Linux可选硬编；share_quality.cpp:90 排空250/120ms、帧率下限24。
- ZJdesk input VideoSurface 8ms合并，InputReceiver 授权代次/序号/线程代次校验，Windows SendInput/Linux XTest。

## Technical Decisions
| Decision | Rationale |
|----------|-----------|

## Issues Encountered
| Issue | Resolution |
|-------|------------|

## Resources
-

- UU 官方 uuyc.163.com 实际 HTTP 可以读取，搜索工具失败不代表网站不可用；后续只引用此官方域名。ToDesk 410 编解码文章公开GPU加速、桌面内容优化、灵活参考帧，未据此推断具体编码标准。向日葵16055说明办公模式调用RDP，其他模式不可混同。
- UU官方游戏页说明300多个节点机房、自动画质依据设备和网络、沉浸式鼠标/手柄；不能据此认定某会话经中继或使用特定传输协议。
- UU官方真彩帮助以Intel H265/AMD H264举兼容例，能证明相关编码路径，不能外推AV1或所有会话默认H265；真彩4:4:4有GPU与平台限制。
- UU远程协助有验证码、手动允许、双重验证三种方式；不同平台/版本支持不同。商业产品也不要求每次弹允许窗口。
- ToDesk 420公开动态Jitter Buffer与ARQ+FEC、Reed-Solomon；9–20ms是厂商特定条件宣称，非本次实测，更不能照抄成通用配置。
- ToDesk 413公开节点探测/动态选路、SD-WAN、SRv6及DPDK转发；ZJdesk的LAN/Tailscale候选选择属于不同规模层次。

- 当前ZJdesk screen_share.cpp:1177-1180确实配置do-nack=true、ULP_RED FEC20%，331/337注册调用；只能证明配置，生效需协商/运行计数，不能写成没有FEC。
- screen_share.cpp:550创建单一zjdesk-control且options=null；696与715分别控制/文件发送共用channel_。GStreamer 1.26.11 gstwebrtcbin.c:7439-7449建立通道时默认ordered=true/lifetime=-1/retransmits=-1；基类属性默认值不同，应看工厂调用。
- file_clipboard.cpp:15每块16KiB；177/208按请求拉取下一块，已有应用层节流，不能描述成无限制灌入。共用有序通道仍可能排队，须测buffered-amount与输入回执。
- 最新ToDesk性能页公开鼠标加速独立通道/本地GPU解码/固定帧率等产品功能，但8000Hz/3ms/8K360均非本次实测，不用于排名。
- 官方材料有技术解释不严谨：RTP不是UDP替代层、TCP非逐包停等、UDP有校验、4:4:4不保证全链路无损；报告采用准确概念，不复述这些误导。

- 最终报告：docs/远控软件与ZJdesk技术策略对比分析.md。总体闭环相似不等于框架协议相同；向日葵必须按模式区分；ToDesk RTC不证明WebRTC；UU未披露协议保持未知；当前FEC/通道/排空风险按源码和待验证推论分开。

# Findings & Decisions

- 修改目标 docs/wan-1080p30-interactive-latency-plan-20261114.md；保留既有其他变更。
- Linux X11 ximagesrc → videoconvert → videorate → videoscale/caps → tee；预览和编码分别有单帧 leaky 原始帧队列。WAN 软件 vp9enc/I420/profile0，rtpvp9pay → webrtcbin；接收 depay → vp9dec → RGB → appsink。
- VideoSurface 使用 QPainter 在 QOpenGLWidget 内绘制，frameSwapped 为软件呈现边界；本地 OS 光标无需等待视频。InputReceiver 在独立线程再次检查授权代次和序号，probe_ack 不是画面完成。
- 初次批量输出截断，已改为范围读取；搜索不存在的 input_sender.cpp 失败，使用 rg --files 确认实际结构，输入发送由 SessionCoordinator 实现。
- 接收视频显式链路是 rtpvp9depay → video/x-vp9 caps → vp9dec → videoconvert → RGB appsink，没有 vp9parse。frameSwapped 后还有物理显示；QImage 包装映射缓冲不等于全链路 GPU 零拷贝。
- controlEpoch 在 grantControl 批准时建立；通道就绪后 sendControlGrant 授权 InputReceiver 并发送 grant。撤销使 generation/epoch 无效并排队 releaseAll，普通输入不要求 UI 线程逐条转发。
- share stats 经 ScreenShare.negotiationReady → SessionCoordinator → 信令反馈；TWCC/GCC 是另一反馈路径。普通 WAN latency=400、sync=true，其他默认 200、sync=false，可覆盖；暂未改变任何配置。
- 图表和说明位于 docs/wan-1080p30-interactive-latency-plan-20261114.md 第 330 行开始；验证报告为 build/docs-flowcharts-qa/rendered/report.json。文档仅说明结构，不增加实机验证结论。
- 集中审查修复了辅助验证脚本的源文件写回副作用；标签格式已经在文档中完成，不应在后续校验时再次编辑文档。修复后校验与渲染只写 build 目录，SHA256 前后对照证明本次验证不改源文档。
- 用户纠正最终文档位置为 docs/项目结构与核心技术.md；此前关于“位于延迟方案第 8 节”的记录为历史状态，现已撤销。最终位置为 docs/项目结构与核心技术.md:465 的第 9 节。目标原有 4 张 Mermaid 图，新补 9 张，全部 13 张均通过验证。

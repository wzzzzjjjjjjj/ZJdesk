# Task Plan: 跨子网 RTP 到达与 VP9 解码停顿

## Goal
在隔离桌面定位 RTP 继续到达时的解码停顿，先验证 Windows 控制 Linux 的产品修复，再改善 Linux 控制 Windows。

## Next Step
三项实现与验证已完成。默认缓冲沿用 400 ms，150 ms 纳入后续公网长测。

## Current Phase
Phase 13

## Phases

### Phase 1: Requirements & Discovery
- [x] Understand user intent
- [x] Identify constraints
- [x] Document in findings.md
- **Status:** complete

### Phase 2: Planning & Structure
- [x] 固定跨网 1080p30 与 400 ms，使用既有后台探针
- [x] 核对收包、解包、解码与呈现事件可区分
- **Status:** complete

### Phase 3: Implementation
- [x] 双端运行后台探针
- [x] 构建逐事件离线统计并留存原始日志
- **Status:** complete

### Phase 4: Testing & Verification
- [x] 核对原生后台呈现和逐事件空档
- [x] 对照量化两轮结果并记录边界
- **Status:** complete

### Phase 5: Delivery
- [x] 复核报告和验证产物
- [x] 交付结论与日志位置
- **Status:** complete

### Phase 6: 接收端因果定位
- [x] 从原始 RTP 日志定位时间戳重校准
- [x] 计算恢复帧播放截止时间与推送解阻的吻合度
- [x] 做只针对接收链路的合成对照并记录结论
- **Status:** complete

### Phase 7: Windows 控制 Linux 的真实视觉反馈
- [x] 明确现有合成测量的覆盖范围
- [x] 隔离桌面回放真实输入，对比相同缓冲时同步开关
- [x] 核对结果与适用范围
- **Status:** complete

### Phase 8: 修复 Windows 控制 Linux
- [x] 直连接收端启用解码后立即交付，保留抖动缓冲
- [x] 使用实际构建复测点击与发送中断恢复
- **Status:** complete

### Phase 9: 改善 Linux 控制 Windows
- [x] 验证反向接收瓶颈，实施对应修复
- [x] 完成相关回归与双端验证
- **Status:** complete

### Phase 10: 分析双端现场事件日志
- [x] 按会话匹配双端记录与输入序号
- [x] 比较接收策略、按键回执、媒体指标及验证边界
- **Status:** complete

### Phase 11: 跨网帧率降档诊断
- [x] 记录变档触发条件和决策指标
- [x] 验证降档及恢复的数据可判读
- **Status:** complete

### Phase 12: 区分窗口停用与前台呈现卡顿
- [x] 标记显示停用及恢复并隔离间隔统计
- [x] 验证前台真实长间隔仍保留
- **Status:** complete

### Phase 13: 真实输入的抖动缓冲对照
- [x] 保持即时交付，对照 400 / 200 / 150 ms
- [x] 匹配点击、拖动、键入和呈现反馈并比较恢复
- **Status:** complete

### Phase 14: 集中审查与复核
- [x] 审查排水、呈现状态、调用链和探针记录
- [x] 复现并修复高接收帧率误触发跨网升档，复核 Windows 单测和既有双机数据
- **Status:** complete

## Decisions Made
| Decision | Rationale |
|----------|-----------|

## Errors Encountered
| Error | Resolution |
|-------|------------|

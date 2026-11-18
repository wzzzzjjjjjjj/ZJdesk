# Findings & Decisions

## Requirements
-

## Research Findings
-

## Technical Decisions
| Decision | Rationale |
|----------|-----------|

## Issues Encountered
| Issue | Resolution |
|-------|------------|

## Resources
-

当前树 ZJ-desk；用户已有两个未跟踪文档，保留。FileClipboard 使用 QFile 非 Text 模式、16384 字节块、Base64；SessionCoordinator 分别路由两个方向，无格式过滤。已有通用二进制回归但缺扩展名矩阵和边界尺寸覆盖。测试无需 GUI，不碰真实剪贴板。


集中审查完成：逐项审阅本轮三个文件 diff、FileClipboard 实现 / 声明、SessionCoordinator 正反向路由与停止授权清理、ScreenShare 文件消息分发 / 32 KiB 上限和现有媒体回归。确认新增测试两个方向使用不同载荷，包含 NUL / 高位字节；空文件及整块 EOF 正确；等待 done 处理完再切换角色，带接收者上下文的队列连接在析构时解绑，临时文件关闭后清理。测试为 QCoreApplication，不访问真实剪贴板。未发现本轮新增的明确逻辑问题，不做额外代码修改。再次解析最终 JUnit：62 项、42 个新增数据行、零失败 / 错误；核对完整回归日志 7/7 和 diff --check。未重跑无需重复的构建与测试；双机、原生粘贴、公网、大文件长测仍未执行。

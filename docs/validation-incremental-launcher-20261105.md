# 构建启动增量编译验证（2026-11-05）

入口为 `构建启动.bat` → `start-both.ps1 -ForceRestart`。Windows 继续复用原 Release 构建；Linux 将每次新建随机快照改为按 Windows 机器名、工作树绝对路径的 SHA-256 前 16 位复用独立缓存。Linux 原项目源码不参与同步，缓存源文件由 Windows 快照管理。

`sync-source.sh` 校验目标属于 `build/start-both-cache.<16位十六进制>` 且不是符号链接，先解压至临时目录，再使用 `rsync --recursive --checksum --delete`。不传 `--times`，避免 ZIP 解压时间变化导致所有对象重新编译。保留缓存的 `build/`、`dist/`、`.git/`；删除快照中已移除的源码。源文件校验和、构建锁、失败标记和两端成功后才更新启动记录的流程保留。需要 Linux 已安装 rsync。

验证结果：

- `tests/scripts/test_start_both.ps1`：24 项模拟场景通过，覆盖固定缓存路径、同步失败不启动且保留失败标记、直接启动使用当前缓存与拒绝其他缓存记录，以及原有重启 / 锁 / 地址 / 端口检查。测试拦截系统启动与进程操作。
- `tests/scripts/test_incremental_sync.sh`：未变文件时间戳保留、新增 / 修改 / 删除生效、编译缓存与部署产物保留、拒绝原始目录和符号链接目标，通过。
- `tests/scripts/test_build_skip_tests.sh`：默认回归与显式跳过测试行为通过。将测试替身改为导出的 Bash 函数，避免本机工具链 PATH 覆盖替身而误调用真实 CMake。
- Windows 实际 Release 无改动构建：`ninja: no work to do.`。Linux 新缓存首次 Release 构建部署通过；重新解压 / 同步相同快照后的第二次构建：`ninja: no work to do.`。
- 仅在 Linux 生成的缓存副本修改 `src/diagnostics/event_log.cpp`，Ninja 只编译 `CMakeFiles/zjdesk_client_core.dir/src/diagnostics/event_log.cpp.o`，其余为必要的链接；已恢复原快照并重新构建部署。Windows 原始源码未修改。

日志：本地 `build/incremental-launcher-tests.log`、`build/incremental-windows.log`；Linux 缓存 `build/incremental-{first-build,second-build,one-file,restored}.log`。生成物均不入库。没有执行真正会关闭客户端并打开桌面的启动器，没有重启现有客户端或执行原生输入；未重跑音视频与真实双机远控验收。

## 集中复核

实际复核启动器 / 同步器 diff 和全文、源码快照 / 校验、两端构建与 Linux 启动脚本、CMake 预设、批处理入口及脚本回归。最终 `build.ps1` / `build.sh` 相对本轮开始没有改动；未扩展到之前的远控业务改动。

未发现新的明确逻辑问题，无额外产品修复。检查确认同步前限制生成目录并拒绝目标符号链接、解压失败不会执行同步、删除排除构建与部署目录、临时目录通过 EXIT 清理；本地启动锁涵盖同步和构建、缓存按机器 / 工作树隔离、失败保留标记阻止直接启动、两端成功并通过快照校验后才发布启动记录。复核已记录的 24 项回归、快照 LF 检查和实际 Ninja 无变更 / 单文件变更日志；未重复执行这些未变化的用例。真正关闭并重启现有双端客户端的完整批处理流程仍未实际运行。

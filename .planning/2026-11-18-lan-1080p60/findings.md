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
两机当前有线同网 10.11.42.19/24 和 10.11.42.4/24。现有自动档取刷新率，局域网未给远控专设 60 帧上限。复用隔离桌面探针做 30/60 对照。
Linux 已下载解包 Xvfb，未安装到系统。对照两档只改远控自动上限；原生后台 Windows OpenGL 接收与 Xvfb/XTest 实输入。

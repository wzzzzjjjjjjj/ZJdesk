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
Baseline: vp9dec -> videoconvert RGB -> mapped QImage without deep copy -> QPainter GL. Prior RGB 4 threads improved 23.64 to 39.68 fps; VA and RGBA did not improve further. SSH LAN baseline reachable. Legacy validation checklist lacks skill phase markers; preserved it.
Four same-host runs (42 s): RGB/1 = 26.70 fps; RGB/4 = 44.81 fps; queue + RGB/4 = 49.08 fps; queue + BGRx/4 = 57.99 fps. Best p99 31.84 ms, no adaptive rung change. Scope to little-endian Linux immediate LAN.

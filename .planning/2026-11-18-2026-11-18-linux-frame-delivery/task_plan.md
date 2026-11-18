# Task Plan: Linux screen receiver conversion and copies

## Goal
Reduce receiver conversion and copy overhead while preserving color, latency, and playback behavior.

## Next Step
Complete; future work can count decoder, conversion queue and sink output separately.

## Current Phase
Phase 5

## Phases

### Phase 1: Requirements & Discovery
- [x] Understand user intent
- [x] Identify constraints
- [x] Document in findings.md
- **Status:** complete

### Phase 2: Planning & Structure
- [x] Define approach
- [x] Prepare same-host native fixtures
- **Status:** complete

### Phase 3: Implementation
- [x] Compare conversion and handoff variants
- [x] Implement selected receiver path and media test
- **Status:** complete

### Phase 4: Testing & Verification
- [x] Verify requirements met
- [x] Document test results
- **Status:** complete

### Phase 5: Delivery
- [x] Review outputs
- [x] Deliver to user
- **Status:** complete

## Decisions Made
| Decision | Rationale |
|----------|-----------|
| Single leaky queue after VP9 decode | Separate conversion scheduling without dropping encoded dependencies. |
| BGRx and Qt RGB32 on little-endian Linux | Reduce painting cost and preserve the existing read-only mapped buffer handoff. |
| Restrict to immediate LAN, normal bandwidth | Preserve scheduled playback and bandwidth fallback paths. |

## Errors Encountered
| Error | Resolution |
|-------|------------|


# Progress Log

## Session: 2026-11-18

### Current Status
- **Phase:** 5 - Delivery
- **Started:** 2026-11-18

### Actions Taken
- Compared RGB/1, RGB/4, queued RGB/4 and queued BGRx/4 on native Mesa X11.
- Implemented queued BGRx/4 for immediate LAN on little-endian Linux and documented the new path.
- Added a LAN marker regression that waits for receiver shutdown before rereading a retained image.

### Test Results
| Test | Expected | Actual | Status |
|------|----------|--------|--------|
| Native 42-second comparison | Isolate scheduling, conversion worker count, and pixel layout | 26.70 / 44.81 / 49.08 / 57.99 presented fps | Pass |
| Ubuntu Release | All selected tests pass | 6/6 | Pass |
| Windows Release | All selected tests pass after final media test edit | 7/7 | Pass |
| Native 72-second baseline and first product | Measure actual swaps without concurrent build | 25.98 / 55.61 presented fps, p99 55.17 / 37.32 ms | Pass |
| Native 72-second product under concurrent Windows build | Record stress result separately | 50.77 presented fps, 710.42 ms maximum gap; sender reduced target to 50 fps | Recorded |
| Native 72-second product after Windows regression | Check steady frame rate without build overlap | 53.81 presented fps, p99 27.35 ms, max 96.79 ms; sender remained at the 60 fps target | Pass |

### Errors
| Error | Resolution |
|-------|------------|
| Build script defaulted to Windows Debug and hit `Set-Acl` permission errors | Explicitly use the required Windows Release preset. |

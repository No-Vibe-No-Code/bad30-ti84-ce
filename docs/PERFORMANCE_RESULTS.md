# Build and installation check — 2026-09-11

- CEdev v15, ARM macOS; speed optimization `-O2`.
- Output: `bin/BAD30.8xp`, 14200 bytes.
- Program payload SHA-256:
  `64255201c6c3008f2fc39d4960f07fbf4c945647ef28f9d2e9364905cbf9efd1`.
- USB transfer to the connected TI-84 Plus CE succeeded with archived storage.
- Received BAD30 back from the calculator and compared its program payload:
  byte-for-byte identical to the build. Transfer-container archive attributes
  are intentionally excluded from this comparison.
- Existing BA30MTA and 659 BA30 data segments were present and retained.
- Previous program backup: `work/BAD30-before.8xp` (ignored local artifact).
- Host validation: 56 incremental ZX7 round trips across four work budgets,
  truncation/declared-size failures, 2000 malformed streams with output guards,
  and exact slowest-1% heap comparison at several sample counts up to 9999.
- Build passes. CEdev's linker emits executable-stack warnings for eZ80
  assembly objects; there are no compiler warnings from the player.

## Still awaiting hardware measurements

No measured 29 FPS 1% low or zero-stutter claim has been established. Launch
BAD30 through prgmA, run at least 60 seconds (then a complete run), and press
Mode. Record 1% low FPS, worst interval, sample count, skipped frames,
starvation count, and visual behavior. Test pause/resume and emergency exit.
USB readback proves installation, not frame pacing or successful execution.

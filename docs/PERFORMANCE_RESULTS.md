# Build and installation check — 2026-09-21

- CEdev v15, ARM macOS; speed optimization `-O2`.
- Output: `bin/BAD30.8xp`, rebuilt with the 29 FPS player timeline.
- Program payload SHA-256:
  `65d3cca09748d5dcddc019db3639e4f1d06fcdbf9f4d96a1dcce1356e944f5ca`.
- The previous USB transfer used the 30 FPS build and is retained only as a
  rollback reference. The 29 FPS package must regenerate metadata and frame
  segments before a physical upload; its FPS byte is validated at startup.
- Previous program backup: `work/BAD30-before.8xp` (ignored local artifact).
- Host validation: 56 incremental ZX7 round trips across four work budgets,
  truncation/declared-size failures, 2000 malformed streams with output guards,
  exact slowest-1% heap comparison at several sample counts up to 9999,
  and actual renderer checks across alternating buffers, including borders
  and changing black/white/random frames.
- Build passes. CEdev's linker emits executable-stack warnings for eZ80
  assembly objects; there are no compiler warnings from the player.

- CEmu execution test with the 29 FPS metadata path ran for 10 seconds,
  decoded 29 segments, produced 220 displayed swaps, and reported zero CRC
  or BAD30 error-screen events. The emulator run is a startup/data-integrity
  check, not proof of the physical calculator's 1% low.

## Still awaiting hardware measurements

No measured 29 FPS 1% low or zero-stutter claim has been established. Launch
BAD30 through prgmA, run at least 60 seconds (then a complete run), and press
Mode. Record 1% low FPS, worst interval, sample count, skipped frames,
starvation count, and visual behavior. Test pause/resume and emergency exit.
USB readback proves installation, not frame pacing or successful execution.

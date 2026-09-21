# 29 FPS 1% low target

Acceptance: over a complete physical-calculator playback, the mean of the
slowest 1% of presentation intervals must be at most 1000/29 = 34.483 ms.
Also report the maximum interval and dropped source frames: a percentile
alone does not establish absence of isolated stutters. Exclude deliberate
pauses and initial loading. Do not claim success from a host benchmark.

Implementation:
1. Clear both display buffers once; overwrite every pixel of the video area
   each frame. Expand each row once, then copy two complete rows.
2. Keep two decoded segments. Prepare the next segment in small, bounded
   ZX7 output batches while waiting for presentation; update its CRC during
   decoding. Check all input, output and back-reference bounds. Regenerate the
   AppVars at 29 FPS so metadata and playback timing agree.
3. Preload before starting the clock. Calculate deadlines once per frame,
   retain pause-aware absolute pacing, and reserve a deadline guard for
   background work. Late frames remain skippable; buffer starvation must be
   observable rather than disguised by slowing the video clock.
4. Record presentation intervals, dropped frames and starvation. Report the
   slowest-1%-mean rate and worst interval on exit; ignore pause intervals.
5. Validate incremental decoding against the existing encoder using varied
   streams, chunk sizes, malformed inputs and short final segments. Build
   with CEdev v15 and install BAD30 only, retaining existing video/rollback.
6. Measure on the connected calculator. If RAM-based row copies or sliced
   decoding still exceed the budget, use measured timings to guide eZ80 or
   display-mode changes. No unmeasured guarantee of zero stutter.

# BAD30 architecture

## Data pipeline

The host encoder converts the selected video into a deterministic stream:

1. `ffmpeg` normalizes the source to constant-rate 29 FPS.
2. The image is scaled and letterboxed into exactly 96×64 pixels.
3. Pixels below grayscale value 128 become black; all others become white.
4. Each row is packed bit-first into 12 bytes, for 768 bytes per frame.
5. Every ten-frame segment stores a complete first bitmap followed by XOR
   deltas against the preceding frame.
6. Each segment is independently compressed with ZX7 and checked by decoding
   it again on the host.

Ten-frame segments are intentional. They reduce the longest blocking read and
decompression operation on the calculator, which matters more for perceived
smoothness than maximizing compression across a longer segment.

## AppVar format

The program is `BAD30`. The metadata AppVar is `BA30MTA`; data AppVars are
`BA30D000`, `BA30D001`, and so on. Names are limited to the TI calculator's
eight-character AppVar name limit.

The metadata header is little-endian:

```text
4 bytes   magic: BA30
1 byte    format version: 1
2 bytes   frame width: 96
2 bytes   frame height: 64
1 byte    scale: 3
1 byte    frame rate: 29
4 bytes   total frame count
2 bytes   frames per segment: 10
2 bytes   segment count
2 bytes   metadata CRC16-CCITT
```

Each segment record contains:

```text
2 bytes   frame count in this segment
2 bytes   compressed byte count
2 bytes   decompressed byte count
2 bytes   decompressed CRC16-CCITT
```

The final segment may contain fewer than ten frames. No segment is split inside
a frame, and the encoder refuses a compressed segment that exceeds the AppVar
payload limit.

## Calculator runtime

At startup the program validates the magic, version, dimensions, scale, FPS,
segment count, record sizes, and metadata CRC. Before displaying a segment it
checks that the AppVar exists, has the declared size, decompresses it, and
checks its CRC. Any failure displays an error and exits through the normal
cleanup path; partially decoded data is never presented.

The renderer uses GraphX's 8-bit buffer with index 0 black and index 255
white. It clears both buffers once at initialization, then overwrites every
pixel in the 288x192 video rectangle. Each source row expands through a lookup
table once; two whole-row copies produce the remaining scaled rows. Borders
remain white because playback never writes to them.

## Real-time scheduler and segment preparation

Two 7680-byte buffers hold the current and next segments. Both are validated
before the timeline starts. After each segment transition, the released slot
receives the following segment. The read-only AppVar pointer remains valid
until decoding finishes; no VAT-changing operations run while it is live.

The resumable ZX7 reader checks input bounds, output bounds and back-reference
distances and updates the CRC as it produces output. Each background call emits
at most 32 bytes. Calls run while waiting for a presentation deadline, with a
two-millisecond guard. Token parsing is bounded, but the guard and batch size
still require physical performance testing. Opening an AppVar remains a
synchronous operation, and insufficient spare CPU time can still cause stalls.

Deadlines use the absolute 29 FPS timeline. They are computed once per frame
and shifted when resuming from pause. Obsolete source frames are reconstructed
but skipped before rendering. Once rendered, a frame is presented even if late.
The first presentation starts the timeline; the final frame is held for its
full period. Deadline arithmetic uses wrap-safe signed differences.

## Performance measurements

After each swap, `gfx_Wait` completes before the clock sample. The player
records software-observed presentation intervals, excludes intervals spanning
pauses, and keeps the largest 100 intervals in a 400-byte min-heap. This suffices
for the worst 1% of at most 9999 intervals allowed by metadata. On completion
or Mode, it reports the inverse mean of the slowest ceil(N/100) intervals,
worst interval, sample count, skipped frames, late renders and buffer starvation.
The calculation runs after playback, not during rendering. Clear and ON remain
immediate exits without statistics. Press a fresh key to dismiss the report.

The target is a measured 1% low of at least 29 FPS, with no isolated stalls;
see [the performance plan](PERFORMANCE_PLAN.md). No physical result is implied
by a successful host test or build. LCD scanout and software sampling are not
identical, so visual acceptance remains necessary.

## Controls and cleanup

- `2nd` toggles pause/resume.
- `Mode` opens the timing report; a fresh key quits to TI-OS.
- `Clear` quits to TI-OS.
- `ON` quits to TI-OS.

While paused, the timeline is shifted by the pause duration so resuming does
not immediately discard a large block of frames. On exit, BAD30 disables the
continuous keyboard mode, clears the ON latch, resets the keypad, ends GraphX,
and returns cleanly to TI-OS.

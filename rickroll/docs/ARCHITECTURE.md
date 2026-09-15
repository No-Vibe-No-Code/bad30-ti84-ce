# RICKROLL architecture

## Data pipeline

The host encoder converts the selected video into a deterministic stream:

1. `ffmpeg` normalizes the source to constant-rate 30 FPS.
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

The program is `RICKROLL`. The metadata AppVar is `RR30MTA`; data AppVars are
`RR30D000`, `RR30D001`, and so on. Names are limited to the TI calculator's
eight-character AppVar name limit.

The metadata header is little-endian:

```text
4 bytes   magic: RR30
1 byte    format version: 1
2 bytes   frame width: 96
2 bytes   frame height: 64
1 byte    scale: 3
1 byte    frame rate: 30
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

The renderer uses GraphX's 8-bit buffer and an explicit palette:

- palette index 0 is black;
- palette index 255 is white.

For each complete frame it waits for the safe draw-buffer boundary, clears the
full 320×240 buffer to white, expands source bytes through a precomputed
256-entry lookup table, writes three LCD rows per source row, and swaps only
after the frame is complete. Clearing the full buffer prevents stale black
pixels from surviving into later frames.

## Real-time scheduler

The target frame is calculated from elapsed monotonic clock ticks:

```text
target = floor((now - timeline_start) × 30 / CLOCKS_PER_SEC)
```

When a segment or delta is already behind that target, the runtime reconstructs
the delta state but does not render the obsolete frame. Once a frame is chosen
for rendering, it is always presented after rendering finishes, even if that
render crossed the next deadline. This rule prevents a slow render path from
discarding every frame and leaving the cleared white buffer visible.

The result prioritizes continuity of real time over displaying every source
frame. It cannot hide a long blocking storage operation completely; a future
prefetch ring buffer could reduce that remaining stall.

## Controls and cleanup

- `2nd` toggles pause/resume.
- `Mode` quits to TI-OS.
- `Clear` quits to TI-OS.
- `ON` quits to TI-OS.

While paused, the timeline is shifted by the pause duration so resuming does
not immediately discard a large block of frames. On exit, RICKROLL disables the
continuous keyboard mode, clears the ON latch, resets the keypad, ends GraphX,
and returns cleanly to TI-OS.

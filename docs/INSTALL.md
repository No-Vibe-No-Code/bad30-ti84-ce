# BAD30 installation checklist

This project targets the TI-84 Plus CE. Do not send the monochrome `.8xk`
version of Bad Apple to this calculator.

## Build package

1. Install or mount CEdev v15 and make its `bin/` directory available in
   `PATH`.
2. Install Python, `ffmpeg`, `ffprobe`, and `yt-dlp` on the Mac.
3. Download the source video to a build-only location. Do not commit the video.
4. Run `tools/encode_bad30.py` with the source path, an output directory, and
   the CEdev `convbin` path.
5. Confirm that `manifest.json` reports the expected frame count, 30 FPS,
   96×64 dimensions, and a successful encoder exit.
6. Run `make` and confirm that `bin/BAD30.8xp` is produced.

The encoder validates every segment's XOR reconstruction and ZX7 round trip.
The calculator validates the metadata and segment CRCs again at runtime.

## Transfer package

Transfer these files through a local USB command-line path while the
calculator is at TI-OS HOME:

```text
bin/BAD30.8xp
dist/BA30MTA.8xv
dist/BA30D000.8xv through the final BA30D###.8xv
```

Install the official CE libraries and the arTIfiCE launcher separately when
the calculator's operating system requires them. Keep the older BADAPP2 files
until the new package passes testing. Do not automatically delete the old
installation to make room; first measure the exact flash-space requirement.

After transfer, disconnect and reconnect the calculator and confirm that the
expected AppVars are present. A transfer acknowledgement alone is not proof
that playback is usable.

## Physical acceptance test

1. Launch `BAD30` through `prgmA` and select the `BAD30` program.
2. Confirm that the first frame appears and that the screen is not left white.
3. Watch at least 60 seconds and note whether playback remains real-time.
4. Check a segment transition and the final segment.
5. Press `2nd` and confirm pause/resume leaves the current frame visible.
6. Press `Mode` or `Clear` and confirm return to TI-OS.
7. Repeat with the `ON` key if safe to do so.
8. Run a complete playback only after the short test succeeds.

The real-time mode may skip frames when the calculator falls behind. That is
expected. Long freezes, a blank screen, missing rows, stale black pixels, CRC
errors, or failure to return to TI-OS are not expected and should be reported
with the exact on-screen symptom.

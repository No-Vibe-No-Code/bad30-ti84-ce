# RICKROLL — smooth Rickroll playback for the TI-84 Plus CE

RICKROLL is a CE-native, visual-only Rickroll player for the TI-84 Plus CE. It
uses a host-side encoder and independently validated AppVar segments, then
renders complete frames through GraphX double buffering.

The player is designed to prioritize smooth real-time playback. It follows a
30 FPS wall-clock timeline and skips frames that are already late instead of
allowing the video to freeze while it catches up. A frame is never presented
until it has been fully decoded and rendered.

## Features

- Native TI-84 Plus CE program (`RICKROLL.8xp`), not a monochrome `.8xk` file.
- 96×64 black-and-white source frames expanded to a centered 288×192 image.
- Constant-rate 30 FPS normalization from the official music-video source.
- Independent 10-frame ZX7-compressed segments to limit blocking storage reads.
- Complete-frame plus XOR-delta encoding for compact storage.
- Metadata and per-segment CRC16 integrity checks.
- Explicit black/white palette setup and full back-buffer clearing.
- `2nd` pause/resume, `Mode` quit, `Clear` emergency quit, and `ON` emergency quit.
- No audio and no network connection on the calculator.

## Repository layout

```text
src/main.c             Calculator player and runtime validation
src/zx7_fixed.s        Standard ZX7 decompressor used by RICKROLL
tools/encode_rickroll.py  Host-side frame normalizer and AppVar encoder
makefile               CEdev build definition
icon.png               Program icon
docs/                  Build, packaging, and runtime documentation
```

Generated calculator files and the downloaded source video are intentionally
not committed. The encoder can reproduce the data package locally.

## Requirements

- TI-84 Plus CE with a compatible CE operating system.
- CEdev v15, including `convbin` and the CE C toolchain.
- Python 3.10 or newer.
- `ffmpeg` and `ffprobe` on the build host.
- `yt-dlp` if downloading the source video from YouTube.
- A local USB transfer tool for sending `.8xp` and `.8xv` files.

The source used for the current package is Rick Astley's official
[Never Gonna Give You Up music video on
YouTube](https://www.youtube.com/watch?v=dQw4w9WgXcQ). Only normalized
black-and-white frames are installed on the calculator.

## Build from source

Set `CEDEV_ROOT` to the mounted CEdev directory. Then download the source to a
build-only location and generate the AppVars:

```sh
export CEDEV_ROOT=/path/to/CEdev
mkdir -p dist

yt-dlp --no-playlist \
  -f 'bv*[height<=720][fps>=30]/bv*' \
  --remux-video mp4 \
  -o rickroll-source.mp4 \
  'https://www.youtube.com/watch?v=dQw4w9WgXcQ'

python3 tools/encode_rickroll.py \
  --source rickroll-source.mp4 \
  --out dist \
  --convbin "$CEDEV_ROOT/bin/convbin"
```

The encoder uses `ffmpeg` to create 96×64 grayscale frames, applies a fixed
black/white threshold, packs each frame into 768 bytes, creates 10-frame XOR
segments, compresses each segment with ZX7, and validates every round trip.
It also writes `dist/manifest.json` containing the frame count and normalized
stream hash.

Build the calculator program with CEdev on the host:

```sh
PATH="$CEDEV_ROOT/bin:$PATH" make
```

The program is written to `bin/RICKROLL.8xp`. Use `make clean` to remove only the
repository's `bin/` and `obj/` directories.

## Install and run

Install `bin/RICKROLL.8xp`, `dist/RR30MTA.8xv`, and every generated
`dist/RR30D###.8xv` using a local TI USB transfer path. The calculator must be
at TI-OS HOME before starting a transfer. Keep the required CE libraries and
launcher installed separately when the operating system needs them.

After transfer:

1. Run `prgmA` if using the arTIfiCE launcher.
2. Select `RICKROLL`.
3. Test a short section before starting a complete playback.

The detailed package format and runtime behavior are documented in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md). The complete installation
checklist is in [`docs/INSTALL.md`](docs/INSTALL.md).

## Playback behavior and limitations

RICKROLL keeps the playback phase tied to the monotonic CE clock. If decoding or
rendering falls behind, it advances to the current timeline frame and drops
older frames. This reduces apparent freezing but can produce an occasional
visual jump. The player does not claim that every physical calculator can
sustain 30 displayed frames per second; the device's actual behavior remains
the acceptance test.

The Rickroll video itself is build input and is intentionally not committed.

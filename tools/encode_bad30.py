#!/usr/bin/env python3
"""Encode the selected visual source into the BAD30 CE AppVar package.

The calculator receives a complete first bitmap and XOR deltas for each
following frame in a fixed 10-frame segment.  Each segment is independently
ZX7-compressed and independently checked before its AppVar is written.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


WIDTH = 96
HEIGHT = 64
SCALE = 3
FPS = 30
FRAME_BYTES = WIDTH * HEIGHT // 8
FRAMES_PER_SEGMENT = 10
MAX_SEGMENT_RAW = FRAME_BYTES * FRAMES_PER_SEGMENT
MAX_APPVAR_PAYLOAD = 65535
META_HEADER_SIZE = 21
SEGMENT_RECORD_SIZE = 8
SOURCE_URL = "https://www.youtube.com/watch?v=ThHvx5a9IYA"


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


class ZX7DecodeError(ValueError):
    pass


class ZX7Reader:
    """Reader matching CEdev's turbo ZX7 bit-buffer behavior."""

    def __init__(self, data: bytes):
        self.data = data
        self.position = 0
        self.a = 0x80

    def byte(self) -> int:
        if self.position >= len(self.data):
            raise ZX7DecodeError("ZX7 stream ended while reading a data byte")
        value = self.data[self.position]
        self.position += 1
        return value

    def control_bit(self) -> int:
        old_a = self.a
        self.a = (old_a << 1) & 0xFF
        carry = 1 if old_a & 0x80 else 0
        if self.a:
            return carry

        if self.position >= len(self.data):
            raise ZX7DecodeError("ZX7 stream ended while reading control bits")
        value = self.data[self.position]
        self.position += 1
        self.a = ((value << 1) & 0xFF) | carry
        return 1 if value & 0x80 else 0


def zx7_decode(data: bytes, expected_size: int) -> bytes:
    """Decode a CEdev ZX7 stream, including its end marker."""

    reader = ZX7Reader(data)
    output = bytearray([reader.byte()])

    while True:
        if reader.control_bit() == 0:
            output.append(reader.byte())
            continue

        zero_bits = 0
        while reader.control_bit() == 0:
            zero_bits += 1

        # A 16-zero Elias prefix is the ZX7 end marker.  The Z80 routine
        # reads the overflow bits from its padded input area; no output is
        # produced, so the host-side validator can stop at the marker.
        if zero_bits >= 16:
            break

        length_value = 1
        for _ in range(zero_bits):
            length_value = (length_value << 1) | reader.control_bit()
        length = length_value + 1

        offset_byte = reader.byte()
        offset = offset_byte & 0x7F
        if offset_byte & 0x80:
            high_bits = 0
            for _ in range(4):
                high_bits = (high_bits << 1) | reader.control_bit()
            # The standard decoder uses a 0x10 marker in D, rotates the
            # four bits through it, increments, then shifts right.  This
            # preserves the fourth bit in E as its inverted bit 7.
            high_byte = (high_bits + 1) >> 1
            low_byte = (offset_byte & 0x7F) | (0x80 if not (high_bits & 1) else 0)
            offset = (high_byte << 8) | low_byte

        distance = offset + 1
        if distance > len(output):
            raise ZX7DecodeError("ZX7 back-reference points before the output")
        for _ in range(length):
            output.append(output[-distance])

        if len(output) > expected_size:
            raise ZX7DecodeError("ZX7 stream expands beyond its declared size")

    if len(output) != expected_size:
        raise ZX7DecodeError(f"ZX7 expanded to {len(output)} bytes, expected {expected_size}")
    return bytes(output)


def pack_frame(gray: bytes) -> bytes:
    if len(gray) != WIDTH * HEIGHT:
        raise ValueError(f"expected {WIDTH * HEIGHT} grayscale pixels, got {len(gray)}")

    packed = bytearray(FRAME_BYTES)
    for index, value in enumerate(gray):
        if value < 128:  # fixed threshold: 0 is black, 255 is white
            packed[index // 8] |= 0x80 >> (index % 8)
    return bytes(packed)


def read_frames(source: Path) -> list[bytes]:
    filter_graph = (
        "fps=30,"
        "scale=96:64:force_original_aspect_ratio=decrease:flags=lanczos,"
        "pad=96:64:(ow-iw)/2:(oh-ih)/2:color=white,"
        "format=gray"
    )
    process = subprocess.Popen(
        [
            "ffmpeg", "-hide_banner", "-loglevel", "error", "-i", str(source),
            "-vf", filter_graph, "-f", "rawvideo", "-pix_fmt", "gray", "pipe:1",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    assert process.stdout is not None
    assert process.stderr is not None

    frames: list[bytes] = []
    pending = bytearray()
    frame_raw_size = WIDTH * HEIGHT
    while True:
        chunk = process.stdout.read(64 * 1024)
        if not chunk:
            break
        pending.extend(chunk)
        while len(pending) >= frame_raw_size:
            raw = bytes(pending[:frame_raw_size])
            del pending[:frame_raw_size]
            frames.append(pack_frame(raw))

    stderr = process.stderr.read().decode("utf-8", errors="replace")
    return_code = process.wait()
    if return_code != 0:
        raise RuntimeError(f"ffmpeg normalization failed: {stderr.strip()}")
    if pending:
        raise RuntimeError(f"normalizer emitted a partial frame ({len(pending)} bytes)")
    if not frames:
        raise RuntimeError("normalizer emitted no frames")
    return frames


def compress_with_convbin(convbin: Path, raw: bytes, name: str, output: Path) -> bytes:
    with tempfile.TemporaryDirectory(prefix="bad30-") as temp_dir:
        raw_path = Path(temp_dir) / "segment.bin"
        compressed_path = Path(temp_dir) / "segment.zx7"
        raw_path.write_bytes(raw)
        subprocess.run(
            [str(convbin), "-j", "bin", "-i", str(raw_path), "-c", "zx7", "-k", "bin", "-o", str(compressed_path)],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        compressed = compressed_path.read_bytes()
        if not compressed:
            raise RuntimeError(f"convbin produced an empty ZX7 stream for {name}")

        subprocess.run(
            [str(convbin), "-j", "bin", "-i", str(compressed_path), "-k", "8xv", "-n", name, "-r", "-o", str(output)],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        return compressed


def write_appvar(convbin: Path, payload: bytes, name: str, output: Path) -> None:
    if len(payload) > MAX_APPVAR_PAYLOAD:
        raise ValueError(f"{name} is {len(payload)} bytes, above the TI AppVar limit")
    with tempfile.TemporaryDirectory(prefix="bad30-meta-") as temp_dir:
        raw_path = Path(temp_dir) / "payload.bin"
        raw_path.write_bytes(payload)
        subprocess.run(
            [str(convbin), "-j", "bin", "-i", str(raw_path), "-k", "8xv", "-n", name, "-r", "-o", str(output)],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )


def write_metadata(records: list[tuple[int, int, int, int]], total_frames: int) -> bytes:
    segment_count = len(records)
    header = bytearray(b"BA30")
    header.extend(struct.pack("<BHHBBIHH", 1, WIDTH, HEIGHT, SCALE, FPS, total_frames, FRAMES_PER_SEGMENT, segment_count))
    header.extend(b"\x00\x00")
    body = bytearray()
    for frames, compressed_size, raw_size, crc in records:
        body.extend(struct.pack("<HHHH", frames, compressed_size, raw_size, crc))
    metadata = header + body
    metadata[19:21] = struct.pack("<H", crc16_ccitt(metadata))
    return bytes(metadata)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="downloaded local video file")
    parser.add_argument("--out", type=Path, required=True, help="output package directory")
    parser.add_argument("--convbin", type=Path, required=True, help="CEdev convbin executable")
    parser.add_argument("--source-url", default=SOURCE_URL)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if not args.source.is_file():
        raise SystemExit(f"source does not exist: {args.source}")
    if not args.convbin.is_file():
        raise SystemExit(f"convbin does not exist: {args.convbin}")

    args.out.mkdir(parents=True, exist_ok=True)
    for old_file in args.out.glob("BA30*.8x*"):
        old_file.unlink()
    manifest_path = args.out / "manifest.json"

    frames = read_frames(args.source)
    normalized_sha = hashlib.sha256(b"".join(frames)).hexdigest()
    records: list[tuple[int, int, int, int]] = []
    compressed_total = 0

    with tempfile.TemporaryDirectory(prefix="bad30-segments-") as temp_dir:
        temp_path = Path(temp_dir)
        previous: bytes | None = None
        for segment_index, first in enumerate(range(0, len(frames), FRAMES_PER_SEGMENT)):
            segment_frames = frames[first:first + FRAMES_PER_SEGMENT]
            encoded = bytearray()
            for frame_index, frame in enumerate(segment_frames):
                delta = frame if frame_index == 0 else bytes(a ^ b for a, b in zip(frame, previous or b""))
                encoded.extend(delta)
                previous = frame

            raw = bytes(encoded)
            expected_raw_size = len(segment_frames) * FRAME_BYTES
            if len(raw) != expected_raw_size or len(raw) > MAX_SEGMENT_RAW:
                raise RuntimeError(f"segment {segment_index} does not contain whole frames")

            name = f"BA30D{segment_index:03d}"
            if len(name) > 8:
                raise RuntimeError(f"AppVar name is too long: {name}")
            output = args.out / f"{name}.8xv"
            compressed = compress_with_convbin(args.convbin, raw, name, output)
            if len(compressed) > MAX_APPVAR_PAYLOAD:
                raise RuntimeError(f"{name} is too large ({len(compressed)} bytes); refusing to split a frame")

            reconstructed = bytearray()
            last = bytearray(FRAME_BYTES)
            for frame_index in range(len(segment_frames)):
                delta = raw[frame_index * FRAME_BYTES:(frame_index + 1) * FRAME_BYTES]
                if frame_index == 0:
                    last[:] = delta
                else:
                    for byte_index, value in enumerate(delta):
                        last[byte_index] ^= value
                reconstructed.extend(last)
            if bytes(reconstructed) != b"".join(segment_frames):
                raise RuntimeError(f"segment {segment_index} failed XOR reconstruction")
            if zx7_decode(compressed, len(raw)) != raw:
                raise RuntimeError(f"segment {segment_index} failed ZX7 round-trip validation")

            records.append((len(segment_frames), len(compressed), len(raw), crc16_ccitt(raw)))
            compressed_total += len(compressed)

    metadata = write_metadata(records, len(frames))
    write_appvar(args.convbin, metadata, "BA30MTA", args.out / "BA30MTA.8xv")

    manifest = {
        "format": "BAD30",
        "source_url": args.source_url,
        "source_file": str(args.source.resolve()),
        "frame_count": len(frames),
        "frame_rate": FPS,
        "width": WIDTH,
        "height": HEIGHT,
        "scale": SCALE,
        "frame_bytes": FRAME_BYTES,
        "frames_per_segment": FRAMES_PER_SEGMENT,
        "segment_count": len(records),
        "normalized_frame_stream_sha256": normalized_sha,
        "compressed_payload_bytes": compressed_total,
        "metadata_crc16": f"{read_u16(metadata, 19):04x}",
        "segment_records": [
            {"frames": f, "compressed_bytes": c, "raw_bytes": r, "crc16": f"{crc:04x}"}
            for f, c, r, crc in records
        ],
    }
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"frames": len(frames), "segments": len(records), "sha256": normalized_sha, "compressed_bytes": compressed_total}, indent=2))


def read_u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


if __name__ == "__main__":
    main()

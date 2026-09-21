#!/usr/bin/env python3
"""Host checks for the actual resumable decoder and timing heap (requires clang/convbin)."""
import ctypes
import importlib.util
import random
import re
import subprocess
import tempfile
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('encoder', ROOT / 'tools/encode_bad30.py')
encoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(encoder)
assert encoder.FPS == 29, f"encoder FPS drifted to {encoder.FPS}; expected 29"
main = (ROOT / 'src/main.c').read_text()
table = re.search(r'const uint16_t crc16_table\[256\] = \{.*?\};', main, re.S).group()
heap = main[main.index('static void record_interval'):main.index('static void show_statistics')]
wrapper = '''#include <stdint.h>
#include <stdlib.h>
#include "zx7_stream.h"
typedef uint32_t clock_t;
static clock_t intervals[100];
static uint16_t interval_count;
static uint8_t heap_size;
'''+table+'\n'+heap+'''
int decode(const uint8_t *in, uint16_t n, uint8_t *out, uint16_t size, uint16_t budget) {
    zx7_stream s; zx7_begin(&s,in,n,out,size);
    unsigned steps=0;
    while (!s.done && !s.failed && ++steps < 20000) zx7_step(&s,budget,crc16_table);
    return s.done && !s.failed ? s.crc : -1;
}
uint64_t tail_sum(const uint32_t *values, uint16_t n) {
    interval_count=0; heap_size=0;
    for (uint16_t i=0;i<n;++i) record_interval(values[i]);
    qsort(intervals,heap_size,sizeof(*intervals),compare_intervals);
    uint64_t sum=0;
    for (unsigned i=heap_size-(n+99)/100;i<heap_size;++i) sum+=intervals[i];
    return sum;
}
'''
# Compile the actual LUT builder and renderer against a host framebuffer.
renderer = main[main.index('static void build_expand_lut'):main.index('static bool load_metadata')]
wrapper += """
#define FRAME_WIDTH 96
#define FRAME_HEIGHT 64
#define SCALE 3
#define GFX_LCD_WIDTH 320
#define SCREEN_X 16
#define SCREEN_Y 24
#define PALETTE_BLACK 0
#define PALETTE_WHITE 255
static uint8_t expand_lut[256][24];
static uint8_t *gfx_vbuffer;
static void gfx_Wait(void) {}
""" + renderer + """
void draw(uint8_t *buffer, const uint8_t *frame) {
    gfx_vbuffer=buffer; build_expand_lut(); render_frame(frame);
}
"""
convbin = Path(sys.argv[1]).resolve()
rng=random.Random(30)
with tempfile.TemporaryDirectory() as d:
    d=Path(d); (d/'wrapper.c').write_text(wrapper)
    subprocess.run(['cc','-shared','-fPIC','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'src'),str(d/'wrapper.c'),'-o',str(d/'runtime.so')],check=True)
    lib=ctypes.CDLL(str(d/'runtime.so'))
    lib.decode.argtypes=[ctypes.c_void_p,ctypes.c_uint16,ctypes.c_void_p,ctypes.c_uint16,ctypes.c_uint16]
    lib.decode.restype=ctypes.c_int
    lib.tail_sum.argtypes=[ctypes.c_void_p,ctypes.c_uint16];lib.tail_sum.restype=ctypes.c_uint64
    lib.draw.argtypes=[ctypes.c_void_p,ctypes.c_void_p]
    # Reuse both buffers across changing frames to catch stale pixels/borders.
    buffers=[ctypes.create_string_buffer(bytes([255])*(320*240)) for _ in range(2)]
    for frame_index in range(8):
        frame=bytes([0 if frame_index%3==0 else 255])*768 if frame_index%3!=2 else bytes(rng.randrange(256) for _ in range(768))
        buffer=buffers[frame_index%2]
        lib.draw(buffer,frame)
        expected=bytearray([255])*(320*240)
        for y in range(64):
            row=bytes(c for x in range(96) for c in [0 if frame[y*12+x//8] & (128>>(x%8)) else 255]*3)
            for dy in range(3):
                start=(24+y*3+dy)*320+16
                expected[start:start+288]=row
        assert buffer.raw[:320*240]==expected
    cases=[]
    for n in [768,1536,7680]:
        cases += [bytes(n),bytes([255])*n,bytes(rng.randrange(2) for _ in range(n)),bytes(i%251 for i in range(n))]
    # Force distant references, changes at segment boundaries, and short final segments.
    seed=bytes(rng.randrange(256) for _ in range(1500))
    cases += [(seed*6)[:7680], b'ABCD'*1920]
    checks=0
    for raw in cases:
        (d/'raw').write_bytes(raw)
        subprocess.run([str(convbin),'-j','bin','-i',str(d/'raw'),'-c','zx7','-k','bin','-o',str(d/'packed')],check=True,capture_output=True)
        packed=(d/'packed').read_bytes()
        assert encoder.zx7_decode(packed,len(raw)) == raw
        for budget in [1,7,32,7680]:
            out=ctypes.create_string_buffer(len(raw)+16)
            ctypes.memset(out,0xA5,len(out))
            result=lib.decode(packed,len(packed),out,len(raw),budget)
            assert result == encoder.crc16_ccitt(raw),(len(raw),budget,result)
            assert out.raw[:len(raw)] == raw
            assert out.raw[len(raw):] == bytes([0xA5])*16
            checks+=1
        for cut in sorted({0,1,len(packed)//2,max(0,len(packed)-2)}):
            out=ctypes.create_string_buffer(len(raw)+16)
            assert lib.decode(packed[:cut],cut,out,len(raw),32) == -1,(len(raw),cut)
        if len(raw)>1:
            assert lib.decode(packed,len(packed),out,len(raw)-1,32)==-1
    for n in [1,99,100,101,6571,9999]:
        values=[rng.randrange(1,100000) for _ in range(n)]
        values_c=(ctypes.c_uint32*n)(*values)
        assert lib.tail_sum(values_c,n)==sum(sorted(values,reverse=True)[:(n+99)//100])
    # Malformed streams may decode by chance but must not overwrite guards or hang.
    for _ in range(2000):
        packed=bytes(rng.randrange(256) for _ in range(rng.randrange(0,200)))
        out=ctypes.create_string_buffer(784);ctypes.memset(out,0xA5,784)
        lib.decode(packed,len(packed),out,768,7)
        assert out.raw[768:]==bytes([0xA5])*16
    print(f'PASS: 29 FPS encoder setting, {checks} decoder round trips, truncation/size checks, 2000 malformed streams, exact 1% heap checks, alternating-buffer renderer checks')

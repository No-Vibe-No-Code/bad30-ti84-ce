#ifndef BAD30_ZX7_STREAM_H
#define BAD30_ZX7_STREAM_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* A bounded, resumable reader for the existing convbin ZX7 format. */
typedef struct {
    const uint8_t *input;
    uint8_t *output;
    uint16_t input_size, output_size, in, out, remaining, distance, crc;
    uint8_t bits;
    bool failed, done;
} zx7_stream;

static uint8_t zx7_byte(zx7_stream *s) {
    if (s->in >= s->input_size) { s->failed = true; return 0; }
    return s->input[s->in++];
}

static uint8_t zx7_bit(zx7_stream *s) {
    uint8_t carry = s->bits >> 7;
    s->bits <<= 1;
    if (!s->bits) {
        uint8_t value = zx7_byte(s);
        s->bits = (uint8_t)((value << 1) | carry);
        carry = value >> 7;
    }
    return carry;
}

static void zx7_begin(zx7_stream *s, const uint8_t *input, uint16_t input_size,
                      uint8_t *output, uint16_t output_size) {
    memset(s, 0, sizeof(*s));
    s->input = input; s->input_size = input_size;
    s->output = output; s->output_size = output_size;
    s->bits = 0x80; s->crc = 0xffff;
}

/* At most budget output bytes, plus a bounded (<=16-bit) token parse.
 * CRC is fused with output so there is no full-segment verification stall. */
static void zx7_step(zx7_stream *s, uint16_t budget, const uint16_t *crc_table) {
    while (budget-- && !s->done && !s->failed) {
        uint8_t value;
        if (!s->remaining) {
            if (s->out && zx7_bit(s)) {
                uint8_t zeros = 0;
                while (!zx7_bit(s) && !s->failed) {
                    if (++zeros == 16) {
                        s->done = true;
                        s->failed = s->out != s->output_size;
                        return;
                    }
                }
                uint16_t length = 1;
                for (uint8_t i = 0; i < zeros; ++i)
                    length = (uint16_t)((length << 1) | zx7_bit(s));
                /* length+1 must fit and stay within the declared output. */
                if (s->failed || length >= s->output_size - s->out) {
                    s->failed = true; return;
                }
                s->remaining = length + 1;
                uint8_t offset = zx7_byte(s);
                s->distance = offset & 127;
                if (offset & 128) {
                    uint8_t high = 0;
                    for (uint8_t i = 0; i < 4; ++i)
                        high = (uint8_t)((high << 1) | zx7_bit(s));
                    s->distance |= (uint16_t)((high + 1) >> 1) << 8;
                    if (!(high & 1)) s->distance |= 128;
                }
                ++s->distance;
                if (s->failed || s->distance > s->out) {
                    s->failed = true; return;
                }
            } else {
                s->distance = 0;
                s->remaining = 1;
            }
        }
        if (s->failed || s->out >= s->output_size) {
            s->failed = true; return;
        }
        value = s->distance ? s->output[s->out - s->distance] : zx7_byte(s);
        if (s->failed) return;
        s->output[s->out++] = value;
        s->crc = (uint16_t)((s->crc << 8) ^ crc_table[(s->crc >> 8) ^ value]);
        --s->remaining;
    }
}
#endif

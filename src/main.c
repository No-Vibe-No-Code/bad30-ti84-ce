#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <fileioc.h>
#include <graphx.h>
#include <keypadc.h>

#define META_NAME "BA30MTA"
#define DATA_PREFIX "BA30D"

#define FRAME_WIDTH 96
#define FRAME_HEIGHT 64
#define FRAME_BYTES (FRAME_WIDTH * FRAME_HEIGHT / 8)
#define SCALE 3
#define SCREEN_X ((GFX_LCD_WIDTH - FRAME_WIDTH * SCALE) / 2)
#define SCREEN_Y ((GFX_LCD_HEIGHT - FRAME_HEIGHT * SCALE) / 2)

#define VIDEO_FPS 30
#define FRAMES_PER_SEGMENT 10
#define MAX_SEGMENTS 1000
#define MAX_METADATA_BYTES 8192
#define MAX_SEGMENT_RAW_BYTES (FRAMES_PER_SEGMENT * FRAME_BYTES)
#define MAX_SEGMENT_COMPRESSED_BYTES 24576

#define PALETTE_BLACK 0
#define PALETTE_WHITE 255

/* The encoder stores one complete bitmap followed by XOR deltas. */
static uint8_t metadata_buffer[MAX_METADATA_BYTES];
static uint8_t compressed_buffer[MAX_SEGMENT_COMPRESSED_BYTES];
static uint8_t segment_buffer[MAX_SEGMENT_RAW_BYTES];
static uint8_t frame_buffer[FRAME_BYTES];
static uint8_t expand_lut[256][24];

/* Local CEdev-compatible decoder.  The stock turbo source in the mounted
 * toolchain does not reconstruct extended (>127-byte) offsets correctly on
 * this release; BAD30 uses the verified standard ZX7 core instead. */
extern void bad30_zx7_decompress(void *destination, const void *source);

typedef struct {
    uint32_t total_frames;
    uint16_t segment_count;
} bad30_metadata_t;

static bool graphics_started;
static bool keyboard_started;
static uint32_t late_frames;

static const uint16_t crc16_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

static uint16_t crc16_ccitt(const uint8_t *data, uint16_t length) {
    uint16_t crc = 0xFFFF;

    for (uint16_t i = 0; i < length; ++i) {
        crc = (uint16_t)((crc << 8) ^ crc16_table[(crc >> 8) ^ data[i]]);
    }

    return crc;
}

static bool any_pressed(void) {
    return kb_Data[1] != 0 || kb_Data[2] != 0 || kb_Data[3] != 0
        || kb_Data[4] != 0 || kb_Data[5] != 0 || kb_Data[6] != 0
        || kb_Data[7] != 0 || kb_On;
}

static void cleanup(void) {
    if (keyboard_started) {
        kb_DisableOnLatch();
        kb_ClearOnLatch();
        kb_Reset();
        keyboard_started = false;
    }

    if (graphics_started) {
        gfx_End();
        graphics_started = false;
    }
}

static void show_error(const char *line1, const char *line2) {
    if (!graphics_started) {
        gfx_Begin();
        graphics_started = true;
        gfx_SetDefaultPalette(gfx_8bpp);
        gfx_palette[PALETTE_BLACK] = gfx_RGBTo1555(0, 0, 0);
        gfx_palette[PALETTE_WHITE] = gfx_RGBTo1555(255, 255, 255);
        gfx_SetDrawBuffer();
    }

    gfx_Wait();
    memset((void *)gfx_vbuffer, PALETTE_WHITE, GFX_LCD_WIDTH * GFX_LCD_HEIGHT);
    gfx_SetTextFGColor(PALETTE_BLACK);
    gfx_PrintStringXY("BAD30 ERROR", 16, 88);
    gfx_PrintStringXY(line1, 16, 112);
    gfx_PrintStringXY(line2, 16, 128);
    gfx_SwapDraw();

    while (!any_pressed()) {
        kb_Scan();
    }

    cleanup();
}

static void initialize_graphics(void) {
    gfx_Begin();
    graphics_started = true;
    gfx_SetDefaultPalette(gfx_8bpp);
    gfx_palette[PALETTE_BLACK] = gfx_RGBTo1555(0, 0, 0);
    gfx_palette[PALETTE_WHITE] = gfx_RGBTo1555(255, 255, 255);
    gfx_SetDrawBuffer();
}

static void initialize_keyboard(void) {
    kb_DisableOnLatch();
    kb_ClearOnLatch();
    kb_SetMode(MODE_3_CONTINUOUS);
    keyboard_started = true;
}

static void build_expand_lut(void) {
    for (uint16_t value = 0; value < 256; ++value) {
        for (uint8_t bit = 0; bit < 8; ++bit) {
            uint8_t color = (value & (uint8_t)(0x80 >> bit))
                ? PALETTE_BLACK : PALETTE_WHITE;
            uint8_t *destination = &expand_lut[value][bit * 3];
            destination[0] = color;
            destination[1] = color;
            destination[2] = color;
        }
    }
}

static void render_frame(const uint8_t *frame) {
    uint8_t *buffer = (uint8_t *)gfx_vbuffer;

    /* gfx_Wait protects the buffer that was just presented. */
    gfx_Wait();
    memset(buffer, PALETTE_WHITE, GFX_LCD_WIDTH * GFX_LCD_HEIGHT);

    for (uint8_t source_y = 0; source_y < FRAME_HEIGHT; ++source_y) {
        uint8_t *row0 = buffer + (SCREEN_Y + source_y * SCALE) * GFX_LCD_WIDTH + SCREEN_X;
        uint8_t *row1 = row0 + GFX_LCD_WIDTH;
        uint8_t *row2 = row1 + GFX_LCD_WIDTH;
        const uint8_t *source = frame + source_y * (FRAME_WIDTH / 8);

        for (uint8_t source_byte = 0; source_byte < FRAME_WIDTH / 8; ++source_byte) {
            const uint8_t *expanded = expand_lut[source[source_byte]];
            uint8_t *destination = row0 + source_byte * 24;

            memcpy(destination, expanded, 24);
            memcpy(row1 + source_byte * 24, expanded, 24);
            memcpy(row2 + source_byte * 24, expanded, 24);
        }
    }
}

static bool load_metadata(bad30_metadata_t *result) {
    uint8_t handle = ti_Open(META_NAME, "r");
    if (!handle) {
        show_error("Metadata BA30MTA is missing.", "Install the complete BAD30 package.");
        return false;
    }

    uint16_t size = ti_GetSize(handle);
    if (size < 21 || size > sizeof(metadata_buffer)
        || ti_Read(metadata_buffer, 1, size, handle) != size) {
        ti_Close(handle);
        show_error("Metadata is truncated.", "BAD30 stopped before playback.");
        return false;
    }
    ti_Close(handle);

    if (memcmp(metadata_buffer, "BA30", 4) != 0
        || metadata_buffer[4] != 1
        || read_u16(&metadata_buffer[5]) != FRAME_WIDTH
        || read_u16(&metadata_buffer[7]) != FRAME_HEIGHT
        || metadata_buffer[9] != SCALE
        || metadata_buffer[10] != VIDEO_FPS
        || read_u16(&metadata_buffer[15]) != FRAMES_PER_SEGMENT) {
        show_error("Metadata format is not supported.", "BAD30 stopped before playback.");
        return false;
    }

    result->total_frames = read_u32(&metadata_buffer[11]);
    result->segment_count = read_u16(&metadata_buffer[17]);

    if (result->total_frames == 0
        || result->segment_count == 0
        || result->segment_count > MAX_SEGMENTS
        || result->segment_count != (result->total_frames + FRAMES_PER_SEGMENT - 1)
            / FRAMES_PER_SEGMENT
        || size != 21 + result->segment_count * 8) {
        show_error("Metadata integrity check failed.", "BAD30 stopped before playback.");
        return false;
    }

    uint16_t stored_metadata_crc = read_u16(&metadata_buffer[19]);
    metadata_buffer[19] = 0;
    metadata_buffer[20] = 0;
    uint16_t calculated_metadata_crc = crc16_ccitt(metadata_buffer, size);
    metadata_buffer[19] = (uint8_t)stored_metadata_crc;
    metadata_buffer[20] = (uint8_t)(stored_metadata_crc >> 8);
    if (calculated_metadata_crc != stored_metadata_crc) {
        show_error("Metadata integrity check failed.", "BAD30 stopped before playback.");
        return false;
    }

    for (uint16_t segment = 0; segment < result->segment_count; ++segment) {
        const uint8_t *record = &metadata_buffer[21 + segment * 8];
        uint16_t frames = read_u16(record);
        uint16_t compressed_size = read_u16(record + 2);
        uint16_t raw_size = read_u16(record + 4);

        if (frames == 0 || frames > FRAMES_PER_SEGMENT
            || compressed_size == 0 || compressed_size > MAX_SEGMENT_COMPRESSED_BYTES
            || raw_size != frames * FRAME_BYTES) {
            show_error("Metadata segment record is invalid.", "BAD30 stopped before playback.");
            return false;
        }

        if (segment + 1 < result->segment_count && frames != FRAMES_PER_SEGMENT) {
            show_error("A non-final segment is short.", "BAD30 stopped before playback.");
            return false;
        }
    }

    return true;
}

static void make_segment_name(char *name, uint16_t segment) {
    name[0] = DATA_PREFIX[0];
    name[1] = DATA_PREFIX[1];
    name[2] = DATA_PREFIX[2];
    name[3] = DATA_PREFIX[3];
    name[4] = 'D';
    name[5] = (char)('0' + (segment / 100) % 10);
    name[6] = (char)('0' + (segment / 10) % 10);
    name[7] = (char)('0' + segment % 10);
    name[8] = '\0';
}

static bool load_segment(uint16_t segment) {
    const uint8_t *record = &metadata_buffer[21 + segment * 8];
    uint16_t compressed_size = read_u16(record + 2);
    uint16_t raw_size = read_u16(record + 4);
    uint16_t expected_crc = read_u16(record + 6);
    char name[9];
    make_segment_name(name, segment);

    uint8_t handle = ti_Open(name, "r");
    if (!handle) {
        show_error("A BAD30 data segment is missing.", name);
        return false;
    }

    if (ti_GetSize(handle) != compressed_size
        || ti_Read(compressed_buffer, 1, compressed_size, handle) != compressed_size) {
        ti_Close(handle);
        show_error("A BAD30 data segment is truncated.", name);
        return false;
    }
    ti_Close(handle);

    bad30_zx7_decompress(segment_buffer, compressed_buffer);
    if (crc16_ccitt(segment_buffer, raw_size) != expected_crc) {
        show_error("A BAD30 data segment is corrupt.", name);
        return false;
    }

    return true;
}

static bool poll_controls(bool *paused, bool *previous_2nd,
                          clock_t *timeline_start, clock_t *pause_started) {
    kb_Scan();

    if (kb_On || (kb_Data[1] & kb_Mode) || (kb_Data[6] & kb_Clear)) {
        return false;
    }

    bool second_down = (kb_Data[1] & kb_2nd) != 0;
    if (second_down && !*previous_2nd) {
        clock_t now = clock();
        if (*paused) {
            *timeline_start += now - *pause_started;
            *paused = false;
        } else {
            *pause_started = now;
            *paused = true;
        }
    }
    *previous_2nd = second_down;
    return true;
}

static clock_t frame_deadline(clock_t timeline_start, uint32_t frame) {
    return timeline_start
        + (clock_t)(((uint64_t)frame * CLOCKS_PER_SEC) / VIDEO_FPS);
}

static uint32_t target_frame(clock_t timeline_start, uint32_t total_frames) {
    clock_t now = clock();
    uint64_t elapsed = now >= timeline_start
        ? (uint64_t)(now - timeline_start) : 0;
    uint64_t target = (elapsed * VIDEO_FPS) / CLOCKS_PER_SEC;

    if (target >= total_frames) {
        return total_frames - 1;
    }
    return (uint32_t)target;
}

static bool wait_until_frame(uint32_t frame, bool *paused, bool *previous_2nd,
                             clock_t *timeline_start, clock_t *pause_started) {
    while (true) {
        if (!poll_controls(paused, previous_2nd, timeline_start, pause_started)) {
            return false;
        }

        if (*paused) {
            continue;
        }

        if (clock() >= frame_deadline(*timeline_start, frame)) {
            return true;
        }
    }
}

#ifdef BAD30_DEBUG
static void show_debug_statistics(uint32_t displayed_frames) {
    char line[32];

    gfx_Wait();
    memset((void *)gfx_vbuffer, PALETTE_WHITE, GFX_LCD_WIDTH * GFX_LCD_HEIGHT);
    gfx_SetTextFGColor(PALETTE_BLACK);
    gfx_PrintStringXY("BAD30 DEBUG", 16, 88);
    sprintf(line, "Frames: %lu", (unsigned long)displayed_frames);
    gfx_PrintStringXY(line, 16, 112);
    sprintf(line, "Late: %lu", (unsigned long)late_frames);
    gfx_PrintStringXY(line, 16, 128);
    gfx_SwapDraw();

    while (!any_pressed()) {
        kb_Scan();
    }
}
#endif

int main(void) {
    bad30_metadata_t metadata;
    bool paused = false;
    bool previous_2nd = false;
    clock_t pause_started = 0;

    initialize_keyboard();
    initialize_graphics();
    build_expand_lut();

    if (!load_metadata(&metadata)) {
        return 1;
    }

    clock_t start = clock();
#ifdef BAD30_DEBUG
    uint32_t displayed_frames = 0;
#endif

    uint16_t segment = 0;
    while (segment < metadata.segment_count) {
        const uint8_t *record = &metadata_buffer[21 + segment * 8];
        uint16_t frames = read_u16(record);
        uint32_t segment_start = (uint32_t)segment * FRAMES_PER_SEGMENT;
        uint32_t current_target = target_frame(start, metadata.total_frames);

        /* If a segment load made us late by more than one segment, jump to
         * the segment containing the current wall-clock frame. */
        if (segment + 1 < metadata.segment_count
            && current_target >= segment_start + frames) {
            uint32_t target_segment = current_target / FRAMES_PER_SEGMENT;
            segment = target_segment >= metadata.segment_count
                ? (uint16_t)(metadata.segment_count - 1)
                : (uint16_t)target_segment;
            continue;
        }

        if (!load_segment(segment)) {
            cleanup();
            return 1;
        }

        current_target = target_frame(start, metadata.total_frames);
        if (segment + 1 < metadata.segment_count
            && current_target >= segment_start + frames) {
            uint32_t target_segment = current_target / FRAMES_PER_SEGMENT;
            segment = target_segment >= metadata.segment_count
                ? (uint16_t)(metadata.segment_count - 1)
                : (uint16_t)target_segment;
            continue;
        }

        for (uint16_t frame = 0; frame < frames; ++frame) {
            const uint8_t *encoded = segment_buffer + frame * FRAME_BYTES;
            if (frame == 0) {
                memcpy(frame_buffer, encoded, FRAME_BYTES);
            } else {
                for (uint16_t byte = 0; byte < FRAME_BYTES; ++byte) {
                    frame_buffer[byte] ^= encoded[byte];
                }
            }

            uint32_t global_frame = segment_start + frame;
            current_target = target_frame(start, metadata.total_frames);
            if (global_frame < current_target) {
                continue;
            }

            render_frame(frame_buffer);
            if (global_frame != 0 && clock() > frame_deadline(start, global_frame)) {
                ++late_frames;
            }
            if (!wait_until_frame(global_frame, &paused, &previous_2nd,
                                  &start, &pause_started)) {
                cleanup();
                return 0;
            }

            /* Frames that were already late are skipped before rendering.
             * Once a frame has been rendered, present it even if rendering
             * crossed another deadline.  Otherwise a slow render path can
             * discard every frame and leave the cleared buffer on screen. */
            gfx_SwapDraw();
#ifdef BAD30_DEBUG
            ++displayed_frames;
#endif
        }

        ++segment;
    }

#ifdef BAD30_DEBUG
    show_debug_statistics(displayed_frames);
#endif
    cleanup();
    return 0;
}

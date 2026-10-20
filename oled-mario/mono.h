// Converts the 240x160 RGB565 game framebuffer into a 1-bit OLED image.
// Shared by the device binary and the Mac preview tool.
#pragma once
#include <stdint.h>
#include <string.h>

#define OLED_W 128
#define OLED_H 64

// Source window: 192x96 px of the 240x160 framebuffer, scaled by 2/3.
// x stops before the on-screen touch buttons (x >= ~205).
static int srcX0 = 0, srcY0 = 48;

static inline int luma565(uint16_t c) {
    int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return (r * 8 * 77 + g * 4 * 150 + b * 8 * 29) >> 8;  // 0..255
}

// Most common colour = background (sky / underground black) -> drawn black.
static uint16_t bgColour(const uint16_t* fb, int fbW, int fbH) {
    static uint16_t hist[65536];
    memset(hist, 0, sizeof(hist));
    uint16_t best = 0;
    int bestN = 0;
    for (int y = srcY0; y < srcY0 + 96 && y < fbH; y += 2)
        for (int x = srcX0; x < srcX0 + 192 && x < fbW; x += 2) {
            uint16_t c = fb[y * fbW + x];
            if (++hist[c] > bestN) { bestN = hist[c]; best = c; }
        }
    return best;
}

static const uint8_t OBAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// out: OLED_W*OLED_H bytes, 0 or 1
static void toMono(const uint16_t* fb, int fbW, int fbH, uint8_t* out) {
    uint16_t bg = bgColour(fb, fbW, fbH);
    for (int y = 0; y < OLED_H; y++)
        for (int x = 0; x < OLED_W; x++) {
            int sx = srcX0 + x * 3 / 2, sy = srcY0 + y * 3 / 2;
            uint16_t c = (sx < fbW && sy < fbH) ? fb[sy * fbW + sx] : bg;
            int on = 0;
            if (c != bg) {
                int l = luma565(c);
                on = l > (OBAYER[y & 3][x & 3] * 16 + 8) * 3 / 4 + 20;  // bias toward lit so mid tones show
            }
            out[y * OLED_W + x] = on;
        }
}

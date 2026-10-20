// ==============================================================================
//  gfx.h - 240x160 (GBA resolution) RGB565 software renderer
//  Pure C++ (no Arduino deps) so the same code runs on the ESP32 and in the
//  desktop simulator (tools/sim.cpp). The firmware pushes this framebuffer to
//  the 480x320 panel at 2x scale.
// ==============================================================================
#pragma once
#include <stdint.h>
#include <string.h>
#include "art.h"

#define FB_W 240
#define FB_H 160

static uint16_t* fb = nullptr;  // allocated by the platform layer

static inline uint16_t RGB(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
static inline uint8_t R8(uint16_t c) { return (uint8_t)(((c >> 11) & 31) * 255 / 31); }
static inline uint8_t G8(uint16_t c) { return (uint8_t)(((c >> 5) & 63) * 255 / 63); }
static inline uint8_t B8(uint16_t c) { return (uint8_t)((c & 31) * 255 / 31); }

// a: 0 = all c0, 256 = all c1
static inline uint16_t blend565(uint16_t c0, uint16_t c1, int a) {
    if (a <= 0) return c0;
    if (a >= 256) return c1;
    int r0 = (c0 >> 11) & 31, g0 = (c0 >> 5) & 63, b0 = c0 & 31;
    int r1 = (c1 >> 11) & 31, g1 = (c1 >> 5) & 63, b1 = c1 & 31;
    return (uint16_t)((((r0 + ((r1 - r0) * a >> 8)) & 31) << 11) |
                      (((g0 + ((g1 - g0) * a >> 8)) & 63) << 5) |
                      ((b0 + ((b1 - b0) * a >> 8)) & 31));
}

static const uint8_t BAYER4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

// ordered-dither an 8-bit colour into RGB565 (kills gradient banding)
static inline uint16_t dither565(int x, int y, int r, int g, int b) {
    int t = BAYER4[(y & 3) * 4 + (x & 3)];
    r += t >> 1;  // 0..7
    g += t >> 2;  // 0..3
    b += t >> 1;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return RGB(r, g, b);
}

static inline void pset(int x, int y, uint16_t c) {
    if ((unsigned)x < FB_W && (unsigned)y < FB_H) fb[y * FB_W + x] = c;
}
static inline void pblend(int x, int y, uint16_t c, int a) {
    if ((unsigned)x < FB_W && (unsigned)y < FB_H) fb[y * FB_W + x] = blend565(fb[y * FB_W + x], c, a);
}

static void fillRect(int x, int y, int w, int h, uint16_t c) {
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > FB_W ? FB_W : x + w, y1 = y + h > FB_H ? FB_H : y + h;
    for (int yy = y0; yy < y1; yy++) {
        uint16_t* p = fb + yy * FB_W;
        for (int xx = x0; xx < x1; xx++) p[xx] = c;
    }
}

static void blendRect(int x, int y, int w, int h, uint16_t c, int a) {
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > FB_W ? FB_W : x + w, y1 = y + h > FB_H ? FB_H : y + h;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) fb[yy * FB_W + xx] = blend565(fb[yy * FB_W + xx], c, a);
}

// vertical dithered gradient between two 24-bit colours
static void gradientV(int y0, int y1, uint32_t top, uint32_t bot) {
    int h = y1 - y0;
    if (h <= 0) return;
    int tr = top >> 16, tg = (top >> 8) & 255, tb = top & 255;
    int br = bot >> 16, bg = (bot >> 8) & 255, bb = bot & 255;
    for (int y = y0; y < y1; y++) {
        if ((unsigned)y >= FB_H) continue;
        int t = (y - y0) * 256 / h;
        int r = tr + ((br - tr) * t >> 8), g = tg + ((bg - tg) * t >> 8), b = tb + ((bb - tb) * t >> 8);
        uint16_t* p = fb + y * FB_W;
        for (int x = 0; x < FB_W; x++) p[x] = dither565(x, y, r, g, b);
    }
}

static void hline(int x0, int x1, int y, uint16_t c) {
    if ((unsigned)y >= FB_H) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (x0 < 0) x0 = 0;
    if (x1 >= FB_W) x1 = FB_W - 1;
    for (int x = x0; x <= x1; x++) fb[y * FB_W + x] = c;
}

static void line(int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        pset(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void thickLine(int x0, int y0, int x1, int y1, uint16_t c) {
    line(x0, y0, x1, y1, c);
    line(x0 + 1, y0, x1 + 1, y1, c);
    line(x0, y0 + 1, x1, y1 + 1, c);
}

static void fillEllipse(int cx, int cy, int rx, int ry, uint16_t c) {
    if (rx <= 0 || ry <= 0) return;
    for (int y = -ry; y <= ry; y++) {
        // x extent at this row: rx * sqrt(1 - y^2/ry^2) using integer search
        int yy = y * y * rx * rx;
        int lim = rx * rx * ry * ry;
        int w = rx;
        while (w > 0 && w * w * ry * ry + yy > lim) w--;
        hline(cx - w, cx + w, cy + y, c);
    }
}

static void blendEllipse(int cx, int cy, int rx, int ry, uint16_t c, int a) {
    if (rx <= 0 || ry <= 0) return;
    for (int y = -ry; y <= ry; y++) {
        int yy = y * y * rx * rx, lim = rx * rx * ry * ry, w = rx;
        while (w > 0 && w * w * ry * ry + yy > lim) w--;
        for (int x = cx - w; x <= cx + w; x++) pblend(x, cy + y, c, a);
    }
}

static void fillCircle(int cx, int cy, int r, uint16_t c) { fillEllipse(cx, cy, r, r, c); }

static void circleOutline(int cx, int cy, int r, uint16_t c) {
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        pset(cx + x, cy + y, c); pset(cx - x, cy + y, c); pset(cx + x, cy - y, c); pset(cx - x, cy - y, c);
        pset(cx + y, cy + x, c); pset(cx - y, cy + x, c); pset(cx + y, cy - x, c); pset(cx - y, cy - x, c);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

// ------------------------------------------------------------------ sprites
enum BlitMode : uint8_t { BM_NORMAL, BM_WHITE, BM_BLACK, BM_TINT, BM_ALPHA };

struct BlitOpt {
    bool flip = false;
    bool vflip = false;     // upside down (knocked-out enemies)
    BlitMode mode = BM_NORMAL;
    uint16_t tint = 0;      // BM_TINT colour / BM_WHITE strength via alpha
    int alpha = 256;        // BM_TINT / BM_ALPHA strength
    int scale = 256;        // 256 = 1:1 (scaled around bottom-centre)
    int clipBottom = FB_H;  // rows at/after this y are not drawn
};

static void blit(const Sprite& s, int x, int y) {
    const uint8_t* px = s.px;
    for (int sy = 0; sy < s.h; sy++) {
        int dy = y + sy;
        if ((unsigned)dy >= FB_H) { px += s.w; continue; }
        uint16_t* row = fb + dy * FB_W;
        for (int sx = 0; sx < s.w; sx++) {
            uint8_t i = *px++;
            int dx = x + sx;
            if (i && (unsigned)dx < FB_W) row[dx] = s.pal[i];
        }
    }
}

// fast path for opaque 16x16 terrain tiles
static void blitTile(const Sprite& s, int x, int y) {
    if (x >= 0 && x + 16 <= FB_W && y >= 0 && y + 16 <= FB_H) {
        const uint8_t* px = s.px;
        for (int sy = 0; sy < 16; sy++) {
            uint16_t* row = fb + (y + sy) * FB_W + x;
            for (int sx = 0; sx < 16; sx++) row[sx] = s.pal[*px++];
        }
        return;
    }
    blit(s, x, y);
}

static void blitEx(const Sprite& s, int x, int y, const BlitOpt& o) {
    if (o.scale <= 0) return;
    int dw = s.w * o.scale >> 8, dh = s.h * o.scale >> 8;
    if (dw <= 0 || dh <= 0) return;
    // scale around bottom-centre so creatures shrink into their platform
    int ox = x + (s.w - dw) / 2, oy = y + (s.h - dh);
    for (int dy = 0; dy < dh; dy++) {
        int ty = oy + dy;
        if ((unsigned)ty >= FB_H || ty >= o.clipBottom) continue;
        int sy = dy * s.h / dh;
        if (o.vflip) sy = s.h - 1 - sy;
        const uint8_t* row = s.px + sy * s.w;
        for (int dx = 0; dx < dw; dx++) {
            int tx = ox + dx;
            if ((unsigned)tx >= FB_W) continue;
            int sx = dx * s.w / dw;
            if (o.flip) sx = s.w - 1 - sx;
            uint8_t i = row[sx];
            if (!i) continue;
            uint16_t c = s.pal[i];
            uint16_t* d = fb + ty * FB_W + tx;
            switch (o.mode) {
                case BM_NORMAL: *d = c; break;
                case BM_WHITE: *d = blend565(c, 0xFFFF, o.alpha); break;
                case BM_BLACK: *d = blend565(c, RGB(24, 24, 40), o.alpha); break;
                case BM_TINT: *d = blend565(c, o.tint, o.alpha); break;
                case BM_ALPHA: *d = blend565(*d, c, o.alpha); break;
            }
        }
    }
}

// ------------------------------------------------------------------ text
static int textWidth(const char* s) {
    int w = 0;
    for (; *s; s++) {
        uint8_t ch = (uint8_t)*s;
        if (ch < 128 && FONT[ch].w) w += FONT[ch].w + 1;
    }
    return w ? w - 1 : 0;
}

static int drawGlyphs(int x, int y, const char* s, int n, uint16_t c) {
    for (int k = 0; s[k] && (n < 0 || k < n); k++) {
        uint8_t ch = (uint8_t)s[k];
        if (ch >= 128 || !FONT[ch].w) continue;
        const Glyph& g = FONT[ch];
        for (int r = 0; r < 8; r++) {
            uint8_t bits = g.rows[r];
            for (int i = 0; bits; i++, bits >>= 1)
                if (bits & 1) pset(x + i, y + r, c);
        }
        x += g.w + 1;
    }
    return x;
}

// GBA style: ink + soft drop shadow (right, down, diagonal); n = chars to show (-1 = all)
static int text(int x, int y, const char* s, uint16_t ink, uint16_t shadow, int n = -1) {
    drawGlyphs(x + 1, y, s, n, shadow);
    drawGlyphs(x, y + 1, s, n, shadow);
    drawGlyphs(x + 1, y + 1, s, n, shadow);
    return drawGlyphs(x, y, s, n, ink);
}

// ------------------------------------------------------------------ panels
// rounded box: 1px outer border, 1px inner bevel, fill
static void panel(int x, int y, int w, int h, uint16_t border, uint16_t bevel, uint16_t fill) {
    fillRect(x + 2, y, w - 4, h, border);
    fillRect(x, y + 2, w, h - 4, border);
    fillRect(x + 1, y + 1, w - 2, h - 2, border);
    fillRect(x + 2, y + 1, w - 4, h - 2, bevel);
    fillRect(x + 1, y + 2, w - 2, h - 4, bevel);
    fillRect(x + 2, y + 2, w - 4, h - 4, fill);
}

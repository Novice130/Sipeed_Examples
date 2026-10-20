// Flappy Bird tribute that runs on the NanoKVM and streams to the ESP32-035 CYD ("cloud gaming").
// The KVM runs the game and renders 240x160 indexed-colour frames; the CYD (examples/cloud-game in
// the CYD repo) only draws them 2x and sends back one button: cover the light sensor or tap.
// All art is drawn here from shapes (fan tribute, no original assets).
//
//   flappy_server [PORT] [BEST_FILE]       default 8766, /root/flappy_best
//   flappy_server --sim OUT TICKS EVERY    Mac preview: writes 2x PPM frames of a scripted run
//
// Protocol (TCP, little endian):
//   client: "HI1\0"                 server: "CGM1", u16 width, u16 height, u16 n, n x u16 RGB565 palette
//   client: 'F', u8 held, u16 presses (running count of button presses)
//   server: u32 sfx bits (1 flap, 2 score, 4 hit, 8 die, 16 swoosh), u32 len, len bytes of frame
// Frame = width*height palette indices, run-length coded: byte b < 128 -> next byte repeated b+1
// times; b >= 128 -> b-127 literal bytes follow. The game advances on its own 30 Hz clock, so a slow
// client gets fewer frames, never a slower game.
#include <arpa/inet.h>
#include <math.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0   // macOS (sim builds): no SIGPIPE flag, SIGPIPE is ignored instead
#endif

#define W 240
#define H 160
#define GROUND_Y 136     // top of the ground strip
#define TICK_HZ 30
#define BIRD_X 60
#define GAP 50           // vertical opening between pipes
#define PIPE_W 26
#define PIPE_SPACING 96
#define PIPE_SPEED 1.5f  // px per tick
#define GRAVITY 0.36f
#define FLAP_VY -3.7f
#define MAX_VY 5.5f

enum Sfx { SFX_FLAP = 1, SFX_SCORE = 2, SFX_HIT = 4, SFX_DIE = 8, SFX_SWOOSH = 16 };

// ----- palette
enum : uint8_t {
    SKY0, SKY1, SKY2, SKY3, CLOUD, CLOUD_SH, CITY, CITY_WIN, BUSH, BUSH_DK,
    PIPE_HI, PIPE_MID, PIPE_SH, PIPE_DK, GROUND, GROUND_DK, GRASS, GRASS_DK,
    BIRD_Y, BIRD_YL, BIRD_OR, BIRD_RD, WHITE, BLACK, PANEL, PANEL_DK, ORANGE, NCOL,
    CLEAR = 255
};
static const uint8_t RGB[NCOL][3] = {
    {78, 192, 202},  {96, 199, 208},  {116, 206, 214}, {136, 213, 220},  // sky bands
    {236, 252, 222}, {212, 240, 200},                                   // clouds
    {172, 222, 192}, {150, 202, 172},                                   // city
    {96, 202, 100},  {70, 168, 78},                                     // bushes
    {170, 236, 98},  {116, 192, 46},  {84, 150, 30},   {48, 64, 34},     // pipe
    {222, 216, 150}, {196, 184, 118}, {158, 228, 90},  {112, 188, 46},   // ground
    {250, 204, 44},  {252, 238, 132}, {250, 124, 32},  {228, 64, 40},    // bird
    {255, 255, 255}, {44, 32, 44},    {224, 216, 152}, {140, 100, 58},  {250, 144, 30},
};

static uint8_t fb[W * H];
static uint8_t bg[W * GROUND_Y];

static uint32_t rng = 12345;
static int rnd(int n) { rng = rng * 1103515245u + 12345u; return (rng >> 16) % n; }

static void px(int x, int y, uint8_t c) { if (x >= 0 && x < W && y >= 0 && y < H) fb[y * W + x] = c; }
static void rect(int x, int y, int w, int h, uint8_t c) {
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) px(i, j, c);
}

// ----- background (static, like the original): sky bands, clouds, city, bushes
static void buildBackground() {
    for (int y = 0; y < GROUND_Y; y++)
        for (int x = 0; x < W; x++) bg[y * W + x] = y < 40 ? SKY0 : y < 68 ? SKY1 : y < 88 ? SKY2 : SKY3;
    auto disc = [](int cx, int cy, int r, uint8_t c, uint8_t shade, int shadeFrom) {
        for (int y = cy - r; y <= cy + r; y++)
            for (int x = cx - r; x <= cx + r; x++)
                if (x >= 0 && x < W && y >= 0 && y < GROUND_Y && (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r)
                    bg[y * W + x] = y >= shadeFrom ? shade : c;
    };
    rng = 7;
    for (int x = -6; x < W + 10; x += 14 + rnd(10)) disc(x, 98 + rnd(6), 9 + rnd(6), CLOUD, CLOUD_SH, 106);
    for (int y = 104; y < GROUND_Y; y++) for (int x = 0; x < W; x++) bg[y * W + x] = y < 108 ? CLOUD : CLOUD_SH;
    for (int x = 0; x < W;) {   // city: blocks of different heights with window dots
        int bw = 10 + rnd(12), top = 104 + rnd(12);
        for (int y = top; y < GROUND_Y; y++)
            for (int i = x; i < x + bw && i < W; i++)
                bg[y * W + i] = ((i - x) % 4 == 2 && (y - top) % 4 == 2 && i < x + bw - 2) ? CITY_WIN : CITY;
        x += bw;
    }
    for (int x = -4; x < W + 8; x += 10 + rnd(8)) disc(x, 126 + rnd(3), 7 + rnd(4), BUSH, BUSH_DK, 131);
    for (int y = 130; y < GROUND_Y; y++) for (int x = 0; x < W; x++) bg[y * W + x] = y < 131 ? BUSH : BUSH_DK;
}

// ----- bird: 3 wing frames, 20x16, built from shapes then outlined
#define BW 20
#define BH 16
static uint8_t bird[3][BW * BH];
static void buildBird() {
    for (int f = 0; f < 3; f++) {
        uint8_t* s = bird[f];
        memset(s, CLEAR, BW * BH);
        auto in = [](float x, float y, float cx, float cy, float rx, float ry) {
            float dx = (x - cx) / rx, dy = (y - cy) / ry;
            return dx * dx + dy * dy <= 1.0f;
        };
        for (int y = 1; y < BH - 1; y++)
            for (int x = 1; x < BW - 1; x++) {
                float fx = x + 0.5f, fy = y + 0.5f;
                if (in(fx, fy, 9.5f, 8.5f, 7.5f, 6.2f)) s[y * BW + x] = fy > 10.5f ? BIRD_YL : BIRD_Y;
                if (in(fx, fy, 12.6f, 5.6f, 3.0f, 3.0f)) s[y * BW + x] = WHITE;                    // eye
                if (fx > 12.5f && fx < 18.5f && fy > 8.0f && fy < 10.0f) s[y * BW + x] = BIRD_RD; // upper beak
                if (fx > 12.5f && fx < 17.5f && fy > 10.0f && fy < 12.0f) s[y * BW + x] = BIRD_OR; // lower beak
                float wy = 8.5f + (f == 0 ? -2.0f : f == 2 ? 2.0f : 0.0f);
                if (in(fx, fy, 5.5f, wy, 4.2f, 2.6f)) s[y * BW + x] = WHITE;                      // wing
            }
        s[5 * BW + 13] = s[5 * BW + 14] = s[6 * BW + 13] = s[6 * BW + 14] = BLACK;                // pupil
        uint8_t copy[BW * BH];
        memcpy(copy, s, sizeof(copy));
        for (int y = 0; y < BH; y++)   // 1 px outline around every solid pixel
            for (int x = 0; x < BW; x++) {
                if (copy[y * BW + x] != CLEAR) continue;
                for (int d = 0; d < 4; d++) {
                    int nx = x + (d == 0) - (d == 1), ny = y + (d == 2) - (d == 3);
                    if (nx >= 0 && nx < BW && ny >= 0 && ny < BH && copy[ny * BW + nx] != CLEAR) { s[y * BW + x] = BLACK; break; }
                }
            }
    }
}

// ----- 5x7 font (column bytes, bit 0 = top), drawn scaled with a dark outline
static const uint8_t FONT_DIG[10][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00}, {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4B, 0x31}, {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1E}};
static const uint8_t FONT_AZ[26][5] = {
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01},
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F},
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}};

static const uint8_t* glyph(char c) {
    static const uint8_t bang[5] = {0x00, 0x00, 0x5F, 0x00, 0x00}, blank[5] = {0};
    if (c >= '0' && c <= '9') return FONT_DIG[c - '0'];
    if (c >= 'A' && c <= 'Z') return FONT_AZ[c - 'A'];
    return c == '!' ? bang : blank;
}

static void glyphAt(char c, int x, int y, int s, uint8_t col) {
    const uint8_t* g = glyph(c);
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 7; j++)
            if (g[i] >> j & 1) rect(x + i * s, y + j * s, s, s, col);
}

static int textW(const char* t, int s) { return (int)strlen(t) * 6 * s - s; }

static void text(const char* t, int x, int y, int s, uint8_t col, uint8_t edge = BLACK) {
    for (int pass = 0; pass < 2; pass++)
        for (int k = 0; t[k]; k++) {
            int gx = x + k * 6 * s;
            if (pass == 0) {   // outline: the glyph stamped one pixel around in the edge colour
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) glyphAt(t[k], gx + dx, y + dy, s, edge);
            } else {
                glyphAt(t[k], gx, y, s, col);
            }
        }
}
static void textC(const char* t, int y, int s, uint8_t col, uint8_t edge = BLACK) { text(t, (W - textW(t, s)) / 2, y, s, col, edge); }

// ----- game
enum State { READY, DEMO, PLAY, DEAD, OVER };
struct Pipe { float x; int gapY; bool scored; };

static State st = READY;
static Pipe pipes[3];
static float birdY, birdVy, groundScroll;
static int score, best, ticks, stateTicks, idleTicks, wingFrame, flash;
static bool newBest;
static uint32_t sfxOut;
static const char* bestFile = "/root/flappy_best";
static bool sim = false;

static void sfx(uint32_t s) { if (st != DEMO) sfxOut |= s; }

static void loadBest() { FILE* f = fopen(bestFile, "r"); if (f) { if (fscanf(f, "%d", &best) != 1) best = 0; fclose(f); } }
static void saveBest() { if (sim) return; FILE* f = fopen(bestFile, "w"); if (f) { fprintf(f, "%d\n", best); fclose(f); } }

static int nextGap(int prev) {
    int lo = 26 + GAP / 2, hi = GROUND_Y - 22 - GAP / 2;
    int g = prev + rnd(81) - 40;
    return g < lo ? lo : g > hi ? hi : g;
}

static void setState(State s) { st = s; stateTicks = 0; }

static void startRun(State s) {   // PLAY or DEMO
    setState(s);
    birdY = 62;
    birdVy = FLAP_VY;
    score = 0;
    newBest = false;
    rng = (uint32_t)time(nullptr) * 2654435761u | 1;
    int g = (GROUND_Y) / 2;
    for (int i = 0; i < 3; i++) { g = nextGap(g); pipes[i] = {W + 40.0f + i * PIPE_SPACING, g, false}; }
    sfx(SFX_SWOOSH);
}

static void flap() { birdVy = FLAP_VY; sfx(SFX_FLAP); }

static void press() {
    idleTicks = 0;
    switch (st) {
        case READY: startRun(PLAY); flap(); break;
        case DEMO: startRun(PLAY); flap(); break;
        case PLAY: flap(); break;
        case DEAD: break;
        case OVER: if (stateTicks > TICK_HZ * 3 / 4) { setState(READY); sfx(SFX_SWOOSH); } break;
    }
}

static bool hitPipe() {
    int bx0 = BIRD_X + 3, bx1 = BIRD_X + BW - 4, by0 = (int)birdY + 3, by1 = (int)birdY + BH - 4;
    for (auto& p : pipes) {
        int px0 = (int)p.x - 2, px1 = (int)p.x + PIPE_W + 2;   // lips are 2 px wider each side
        if (bx1 < px0 || bx0 > px1) continue;
        if (by0 < p.gapY - GAP / 2 || by1 > p.gapY + GAP / 2) return true;
    }
    return false;
}

static void autopilot() {   // demo player: aim a little below the next gap's centre
    const Pipe* n = &pipes[0];
    for (auto& p : pipes) if (p.x + PIPE_W > BIRD_X - 4 && (n->x + PIPE_W <= BIRD_X - 4 || p.x < n->x)) n = &p;
    if (birdY + BH / 2 > n->gapY + 6 && birdVy > 0.5f) flap();
}

static void tick() {
    ticks++;
    stateTicks++;
    idleTicks++;
    if (flash) flash--;
    if (st != DEAD && st != OVER) {
        groundScroll += PIPE_SPEED;
        if (ticks % 4 == 0) wingFrame = (wingFrame + 1) % 3;
    }
    switch (st) {
        case READY:
            birdY = 62 + 4 * sinf(ticks * 0.15f);
            if (idleTicks > TICK_HZ * 6) startRun(DEMO);
            return;
        case OVER:
            if (idleTicks > TICK_HZ * 20) setState(READY);   // nobody playing: back to the title
            return;
        case DEMO: autopilot(); break;
        default: break;
    }
    birdVy += GRAVITY;
    if (birdVy > MAX_VY) birdVy = MAX_VY;
    birdY += birdVy;
    if (birdY < -BH) birdY = -BH;
    if (st == DEAD) {
        if (birdY >= GROUND_Y - BH + 2) { birdY = GROUND_Y - BH + 2; setState(OVER); sfx(SFX_DIE); }
        return;
    }
    for (auto& p : pipes) {
        p.x -= PIPE_SPEED;
        if (!p.scored && p.x + PIPE_W < BIRD_X) {
            p.scored = true;
            score++;
            sfx(SFX_SCORE);
        }
    }
    if (pipes[0].x < -PIPE_W - 4) {   // recycle the leftmost pipe to the right
        pipes[0] = pipes[1];
        pipes[1] = pipes[2];
        pipes[2] = {pipes[1].x + PIPE_SPACING, nextGap(pipes[1].gapY), false};
    }
    bool ground = birdY >= GROUND_Y - BH + 2;
    if (ground || hitPipe()) {
        sfx(SFX_HIT);
        flash = 3;
        if (st == PLAY && score > best) { best = score; newBest = true; saveBest(); }
        bool demo = st == DEMO;
        setState(DEAD);
        if (demo) idleTicks = TICK_HZ * 18;   // demo game over shows briefly, then READY again
        if (ground) { birdY = GROUND_Y - BH + 2; setState(OVER); sfx(SFX_DIE); }
    }
}

// ----- rendering
static uint8_t stripe(int o, int w) {
    if (o == 0 || o == w - 1) return PIPE_DK;
    if (o < w * 12 / 100 + 1) return PIPE_MID;
    if (o < w * 28 / 100 + 1) return PIPE_HI;
    if (o < w * 70 / 100) return PIPE_MID;
    return PIPE_SH;
}

static void drawPipe(const Pipe& p) {
    int x = (int)p.x, top = p.gapY - GAP / 2, bot = p.gapY + GAP / 2;
    for (int i = 0; i < PIPE_W; i++) {
        uint8_t c = stripe(i, PIPE_W);
        for (int y = 0; y < top - 10; y++) px(x + i, y, c);
        for (int y = bot + 10; y < GROUND_Y; y++) px(x + i, y, c);
    }
    const int LW = PIPE_W + 4;   // lips
    for (int i = 0; i < LW; i++) {
        uint8_t c = stripe(i, LW);
        for (int y = top - 10; y < top; y++) px(x - 2 + i, y, (y == top - 10 || y == top - 1) ? PIPE_DK : c);
        for (int y = bot; y < bot + 10; y++) px(x - 2 + i, y, (y == bot || y == bot + 9) ? PIPE_DK : c);
    }
}

static void drawGround() {
    int s = (int)groundScroll;
    for (int x = 0; x < W; x++) {
        px(x, GROUND_Y, BLACK);
        for (int y = GROUND_Y + 1; y < GROUND_Y + 6; y++) px(x, y, ((x + s + (y - GROUND_Y)) / 6) % 2 ? GRASS : GRASS_DK);
        px(x, GROUND_Y + 6, GRASS_DK);
        px(x, GROUND_Y + 7, GROUND_DK);
        for (int y = GROUND_Y + 8; y < H; y++) px(x, y, GROUND);
    }
}

static void drawBird() {
    const uint8_t* s = bird[st == DEAD || st == OVER ? 1 : wingFrame];
    int by = (int)lroundf(birdY);
    for (int y = 0; y < BH; y++)
        for (int x = 0; x < BW; x++)
            if (s[y * BW + x] != CLEAR) px(BIRD_X + x, by + y, s[y * BW + x]);
}

static void panel(int x, int y, int w, int h) {
    rect(x, y, w, h, BLACK);
    rect(x + 1, y + 1, w - 2, h - 2, PANEL_DK);
    rect(x + 3, y + 3, w - 6, h - 6, PANEL);
}

static void render() {
    memcpy(fb, bg, sizeof(bg));
    if (st != READY) for (auto& p : pipes) drawPipe(p);
    drawGround();
    drawBird();
    char buf[24];
    switch (st) {
        case READY:
            textC("FLAPPY", 14, 3, WHITE);
            textC("GET READY!", 44, 2, ORANGE);
            textC("COVER THE LIGHT SENSOR", 96, 1, WHITE);
            textC("OR TAP THE SCREEN", 106, 1, WHITE);
            snprintf(buf, sizeof(buf), "BEST %d", best);
            textC(buf, 146, 1, WHITE);
            break;
        case DEMO:
        case PLAY:
        case DEAD:
            snprintf(buf, sizeof(buf), "%d", score);
            textC(buf, 8, 3, WHITE);
            if (st == DEMO) { text("DEMO", 4, 4, 1, WHITE); textC("TAP TO PLAY", 146, 1, WHITE); }
            break;
        case OVER: {
            textC("GAME OVER", 22, 3, ORANGE);
            panel(60, 52, 120, 56);
            text("SCORE", 70, 60, 1, ORANGE, PANEL);
            text("BEST", 70, 84, 1, ORANGE, PANEL);
            snprintf(buf, sizeof(buf), "%d", score);
            text(buf, 170 - textW(buf, 2), 58, 2, WHITE);
            snprintf(buf, sizeof(buf), "%d", best);
            text(buf, 170 - textW(buf, 2), 82, 2, WHITE);
            if (newBest) text("NEW", 104, 86, 1, WHITE, BIRD_RD);
            if (stateTicks > TICK_HZ * 3 / 4) textC("TAP TO RETRY", 116, 1, WHITE);
            break;
        }
    }
    if (flash) memset(fb, WHITE, sizeof(fb));
}

// ----- run-length coding: b < 128 -> repeat next byte b+1 times; b >= 128 -> b-127 literals
static size_t rle(const uint8_t* src, size_t n, uint8_t* out) {
    size_t i = 0, o = 0;
    while (i < n) {
        size_t run = 1;
        while (i + run < n && run < 128 && src[i + run] == src[i]) run++;
        if (run >= 3) { out[o++] = (uint8_t)(run - 1); out[o++] = src[i]; i += run; continue; }
        size_t lit = 0;
        while (i + lit < n && lit < 128) {
            if (i + lit + 2 < n && src[i + lit] == src[i + lit + 1] && src[i + lit] == src[i + lit + 2]) break;
            lit++;
        }
        out[o++] = (uint8_t)(127 + lit);
        memcpy(out + o, src + i, lit);
        o += lit;
        i += lit;
    }
    return o;
}

// ----- networking
static double now() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

static bool sendAll(int s, const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    while (n) { ssize_t w = send(s, b, n, MSG_NOSIGNAL); if (w <= 0) return false; b += w; n -= w; }
    return true;
}
static bool recvAll(int s, void* p, size_t n) {
    uint8_t* b = (uint8_t*)p;
    while (n) { ssize_t r = recv(s, b, n, 0); if (r <= 0) return false; b += r; n -= r; }
    return true;
}

static void serve(int c) {
    char hello[4];
    if (!recvAll(c, hello, 4) || memcmp(hello, "HI1", 4) != 0) return;
    uint8_t hdr[10 + NCOL * 2];
    memcpy(hdr, "CGM1", 4);
    uint16_t v[3] = {W, H, NCOL};
    memcpy(hdr + 4, v, 6);
    for (int i = 0; i < NCOL; i++) {
        uint16_t c565 = (uint16_t)((RGB[i][0] & 0xF8) << 8 | (RGB[i][1] & 0xFC) << 3 | RGB[i][2] >> 3);
        memcpy(hdr + 10 + i * 2, &c565, 2);
    }
    if (!sendAll(c, hdr, sizeof(hdr))) return;

    setState(READY);
    idleTicks = 0;
    sfxOut = 0;
    static uint8_t out[W * H + W * H / 128 + 16];
    double t0 = now(), statT = t0, busy = 0;
    long done = 0, frames = 0, bytes = 0;
    int lastPresses = -1;
    uint8_t req[4];
    while (recvAll(c, req, 4) && req[0] == 'F') {
        double a = now();
        int presses = req[2] | req[3] << 8;
        if (lastPresses >= 0 && presses != lastPresses) press();
        lastPresses = presses;
        long due = (long)((a - t0) * TICK_HZ);
        if (due - done > 4) { done = due - 4; }   // client stalled: don't fast-forward the game
        while (done < due) { tick(); done++; }
        render();
        uint32_t len = (uint32_t)rle(fb, sizeof(fb), out), head[2] = {sfxOut, len};
        sfxOut = 0;
        busy += now() - a;
        if (!sendAll(c, head, 8) || !sendAll(c, out, len)) return;
        frames++;
        bytes += len;
        if (a - statT >= 10) {
            printf("%.1f fps to client, %.1f KB/frame, %.2f ms/frame on the KVM, score %d best %d\n",
                   frames / (a - statT), bytes / 1024.0 / frames, busy * 1000 / frames, score, best);
            fflush(stdout);
            statT = a; frames = bytes = 0; busy = 0;
        }
    }
}

static void simulate(const char* dir, int total, int every) {
    sim = true;
    for (int t = 1; t <= total; t++) {
        if (t == 40) press();                                // start playing
        if (st == PLAY && t < total - 220) { autopilot(); idleTicks = 0; }   // a busy player, then a crash
        if (st == OVER && t == total - 40) press();
        tick();
        if (t % every) continue;
        render();
        char path[512];
        snprintf(path, sizeof(path), "%s/f%05d.ppm", dir, t);
        FILE* f = fopen(path, "wb");
        if (!f) { perror(path); return; }
        fprintf(f, "P6\n%d %d\n255\n", W * 2, H * 2);
        for (int y = 0; y < H * 2; y++)
            for (int x = 0; x < W * 2; x++) fwrite(RGB[fb[(y / 2) * W + x / 2]], 1, 3, f);
        fclose(f);
    }
    render();
    static uint8_t out[W * H * 2];
    printf("sim done: state %d score %d, last frame %zu bytes RLE\n", st, score, rle(fb, sizeof(fb), out));
}

int main(int argc, char** argv) {
    buildBackground();
    buildBird();
    if (argc >= 5 && !strcmp(argv[1], "--sim")) { simulate(argv[2], atoi(argv[3]), atoi(argv[4])); return 0; }
    int port = argc > 1 ? atoi(argv[1]) : 8766;
    if (argc > 2) bestFile = argv[2];
    loadBest();
    signal(SIGPIPE, SIG_IGN);

    int ls = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    if (bind(ls, (sockaddr*)&a, sizeof(a)) < 0 || listen(ls, 2) < 0) { perror("bind"); return 1; }
    printf("flappy_server on port %d, best %d\n", port, best);
    fflush(stdout);
    for (;;) {   // one player at a time; a dead connection times out after 5 s
        sockaddr_in peer;
        socklen_t pl = sizeof(peer);
        int c = accept(ls, (sockaddr*)&peer, &pl);
        if (c < 0) continue;
        setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        timeval tv = {5, 0};
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        printf("player %s connected\n", inet_ntoa(peer.sin_addr));
        fflush(stdout);
        serve(c);
        close(c);
        printf("player left\n");
        fflush(stdout);
    }
}

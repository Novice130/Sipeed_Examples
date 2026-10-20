// SSD1306 / SH1106 OLED on the NanoKVM Lite (LicheeRV Nano, SG2002) over hardware I2C3,
// running the Mario arcade demo from ../cyd-display/examples/mario-arcade.
//
// Wiring (pinout top view): SCL = GPIOP22 (IIC3 SCL), SDA = GPIOP23 (IIC3 SDA),
// VCC = GPIOA16 driven high, GND = GND. oled-on.sh sets up the pins after each boot.
//
//   oled_mario test   [opts]   border + X pattern
//   oled_mario mario  [opts]   self-playing Mario demo at up to 30 fps
//   oled_mario clear  [opts]   blank the panel and switch it off
// opts: --sh1106  --h32 (128x32 panel)  --addr 0x3d  --bus N (default 3)
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "game.h"
#include "mono.h"

void plat_sfx(uint8_t, uint16_t) {}
void plat_led(uint8_t, uint8_t, uint8_t, uint16_t) {}

static int i2c = -1, addr = 0x3C, rows = 64;
static bool sh1106 = false;
static volatile bool quit = false;

// one I2C write: control byte (0x00 = commands, 0x40 = data) + payload
static bool xfer(uint8_t ctrl, const uint8_t* p, int n) {
    static uint8_t buf[1 + OLED_W * 8];
    buf[0] = ctrl;
    memcpy(buf + 1, p, n);
    return write(i2c, buf, n + 1) == n + 1;
}
static bool cmds(const uint8_t* c, int n) { return xfer(0x00, c, n); }

static bool oledInit() {
    const uint8_t init[] = {0xAE, 0xD5, 0x80, 0xA8, (uint8_t)(rows - 1), 0xD3, 0x00, 0x40, 0x8D, 0x14,
                            0x20, 0x00, 0xA1, 0xC8, 0xDA, (uint8_t)(rows == 64 ? 0x12 : 0x02),
                            0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF};
    return cmds(init, sizeof(init));
}

// px: OLED_W x rows, one byte per pixel (0/1)
static void push(const uint8_t* px) {
    static uint8_t all[OLED_W * 8];
    for (int p = 0; p < rows / 8; p++)
        for (int x = 0; x < OLED_W; x++) {
            uint8_t b = 0;
            for (int k = 0; k < 8; k++) b |= px[(p * 8 + k) * OLED_W + x] << k;
            all[p * OLED_W + x] = b;
        }
    if (sh1106) {  // no horizontal addressing mode: one page at a time, 2-column offset
        for (int p = 0; p < rows / 8; p++) {
            const uint8_t pos[] = {(uint8_t)(0xB0 + p), 0x02, 0x10};
            cmds(pos, sizeof(pos));
            xfer(0x40, all + p * OLED_W, OLED_W);
        }
        return;
    }
    const uint8_t win[] = {0x21, 0, OLED_W - 1, 0x22, 0, (uint8_t)(rows / 8 - 1)};
    cmds(win, sizeof(win));
    xfer(0x40, all, OLED_W * rows / 8);
}

static void onSig(int) { quit = true; }

static double now() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: oled_mario test|mario|clear [--sh1106] [--h32] [--addr 0x3d] [--bus N]\n"); return 1; }
    int bus = 3;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--sh1106")) sh1106 = true;
        else if (!strcmp(argv[i], "--h32")) rows = 32;
        else if (!strcmp(argv[i], "--addr") && i + 1 < argc) addr = strtol(argv[++i], nullptr, 0);
        else if (!strcmp(argv[i], "--bus") && i + 1 < argc) bus = atoi(argv[++i]);
    }
    char dev[32];
    snprintf(dev, sizeof(dev), "/dev/i2c-%d", bus);
    i2c = open(dev, O_RDWR);
    if (i2c < 0 || ioctl(i2c, I2C_SLAVE_FORCE, addr) < 0) { perror(dev); return 1; }

    bool ack = oledInit();
    printf("OLED at %s 0x%02X: %s\n", dev, addr, ack ? "ok" : "NO ACK - check power/wiring/address");
    if (!ack) return 2;

    static uint8_t px[OLED_W * OLED_H];
    if (!strcmp(argv[1], "clear")) {
        memset(px, 0, sizeof(px));
        push(px);
        const uint8_t off[] = {0xAE};
        cmds(off, 1);
        return 0;
    }
    if (!strcmp(argv[1], "test")) {
        for (int y = 0; y < rows; y++)
            for (int x = 0; x < OLED_W; x++)
                px[y * OLED_W + x] = x == 0 || y == 0 || x == OLED_W - 1 || y == rows - 1 ||
                                     x * (rows - 1) == y * (OLED_W - 1) || (OLED_W - 1 - x) * (rows - 1) == y * (OLED_W - 1);
        double t0 = now();
        push(px);
        printf("frame push took %.1f ms\n", (now() - t0) * 1000);
        return 0;
    }

    signal(SIGINT, onSig);
    signal(SIGTERM, onSig);
    static uint16_t buf[FB_W * FB_H];
    fb = buf;
    game_init((uint32_t)time(nullptr) | 1);
    double next = now(), fpsT = now();
    int frames = 0;
    static uint8_t mono[OLED_W * OLED_H];
    while (!quit) {
        game_tick(Input{});
        game_render();
        toMono(fb, FB_W, FB_H, mono);
        if (rows == 32)  // squash 64 -> 32 rows
            for (int y = 0; y < 32; y++) memcpy(px + y * OLED_W, mono + y * 2 * OLED_W, OLED_W);
        else
            memcpy(px, mono, sizeof(px));
        push(px);
        frames++;
        next += 1.0 / TICK_HZ;
        double d = next - now();
        if (d > 0) usleep((useconds_t)(d * 1e6));
        else if (d < -0.5) next = now();  // too slow: don't try to catch up
        if (now() - fpsT >= 5) {
            printf("%.1f fps\n", frames / (now() - fpsT));
            fflush(stdout);
            frames = 0;
            fpsT = now();
        }
    }
    memset(px, 0, sizeof(px));
    push(px);
    return 0;
}

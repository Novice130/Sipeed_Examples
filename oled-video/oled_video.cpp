// Plays a .oledv stream (made by convert.py on the Mac) on the SSD1306 / SH1106 OLED
// of the NanoKVM Lite over hardware I2C3. Same wiring as ../oled-mario.
//
//   oled_video FILE.oledv [opts]     play a file stored on the KVM
//   oled_video - [opts]              play from stdin (streamed over ssh by play.sh)
// opts: --loop (files only)  --sh1106  --addr 0x3d  --bus N (default 3)
//
// Frames are shown on a wall clock: if the I2C bus can't keep up, frames are dropped
// so the video still runs at the right speed.
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define W 128
#define FRAME (W * 8)

static int i2c = -1, addr = 0x3C;
static bool sh1106 = false;
static volatile bool quit = false;

// one I2C write: control byte (0x00 = commands, 0x40 = data) + payload
static bool xfer(uint8_t ctrl, const uint8_t* p, int n) {
    static uint8_t buf[1 + FRAME];
    buf[0] = ctrl;
    memcpy(buf + 1, p, n);
    return write(i2c, buf, n + 1) == n + 1;
}
static bool cmds(const uint8_t* c, int n) { return xfer(0x00, c, n); }

static bool oledInit() {
    const uint8_t init[] = {0xAE, 0xD5, 0x80, 0xA8, 63, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8,
                            0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF};
    return cmds(init, sizeof(init));
}

// frame: 8 pages x 128 columns, already packed by convert.py
static void push(const uint8_t* frame) {
    if (sh1106) {  // no horizontal addressing mode: one page at a time, 2-column offset
        for (int p = 0; p < 8; p++) {
            const uint8_t pos[] = {(uint8_t)(0xB0 + p), 0x02, 0x10};
            cmds(pos, sizeof(pos));
            xfer(0x40, frame + p * W, W);
        }
        return;
    }
    const uint8_t win[] = {0x21, 0, W - 1, 0x22, 0, 7};
    cmds(win, sizeof(win));
    xfer(0x40, frame, FRAME);
}

static bool readFull(int fd, uint8_t* p, int n) {
    while (n > 0) {
        ssize_t r = read(fd, p, n);
        if (r <= 0) return false;
        p += r;
        n -= r;
    }
    return true;
}

static void onSig(int) { quit = true; }

static double now() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: oled_video FILE.oledv|- [--loop] [--sh1106] [--addr 0x3d] [--bus N]\n"); return 1; }
    int bus = 3;
    bool loop = false;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--loop")) loop = true;
        else if (!strcmp(argv[i], "--sh1106")) sh1106 = true;
        else if (!strcmp(argv[i], "--addr") && i + 1 < argc) addr = strtol(argv[++i], nullptr, 0);
        else if (!strcmp(argv[i], "--bus") && i + 1 < argc) bus = atoi(argv[++i]);
    }
    bool isStdin = !strcmp(argv[1], "-");
    int in = isStdin ? 0 : open(argv[1], O_RDONLY);
    if (in < 0) { perror(argv[1]); return 1; }
    if (isStdin) loop = false;

    char dev[32];
    snprintf(dev, sizeof(dev), "/dev/i2c-%d", bus);
    i2c = open(dev, O_RDWR);
    if (i2c < 0 || ioctl(i2c, I2C_SLAVE_FORCE, addr) < 0) { perror(dev); return 1; }
    if (!oledInit()) { printf("OLED at %s 0x%02X: NO ACK - check power/wiring/address\n", dev, addr); return 2; }

    signal(SIGINT, onSig);
    signal(SIGTERM, onSig);
    signal(SIGHUP, onSig);
    signal(SIGPIPE, SIG_IGN);

    static uint8_t frame[FRAME];
    do {
        uint8_t hdr[12];
        if (!readFull(in, hdr, sizeof(hdr)) || memcmp(hdr, "OLV1", 4) || hdr[5] != 64) {
            fprintf(stderr, "not a 128x64 .oledv stream\n");
            return 1;
        }
        int fps = hdr[4] ? hdr[4] : 30;
        uint32_t count = hdr[8] | hdr[9] << 8 | hdr[10] << 16 | (uint32_t)hdr[11] << 24;
        printf("%u frames at %d fps\n", count, fps);
        fflush(stdout);

        double t0 = now(), statT = t0;
        int shown = 0, dropped = 0;
        for (uint32_t i = 0; !quit && readFull(in, frame, FRAME); i++) {
            double due = t0 + (double)i / fps;
            double d = due - now();
            if (d < -1.0 / fps) { dropped++; continue; }  // more than a frame late: skip it
            if (d > 0) usleep((useconds_t)(d * 1e6));
            push(frame);
            shown++;
            if (now() - statT >= 5) {
                printf("%.1f fps shown, %d dropped\n", shown / (now() - statT), dropped);
                fflush(stdout);
                shown = dropped = 0;
                statT = now();
            }
        }
        if (loop) lseek(in, 0, SEEK_SET);
    } while (loop && !quit);

    memset(frame, 0, sizeof(frame));
    push(frame);
    return 0;
}

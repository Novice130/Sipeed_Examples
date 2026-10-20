// Mac preview: runs the real Mario demo and writes OLED-sized frames (scaled 4x) as PGM.
//   clang++ -std=c++17 -O2 -Iinclude preview.cpp -o preview && ./preview OUT TICKS EVERY
#include <stdio.h>
#include <stdlib.h>
#include "game.h"
#include "mono.h"

void plat_sfx(uint8_t, uint16_t) {}
void plat_led(uint8_t, uint8_t, uint8_t, uint16_t) {}

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: preview OUT TICKS EVERY\n"); return 1; }
    static uint16_t buf[FB_W * FB_H];
    fb = buf;
    game_init(12345);
    int ticks = atoi(argv[2]), every = atoi(argv[3]);
    static uint8_t mono[OLED_W * OLED_H];
    for (int t = 1; t <= ticks; t++) {
        game_tick(Input{});
        if (t % every) continue;
        game_render();
        toMono(fb, FB_W, FB_H, mono);
        char path[512];
        snprintf(path, sizeof(path), "%s/o%05d.pgm", argv[1], t);
        FILE* f = fopen(path, "wb");
        fprintf(f, "P5\n%d %d\n255\n", OLED_W * 4, OLED_H * 4);
        for (int y = 0; y < OLED_H * 4; y++)
            for (int x = 0; x < OLED_W * 4; x++) fputc(mono[(y / 4) * OLED_W + x / 4] ? 255 : 0, f);
        fclose(f);
    }
    return 0;
}

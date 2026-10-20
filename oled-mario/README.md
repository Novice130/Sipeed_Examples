# oled-mario: self-playing Mario on the NanoKVM's OLED

<img src="docs/preview.png" alt="Mario demo frames as shown on the 128x64 OLED" width="520">

A side-scrolling Mario fan tribute that plays itself on a 0.96" SSD1306 (128x64, I2C) wired to a
Sipeed NanoKVM Lite. It starts at boot and keeps running next to the KVM services (static binary,
~140 KB, a few MB of RAM).

The game engine (`include/game.h`, `gfx.h`, `art.h`) is copied from the
[ESP32-035 CYD repo's mario-arcade example](https://github.com/Novice130/ESP32-035-CYD-3.5-display/tree/main/examples/mario-arcade).
It renders a 240x160 colour frame with no hardware dependencies. [`mono.h`](mono.h) crops a 192x96
window around Mario, scales it by 2/3 to 128x64, draws the most common colour (sky) as black, and
dithers the rest with a 4x4 Bayer matrix.

## Files

| File | Runs on | Purpose |
| :--- | :--- | :--- |
| `oled_mario.cpp` | KVM | I2C SSD1306/SH1106 driver + 30 Hz game loop |
| `mono.h` | both | colour framebuffer → 1-bit 128x64 |
| `include/` | both | Mario engine and art (hand-made, generated) |
| `oled-on.sh` | KVM | powers the OLED from GPIOA16 and muxes GPIOP22/P23 to I2C3, run after every boot |
| `S99oledmario` | KVM | init script: starts the demo at boot |
| `build.sh` | Mac | cross-compiles with zig; `./build.sh deploy` copies everything to `$KVM:/root/` |
| `preview.cpp` | Mac | renders OLED frames as PGM images without hardware (made `docs/preview.png`) |

## Build and run

```sh
brew install zig
export KVM=root@192.168.0.222          # your NanoKVM
./build.sh deploy
ssh $KVM '/root/oled-on.sh && /root/oled_mario test'   # border + X pattern, prints frame push time
ssh $KVM '/root/oled_mario mario'                      # Ctrl-C to stop (blanks the panel)
```

Start at boot:

```sh
ssh $KVM 'cp /root/S99oledmario /etc/init.d/ && chmod +x /etc/init.d/S99oledmario && /etc/init.d/S99oledmario start'
# later: /etc/init.d/S99oledmario stop|start|restart
```

Options for `oled_mario test|mario|clear`: `--sh1106` (1.3" panels), `--h32` (128x32 panels),
`--addr 0x3d`, `--bus N` (default 3).

Preview on the Mac: `clang++ -std=c++17 -O2 -Iinclude preview.cpp -o preview && mkdir -p out && ./preview out 1800 150`
writes one 4x-scaled PGM every 150 ticks.

Wiring, pin muxing and the I2C protocol are explained in [../docs/GUIDE.md](../docs/GUIDE.md).

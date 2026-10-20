# How it's done: an OLED on the NanoKVM Lite, from wiring to video

This guide explains every step behind the examples in this repo, so you can reuse the pieces in
your own project: powering and wiring a display, routing pins with the SoC's pin mux, driving I2C
from Linux userspace, cross-compiling for RISC-V, starting at boot, and streaming video.

- [1. The board](#1-the-board)
- [2. Back up before you touch anything](#2-back-up-before-you-touch-anything)
- [3. Wiring the OLED](#3-wiring-the-oled)
- [4. Pin muxing with devmem](#4-pin-muxing-with-devmem)
- [5. Talking to the SSD1306 over I2C](#5-talking-to-the-ssd1306-over-i2c)
- [6. Cross-compiling for the KVM](#6-cross-compiling-for-the-kvm)
- [7. Starting at boot](#7-starting-at-boot)
- [8. Turning video into OLED frames](#8-turning-video-into-oled-frames)
- [9. Troubleshooting](#9-troubleshooting)
- [10. Using this in your own project](#10-using-this-in-your-own-project)

---

## 1. The board

The NanoKVM Lite is a **LicheeRV Nano-E** (Sophgo **SG2002**: 1 GHz RISC-V C906 core, 256 MB DDR in
the package) with an HDMI capture board stacked on top. It runs Buildroot Linux with the NanoKVM
app. What matters for side projects:

| Thing | Value on the tested unit |
| :--- | :--- |
| Shell | `ssh root@<kvm-ip>` (enable SSH in the NanoKVM web UI). Add your key to `/root/.ssh/authorized_keys` |
| CPU / ABI | `riscv64`, Linux 5.10, musl-compatible static binaries run fine |
| RAM | ~158 MB visible to Linux (the rest is reserved for video), **~50 MB free** with the KVM running |
| Storage | microSD: `/boot` (16 MB), `/` (~7.6 GB, ~6 GB free), optional big data partition |
| Tools on the image | Python 3.11 + pip, `devmem`, i2c-dev, `start-stop-daemon`, cron, zstd |

Official docs: [NanoKVM wiki](https://wiki.sipeed.com/hardware/en/kvm/NanoKVM/introduction.html) ·
[LicheeRV Nano wiki (pinout, schematics)](https://wiki.sipeed.com/hardware/en/lichee/RV_Nano/1_intro.html) ·
[NanoKVM source](https://github.com/sipeed/NanoKVM).

**Design rule used everywhere here:** the KVM must keep working. So the on-device programs are tiny
static C++ binaries (40–140 KB, a few MB of RAM). Anything heavy (downloading, decoding video,
dithering) runs on the Mac, and the KVM only pushes ready-made bytes to the display.

## 2. Back up before you touch anything

[`tools/backup-kvm.sh`](../tools/backup-kvm.sh) copies the MBR, `/boot` and `/` (everything up to
the end of partition 2) over SSH while the KVM keeps running:

- reads `/dev/mmcblk0` in 1 GiB chunks with `dd`, compresses on the KVM with `zstd -1` at `nice 19`
  so the KVM stays responsive,
- verifies each chunk's decompressed size and skips finished chunks, so a dropped Wi-Fi/SSH
  connection only costs the current chunk,
- saves `fdisk -l` next to the image.

Restore by putting the card in a USB reader on the Mac:
`cat part-*.zst | zstd -dc | sudo dd of=/dev/rdiskN bs=4m` (check `N` with `diskutil list`!).
It's a live copy of a mounted filesystem, so it's good for disaster recovery, not forensics.

## 3. Wiring the OLED

A 0.96" SSD1306 module has four pins: VCC, GND, SCL, SDA. On the NanoKVM Lite the **3V3 pad is
covered by the HDMI board**, so this build powers the OLED from a GPIO driven HIGH:

| OLED | Pad | Notes |
| :--- | :--- | :--- |
| VCC | **GPIOA16** | Linux GPIO number 496 (bank A starts at 480). Set to output HIGH = 3.3 V |
| GND | GND | |
| SCL | **GPIOP22** | pin-muxed to IIC3 SCL → `/dev/i2c-3` |
| SDA | **GPIOP23** | pin-muxed to IIC3 SDA |

Find the physical pads with the LicheeRV Nano pinout from the wiki above.

Why powering from a GPIO works: an SSD1306 module draws roughly 10–25 mA depending on how many
pixels are lit, and the module has its own regulator and charge pump. It's a hack: if the panel
browns out (resets, garbage on full-white frames), give it a real 3.3 V supply instead.

**Catch:** GPIOA16 is the **UART0 TX** pad. Once it's switched to GPIO, the serial console is gone
until the next reboot. SSH is unaffected.

## 4. Pin muxing with devmem

Every SG2002 pad has a function-select register in the pin-mux block at `0x03001000`. Writing a
function number routes the pad to that peripheral. No device-tree change or reboot needed:
[`oled-mario/oled-on.sh`](../oled-mario/oled-on.sh) does it at runtime:

```sh
[ -d /sys/class/gpio/gpio496 ] || echo 496 > /sys/class/gpio/export
echo high > /sys/class/gpio/gpio496/direction   # VCC on
devmem 0x03001040 32 0x3   # pad GPIOA16 -> function 3 = GPIO
devmem 0x030010E0 32 0x2   # pad GPIOP22 -> function 2 = IIC3_SCL
devmem 0x030010E4 32 0x2   # pad GPIOP23 -> function 2 = IIC3_SDA
```

Pin-mux settings are lost on reboot, so this runs at every boot (see section 7). Look up other pads
in the SG2002 TRM's "pinmux" table: the register is `0x03001000 + offset` and the value is the
function number listed for that pad.

## 5. Talking to the SSD1306 over I2C

Linux exposes the bus as `/dev/i2c-3`. Userspace opens it, picks the slave address, and every
`write()` becomes one I2C transaction. The SSD1306 protocol is just two kinds of writes:

- `0x00, cmd, cmd, ...`: control byte `0x00`, then commands
- `0x40, data, data, ...`: control byte `0x40`, then display RAM bytes

**Init sequence** (from [`oled_mario.cpp`](../oled-mario/oled_mario.cpp) `oledInit()`):

```
AE           display off
D5 80        clock divide
A8 3F        multiplex = 64 rows
D3 00        no display offset
40           start line 0
8D 14        charge pump ON (needed when the module has no external VCC boost)
20 00        horizontal addressing mode: RAM pointer wraps column → page automatically
A1 C8        flip X and Y (normal orientation for most modules)
DA 12        COM pins for 128x64
81 CF        contrast
D9 F1        pre-charge
DB 40        VCOMH
A4 A6        show RAM, not inverted
AF           display on
```

**Frame format:** 128x64 = 8 *pages* of 8 rows. Each byte is one column of 8 pixels in a page,
**bit 0 = top pixel**. A full frame is 8 × 128 = 1024 bytes, page 0 first. To send a frame: set the
window `21 00 7F` (columns 0–127) `22 00 07` (pages 0–7), then write `0x40` + 1024 bytes in one go.

Packing a 1-byte-per-pixel image into that layout (C++, from `oled_mario.cpp`):

```cpp
for (int p = 0; p < 8; p++)
    for (int x = 0; x < 128; x++) {
        uint8_t b = 0;
        for (int k = 0; k < 8; k++) b |= px[(p * 8 + k) * 128 + x] << k;
        frame[p * 128 + x] = b;
    }
```

**Speed:** a frame is ~1 KB on the wire, so at 400 kHz I2C a full refresh takes roughly 25 ms. That's
enough for 30 fps if each frame is sent as a single write (oled_video holds 30.0 fps with no dropped
frames on the tested NanoKVM Lite). If the bus runs at 100 kHz, expect ~10 fps.
`oled_mario test` prints how long one frame push takes on your board.

**SH1106 (1.3") panels** have 132 columns of RAM and no horizontal addressing mode: write one page
at a time (`B0+page, 02, 10`, then 128 data bytes). Both programs support this with `--sh1106`.

## 6. Cross-compiling for the KVM

No toolchain on the device, no Docker: [Zig](https://ziglang.org) ships a clang that cross-compiles
C/C++ for any target with its own musl libc, so one `brew install zig` is enough:

```sh
zig c++ -target riscv64-linux-musl -std=c++17 -O2 -static -s -fno-exceptions -fno-rtti \
    oled_video.cpp -o oled_video
scp oled_video root@<kvm-ip>:/root/
```

- `-static`: no dependency on the KVM's libc, so the binary runs on any firmware version.
- `-s -fno-exceptions -fno-rtti`: small binaries (oled_video ≈ 37 KB, oled_mario ≈ 140 KB with all its art).
- Each example has a `build.sh` (`./build.sh deploy` also copies the files to `$KVM`).

The Mario game logic has no hardware dependencies (`game_init / game_tick / game_render` into a
240x160 RGB565 buffer). That's why the same engine runs on an ESP32 and here, and why `preview.cpp`
can render OLED frames on the Mac for screenshots without hardware.

## 7. Starting at boot

Buildroot runs `/etc/init.d/S??*` in order at boot. [`oled-mario/S99oledmario`](../oled-mario/S99oledmario):

```sh
start() {
    /root/oled-on.sh
    sleep 1        # give the OLED a moment after its VCC pin goes high, before the init sequence
    start-stop-daemon -S -b -m -p "$PIDFILE" -x /root/oled_mario -- mario
}
stop() {
    start-stop-daemon -K -q -p "$PIDFILE"; rm -f "$PIDFILE"
    sleep 1; /root/oled_mario clear >/dev/null 2>&1
}
```

`-b` backgrounds it, `-m -p` writes a pid file so `stop` (and `oled-video/play.sh`) can find it.
The program handles SIGTERM by blanking the panel, so stopping never leaves a frozen image.

## 8. Turning video into OLED frames

The KVM can't decode video in ~50 MB of RAM without ffmpeg, so the work is split:

```mermaid
flowchart LR
    A[YouTube URL / video file] -->|yt-dlp| B[mp4 on the Mac]
    B -->|convert.py: OpenCV decode, grey, resize, dither, pack| C[.oledv file<br/>1 KB per frame]
    C -->|ssh stdin stream, ~30 KB/s| D[oled_video on the KVM]
    D -->|/dev/i2c-3| E[SSD1306]
```

[`oled-video/convert.py`](../oled-video/convert.py), per frame:

1. **Resample** to the target fps (default 30) by picking source frames on a time grid.
2. **Fit** to 128x64: letterbox (default) or `--fill` to crop to 2:1.
3. **Levels**: video white is ~235, not 255. Stretch 16–235 to 0–255, otherwise flat white areas
   get speckled by the dither.
4. **`--enhance`** (for busy footage like games): CLAHE local contrast + unsharp mask, so shapes survive
   the drop to 1 bit; pure blacks are kept black so CLAHE doesn't dot them.
5. **Dither** with an 8x8 **Bayer** matrix. Ordered dithering gives the same pattern for the same
   brightness every frame, so video doesn't shimmer. Error diffusion (Floyd–Steinberg) shows more
   detail in a still frame but crawls when the image moves.
6. **Pack** into SSD1306 page order (section 5) so the device does zero processing.

**`.oledv` format:** 12-byte header `"OLV1"`, `u8 fps`, `u8 rows (64)`, `u16 0`, `u32 frame count`
(little endian), then 1024-byte frames back to back. A 3.5-minute video at 30 fps ≈ 6.7 MB.

[`oled-video/oled_video.cpp`](../oled-video/oled_video.cpp) plays a file or stdin on a **wall clock**:
frame *i* is due at `t0 + i/fps`. If it's already more than a frame late, it's skipped, so playback
stays in real time even if the bus or SSH can't keep up. [`play.sh`](../oled-video/play.sh) pipes the
file through `ssh` and pauses the Mario service while the video runs. Ctrl-C closes the stream,
the player sees EOF, and Mario comes back.

## 9. Troubleshooting

| Symptom | Fix |
| :--- | :--- |
| `NO ACK` | Run `/root/oled-on.sh` first (pin-mux resets on boot). Check VCC/GND, swap SDA/SCL, try `--addr 0x3d` |
| Image shifted 2 px / noise column on the right | It's an SH1106 panel: add `--sh1106` |
| Image upside down / mirrored | Change `A1 C8` to `A0 C0` in the init sequence |
| Panel resets on bright frames | GPIO power is too weak for your module. Use a real 3.3 V supply |
| Serial console died | Expected: GPIOA16 (UART0 TX) is the OLED's VCC. Reboot to get it back |
| `/dev/i2c-3` missing | Your firmware's device tree doesn't enable I2C3. Pick a bus that exists (`ls /dev/i2c-*`) and mux its pads instead |
| Video plays slowly / drops frames | Look at `/tmp/oled_video.log` on the KVM. Lower `--fps` when converting |

## 10. Using this in your own project

The I2C part needs no libraries at all. Minimal Python version for the KVM's built-in Python
(same bytes as the C++ code: init, set the window, send one frame):

```python
import fcntl, os
I2C_SLAVE_FORCE = 0x0706
fd = os.open("/dev/i2c-3", os.O_RDWR)
fcntl.ioctl(fd, I2C_SLAVE_FORCE, 0x3C)
def cmd(*c): os.write(fd, bytes((0x00,) + c))

cmd(0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00,
    0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF)
cmd(0x21, 0, 127, 0x22, 0, 7)
frame = bytearray(1024)                      # 8 pages x 128 columns, bit 0 = top pixel
for x in range(128): frame[3 * 128 + x] = 0x18   # a 2-px line across the middle
os.write(fd, b"\x40" + frame)
```

Ideas that reuse the same pieces: a KVM status screen (IP, CPU temp, HDMI signal), Tailscale/network
indicator, notifications pushed over SSH (`ssh kvm 'oled_video -' < message.oledv`), or any
hardware-agnostic game engine rendered into a buffer and dithered with `mono.h`.

# cyd-cloud-game: the NanoKVM runs the game, an ESP32 display plays it over Wi-Fi

<img src="docs/screens.png" alt="Flappy tribute: title, gameplay and game over, as streamed to the CYD" width="100%">

"Cloud gaming" at desk scale: a Flappy Bird tribute runs on the Sipeed NanoKVM (1 GHz RISC-V), and
an ESP32 "Cheap Yellow Display" (ESP32-035, 480x320) is a thin client. It shows the frames and sends
back a single button: a fingertip over its light sensor (or a tap on the screen).

```
            ┌──────── NanoKVM ────────┐                      ┌──────── CYD (ESP32) ────────┐
 button ──▶ │ game logic, 30 Hz tick   │ ◀── 'F' held presses │ light sensor / touch         │
            │ render 240x160, 27 cols  │                      │ decode, diff against screen  │
            │ run-length code ~5 KB    │ ── sfx + frame ────▶ │ push changed stretches 2x    │
            └──────────────────────────┘      (Wi-Fi/TCP)     │ beep + LED on score / hit    │
                                                              └──────────────────────────────┘
```

Measured: **25-30 fps** on the CYD, ~5 KB per frame, the KVM spends ~1-2 ms per frame (≈9 % CPU),
and a flap reaches the screen in roughly 50-70 ms.

The CYD side is the
[cloud-game example in the ESP32-035 CYD repo](https://github.com/Novice130/ESP32-035-CYD-3.5-display/tree/main/examples/cloud-game).

## Files

| File | Runs on | Purpose |
| :--- | :--- | :--- |
| `flappy_server.cpp` | KVM (or Mac for previews) | the game, renderer, run-length coder and TCP server, one file |
| `build.sh` | Mac | cross-compiles with zig; `./build.sh deploy` copies the server and init script to `$KVM` |
| `S97flappy` | KVM | init script: runs the server on port 8766 at boot, high score in `/root/flappy_best` |

## Setup

```sh
export KVM=root@192.168.0.222
./build.sh deploy
ssh $KVM 'cp /root/S97flappy /etc/init.d/ && chmod +x /etc/init.d/S97flappy && /etc/init.d/S97flappy start'
```

Then flash the CYD's `cloud_game` env with your Wi-Fi and the KVM's IP in its `include/config.h`.

**Preview without hardware** (this is how the art was checked):

```sh
clang++ -std=c++17 -O2 flappy_server.cpp -o flappy_sim
mkdir -p out && ./flappy_sim --sim out 900 30     # a scripted run: title, autopilot play, crash, game over
```

It writes 2x PPM frames; any image viewer or `python -c` with Pillow/OpenCV turns them into PNGs.

## The game

- States: title (bobbing bird, high score) → play → crash flash → game over panel (score, best, NEW)
  → title. Idle on the title for 6 s and an autopilot demo plays (silent); any press takes over.
- Physics per 30 Hz tick: gravity 0.36 px, flap sets −3.7 px, pipes move 1.5 px, gap 50 px.
  All tunable at the top of the file.
- The game runs on its own clock, so a slow client gets fewer frames, never a slower game.
  A client that stalls doesn't fast-forward it either (at most 4 ticks are caught up per frame).
- All art is drawn from shapes in code (sky bands, clouds, city, bushes, striped pipes, a bird built
  from ellipses with an auto outline, a 5x7 font). It's a fan tribute: no original assets.

## Protocol

TCP, little endian. Any game can use it; the CYD client doesn't know it's Flappy.

```
client → "HI1\0"
server → "CGM1", u16 width, u16 height, u16 n, n × u16 RGB565 palette
loop:
client → 'F', u8 held, u16 presses        (presses = running count of button presses)
server → u32 sfx bits, u32 len, len bytes (frame)
```

- Frame = width × height palette indices, run-length coded: byte `b < 128` → next byte repeated
  `b + 1` times; `b ≥ 128` → `b − 127` literal bytes follow. Flat pixel art compresses ~7x, and
  unlike JPEG the edges stay crisp.
- Sound bits: 1 flap, 2 score, 4 hit, 8 die, 16 swoosh. The client turns them into beeps and LED flashes.
- Sending a running press count (not "pressed now") means a quick tap between two frames is never lost.
- One player at a time; a dead connection is dropped after 5 s.

## Write your own game for it

Keep the `serve()` loop and the run-length coder, replace `tick()` / `render()` / `press()`:
anything that draws into a 240x160 (or smaller) palette framebuffer works, up to 256 colours.
Good one-button candidates: Chrome-Dino-style runner, Helicopter/Jetpack (use `held`), Stack.

Flappy Bird is a trademark of its owner; this is an unaffiliated tribute.

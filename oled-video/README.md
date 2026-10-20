# oled-video: play YouTube (or any video) on the NanoKVM's OLED

Turns any video into a dithered 1-bit 128x64 stream and plays it on the SSD1306 OLED wired to a
Sipeed NanoKVM Lite at up to 30 fps. No sound (the KVM has no speaker).

Measured on a NanoKVM Lite while streaming a 13-minute video: **30 fps, 0 dropped frames**, ~8% CPU,
under 200 KB of RAM, and the KVM web UI keeps working.

The KVM can't decode video in ~50 MB of free RAM, so the Mac does the heavy lifting and the KVM only
pushes 1 KB frames to I2C:

```
YouTube URL ──yt-dlp──▶ mp4 ──convert.py──▶ .oledv (1 KB/frame) ──ssh──▶ oled_video ──I2C──▶ OLED
                 (Mac)          (Mac: OpenCV)                              (KVM)
```

## Files

| File | Runs on | Purpose |
| :--- | :--- | :--- |
| `play.sh` | Mac | one command: download → convert → stream, pauses the Mario service while playing |
| `convert.py` | Mac | video → `.oledv` (resample, fit, level stretch, optional enhance, Bayer dither, pack) |
| `oled_video.cpp` | KVM | plays a `.oledv` file or stdin on a wall clock, dropping frames if late |
| `build.sh` | Mac | cross-compiles the player with zig; `./build.sh deploy` copies it to the KVM |
| `requirements.txt` | Mac | yt-dlp, OpenCV, numpy |

## Setup (once)

```sh
brew install zig
cd oled-video
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt   # everything stays in this folder
export KVM=root@192.168.0.222                                       # your NanoKVM
```

The OLED must already work: wiring and `oled-on.sh` from [../oled-mario](../oled-mario/README.md).

## Play

```sh
./play.sh "https://www.youtube.com/watch?v=..."            # download, convert, stream
./play.sh "https://www.youtube.com/watch?v=..." --fill --enhance   # game/vlog footage: crop to fill + boost contrast
./play.sh videos/some-file.mp4                              # local file
```

Downloads and converted files go to `videos/` (git-ignored). A contact sheet `*-preview.png` is
written next to each conversion so you can judge the look before it plays. Re-running with the same
file and no options reuses the existing `.oledv`. Ctrl-C stops playback, and Mario resumes if it was running.

`convert.py` options (also accepted by `play.sh` after the source):

| Option | Effect |
| :--- | :--- |
| `--fill` | crop to 2:1 to fill the screen (default: letterbox with black bars) |
| `--enhance` | CLAHE local contrast + sharpening: use it for busy, mid-tone footage (games, vlogs). Leave it off for clean black-and-white video |
| `--dither none` | hard threshold instead of Bayer dithering |
| `--invert` | swap black and white |
| `--fps N` | output frame rate (default 30, never above the source) |

### Play without the Mac

Copy the file to the KVM and play it there (6.7 MB for 3.5 minutes at 30 fps; `/` has GBs free):

```sh
scp videos/clip.oledv $KVM:/root/
ssh $KVM '/etc/init.d/S99oledmario stop; /root/oled_video /root/clip.oledv --loop'
```

Player options: `--loop` (files only), `--sh1106`, `--addr 0x3d`, `--bus N`.

## How it works

The conversion steps, why ordered (Bayer) dithering instead of error diffusion, the `.oledv` file
format, and the wall-clock player are explained in [../docs/GUIDE.md](../docs/GUIDE.md#8-turning-video-into-oled-frames).

## Notes

- Many YouTube videos (official channels especially) only download with a JavaScript runtime:
  without one yt-dlp says *"This video is not available"* or *"Requested format is not available"*.
  Install [Node.js](https://nodejs.org) or [deno](https://github.com/yt-dlp/yt-dlp/wiki/EJS);
  `play.sh` passes `--js-runtimes node` automatically when `node` is on your PATH, and
  `requirements.txt` includes the `yt-dlp-ejs` helper it needs.
- YouTube changes often. If downloads fail, update yt-dlp: `.venv/bin/pip install -U yt-dlp yt-dlp-ejs`.
- Only download and play videos you have the right to use.

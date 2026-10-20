#!/usr/bin/env python3
# Converts any video into a 1-bit 128x64 stream for the SSD1306 OLED on the NanoKVM.
#
#   convert.py IN.mp4 OUT.oledv [--fps 30] [--fill] [--dither bayer|none] [--invert] [--enhance] [--preview OUT.png]
#
# --enhance boosts local contrast + sharpens before dithering: needed for busy, mid-tone footage
# (games, vlogs); leave it off for clean black/white video like Bad Apple.
#
# OUT.oledv: 12-byte header ("OLV1", u8 fps, u8 rows, u16 0, u32 frame count, little endian)
# then 1024-byte frames already in SSD1306 page order (8 pages x 128 columns, LSB = top row),
# so the player on the KVM just writes them to I2C.
import argparse
import struct
import sys

import cv2
import numpy as np

W, H = 128, 64

# 8x8 Bayer matrix: ordered dithering stays put from frame to frame, so video doesn't shimmer
BAYER = np.array([[0, 32, 8, 40, 2, 34, 10, 42], [48, 16, 56, 24, 50, 18, 58, 26],
                  [12, 44, 4, 36, 14, 46, 6, 38], [60, 28, 52, 20, 62, 30, 54, 22],
                  [3, 35, 11, 43, 1, 33, 9, 41], [51, 19, 59, 27, 49, 17, 57, 25],
                  [15, 47, 7, 39, 13, 45, 5, 37], [63, 31, 55, 23, 61, 29, 53, 21]])
THRESH = np.tile((BAYER + 0.5) * 4, (H // 8, W // 8))  # 0..255
CLAHE = cv2.createCLAHE(clipLimit=3.0, tileGridSize=(4, 4))


def to_mono(frame, fill, dither, invert, enhance):
    g = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    h, w = g.shape
    if fill:  # crop to 2:1, no bars
        if w / h > W / H:
            cw = h * W // H
            g = g[:, (w - cw) // 2:(w - cw) // 2 + cw]
        else:
            ch = w * H // W
            g = g[(h - ch) // 2:(h - ch) // 2 + ch, :]
        img = cv2.resize(g, (W, H), interpolation=cv2.INTER_AREA)
    else:  # letterbox: whole picture, black bars at the sides
        s = min(W / w, H / h)
        nw, nh = max(1, round(w * s)), max(1, round(h * s))
        img = np.zeros((H, W), np.uint8)
        x0, y0 = (W - nw) // 2, (H - nh) // 2
        img[y0:y0 + nh, x0:x0 + nw] = cv2.resize(g, (nw, nh), interpolation=cv2.INTER_AREA)
    # video white is ~235, not 255: stretch 16..235 to full range or flat whites get dotted
    img = np.clip((img.astype(np.int32) - 16) * 255 // 219, 0, 255).astype(np.uint8)
    if enhance:  # local contrast, then unsharp mask so edges survive 1-bit
        black = img < 10  # CLAHE lifts flat black into a dot grid: keep it black
        img = CLAHE.apply(img)
        img = cv2.addWeighted(img, 1.8, cv2.GaussianBlur(img, (0, 0), 1.2), -0.8, 0)
        img[black] = 0
    if invert:
        img = 255 - img
    return img > (THRESH if dither == "bayer" else 127)


def pack(bits):
    # (64,128) bool -> SSD1306 pages: byte[p*128+x] bit k = row p*8+k
    pages = bits.reshape(H // 8, 8, W).astype(np.uint8)
    return (pages << np.arange(8, dtype=np.uint8)[None, :, None]).sum(axis=1, dtype=np.uint8).tobytes()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("out")
    ap.add_argument("--fps", type=float, default=30)
    ap.add_argument("--fill", action="store_true", help="crop to fill the screen instead of letterboxing")
    ap.add_argument("--dither", choices=["bayer", "none"], default="bayer")
    ap.add_argument("--invert", action="store_true")
    ap.add_argument("--enhance", action="store_true", help="local contrast + sharpen, for busy footage")
    ap.add_argument("--preview", help="write a contact sheet PNG of 16 frames")
    a = ap.parse_args()

    cap = cv2.VideoCapture(a.src)
    if not cap.isOpened():
        sys.exit(f"cannot open {a.src}")
    src_fps = cap.get(cv2.CAP_PROP_FPS) or 30
    fps = min(a.fps, src_fps)
    total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))

    frames, shots = [], []
    i, next_t = 0, 0.0
    while True:
        ok, f = cap.read()
        if not ok:
            break
        if i / src_fps + 1e-6 >= next_t:  # resample to the target fps
            bits = to_mono(f, a.fill, a.dither, a.invert, a.enhance)
            frames.append(pack(bits))
            shots.append(bits)
            next_t += 1 / fps
        i += 1
        if i % 300 == 0:
            print(f"\r{i}/{total} source frames", end="", file=sys.stderr)
    print(f"\r{len(frames)} frames at {fps:g} fps ({len(frames) / fps:.0f} s)", file=sys.stderr)

    with open(a.out, "wb") as o:
        o.write(b"OLV1" + struct.pack("<BBHI", round(fps), H, 0, len(frames)))
        o.writelines(frames)

    if a.preview and shots:
        pick = [shots[k * (len(shots) - 1) // 15] for k in range(16)]
        tiles = [np.pad(s.astype(np.uint8) * 255, 2, constant_values=80) for s in pick]
        rows = [np.hstack(tiles[r * 4:r * 4 + 4]) for r in range(4)]
        sheet = cv2.resize(np.vstack(rows), None, fx=3, fy=3, interpolation=cv2.INTER_NEAREST)
        cv2.imwrite(a.preview, sheet)


if __name__ == "__main__":
    main()

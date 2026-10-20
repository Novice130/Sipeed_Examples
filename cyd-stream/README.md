# cyd-stream: the NanoKVM serves whole videos to an ESP32 display over Wi-Fi

The ESP32 "Cheap Yellow Display" (ESP32-035, 480x320) has only ~3 MB of flash for video, which is
about 50 seconds at good quality. Here the full video lives on the Sipeed NanoKVM instead, and the
CYD becomes a Wi-Fi display: it fetches each frame and chunk of audio just before it needs it.

```
video ──make_clip.py──▶ clip.bin (JPEG frames + 8-bit audio) ──scp──▶ NanoKVM /root/videos/stream.bin
                (Mac)                                                       │ clip_server :8765
                                                                            ▼
                                       CYD video-clip player ◀── Wi-Fi: "give me bytes X..Y"
```

- **Any length, better quality:** a 4-minute clip at JPEG quality 70 is ~23 MB; the KVM has GBs free.
- **The CYD stays in charge of time:** it plays on its own clock and asks for the byte ranges it
  needs, so pause, the light-sensor skip and looping work exactly as with a flash clip.
- **Tiny on the KVM:** a 39 KB static binary, one forked process per connection (the CYD uses two:
  video and audio), a few hundred KB of RAM, idle unless the CYD is asking.
- **Falls back:** if the CYD can't reach Wi-Fi or the server, it plays the clip in its own flash.

The CYD side (firmware, converter, Wi-Fi config) is the
[video-clip example in the ESP32-035 CYD repo](https://github.com/Novice130/ESP32-035-CYD-3.5-display/tree/main/examples/video-clip).

## Files

| File | Runs on | Purpose |
| :--- | :--- | :--- |
| `clip_server.cpp` | KVM | serves byte ranges of one clip file over TCP |
| `build.sh` | Mac | cross-compiles with zig; `./build.sh deploy` copies the server and init script to `$KVM` |
| `S98clipserver` | KVM | init script: serves `/root/videos/stream.bin` on port 8765 at boot |

## Setup

```sh
export KVM=root@192.168.0.222
./build.sh deploy
ssh $KVM 'cp /root/S98clipserver /etc/init.d/ && chmod +x /etc/init.d/S98clipserver && mkdir -p /root/videos'

# make the clip with the CYD repo's converter (any length; --budget lifts the flash limit)
make_clip.py my_video.mp4 --quality 70 --budget 0x10000000 -o videos/my_video.bin
scp videos/my_video.bin $KVM:/root/videos/stream.bin
ssh $KVM /etc/init.d/S98clipserver restart
```

Then on the CYD: copy `include/config.example.h` to `include/config.h`, fill in your 2.4 GHz Wi-Fi
name and password and the KVM's IP (`STREAM_HOST`), build and flash the `video_clip` env. If it
can't join, the serial log lists every network it can see.

Measured: the server answers a random read in ~7 ms (median) over the LAN; one frame is ~5.6 KB at
quality 70, so 15 fps needs only ~90 KB/s plus 8 KB/s of audio.

## Protocol

TCP, little endian. The client sends `u32 offset, u32 length` and gets `u32 n` followed by `n` bytes
of the file (`n < length` only at the end of the file). `length = 0` asks for the file size, returned
in `n` with no data. A connection that sends nothing for 60 s is closed, and the player reconnects
on its next read. Because the server only hands out byte ranges, it doesn't need to understand the
clip format at all: any client that wants random access to a file over Wi-Fi can use it.

## Notes

- `videos/` and the built `clip_server` are git-ignored. Only stream videos you have the right to use.
- Change the video: copy another clip to `/root/videos/stream.bin` and `S98clipserver restart`.
  Restart the CYD too, since it reads the clip's header and frame index once at boot.

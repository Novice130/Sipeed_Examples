#!/bin/sh
# Play a YouTube link (or a local video file) on the NanoKVM's OLED.
#
#   ./play.sh <youtube-url|video-file> [convert.py opts, e.g. --fill --fps 20 --dither none]
#
# Downloads go to ./videos, the converted .oledv lands next to it, then the frames are
# streamed over ssh. Mario is paused while the video plays and comes back afterwards.
# Ctrl-C stops playback.
set -e
cd "$(dirname "$0")"
KVM="${KVM:-root@192.168.0.222}"
SRC="$1"
[ -n "$SRC" ] || { sed -n '2,8p' "$0"; exit 1; }
shift

case "$SRC" in
http*)
    SRC=$(.venv/bin/yt-dlp --cache-dir .cache/yt-dlp --no-playlist --no-progress \
        -f "bv*[height<=360][ext=mp4]/bv*[height<=360]/b" -P videos -o "%(id)s.%(ext)s" \
        --print after_move:filepath "$SRC")
    ;;
esac

OUT="${SRC%.*}.oledv"
[ -f "$OUT" ] && [ $# -eq 0 ] || .venv/bin/python -I convert.py "$SRC" "$OUT" --preview "${SRC%.*}-preview.png" "$@"

[ -x oled_video ] || ./build.sh
ssh "$KVM" '[ -x /root/oled_video ]' || scp oled_video "$KVM":/root/

echo "playing $OUT on $KVM (Ctrl-C to stop)"
ssh "$KVM" '
P=/var/run/oled_mario.pid
if [ -f $P ] && kill -0 $(cat $P) 2>/dev/null; then MARIO=1; /etc/init.d/S99oledmario stop >/dev/null 2>&1; fi
/root/oled_video - >/tmp/oled_video.log 2>&1
[ -n "$MARIO" ] && /etc/init.d/S99oledmario start >/dev/null 2>&1
true' < "$OUT"

#!/bin/bash
# Chunked, resumable backup of the NanoKVM's system partitions (MBR + /boot + /) over SSH,
# taken while the KVM keeps running. Run it before hacking on the device.
#
#   tools/backup-kvm.sh [OUT_DIR]        default OUT_DIR: backups/<today>
#   KVM=root@<ip> tools/backup-kvm.sh    default KVM: root@192.168.0.222
#
# Re-run safely: finished chunks are verified and skipped. Needs zstd on the Mac (brew install zstd);
# the NanoKVM image already ships zstd.
# Restore (card in a USB reader on the Mac, check N with `diskutil list` first!):
#   cat part-*.zst | zstd -dc | sudo dd of=/dev/rdiskN bs=4m
KVM="${KVM:-root@192.168.0.222}"
OUT="${1:-$(dirname "$0")/../backups/$(date +%F)}"
mkdir -p "$OUT" && cd "$OUT" || exit 1

# everything up to the end of partition 2 (the root fs); the big exFAT data partition is skipped
TOTAL=$(ssh "$KVM" 'P=/sys/block/mmcblk0/mmcblk0p2; echo $(( ($(cat $P/start) + $(cat $P/size)) / 2048 ))') || exit 1
CH=1024  # MiB per chunk
echo "backing up ${TOTAL} MiB from $KVM into $PWD"
ssh "$KVM" 'fdisk -l /dev/mmcblk0' > partition-info.txt 2>&1

for ((i = 0; i * CH < TOTAL; i++)); do
  f=$(printf "part-%d.zst" "$i")
  cnt=$CH; [ $(( (i+1)*CH )) -gt "$TOTAL" ] && cnt=$(( TOTAL - i*CH ))
  want=$(( cnt * 1048576 ))
  if [ -f "$f" ] && [ "$(zstd -dc "$f" 2>/dev/null | wc -c | tr -d ' ')" = "$want" ]; then
    echo "$f ok (skip)"; continue
  fi
  echo "$f: reading ${cnt}MiB from offset $((i*CH))MiB"
  ssh -o ServerAliveInterval=15 "$KVM" \
    "sync; nice -n 19 dd if=/dev/mmcblk0 bs=1M skip=$((i*CH)) count=$cnt 2>/dev/null | nice -n 19 zstd -1 -T1 -c" > "$f.tmp" \
    && [ "$(zstd -dc "$f.tmp" | wc -c | tr -d ' ')" = "$want" ] && mv "$f.tmp" "$f" && echo "$f done" || { echo "$f FAILED"; exit 1; }
done
echo "ALL CHUNKS DONE"

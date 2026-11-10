#!/bin/sh
# Replay every stage against fresh MAME recordings and print one summary line
# per window. Takes roughly half an hour and about 1 GB of scratch disk.
# Usage: tools/ref/verify_all.sh [stage-index ...]     (default: all six)
root=$(cd "$(dirname "$0")/../.." && pwd); cd "$root" || exit 1
make -s -f tools/ref/build.mk >/dev/null 2>&1 || { echo "build failed"; exit 1; }
AIM='aim={[0x7c690]=true,[0x7b572]=true,[0x7b61e]=true,[0x7b64e]=true,[0x7b696]=true,[0x7b6ef]=true,[0x7bb61]=true,[0x7b050]={pri=3},[0x7b2a4]={pri=3},[0x7b1ff]={pri=3},[0x7af56]={pri=5}}'
run() { # stage start end options
  rec=".local/ref/verify-$1-$2"
  tools/ref/run_capture.sh "$rec" "start=$2, frames=$3, refill=true$4" >/dev/null 2>&1
  printf 'stage %s frames %s-%s: ' "$1" "$2" "$3"
  .local/bin/check_replay "$rec" --keep-going --quiet | tail -1 | sed 's/^[^:]*: //'
  .local/bin/check_replay "$rec" --keep-going --quiet | grep '^missing handler' | sed 's/^/    /'
  rm -rf "$rec"
}
for s in ${@:-0 1 2 3 4 5}; do case $s in
0) run 0 1400 7400 ""; run 0 7400 13300 "" ;;
1) o=", stage=1, unstick=6"; run 1 1400 7400 "$o"; run 1 7400 13400 "$o"; run 1 13400 16200 "$o" ;;
2) o=", stage=2, track_kind=0x43, surface=true"; run 2 1400 5700 "$o"; run 2 5600 10450 "$o" ;;
3) o=", stage=3, follow=true"; run 3 1400 7400 "$o"; run 3 7400 13400 "$o"; run 3 13400 19400 "$o"; run 3 19400 22900 "$o"
   run 3 22900 27400 "$o, steer_from=22900, steer_x=0xc0" ;;
4) o=", stage=4, climb=true, climb_period=400"; run 4 1400 6200 "$o"; run 4 6200 11000 "$o"; run 4 11000 15800 "$o" ;;
5) o=", stage=5, $AIM"; run 5 1400 7400 "$o"; run 5 7400 13400 "$o"; run 5 13400 19400 "$o"; run 5 19400 26450 "$o"; run 5 26200 31950 "$o" ;;
esac; done

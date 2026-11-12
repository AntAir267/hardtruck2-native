#!/bin/bash
# Baselines for a change that should not move anything (a toolkit move, a runtime refactor):
# the compare_render.sh scenes through both renderers, each with per-frame memory hashes (--hash)
# and presents dumped at fixed frames, plus the race-start run the translator check uses.
# Deterministic pixels need lavapipe (t7 run --vk-software). Writes OUT (default work/baseline):
# <renderer>_<scene>/{hash.txt,present_*.ppm}, race.hash, sums.txt (one line per hash file and
# dump: what two baselines are compared by).
#   tools/dev/baseline.sh [OUT] [BINARY]
OUT=${1:-work/baseline}; BIN=${2:-build/ht2}
G=${GAME:-$HOME/.local/share/hardtruck2-native/game}
rm -rf $OUT; mkdir -p $OUT
scenes=(
  "menus|900|300,420,600,800|200:ESCAPE*4,300:UP*3,330:UP*3,360:UP*3,390:UP*3,420:RETURN*3,600:UP*3,640:RETURN*3"
  "yard|2000|1500,1700,1900|200:ESCAPE*4,300:RETURN*4,400:RETURN*4"
  "drive|2600|1900,2100,2300,2500|200:ESCAPE*4,300:RETURN*4,400:RETURN*4,1500:SPACE*8,1700:UP*250,2100:DELETE*200"
)
run() {   # renderer, scene line
    IFS='|' read -r name frames dumps keys <<< "$2"
    local d=$OUT/$1_$name; mkdir -p $d/user
    HT2_WIDESCREEN=1 HT2_GPU_OFFSCREEN=1 HT2_GPU_OFFSCREEN_SIZE=1280x720 HT2_DUMP_PRESENT=1 \
    HT2_DUMP_FRAMES=$d HT2_DUMP_LIST=$dumps HT2_KEYS="$keys" \
        $BIN --test --user $d/user --option "renderer=$1" --frames $frames --logfile $d/log.txt \
        --hash $d/hash.txt > /dev/null 2>&1
    echo "$1 $name exit $?" >> $OUT/exits.txt
}
for sc in "${scenes[@]}"; do run original "$sc" & run native "$sc" & wait; done
mkdir -p $OUT/race_user; cp -r work/seed/. $OUT/race_user/
HT2_KEYS="200:ESCAPE*4,300:RETURN*4,400:RETURN*4,1500:SPACE*4,1560:UP*200" $BIN --game $G --user $OUT/race_user \
    --test --frames 1800 --logfile $OUT/race.log --hash $OUT/race.hash > /dev/null 2>&1
echo "race exit $?" >> $OUT/exits.txt
(cd $OUT && md5sum race.hash */hash.txt */present_*.ppm) > $OUT/sums.txt

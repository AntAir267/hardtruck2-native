#!/bin/bash
# The full check, spread over the farm (run here, on the laptop; ~/DevWork/Games/tools/t7): the
# function check over every function in SHARDS shared CPU jobs (--shard K/N; the helper places
# them on the T7, Lappy, Penguin or the studio PC), and one graphics job with the 1800-frame
# lockstep race start and the frame dumps of tools/dev/baseline.sh (lavapipe). Writes
# work/full_check.txt: the shards' FAIL functions against tests/fncheck_baseline.txt, lockstep,
# the dumps against work/baseline_t7 (master's baselines before the toolkit move).
#   tools/dev/full_check.sh [SHARDS]
N=${1:-4}; T7=$HOME/DevWork/Games/tools/t7; OUT=work/full_check.txt; mkdir -p work; : > $OUT
start=$(date +%s)
for k in $(seq 0 $((N - 1))); do
    $T7 run hardtruck2-native --wait --timeout 240 --share --back work/fc_shard_$k.log -- bash -c \
        "make JOBS=\$T7_JOBS build/fncheck > work/fc_build_$k.log 2>&1 && HT2_NATIVE=0 build/fncheck --shard $k/$N > work/fc_shard_$k.log 2>&1" \
        > work/full_shard_$k.out 2>&1 &
done
$T7 run hardtruck2-native --wait --timeout 240 --share-gpu --vk-software --back work/full_gpu --back work/lockstep_full.txt -- bash -c \
    'mkdir -p work/seed && G=$HOME/.local/share/hardtruck2-native/game && make JOBS=$T7_JOBS build/ht2 build/ht2_ref > work/full_gpu_build.log 2>&1 &&
     sed -e "s/^fullscreen=off/fullscreen=on/" -e "s/^d3d=off/d3d=on/" $G/TRUCK.INI > work/seed/TRUCK.INI && rm -f work/seed/options.txt &&
     .venv/bin/python tools/lockstep.py --game $G --seed work/seed --frames 1800 --no-bisect --keys "200:ESCAPE*4,300:RETURN*4,400:RETURN*4,1500:SPACE*4,1560:UP*200" 2>&1 | tail -1 > work/lockstep_full.txt;
     tools/dev/baseline.sh work/full_gpu build/ht2' > work/full_gpu.out 2>&1 &
wait
res() { grep -o "work/t7/[0-9-]*" "$1" | tail -1; }
: > work/fc_full_fails.txt; runs=""
for k in $(seq 0 $((N - 1))); do
    d=$(res work/full_shard_$k.out); f=$d/work/fc_shard_$k.log
    [ -f "$f" ] || { echo "shard $k: no result (work/full_shard_$k.out)" >> $OUT; continue; }
    echo "shard $k/$N: $(grep '^runs:' $f) [$(grep -o 'on [a-z0-9-]*,' $d/run.log | head -1)]" >> $OUT
    grep "^FAIL" $f | cut -c1-14 >> work/fc_full_fails.txt
done
sort -u -o work/fc_full_fails.txt work/fc_full_fails.txt
grep "^FAIL" tests/fncheck_baseline.txt | cut -c1-14 | sort > work/fc_base.txt
echo "fncheck FAIL functions not in the baseline: $(comm -23 work/fc_full_fails.txt work/fc_base.txt | tr '\n' ' ')" >> $OUT
d=$(res work/full_gpu.out)
echo "lockstep 1800: $(cat $d/work/lockstep_full.txt 2>/dev/null)" >> $OUT
same=0; diff=0
for f in $(cd work/baseline_t7 && ls */present_*.ppm); do
    if cmp -s work/baseline_t7/$f $d/work/full_gpu/$f; then same=$((same+1)); else diff=$((diff+1)); echo "  dump differs: $f" >> $OUT; fi
done
echo "frame dumps: $same identical to work/baseline_t7, $diff not (6 expected within rounding: DESIGN.md 14)" >> $OUT
echo "total: $(( $(date +%s) - start )) s" >> $OUT
cat $OUT

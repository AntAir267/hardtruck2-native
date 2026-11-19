#!/bin/bash
# The full check, in one T7 job that uses its threads (run here, on the laptop; the T7 has this
# project's game files and Python; the studio PC has neither): the function check over every
# function in SHARDS processes at once (--shard K/N), the 1800-frame lockstep race start beside
# them, then the frame dumps of tools/dev/baseline.sh (lavapipe). Writes work/full_check.txt: FAIL
# functions against tests/fncheck_baseline.txt, lockstep, the dumps against work/baseline_t7
# (master's baselines before the toolkit move, DESIGN.md 14).
#   tools/dev/full_check.sh [SHARDS]
N=${1:-6}; OUT=work/full_check.txt; mkdir -p work; : > $OUT
start=$(date +%s)
~/DevWork/Games/tools/t7 run hardtruck2-native --on t7 --wait --timeout 240 --share-gpu --vk-software \
    --back work/fc_full --back work/full_gpu --back work/lockstep_full.txt -- bash -c "
    G=\$HOME/.local/share/hardtruck2-native/game; mkdir -p work/seed work/fc_full
    make JOBS=\$T7_JOBS build/ht2 build/ht2_ref build/fncheck > work/full_build.log 2>&1 || exit 1
    sed -e 's/^fullscreen=off/fullscreen=on/' -e 's/^d3d=off/d3d=on/' \$G/TRUCK.INI > work/seed/TRUCK.INI; rm -f work/seed/options.txt
    for k in \$(seq 0 $((N - 1))); do HT2_NATIVE=0 build/fncheck --game \$G --user work/fc_full/u\$k --shard \$k/$N > work/fc_full/shard_\$k.log 2>&1 & done
    .venv/bin/python tools/lockstep.py --game \$G --seed work/seed --frames 1800 --no-bisect \
        --keys '200:ESCAPE*4,300:RETURN*4,400:RETURN*4,1500:SPACE*4,1560:UP*200' 2>&1 | tail -1 > work/lockstep_full.txt &
    wait
    tools/dev/baseline.sh work/full_gpu build/ht2" > work/full_check.out 2>&1
d=$(grep -o "work/t7/[0-9-]*" work/full_check.out | tail -1)
pass=0; fail=0
for f in $d/work/fc_full/shard_*.log; do
    p=$(grep -o "^runs: [0-9]* pass" $f | grep -o "[0-9]*"); x=$(grep -o "[0-9]* FAIL$" $f | grep -o "[0-9]*")
    pass=$((pass + ${p:-0})); fail=$((fail + ${x:-0}))
done
echo "fncheck: $pass pass, $fail FAIL in $(ls $d/work/fc_full/shard_*.log 2>/dev/null | wc -l) shards" >> $OUT
cat $d/work/fc_full/shard_*.log 2>/dev/null | grep "^FAIL" | cut -c1-14 | sort -u > work/fc_full_fails.txt
grep "^FAIL" tests/fncheck_baseline.txt | cut -c1-14 | sort > work/fc_base.txt
echo "fncheck FAIL functions not in the baseline: $(comm -23 work/fc_full_fails.txt work/fc_base.txt | tr '\n' ' ')" >> $OUT
echo "lockstep 1800: $(cat $d/work/lockstep_full.txt 2>/dev/null)" >> $OUT
same=0; diff=0
for f in $(cd work/baseline_t7 && ls */present_*.ppm); do
    if cmp -s work/baseline_t7/$f $d/work/full_gpu/$f; then same=$((same+1)); else diff=$((diff+1)); echo "  dump differs: $f" >> $OUT; fi
done
echo "frame dumps: $same identical to work/baseline_t7, $diff not (6 expected within rounding: DESIGN.md 14)" >> $OUT
echo "total: $(( $(date +%s) - start )) s (results: $d)" >> $OUT
cat $OUT

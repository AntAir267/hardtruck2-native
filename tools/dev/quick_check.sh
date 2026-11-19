#!/bin/bash
# The per-change check (a few minutes): the three builds, the function check on just the functions
# the change touches (ADDRS: a comma list of their addresses; none: skipped) and lockstep on a short
# route (boot, intro film and menus: 600 frames). Writes work/quick_check.txt. The full set
# (tools/dev/full_check.sh: every function, the 1800-frame race start, the frame dumps) runs before
# a merge, an install, a toolkit pin move or at a milestone (DESIGN.md "Checks").
#   tools/dev/quick_check.sh [ADDR,ADDR...]
# On the T7: t7 run hardtruck2-native --on t7 --share --back work/quick_check.txt -- tools/dev/quick_check.sh ADDRS
# (the T7 has the game files and the Python with Capstone and Unicorn; the studio PC has neither)
G=${GAME:-$HOME/.local/share/hardtruck2-native/game}; OUT=work/quick_check.txt; mkdir -p work; : > $OUT
start=$(date +%s)
make JOBS=${T7_JOBS:-${JOBS:-8}} build/ht2 build/ht2_ref build/fncheck > work/qc_build.log 2>&1 || { tail -20 work/qc_build.log >> $OUT; exit 1; }
echo "build: $(( $(date +%s) - start )) s" >> $OUT
if [ -n "$1" ]; then
    t=$(date +%s)
    HT2_NATIVE=0 build/fncheck --game $G --only "$1" > work/qc_fncheck.log 2>&1
    grep "^runs:\|^FAIL" work/qc_fncheck.log | cut -c1-160 >> $OUT
    grep "^FAIL" work/qc_fncheck.log | cut -c1-14 | sort > work/qc_fails.txt
    grep "^FAIL" tests/fncheck_baseline.txt | cut -c1-14 | sort > work/qc_base.txt
    echo "fncheck FAIL functions not in the baseline: $(comm -23 work/qc_fails.txt work/qc_base.txt | tr '\n' ' ') ($(( $(date +%s) - t )) s)" >> $OUT
fi
t=$(date +%s)
mkdir -p work/seed
sed -e "s/^fullscreen=off/fullscreen=on/" -e "s/^d3d=off/d3d=on/" $G/TRUCK.INI > work/seed/TRUCK.INI; rm -f work/seed/options.txt
r=$(timeout 900 .venv/bin/python tools/lockstep.py --game $G --seed work/seed --frames 600 --no-bisect \
    --keys "200:ESCAPE*4,300:RETURN*4,400:RETURN*4" 2>&1 | tail -1)
echo "lockstep 600: $r ($(( $(date +%s) - t )) s)" >> $OUT
echo "total: $(( $(date +%s) - start )) s" >> $OUT
cat $OUT

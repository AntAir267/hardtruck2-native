#!/bin/bash
# The AntiStatic move's checks on a worker (run in work/as through t7 --also): the libraries linked
# from the main checkout, the three builds, then translator_check.sh (fncheck, lockstep) and
# baseline.sh (scenes and frame dumps) into work/move_check.
M=$HOME/DevWork/hardtruck2-native
for e in $M/third_party/*; do b=$(basename $e); [ "$b" = README.md ] || [ "$b" = antistatic ] || [ -e third_party/$b ] || ln -s $e third_party/$b; done
[ -e .venv ] || ln -s $M/.venv .venv
mkdir -p work
JOBS=${T7_JOBS:-8} tools/dev/translator_check.sh
cp work/translator_check.txt work/tc_fncheck.log work/tc_fails.txt work/ 2>/dev/null
tools/dev/baseline.sh work/move_check build/ht2
cat work/translator_check.txt work/move_check/exits.txt

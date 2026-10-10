# Phase 16 step 4: the planner's CPU/CUDA choice before (generic kernel only,
# 9680b6b, in the worktree ~/MENOS-p16-base) and after (~/MENOS HEAD), three
# times each, alternating, so both see the same machine state. Calibration
# runs inside each process, so each line is a fresh calibration.
set -e
SRC=~/MENOS/docs/benchmarks/phase-16-gate/scripts/planner_choice.cpp.txt
for root in ~/MENOS-p16-base ~/MENOS; do
    cd $root
    B=build/cuda12-release
    cmake --preset cuda12-release > /dev/null
    cmake --build $B -j"$(nproc)" > /dev/null
    LIBS=$(find $B/src -name '*.a' | grep -v _deps | tr '\n' ' ')
    g++-13 -std=c++20 -O2 -pthread -Iinclude -Isrc -x c++ $SRC -x none \
        -Wl,--start-group $LIBS -Wl,--end-group -llapacke -llapack -lblas -lcudart \
        -o /tmp/p16_planner_$(basename $root)
done
for rep in 1 2 3; do
    for root in ~/MENOS-p16-base ~/MENOS; do
        echo "== $(basename $root) $(git -C $root log --oneline -1 | cut -c1-7) rep $rep"
        /tmp/p16_planner_$(basename $root)
    done
done

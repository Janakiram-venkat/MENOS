set -e
S=/mnt/c/Users/janak/AppData/Local/Temp/claude/C--Users-janak-MENOS/0d99a7c3-bfca-411c-95c5-a8fa1701ab83/scratchpad
for root in ~/MENOS-p15-base ~/MENOS; do
  cd $root
  echo "== $(git log --oneline -1)"
  B=build/cuda12-release
  cmake --build $B -j"$(nproc)" > /dev/null 2>&1 || true
  LIBS=$(find $B/src -name '*.a' | grep -v _deps | tr '\n' ' ')
  g++-13 -std=c++20 -O2 -pthread -Iinclude -Isrc $S/planner_choice.cpp \
      -Wl,--start-group $LIBS -Wl,--end-group -llapacke -llapack -lblas -lcudart -o /tmp/p15_planner_$(basename $root) 2>&1 | head -20
  /tmp/p15_planner_$(basename $root)
done

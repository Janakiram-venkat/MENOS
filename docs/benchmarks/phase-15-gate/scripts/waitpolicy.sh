for p in active passive; do
  echo "== OMP_WAIT_POLICY=$p"
  OMP_WAIT_POLICY=$p /tmp/p15_teams 1 4 8 11 16 22
done

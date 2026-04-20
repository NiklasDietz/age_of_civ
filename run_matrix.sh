#!/bin/bash
set -u
mkdir -p matrix_logs
: > matrix_summary.tsv
echo -e "idx\tmap\tturns\tplayers\twin_type\twin_player\tsecessions\tcaptures\tspies\tgrievances\tgreat_ppl\ttech_diffus\tbubbles\tfloods\twars\tevents\tgov_adopt\truntime_s" >> matrix_summary.tsv

# Matrix: 6 maps x 3 turn counts x 6 player counts (Latin-ish, 18 games)
jobs=(
  "continents 330 2"
  "continents 500 4"
  "continents 750 6"
  "islands 330 8"
  "islands 500 10"
  "islands 750 12"
  "continentsplusislands 330 4"
  "continentsplusislands 500 6"
  "continentsplusislands 750 8"
  "landonly 330 6"
  "landonly 500 8"
  "landonly 750 10"
  "landwithseas 330 10"
  "landwithseas 500 12"
  "landwithseas 750 2"
  "fractal 330 12"
  "fractal 500 2"
  "fractal 750 4"
)

run_one() {
  local idx=$1 map=$2 turns=$3 players=$4
  local logfile="matrix_logs/${idx}_${map}_${turns}t_${players}p.log"
  local start=$SECONDS
  ./build/aoc_simulate --turns "$turns" --players "$players" \
    --map-type "$map" --tuned-dir tuned_run2 --victory-types all \
    --output "matrix_logs/${idx}.csv" > "$logfile" 2> "${logfile}.err"
  local dur=$((SECONDS - start))

  local win=$(grep -oE "wins by [A-Z]+" "$logfile" | head -1 | awk '{print $3}')
  [ -z "$win" ] && win="none"
  local wp=$(grep -oE "Player [0-9]+ wins" "$logfile" | head -1 | awk '{print $2}')
  [ -z "$wp" ] && wp="-"
  local secessions=$(grep -cE "SECESSION|REVOLT" "$logfile")
  local caps=$(grep -c "captured by player" "$logfile")
  local spies=$(grep -c "Spy.*assigned" "$logfile")
  local grievs=$(grep -c "added grievance" "$logfile")
  local gp=$(grep -c "Activating Great Person" "$logfile")
  local td=$(grep -c "Tech diffusion" "$logfile")
  local bub=$(grep -cE "bubble (FORMING|INFLATING|BURSTING)" "$logfile")
  local fld=$(grep -c "coastal tile.*flooded" "$logfile")
  local wars=$(grep -c "declared war" "$logfile")
  local evt=$(grep -c "World event triggered" "$logfile")
  local gov=$(grep -c "Adopted government" "$logfile")

  printf "%d\t%s\t%s\t%s\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n" \
    "$idx" "$map" "$turns" "$players" "$win" "$wp" \
    "$secessions" "$caps" "$spies" "$grievs" "$gp" "$td" "$bub" "$fld" "$wars" "$evt" "$gov" "$dur" \
    >> matrix_summary.tsv
  echo "[DONE] $idx $map t=$turns p=$players -> $win (${dur}s)"
}

export -f run_one

# Run N-way parallel (tune via MATRIX_PAR env, default 4 to leave cores free)
PAR=${MATRIX_PAR:-4}
idx=0
for spec in "${jobs[@]}"; do
  idx=$((idx + 1))
  IFS=' ' read -r map turns players <<< "$spec"
  (run_one "$idx" "$map" "$turns" "$players") &
  # Throttle
  while [ "$(jobs -rp | wc -l)" -ge "$PAR" ]; do
    sleep 2
  done
done
wait
echo "ALL DONE"

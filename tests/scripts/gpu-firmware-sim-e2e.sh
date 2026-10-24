#!/usr/bin/env bash
set -euo pipefail
compiler="$1"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
"$compiler" check Lib/std/gpu_firmware.noqeri
"$compiler" build Lib/std/gpu_firmware.noqeri "$work/gpu_firmware.s" -O3
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pthread \
  "$work/gpu_firmware.s" tests/cpp/gpu_firmware_sim_test.cpp -o "$work/gpu-firmware-sim"
readelf -W -l "$work/gpu-firmware-sim" |
  awk '$1=="GNU_STACK" {found=1; if($7 ~ /E/)bad=1} END {if(!found || bad)exit 1}'
timeout 20s "$work/gpu-firmware-sim"
"$compiler" check examples/gpu_firmware_sim.noqeri
"$compiler" build examples/gpu_firmware_sim.noqeri "$work/gpu-firmware-demo.s" -O3
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pthread \
  "$work/gpu-firmware-demo.s" tests/cpp/gpu_firmware_sim_app.cpp -o "$work/gpu-firmware-demo"
timeout 20s "$work/gpu-firmware-demo"

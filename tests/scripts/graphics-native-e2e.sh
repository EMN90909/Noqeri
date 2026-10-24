#!/usr/bin/env bash
set -euo pipefail
compiler="${1:?pass Noqeri compiler}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

"$compiler" check Lib/std/render2d.noqeri
"$compiler" check Lib/std/os_framebuffer.noqeri
"$compiler" build examples/os_framebuffer_demo.noqeri "$work/os_framebuffer.s" -O0
g++ -std=c++17 -Wall -Wextra -Werror -pthread \
  "$work/os_framebuffer.s" tests/cpp/os_framebuffer_headless_test.cpp -o "$work/noqeri-osfb"
timeout 20s "$work/noqeri-osfb"
"$compiler" build examples/graphics_demo.noqeri "$work/graphics.s" -O0
g++ -std=c++17 -Wall -Wextra -Werror -pthread \
  "$work/graphics.s" tests/cpp/graphics_headless_test.cpp -o "$work/noqeri-headless"
timeout 20s "$work/noqeri-headless"

if pkg-config --exists sdl2; then
  g++ -std=c++17 -Wall -Wextra -Werror -pthread \
    -DNOQERI_OS_RENDERER_TEST_APP -IInclude $(pkg-config --cflags sdl2) \
    "$work/graphics.s" Runtime/native/os_renderer_sdl.cpp \
    $(pkg-config --libs sdl2) -o "$work/noqeri-sdl"
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 20s "$work/noqeri-sdl"
  g++ -std=c++17 -Wall -Wextra -Werror -pthread \
    -IInclude $(pkg-config --cflags sdl2) \
    Runtime/native/os_renderer_sdl.cpp tests/cpp/os_renderer_viewport_test.cpp \
    $(pkg-config --libs sdl2) -o "$work/noqeri-viewport"
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 20s "$work/noqeri-viewport"
else
  echo "SDL2 unavailable: verified headless host only" >&2
  if [ "${NOQERI_REQUIRE_SDL2:-0}" = 1 ]; then exit 1; fi
fi
echo "Noqeri raster and OS renderer compilation: PASS"

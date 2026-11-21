#!/bin/sh
# Render the controller screen on a PC. Usage: test/host/render_preview.sh <outdir>
# Writes one PPM per scene into <outdir> (run from the project root).
set -e
OUT="${1:?usage: render_preview.sh <outdir>}"
mkdir -p "$OUT"
cc -std=c11 -Wall -Wextra -c components/app_display/src/font5x7.c \
    -I components/app_display/include -o "$OUT/font5x7.o"
c++ -std=gnu++17 -Wall -Wextra -Werror \
    -I components/app_display/include -I components/app_ui/include \
    test/host/render_preview.cpp \
    components/app_ui/screen.cpp \
    components/app_display/src/canvas.cpp \
    components/app_display/src/theme.cpp \
    "$OUT/font5x7.o" -o "$OUT/render_preview"
"$OUT/render_preview" "$OUT"

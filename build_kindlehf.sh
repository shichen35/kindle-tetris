#!/bin/bash

set -euo pipefail

SDK="$HOME/x-tools/arm-kindlehf-linux-gnueabihf/meson-crosscompile.txt"

if [ ! -d build_kindlehf ]; then
    meson setup --cross-file "$SDK" build_kindlehf
else
    meson setup --cross-file "$SDK" --reconfigure build_kindlehf
fi

meson compile -C build_kindlehf

cp build_kindlehf/tetris tetris/
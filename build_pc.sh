#!/bin/bash

set -euo pipefail

if [ ! -d build_pc ]; then
meson setup build_pc
else
    meson setup build_pc --reconfigure
fi

meson compile -C build_pc

cd build_pc
./tetris
#!/bin/bash
# Build exefs/subsdk9 and the IPS patch.
#   MAIN=<path to your own decompressed main of OoT 1.1.1 (build E0F3CCFA...)> ./build.sh
# The patch builder only reads main to check the hook sites; nothing from it ends up in the output.
# generated/ holds pre-made tables (collectibles, font, archive patch) so the game's files aren't needed to build.
set -e
cd "$(dirname "$0")"
: "${MAIN:?set MAIN to your decompressed main (see README)}"
CXX=${CXX:-aarch64-linux-gnu-g++}
FLAGS="-std=gnu++20 -O2 -fPIC -fno-plt -ffreestanding -fno-exceptions -fno-rtti -fno-unwind-tables -fno-asynchronous-unwind-tables
       -fno-stack-protector -fno-threadsafe-statics -fvisibility=hidden -march=armv8.2-a -mno-outline-atomics -Wall -Wextra -nostdlib -fno-tree-loop-distribute-patterns -fno-math-errno $EXTRA"
mkdir -p build exefs
cp generated/items.inc generated/font.inc generated/archive_patch.inc build/
python3 tools/make_patch.py "$MAIN" hooks.json exefs/E0F3CCFAF50065AE4EA01EB5AEB13B1A45DA3348.ips build/hooks.inc
OBJS="build/crt0.o"
$CXX $FLAGS -c src/crt0.S -o build/crt0.o
for f in main log hooks resources icons gamestate overlay canvas menu input hints settings probe; do $CXX $FLAGS -Ibuild -c src/$f.cpp -o build/$f.o; OBJS="$OBJS build/$f.o"; done
${LD:-aarch64-linux-gnu-ld} -shared -T link.ld -init=oot_minimap_init --build-id=sha1 --hash-style=both -z text -z max-page-size=0x1000 \
    -z norelro --no-undefined-version -o build/module.elf $OBJS
python3 tools/elf2nso.py build/module.elf exefs/subsdk9

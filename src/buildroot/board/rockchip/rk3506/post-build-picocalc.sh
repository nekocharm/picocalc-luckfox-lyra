#!/bin/bash -e

TARGET_DIR="${TARGET_DIR:-"$@"}"

for file in `ls $TARGET_DIR/usr/lib/libretro/`
do
    ln -sf /usr/lib/libretro/$file $TARGET_DIR/root/.config/retroarch/cores/$file
done

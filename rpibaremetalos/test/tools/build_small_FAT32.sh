#! /bin/bash

#   Create a 32MB-ish FAT32 image, a little extra space is needed for meta-data.
#       Requires the mtools package to be installed.
#
#   Example:   ./test/utilities/build_small_FAT32.sh ./test/data/empty_fat32.img EMPTYFAT32

dd if=/dev/zero of=$1 bs=34M count=1
mformat -F -v $2 -i $1 ::

#! /bin/bash

#   Create a 32MB-ish FAT32 image, and fill it with some directories and files.
#       Requires the mtools package to be installed.

rm ../data/test_fat32.img

dd if=/dev/zero of="../data/test_fat32.img" bs=34M count=1

mformat -F -v TESTFAT32 -i "../data/test_fat32.img" ::

# Create subdirectories and files.
#   The ordering is purposeful to result in the various files and subdirectory records being
#   spread around the FAT and directory clusters in a bit of a non-sequential manner.

#   The file names are chosen to test long filename to basis filename conversion.

mmd -i "../data/test_fat32.img" "subdir1" "subdir2" "subdir3" "file testing" "test 1" "test+1" "Test 1.t x" "Test1.t+x" "...Name.With.Leading.Periods.lNg"
mmd -i "../data/test_fat32.img" "subdir1/this is a long subdirectory name" "subdir1/another long subdirectory name.with period"
mmd -i "../data/test_fat32.img" "subdir2/subdir2_1"
mmd -i "../data/test_fat32.img" "subdir1/this is a long subdirectory name/subdir1_1_1" "subdir1/another long subdirectory name.with period/subdir1_2_1"

mcopy -i "../data/test_fat32.img" "../data/Lorem_ipsum_dolor_sit_amet.txt" "::subdir1/Lorem ipsum dolor sit amet.text"
mcopy -i "../data/test_fat32.img" "../data/A_diam_maecenas_sed_enim_ut sem.Pellentesque" "::subdir1/A diam maecenas sed enim ut sem.Pellentesque"

mmd -i "../data/test_fat32.img" "subdir2/subdir2_1/subdir_2_1_1"
mmd -i "../data/test_fat32.img" "subdir2/subdir2_2"
mmd -i "../data/test_fat32.img" "subdir2/subdir2_2/subdir_2_2_1"
mmd -i "../data/test_fat32.img" "subdir2/subdir2_1/subdir_2_1_1/subdir_2_1_1_1"
mmd -i "../data/test_fat32.img" "subdir2/subdir2_1/subdir_2_1_1/subdir_2_1_1_2"
mmd -i "../data/test_fat32.img" "subdir2/subdir2_2/subdir_2_2_1/subdir_2_2_1_1"
mmd -i "../data/test_fat32.img" "subdir2/subdir2_1/subdir_2_1_1/subdir_2_1_1_3"
mmd -i "../data/test_fat32.img" "subdir2/subdir2_2/subdir_2_2_1/subdir_2_2_1_2"

mcopy -i "../data/test_fat32.img" "../data/Magna_eget_est_lorem_ipsum" "::subdir1/Magna eget est lorem ipsum"


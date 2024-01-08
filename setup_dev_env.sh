#!/bin/bash -eu

ARM_TOOLCHAIN_VERSION=12.3.rel1
ARM_TOOLCHAIN_NAME=arm-gnu-toolchain-$ARM_TOOLCHAIN_VERSION-x86_64-aarch64-none-elf

mkdir ~/dev_tools

wget https://developer.arm.com/-/media/Files/downloads/gnu/$ARM_TOOLCHAIN_VERSION/binrel/$ARM_TOOLCHAIN_NAME.tar.xz -O /tmp/$ARM_TOOLCHAIN_NAME.tar.xz

tar xf /tmp/$ARM_TOOLCHAIN_NAME.tar.xz -C ~/dev_tools

mkdir -p ~/dev/gcc-cross/aarch64-none-elf/lib

cp -r ~/dev_tools/$ARM_TOOLCHAIN_NAME/lib/gcc ~/dev/gcc-cross/aarch64-none-elf/lib/

mkdir -p ~/dev/gcc-cross/aarch64-none-elf/aarch64-none-elf/include/bits

cp ~/dev_tools/$ARM_TOOLCHAIN_NAME/aarch64-none-elf/include/c++/12.3.1/cstdarg ~/dev/gcc-cross/aarch64-none-elf/aarch64-none-elf/include/.
cp ~/dev_tools/$ARM_TOOLCHAIN_NAME/aarch64-none-elf/include/c++/12.3.1/cstddef ~/dev/gcc-cross/aarch64-none-elf/aarch64-none-elf/include/.

cp ~/dev_tools/$ARM_TOOLCHAIN_NAME/aarch64-none-elf/include/c++/12.3.1/aarch64-none-elf/bits/c++config.h ~/dev/gcc-cross/aarch64-none-elf/aarch64-none-elf/include/bits/.
cp ~/dev_tools/$ARM_TOOLCHAIN_NAME/aarch64-none-elf/include/c++/12.3.1/aarch64-none-elf/bits/cpu_defines.h ~/dev/gcc-cross/aarch64-none-elf/aarch64-none-elf/include/bits/.
cp ~/dev_tools/$ARM_TOOLCHAIN_NAME/aarch64-none-elf/include/c++/12.3.1/aarch64-none-elf/bits/os_defines.h ~/dev/gcc-cross/aarch64-none-elf/aarch64-none-elf/include/bits/.

#   Install Catch2 for Unit Testing

mkdir ~/dev_tools/Catch2
mkdir ~/dev_tools/Catch2_build

cd ~/dev_tools/Catch2_build
git clone https://github.com/catchorg/Catch2.git

cd ~/dev_tools/Catch2_build/Catch2
cmake -Bbuild -H. -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX=~/dev_tools/Catch2
cmake --build build/ --target install

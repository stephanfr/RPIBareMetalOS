# RPIBareMetalOS

This repository contains the code for a very minimal bare metal operating system built for AARCH64 capable Raspberry Pis.  The OS is written in C++, C and some assembly language and the repository contains a couple of very lightweight system libraries.

## Native ARM64 builds

When working on an ARM64 Linux host, you can build the minimal libraries natively:

- minimalclib: run `make arm64` in minimalclib/
- minimalstdio: run `make arm64` in minimalstdio/
- minimalstdlib: run `make arm64` in minimalstdlib/

I have a series of explanatory blog posts for this project:

http://stephanfr.blog/2024/01/08/building-a-raspberry-pi-64-bit-operating-system-with-c/

http://stephanfr.blog/2024/01/09/using-packer-to-build-development-vms/

http://stephanfr.blog/2024/01/08/rpi-bare-metal-cross-assembly-toolchain/

http://stephanfr.blog/2024/01/21/bare-metal-build-system/

http://stephanfr.blog/2024/02/04/basics-of-gcc-linker-scripts/


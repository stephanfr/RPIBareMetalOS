// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/user_image.h"

    .section .text.header, "a"
    .quad   S_USER_IMAGE_MAGIC
    .quad   __text_size
    .quad   __data_size
    .quad   __bss_size

    .section .text.start, "ax"
    .globl  _start
_start:
    bl      main
    mov     w8, #3                      // SYS_EXIT_NUMBER
    svc     #0
1:  b       1b

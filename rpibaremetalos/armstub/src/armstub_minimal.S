// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.


//
//  This is an absolutely minimal stub that will jump jump immediately to the kernel entry point.
//      It will enter the kernel at EL3 - which will permit the creation of a secure kernel if desired.
//

.globl _start
_start:

	ldr w4, kernel_entry32
	ldr w0, dtb_ptr32

	mov x1, #0
	mov x2, #0
	mov x3, #0
	br x4

.ltorg

.org 0xf0
.globl stub_magic
stub_magic:
	.word 0x5afe570b

.org 0xf4
.globl stub_version
stub_version:
	.word 0

.org 0xf8
.globl dtb_ptr32
dtb_ptr32:
	.word 0x0

.org 0xfc
.globl kernel_entry32
kernel_entry32:
	.word 0x0


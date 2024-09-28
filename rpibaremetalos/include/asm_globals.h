// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

//
//  Symbols generated by the Linker Script
//

extern const unsigned int __start;

extern const unsigned int __bss_start;
extern const unsigned int __bss_end;
extern const unsigned int __bss_size_in_double_words;

extern const unsigned int __init_array_start;
extern const unsigned int __init_array_end;

extern const unsigned int __static_heap_start;
extern const unsigned int __static_heap_end;
extern const unsigned int __static_heap_size_in_bytes;

extern const unsigned int __dynamic_heap_start;
extern const unsigned int __dynamic_heap_end;
extern const unsigned int __dynamic_heap_size_in_bytes;

extern const unsigned int __el1_stack_top;
extern const unsigned int __el1_stack_bottom;
extern const unsigned int __el1_stack_size_in_bytes;

extern const unsigned int __el0_stack_top;
extern const unsigned int __el0_stack_bottom;
extern const unsigned int __el0_stack_size_in_bytes;

extern const unsigned int __os_process_start;

//
//  Variables defined in assmbly language code
//

extern uint32_t __hw_board_type;
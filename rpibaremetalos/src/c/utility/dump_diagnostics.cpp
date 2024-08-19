// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stdint.h>

#include <minimalstdio.h>
#include <memory>

#include "asm_globals.h"
#include "asm_utility.h"

#include "platform/platform_info.h"

#include "platform/kernel_command_line.h"

#include "devices/character_io.h"

#include "heaps.h"

extern "C" uint32_t GetExceptionLevel();

extern CharacterIODevice *stdout;

void DumpDiagnostics()
{
    const PlatformInfo &platformInfo = GetPlatformInfo();

    minstd::fixed_string<128>   board_revision;

    platformInfo.DecodeBoardRevision( board_revision );

    printf("Board Info: \n");
    printf("RPI Version: %s\n", platformInfo.GetBoardTypeName());
    printf("Board Model: %u\n", platformInfo.GetBoardModelNumber());
    printf("Board Revision: 0x%08x : %s\n", platformInfo.GetBoardRevision(), board_revision.c_str());
    printf("Board Serial Number: %lu\n", platformInfo.GetBoardSerialNumber());
    printf("Board MAC Address:  %02x:%02x:%02x:%02x:%02x:%02x\n", platformInfo.GetBoardMACAddress()[0], platformInfo.GetBoardMACAddress()[1], platformInfo.GetBoardMACAddress()[2], platformInfo.GetBoardMACAddress()[3], platformInfo.GetBoardMACAddress()[4], platformInfo.GetBoardMACAddress()[5] );
    
    printf("\nException Level Info:\n");
    printf("Current Exception Level: %u\n", GetExceptionLevel());
        
    printf("\nMemory Info:\n");
//    printf("Memory Size in bytes 0x%08x\n", platformInfo.GetMemorySizeInBytes());
    printf("Memory Base Address 0x%08x\n", platformInfo.GetMemoryBaseAddress());
    printf("Code Start: %p\n", (uint8_t *)&__start);
    printf("BSS Start: %p\n", (uint8_t *)&__bss_start);
    printf("BSS End: %p\n", (uint8_t *)&__bss_end);
    printf("Init Array Start: %p\n", (uint8_t *)&__init_array_start);
    printf("Init Array End: %p\n", (uint8_t *)&__init_array_end);
    printf("Static Heap Reserved Space Start: %p\n", (uint8_t *)&__static_heap_start);
    printf("Static Heap Reserved Space End: %p\n", (uint8_t *)&__static_heap_end);
    printf("Static Heap Reserved Space Size: %p\n", (uint32_t *)&__static_heap_size_in_bytes);
    printf("Static Heap Start: %p\n", __os_static_heap.heap_start());
    printf("Static Heap End: %p\n", __os_static_heap.current_end());
    printf("Dynamic Heap Reserved Space Start: %p\n", (uint8_t *)&__dynamic_heap_start);
    printf("Dynamic Heap Reserved Space End: %p\n", (uint8_t *)&__dynamic_heap_end);
    printf("Dynamic Heap Reserved Space Size: %p\n", (uint32_t *)&__dynamic_heap_size_in_bytes);
    printf("Dynamic Heap Start: %p\n", __os_dynamic_heap.heap_start());
    printf("Dynamic Heap End: %p\n", __os_dynamic_heap.current_end());
    printf("EL1 Stack Top: %p\n", (uint8_t *)&__el1_stack_top);
    printf("EL1 Stack Bottom: %p\n", (uint8_t *)&__el1_stack_bottom);
    printf("EL0 Stack Top: %p\n", (uint8_t *)&__el0_stack_top);
    printf("EL0 Stack Bottom: %p\n", (uint8_t *)&__el0_stack_bottom);
    printf("Current Stack Location: %p\n", (uint8_t *)get_stack_pointer());

    printf("\nKernal Command Line: %s\n", KernelCommandLine::RawCommandLine().c_str());
    
    printf("\nIO Mapping:\n");
    printf("STDOUT mapped to: %s\n", (const char*)stdout->Name());

    printf("\n\n");
}

// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cli/show_command.h"

#include "asm_globals.h"
#include "asm_utility.h"

#include "platform/platform_info.h"

#include "platform/kernel_command_line.h"

#include "devices/character_io.h"

#include "heaps.h"

#include <format>
#include <minimalstdio.h>

extern "C" uint32_t GetExceptionLevel();

extern CharacterIODevice *stdout;

namespace cli
{
    void ShowCommandDispatcher::DispatchCommand(CommandParser &parser,
                                                CLISessionContext &context)
    {
        const char *element_to_show = parser.NextToken();

        if (element_to_show == nullptr)
        {
            context.output_stream_ << "Incomplete Command\n";
            return;
        }

        if (strnicmp(element_to_show, "diagnostics", MAX_CLI_COMMAND_TOKEN_LENGTH) == 0)
        {
            ShowDiagnostics(parser, context);
            return;
        }

        context.output_stream_ << "Unrecognized token: " << element_to_show << "\n";
    }

    void ShowCommandDispatcher::ShowDiagnostics(CommandParser &parser,
                                                CLISessionContext &context)
    {
        minstd::fixed_string<256> format_buffer;

        const PlatformInfo &platformInfo = GetPlatformInfo();

        minstd::fixed_string<128> board_revision;

        platformInfo.DecodeBoardRevision(board_revision);

        context.output_stream_ << "Board Info: \n";

        context.output_stream_ << minstd::format(format_buffer, "RPI Version: {}\n", platformInfo.GetBoardTypeName());
        context.output_stream_ << minstd::format(format_buffer, "Board Model: {}\n", platformInfo.GetBoardModelNumber());
        context.output_stream_ << minstd::format(format_buffer, "Board Revision: {:#010x} : {}\n", platformInfo.GetBoardRevision(), board_revision.c_str());
        context.output_stream_ << minstd::format(format_buffer, "Board Serial Number: {}\n", platformInfo.GetBoardSerialNumber());
        context.output_stream_ << minstd::format(format_buffer, "Board MAC Address:  {:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}\n", platformInfo.GetBoardMACAddress()[0], platformInfo.GetBoardMACAddress()[1], platformInfo.GetBoardMACAddress()[2], platformInfo.GetBoardMACAddress()[3], platformInfo.GetBoardMACAddress()[4], platformInfo.GetBoardMACAddress()[5] );
    
        context.output_stream_ << "\nException Level Info:\n";
        context.output_stream_ << minstd::format(format_buffer, "Current Exception Level: {}\n", GetExceptionLevel());

        context.output_stream_ << "\nMemory Info:\n";
        context.output_stream_ << minstd::format(format_buffer, "Memory Base Address {:#010x}\n", platformInfo.GetMemoryBaseAddress());
        context.output_stream_ << minstd::format(format_buffer, "Code Start: {}\n", (void*)&__start);
        context.output_stream_ << minstd::format(format_buffer, "BSS Start: {}\n", (void*)&__bss_start);
        context.output_stream_ << minstd::format(format_buffer, "BSS End: {}\n", (void*)&__bss_end);
        context.output_stream_ << minstd::format(format_buffer, "Init Array Start: {}\n", (void*)&__init_array_start);
        context.output_stream_ << minstd::format(format_buffer, "Init Array End: {}\n", (void*)&__init_array_end);
        context.output_stream_ << minstd::format(format_buffer, "Static Heap Reserved Space Start: {}\n", (void*)&__static_heap_start);
        context.output_stream_ << minstd::format(format_buffer, "Static Heap Reserved Space End: {}\n", (void*)&__static_heap_end);
        context.output_stream_ << minstd::format(format_buffer, "Static Heap Reserved Space Size: {}\n", (uint32_t *)&__static_heap_size_in_bytes);
        context.output_stream_ << minstd::format(format_buffer, "Static Heap Start: {}\n", __os_static_heap.heap_start());
        context.output_stream_ << minstd::format(format_buffer, "Static Heap End: {}\n", __os_static_heap.current_end());
        context.output_stream_ << minstd::format(format_buffer, "Dynamic Heap Reserved Space Start: {}\n", (void*)&__dynamic_heap_start);
        context.output_stream_ << minstd::format(format_buffer, "Dynamic Heap Reserved Space End: {}\n", (void*)&__dynamic_heap_end);
        context.output_stream_ << minstd::format(format_buffer, "Dynamic Heap Reserved Space Size: {}\n", (uint32_t *)&__dynamic_heap_size_in_bytes);
        context.output_stream_ << minstd::format(format_buffer, "Dynamic Heap Start: {}\n", __os_dynamic_heap.heap_start());
        context.output_stream_ << minstd::format(format_buffer, "Dynamic Heap End: {}\n", __os_dynamic_heap.current_end());
        context.output_stream_ << minstd::format(format_buffer, "EL1 Stack Top: {}\n", (void*)&__el1_stack_top);
        context.output_stream_ << minstd::format(format_buffer, "EL1 Stack Bottom: {}\n", (void*)&__el1_stack_bottom);
        context.output_stream_ << minstd::format(format_buffer, "EL0 Stack Top: {}\n", (void*)&__el0_stack_top);
        context.output_stream_ << minstd::format(format_buffer, "EL0 Stack Bottom: {}\n", (void*)&__el0_stack_bottom);
        context.output_stream_ << minstd::format(format_buffer, "Current Stack Location: {}\n", (void*)get_stack_pointer());

        context.output_stream_ << "\nKernal Command Line: " << KernelCommandLine::RawCommandLine() << "\n";

        context.output_stream_ << "\nIO Mapping:\n";
        context.output_stream_ << minstd::format(format_buffer, "STDOUT mapped to: {}\n", (const char*)stdout->Name());

        context.output_stream_ << "\n\n";
    }

} // namespace cli
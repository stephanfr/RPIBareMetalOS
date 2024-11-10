// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_config.h"

#include "asm_globals.h"

#include "platform/mmu.h"

#include <minimalstdio.h>

extern "C" uint32_t GetVideocoreMemoryStart(uint32_t board_type);
extern "C" uint32_t GetBoardVersion( uint32_t core_type );

#define MEMORY_ATTRIBUTE_DEVICE_NO_GATHER_NO_REORDER_NO_EARLY_WRITE_ACK 0
#define MEMORY_ATTRIBUTE_DEVICE_NO_GATHER_NO_REORDER_EARLY_WRITE_ACK 1
#define MEMORY_ATTRIBUTE_DEVICE_GATHER_REORDER_EARLY_WRITE_ACK 2
#define MEMORY_ATTRIBUTE_NORMAL_NO_CACHING 3
#define MEMORY_ATTRIBUTE_NORMAL 4

class AARCH64PlatformMemoryManager : public MemoryManager
{
    using MemoryModel = MemoryManager::MemoryModel;

protected:
    typedef enum TableType : uint32_t
    {
        BLOCK_TABLE = 1,
        PAGE_TABLE = 3
    } TableType;

    typedef enum Stage2AccessPermission : uint32_t
    {
        STAGE2_S2AP_NOREAD_EL0 = 1, //			No read access for EL0
        STAGE2_S2AP_NO_WRITE = 2,   //			No write access
    } Stage2AccessPermission;

    typedef enum Stage2Sharability : uint32_t
    {
        STAGE2_SH_OUTER_SHAREABLE = 2, //			Outer shareable
        STAGE2_SH_INNER_SHAREABLE = 3, //			Inner shareable
    } Stage2Sharability;

    typedef union
    {
        struct
        {
            TableType EntryType : 2; // @0-1		1 for a block table, 3 for a page table

            // These are only valid on BLOCK DESCRIPTOR
            uint64_t MemAttr : 4;            // @2-5
            Stage2AccessPermission S2AP : 2; // @6-7
            Stage2Sharability SH : 2;        // @8-9
            uint64_t AF : 1;                 // @10		Accessable flag

            uint64_t _reserved11 : 1;    // @11		Set to 0
            uint64_t Address : 36;       // @12-47	36 Bits of address
            uint64_t _reserved48_51 : 4; // @48-51	Set to 0
            uint64_t Contiguous : 1;     // @52		Contiguous
            uint64_t _reserved53 : 1;    // @53		Set to 0
            uint64_t XN : 1;             // @54		No execute if bit set
            uint64_t _reserved55_58 : 4; // @55-58	Set to 0

            uint64_t PXNTable : 1; // @59      Never allow execution from a lower EL level
            uint64_t XNTable : 1;  // @60		Never allow translation from a lower EL level
                                   //		enum {
                                   //			APTABLE_NOEFFECT = 0,			// No effect
                                   //			APTABLE_NO_EL0 = 1,				// Access at EL0 not permitted, regardless of permissions in subsequent levels of lookup
                                   //			APTABLE_NO_WRITE = 2,			// Write access not permitted, at any Exception level, regardless of permissions in subsequent levels of lookup
                                   //			APTABLE_NO_WRITE_EL0_READ = 3	// Write access not permitted,at any Exception level, Read access not permitted at EL0.
                                   //		}
            uint64_t APTable : 2;  // @61-62	AP Table control .. see enumerate options
            uint64_t NSTable : 1;  // @63		Secure state, for accesses from Non-secure state this bit is RES0 and is ignored
        };
        uint64_t Raw64; // @0-63	Raw access to all 64 bits via this union
    } VMSAv8_64_DESCRIPTOR;

public:
    AARCH64PlatformMemoryManager(MemoryModel memory_model)
        : memory_model_(memory_model)
    {
        //  Get the memory size from the board revision
        
        board_revision_ = GetBoardVersion(__hw_board_type);

        //  Memory size is in bits 20 though 22

        uint32_t memory = ( board_revision_ >> 20 ) & 0x07;

        switch( memory )
        {
            case 2:
                platform_memory_in_bytes_ = BYTES_1G;
                break;

            case 4:
                platform_memory_in_bytes_ = 4 * BYTES_1G;
                break;

            case 5:
                platform_memory_in_bytes_ = 8 * BYTES_1G;
                break;

            default:
                ParkCore();
                break;
        }
    }

    AARCH64PlatformMemoryManager() = delete;

protected:
    MemoryModel memory_model_;

    uint32_t board_revision_;
    uint64_t platform_memory_in_bytes_;

    uint32_t number_of_pagetable_entries_;
    uint32_t level1_blocksize_;
    uint32_t level2_blocksize_;
    uint32_t granule_size_shift_;

    uint32_t videocore_memory_start_block_;
    uint32_t dma_block_;
    uint32_t page_table_block_;

    uint64_t *kernel_page_table_1_to_1_;
    VMSAv8_64_DESCRIPTOR *Stage2map1to1_;
};

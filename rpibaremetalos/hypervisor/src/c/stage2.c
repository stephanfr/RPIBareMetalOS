// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "stage2.h"

#define ENTRIES_PER_TABLE 512
#define FIRST_LEVEL 1
#define LAST_LEVEL 3

#define DESCRIPTOR_VALID 1ull
#define DESCRIPTOR_TYPE_MASK 3ull
#define DESCRIPTOR_BLOCK 1ull
#define DESCRIPTOR_TABLE_OR_PAGE 3ull
#define DESCRIPTOR_ADDRESS_MASK 0x0000FFFFFFFFF000ull
#define DESCRIPTOR_ATTRIBUTE_MASK (~(DESCRIPTOR_ADDRESS_MASK | DESCRIPTOR_TYPE_MASK))

#define REASON_SHIFT 55 //  software bits [58:55]; the walker ignores every bit of an invalid descriptor
#define REASON_MASK 0xFull

#define PA_LIMIT (1ull << 48)

static uint32_t LevelShift(uint32_t level)
{
    return STAGE2_IPA_BITS - (9 * level);
}

static uint32_t EntryIndex(uint64_t ipa, uint32_t level)
{
    return (uint32_t)((ipa >> LevelShift(level)) & (ENTRIES_PER_TABLE - 1));
}

static volatile uint64_t *TableAt(const volatile Stage2Tables *tables, uint64_t table_pa)
{
    return (volatile uint64_t *)(uintptr_t)(table_pa + tables->pointer_offset);
}

static int IsTable(uint64_t descriptor, uint32_t level)
{
    return (level < LAST_LEVEL) && ((descriptor & DESCRIPTOR_TYPE_MASK) == DESCRIPTOR_TABLE_OR_PAGE);
}

//  A valid descriptor that isn't a table.  At level 3, type 0b01 is reserved and translates as invalid.

static int IsLeaf(uint64_t descriptor, uint32_t level)
{
    return (level < LAST_LEVEL) ? ((descriptor & DESCRIPTOR_TYPE_MASK) == DESCRIPTOR_BLOCK)
                                : ((descriptor & DESCRIPTOR_TYPE_MASK) == DESCRIPTOR_TABLE_OR_PAGE);
}

static uint64_t Leaf(uint32_t level, uint64_t output, uint64_t attributes)
{
    return (output & DESCRIPTOR_ADDRESS_MASK) | (attributes & DESCRIPTOR_ATTRIBUTE_MASK) |
           ((level == LAST_LEVEL) ? DESCRIPTOR_TABLE_OR_PAGE : DESCRIPTOR_BLOCK);
}

static uint64_t Unmapped(uint32_t reason)
{
    return ((uint64_t)reason & REASON_MASK) << REASON_SHIFT;
}

//  A zeroed page from the pool, or 0 when it is used up.

static uint64_t AllocateTable(volatile Stage2Tables *tables)
{
    if (tables->pool_used >= tables->pool_pages)
    {
        return 0;
    }

    const uint64_t table_pa = tables->pool_base + ((uint64_t)tables->pool_used * STAGE2_PAGE_SIZE);
    volatile uint64_t *table = TableAt(tables, table_pa);

    for (uint32_t i = 0; i < ENTRIES_PER_TABLE; i++)
    {
        table[i] = 0;
    }

    tables->pool_used = tables->pool_used + 1;

    return table_pa;
}

//  A table one level down that maps exactly what 'descriptor', a leaf or an invalid entry, did.

static uint64_t Split(volatile Stage2Tables *tables, uint64_t descriptor, uint32_t level)
{
    const uint64_t table_pa = AllocateTable(tables);

    if (table_pa == 0)
    {
        return 0;
    }

    volatile uint64_t *table = TableAt(tables, table_pa);
    const uint64_t child_size = 1ull << LevelShift(level + 1);

    for (uint32_t i = 0; i < ENTRIES_PER_TABLE; i++)
    {
        table[i] = (descriptor & DESCRIPTOR_VALID)
                       ? Leaf(level + 1, (descriptor & DESCRIPTOR_ADDRESS_MASK) + (i * child_size), descriptor)
                       : descriptor;
    }

    return table_pa;
}

typedef struct Edit
{
    int map;
    uint64_t output_delta; //  PA - IPA
    uint64_t attributes;
    uint32_t reason;
} Edit;

static uint32_t EditRange(volatile Stage2Tables *tables, uint64_t table_pa, uint32_t level, uint64_t start,
                          uint64_t end, const Edit *edit)
{
    const uint64_t entry_size = 1ull << LevelShift(level);
    volatile uint64_t *table = TableAt(tables, table_pa);

    for (uint64_t address = start; address < end;)
    {
        const uint64_t entry_start = address & ~(entry_size - 1);
        const uint64_t entry_end = entry_start + entry_size;
        const uint64_t chunk_end = (end < entry_end) ? end : entry_end;
        volatile uint64_t *entry = &table[EntryIndex(address, level)];
        const uint64_t current = *entry;

        //  A table already under a wholly replaced entry is edited rather than dropped: the pool can't free it.

        const int whole = (address == entry_start) && (chunk_end == entry_end) && !IsTable(current, level);
        const int output_aligned = !edit->map || (((address + edit->output_delta) & (entry_size - 1)) == 0);

        if (whole && output_aligned)
        {
            *entry = edit->map ? Leaf(level, address + edit->output_delta, edit->attributes) : Unmapped(edit->reason);
        }
        else
        {
            uint64_t next_table = current & DESCRIPTOR_ADDRESS_MASK;

            if (!IsTable(current, level))
            {
                next_table = Split(tables, current, level);

                if (next_table == 0)
                {
                    return STAGE2_ERROR_NO_MEMORY;
                }

                *entry = next_table | DESCRIPTOR_TABLE_OR_PAGE;
            }

            const uint32_t result = EditRange(tables, next_table, level + 1, address, chunk_end, edit);

            if (result != STAGE2_SUCCESS)
            {
                return result;
            }
        }

        address = chunk_end;
    }

    return STAGE2_SUCCESS;
}

static uint32_t CheckRange(const volatile Stage2Tables *tables, uint64_t ipa, uint64_t size)
{
    if (tables->root == 0)
    {
        return STAGE2_ERROR_NOT_INITIALIZED;
    }

    if (((ipa | size) & (STAGE2_PAGE_SIZE - 1)) != 0)
    {
        return STAGE2_ERROR_ALIGNMENT;
    }

    if ((size == 0) || (ipa >= STAGE2_IPA_LIMIT) || (size > STAGE2_IPA_LIMIT - ipa))
    {
        return STAGE2_ERROR_RANGE;
    }

    return STAGE2_SUCCESS;
}

uint32_t Stage2Init(volatile Stage2Tables *tables, uint64_t pool_base, uint32_t pool_pages, uint64_t pointer_offset)
{
    tables->root = 0;
    tables->pool_used = 0;

    if (((pool_base & (STAGE2_PAGE_SIZE - 1)) != 0) || (pool_base >= PA_LIMIT))
    {
        tables->pool_pages = 0;
        return STAGE2_ERROR_ALIGNMENT;
    }

    tables->pool_base = pool_base;
    tables->pool_pages = pool_pages;
    tables->pointer_offset = pointer_offset;

    const uint64_t root = AllocateTable(tables);

    if (root == 0)
    {
        return STAGE2_ERROR_NO_MEMORY;
    }

    tables->root = root;

    return STAGE2_SUCCESS;
}

uint32_t Stage2Map(volatile Stage2Tables *tables, uint64_t ipa, uint64_t pa, uint64_t size, uint64_t attributes)
{
    uint32_t result = CheckRange(tables, ipa, size);

    if (result != STAGE2_SUCCESS)
    {
        return result;
    }

    if ((pa & (STAGE2_PAGE_SIZE - 1)) != 0)
    {
        return STAGE2_ERROR_ALIGNMENT;
    }

    if ((pa >= PA_LIMIT) || (size > PA_LIMIT - pa))
    {
        return STAGE2_ERROR_RANGE;
    }

    const Edit edit = {1, pa - ipa, attributes, STAGE2_REASON_NONE};

    return EditRange(tables, tables->root, FIRST_LEVEL, ipa, ipa + size, &edit);
}

uint32_t Stage2Unmap(volatile Stage2Tables *tables, uint64_t ipa, uint64_t size, uint32_t reason)
{
    uint32_t result = CheckRange(tables, ipa, size);

    if (result != STAGE2_SUCCESS)
    {
        return result;
    }

    if (reason > STAGE2_REASON_MAX)
    {
        return STAGE2_ERROR_RANGE;
    }

    const Edit edit = {0, 0, 0, reason};

    return EditRange(tables, tables->root, FIRST_LEVEL, ipa, ipa + size, &edit);
}

uint32_t Stage2Translate(const volatile Stage2Tables *tables, uint64_t ipa, Stage2Translation *translation)
{
    translation->physical_address = 0;
    translation->attributes = 0;
    translation->level = 0;
    translation->reason = STAGE2_REASON_NONE;

    if ((tables->root == 0) || (ipa >= STAGE2_IPA_LIMIT))
    {
        return 0;
    }

    uint64_t table_pa = tables->root;

    for (uint32_t level = FIRST_LEVEL; level <= LAST_LEVEL; level++)
    {
        const uint64_t descriptor = TableAt(tables, table_pa)[EntryIndex(ipa, level)];

        translation->level = level;

        if (IsTable(descriptor, level))
        {
            table_pa = descriptor & DESCRIPTOR_ADDRESS_MASK;
            continue;
        }

        if (!IsLeaf(descriptor, level))
        {
            translation->reason = (uint32_t)((descriptor >> REASON_SHIFT) & REASON_MASK);
            return 0;
        }

        const uint64_t offset_mask = (1ull << LevelShift(level)) - 1;

        translation->physical_address = ((descriptor & DESCRIPTOR_ADDRESS_MASK) & ~offset_mask) | (ipa & offset_mask);
        translation->attributes = descriptor & DESCRIPTOR_ATTRIBUTE_MASK;
        return 1;
    }

    return 0;
}

uint32_t Stage2RangeIsIdentity(const volatile Stage2Tables *tables, uint64_t ipa, uint64_t size, uint64_t attributes)
{
    if ((size == 0) || (ipa > UINT64_MAX - size))
    {
        return 0;
    }

    const uint64_t end = ipa + size;

    for (uint64_t address = ipa & ~(STAGE2_PAGE_SIZE - 1); address < end;)
    {
        Stage2Translation translation;

        if (!Stage2Translate(tables, address, &translation) || (translation.physical_address != address) ||
            (translation.attributes != (attributes & DESCRIPTOR_ATTRIBUTE_MASK)))
        {
            return 0;
        }

        const uint64_t leaf_size = 1ull << LevelShift(translation.level);

        address = (address & ~(leaf_size - 1)) + leaf_size;
    }

    return 1;
}

const char *Stage2ReasonName(uint32_t reason)
{
    switch (reason)
    {
    case STAGE2_REASON_NONE:
        return "unmapped";

    case STAGE2_REASON_HYPERVISOR:
        return "hypervisor";

    case STAGE2_REASON_POOL:
        return "pool";

    case STAGE2_REASON_BLOCKED:
        return "blocked";

    case STAGE2_REASON_MEDIATED:
        return "mediated";

    case STAGE2_REASON_FIRMWARE:
        return "firmware";

    case STAGE2_REASON_VIDEOCORE:
        return "videocore";

    default:
        return "unknown";
    }
}

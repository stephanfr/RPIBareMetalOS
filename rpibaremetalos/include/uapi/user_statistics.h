// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  User statistics.  Part of the user ABI (include/uapi): plain C, also included by the kernel, and including
//      nothing but other uapi headers and the compiler's own.  Two halves: defining statistics, and reading them
//      through the system calls declared in system_calls.h.

#include <stdint.h>

//  Defining statistics.
//
//  A user program defines each statistic at file scope with one of the USER_*_STATISTIC macros, which place it in
//      the .stats section that user.ld gathers into whole pages between text and data.  For a service (a process
//      the kernel starts with ForkServiceTask()) the loader backs those pages with kernel-owned memory, maps them
//      read-write into the process and registers its statistics before the first instruction; in any other
//      process they are private data, never registered.  Either way the program updates a value with a plain
//      atomic, no system call.

#define USER_STATISTIC_MAGIC               0x54415453u     /* "STAT" little-endian */

#define USER_STATISTIC_GAUGE               1u              /* values match system_stats::StatKind */
#define USER_STATISTIC_COUNTER             2u
#define USER_STATISTIC_TIMESTAMP           3u

#define USER_STATISTIC_GROUP_LENGTH        25              /* SystemStatistic::GROUP_MAX + 1 */
#define USER_STATISTIC_NAME_LENGTH         33              /* SystemStatistic::NAME_MAX + 1 */
#define USER_STATISTIC_DESCRIPTION_LENGTH  81              /* SystemStatistic::DESCRIPTION_MAX + 1 */

#define USER_STATISTIC_PRIVATE             0u              /* visibility, in flags */
#define USER_STATISTIC_PUBLIC              1u

#define USER_STATISTIC_KNOWN_FLAGS         (USER_STATISTIC_PUBLIC)

#define USER_STATISTIC_DESCRIPTOR_SIZE     160

typedef struct UserStatisticDescriptor
{
    uint32_t magic_;                                        //  USER_STATISTIC_MAGIC; an all-zero slot is padding
    uint32_t kind_;                                         //  USER_STATISTIC_GAUGE, _COUNTER or _TIMESTAMP
    int64_t  value_;                                        //  the only field read after loading; atomic access only
    char     group_[USER_STATISTIC_GROUP_LENGTH];           //  each string null-terminated within its array
    char     name_[USER_STATISTIC_NAME_LENGTH];
    char     description_[USER_STATISTIC_DESCRIPTION_LENGTH];
    uint8_t  flags_;                                        //  USER_STATISTIC_PRIVATE or USER_STATISTIC_PUBLIC
    uint32_t id_;                                           //  unique within the binary
} UserStatisticDescriptor;

#ifdef __cplusplus
static_assert(sizeof(UserStatisticDescriptor) == USER_STATISTIC_DESCRIPTOR_SIZE, "descriptors are packed at a fixed stride");
static_assert(alignof(UserStatisticDescriptor) == 8, "descriptors are 8-byte aligned");
static_assert(__builtin_offsetof(UserStatisticDescriptor, value_) == 8, "value is at a fixed offset");
static_assert(__builtin_offsetof(UserStatisticDescriptor, id_) == 156, "id is at a fixed offset");
#else
_Static_assert(sizeof(UserStatisticDescriptor) == USER_STATISTIC_DESCRIPTOR_SIZE, "descriptors are packed at a fixed stride");
_Static_assert(_Alignof(UserStatisticDescriptor) == 8, "descriptors are 8-byte aligned");
_Static_assert(__builtin_offsetof(UserStatisticDescriptor, value_) == 8, "value is at a fixed offset");
_Static_assert(__builtin_offsetof(UserStatisticDescriptor, id_) == 156, "id is at a fixed offset");
#endif

//  Every descriptor has the same size and alignment, so the linker packs the .stats section as an array.  A string
//      longer than its array does not compile in C++ and warns in C.  visibility, USER_STATISTIC_PRIVATE or
//      USER_STATISTIC_PUBLIC, has no default: exposing a statistic to other processes is always explicit.

#define USER_STATISTIC(variable, kind, visibility, id, group, name, description, initial_value)     \
    __attribute__((section(".stats"), used, aligned(8))) UserStatisticDescriptor variable =          \
        {USER_STATISTIC_MAGIC, (kind), (initial_value), group, name, description, (visibility), (id)}

#define USER_GAUGE_STATISTIC(variable, visibility, id, group, name, description, initial_value) \
    USER_STATISTIC(variable, USER_STATISTIC_GAUGE, visibility, id, group, name, description, initial_value)

#define USER_COUNTER_STATISTIC(variable, visibility, id, group, name, description, initial_value) \
    USER_STATISTIC(variable, USER_STATISTIC_COUNTER, visibility, id, group, name, description, initial_value)

#define USER_TIMESTAMP_STATISTIC(variable, visibility, id, group, name, description) \
    USER_STATISTIC(variable, USER_STATISTIC_TIMESTAMP, visibility, id, group, name, description, 0)

static inline int64_t UserStatisticGet(const UserStatisticDescriptor *statistic)
{
    return __atomic_load_n(&statistic->value_, __ATOMIC_RELAXED);
}

static inline void UserGaugeSet(UserStatisticDescriptor *statistic, int64_t value)
{
    __atomic_store_n(&statistic->value_, value, __ATOMIC_RELAXED);
}

//  Returns the value before the increment.

static inline int64_t UserCounterIncrement(UserStatisticDescriptor *statistic, int64_t amount)
{
    return __atomic_fetch_add(&statistic->value_, amount, __ATOMIC_RELAXED);
}

#if defined(__aarch64__)

static inline void UserTimestampStamp(UserStatisticDescriptor *statistic)
{
    uint64_t ticks;

    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(ticks));
    __atomic_store_n(&statistic->value_, (int64_t)ticks, __ATOMIC_RELAXED);
}

#endif

//  Reading statistics.  Each statistic call copies out a UserStatisticRecord: plain data, never an address.
//
//  Only a service's statistics are registered, and only those can be read.  A process sees its own statistics and
//      every process's USER_STATISTIC_PUBLIC ones; kernel statistics are not reachable.  A statistic it may not see
//      is reported exactly as an absent one, USER_STATISTIC_NOT_FOUND.
//
//  Each call returns USER_STATISTIC_SUCCESS and fills *record (or *process_id), or returns an error and leaves it
//      unchanged.
//
//  A statistic is identified by its process id and its id, unique within the binary.  Process ids are never
//      reused, so a pair kept after its process exits fails rather than reading a later statistic.

#define USER_STATISTIC_SUCCESS             0
#define USER_STATISTIC_NOT_FOUND           1               /* absent, not visible to the caller, or end of the list */
#define USER_STATISTIC_BAD_ADDRESS         2               /* record or process_id is not writable memory of the caller's */
#define USER_STATISTIC_INVALID_ARGUMENT    3               /* a string is unreadable, or longer than any can be */

#define USER_STATISTIC_SELF                0u              /* as a process id: the caller's own process */

#define USER_BINARY_PATH_LENGTH            128             /* longest binary path sc_FindProcess takes, null included */

#define USER_STATISTIC_RECORD_SIZE         176

//  16-byte aligned: GCC zeroes and copies it with 16-byte vector pair stores even under -mstrict-align, and those
//      fault on a merely 8-byte aligned record when SCTLR_EL1.A is set.

typedef struct __attribute__((aligned(16))) UserStatisticRecord
{
    uint32_t process_id_;                                   //  the owning process
    uint32_t id_;
    int64_t  value_;                                        //  as of the call
    uint32_t kind_;                                         //  USER_STATISTIC_GAUGE, _COUNTER or _TIMESTAMP
    uint32_t flags_;                                        //  USER_STATISTIC_PRIVATE or USER_STATISTIC_PUBLIC
    char     group_[USER_STATISTIC_GROUP_LENGTH];           //  null-terminated
    char     name_[USER_STATISTIC_NAME_LENGTH];
    char     description_[USER_STATISTIC_DESCRIPTION_LENGTH];
    char     reserved_[USER_STATISTIC_RECORD_SIZE - 24 - USER_STATISTIC_GROUP_LENGTH - USER_STATISTIC_NAME_LENGTH - USER_STATISTIC_DESCRIPTION_LENGTH];
} UserStatisticRecord;

#ifdef __cplusplus
static_assert(sizeof(UserStatisticRecord) == USER_STATISTIC_RECORD_SIZE, "the record layout is part of the system call ABI");
static_assert(alignof(UserStatisticRecord) == 16, "see the alignment note above");
#else
_Static_assert(sizeof(UserStatisticRecord) == USER_STATISTIC_RECORD_SIZE, "the record layout is part of the system call ABI");
_Static_assert(_Alignof(UserStatisticRecord) == 16, "see the alignment note above");
#endif

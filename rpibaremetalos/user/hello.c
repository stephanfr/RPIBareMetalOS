// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  The EL0 side of the address-space tests.  Built as a fixed-address flat binary linked
//      at USER_IMAGE_BASE (see user/user.ld) and loaded by LoadUserBinary().
//
//  main() receives ONE unsigned long in x0 -- there is no argv/envp yet -- so the case
//      number and its payload are packed into it:
//
//      case    = arg & 0xFF
//      payload = arg >> 8
//
//  Case numbering follows the plan's labels:
//
//      0  -> plan case 0    the positive case: it just prints
//      1  -> plan case 1    read the kernel linear map        -> permission fault, both models
//      2  -> plan case 1b   read 0x80000, the kernel image PA -> translation fault under
//                                kernel_high_user_low, permission fault under
//                                kernel_only_1_to_1.  Different fault, same outcome: the
//                                task is killed.  This is the case that proves the
//                                compatibility model did not quietly become a hole.
//      3  -> plan case 2    write user text                   -> permission fault (RO)
//      4  -> plan case 3    execute from the stack            -> permission fault (UXN)
//      5  -> plan case 4    per-task heap isolation           -> must print "isolated"
//
//  Every case must produce the same outcome under BOTH memory models.  That is the whole
//      point of the design.

typedef unsigned long uint64_t;
typedef unsigned int uint32_t;

extern void sc_Write(const char *buffer);
extern unsigned long sc_Malloc(unsigned long block_size);
extern void sc_Yield(void);
extern void sc_Exit(void);

//  Must match USER_IMAGE_BASE in include/platform/address_space_layout.h.

#define USER_IMAGE_BASE 0x0000004000400000UL
#define KERNEL_VA_BASE  0xFFFFFF8000000000UL

int main(unsigned long arg)
{
    const unsigned long test_case = arg & 0xFF;
    const unsigned long payload = arg >> 8;

    switch (test_case)
    {
        case 0:
            sc_Write("hello from EL0\n");
            break;

        case 1:
        {
            //  TCR_EL1.EPD1 is 0, so the walk really happens and finds a descriptor -- but
            //      its S2AP is EL1_READ_WRITE, so EL0 takes a permission fault.

            sc_Write("case 1: reading the kernel linear map\n");
            volatile uint64_t *kernel = (volatile uint64_t *)KERNEL_VA_BASE;
            uint64_t value = *kernel;
            (void)value;
            sc_Write("case 1: NOT KILLED\n");
            break;
        }

        case 2:
        {
            sc_Write("case 1b: reading 0x80000\n");
            volatile uint64_t *kernel_image = (volatile uint64_t *)0x80000UL;
            uint64_t value = *kernel_image;
            (void)value;
            sc_Write("case 1b: NOT KILLED\n");
            break;
        }

        case 3:
        {
            sc_Write("case 2: writing to user text\n");
            volatile uint32_t *text = (volatile uint32_t *)USER_IMAGE_BASE;
            *text = 0;
            sc_Write("case 2: NOT KILLED\n");
            break;
        }

        case 4:
        {
            //  Copy a `ret` onto the stack and branch to it.  The stack is mapped UXN, so
            //      the fetch faults.

            sc_Write("case 3: executing from the stack\n");
            volatile uint32_t code[2];
            code[0] = 0xD65F03C0;                   //  ret
            code[1] = 0xD65F03C0;
            void (*fn)(void) = (void (*)(void))(void *)code;
            fn();
            sc_Write("case 3: NOT KILLED\n");
            break;
        }

        case 5:
        {
            //  Two tasks run this concurrently with different payloads.  Both must print
            //      "isolated": the heap page each one gets must be its own.  This is what
            //      proves the spaces are DISTINCT rather than merely permission-protected,
            //      and it must hold under kernel_only_1_to_1 too -- the two tasks have
            //      separate L1 tables there as well, differing only in the kernel slots.

            unsigned long address = sc_Malloc(4096);

            if (address == (unsigned long)-1)
            {
                sc_Write("case 4: MALLOC FAILED\n");
                break;
            }

            volatile uint64_t *page = (volatile uint64_t *)address;

            *page = payload;
            sc_Yield();

            sc_Write(*page == payload ? "case 4: isolated\n" : "case 4: ALIASED\n");
            break;
        }

        default:
            sc_Write("unknown case\n");
            break;
    }

    return 0;
}

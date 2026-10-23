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
//      1  -> plan case 1    read the kernel linear map        -> permission fault (EL1-only)
//      2  -> plan case 1b   read 0x80000, the kernel image PA -> translation fault: nothing
//                                is mapped there in a task's TTBR0
//      3  -> plan case 2    write user text                   -> permission fault (RO)
//      4  -> plan case 3    execute from the stack            -> permission fault (UXN)
//      5  -> plan case 4    per-task heap isolation           -> must print "isolated"


typedef unsigned long uint64_t;
typedef unsigned int uint32_t;

extern void sc_Write(const char *buffer);
extern unsigned long sc_Malloc(unsigned long block_size);
extern void sc_Yield(void);
extern void sc_Exit(void);

//  Must match include/platform/address_space_layout.h (and os_memory_config.h).

#define USER_SPACE_BASE   0x0000004000000000UL
#define USER_IMAGE_BASE   (USER_SPACE_BASE + (4UL    << 20))
#define USER_GUARD_BOTTOM (USER_SPACE_BASE + (62UL << 30))     //  base + 62GB (64GB window, 1GB guard, 1GB stack)
#define KERNEL_VA_BASE    0xFFFFFF8000000000UL

//  1MB of stack, touched one page at a time from the top down: 256 demand faults.

static __attribute__((noinline)) int TouchDeepStack(void)
{
    volatile unsigned char frame[1024 * 1024];

    for (unsigned long i = 0; i < 256; i++)
    {
        frame[sizeof(frame) - 1 - (i * 4096)] = (unsigned char)i;
    }

    for (unsigned long i = 0; i < 256; i++)
    {
        if (frame[sizeof(frame) - 1 - (i * 4096)] != (unsigned char)i)
        {
            return 0;
        }
    }

    return 1;
}


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
            //      proves the spaces are DISTINCT rather than merely permission-protected.

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

        case 6:
        {
            //  Issue an out-of-range syscall number.  EL0_SyscallIndexOutOfRange routes
            //      this to HandleUserTaskFault, so the task must die here.

            sc_Write("case 5: invalid syscall (w8=99)\n");
            asm volatile("mov w8, #99\n\t"
                         "svc #0"
                         :
                         :
                         : "x8", "memory");
            sc_Write("case 5: NOT KILLED\n");
            break;
        }

        case 7:
        {
            //  Demand paging at scale.  Allocate 64 heap pages and touch each one for the
            //      first time (a translation fault), then read it back.  If any page-in failed
            //      the store would fault fatally and the OK line would never print.

            sc_Write("case 7: demand paging\n");

            int ok = 1;

            for (unsigned long i = 0; i < 64; i++)
            {
                unsigned long p = sc_Malloc(4096);

                if (p == (unsigned long)-1)
                {
                    ok = 0;
                    break;
                }

                volatile uint64_t *q = (volatile uint64_t *)p;

                if (*q != 0)                        //  first touch is a READ: must be a zeroed frame
                {
                    ok = 0;
                    break;
                }

                *q = 0x1000UL + i;

                if (*q != (0x1000UL + i))
                {
                    ok = 0;
                    break;
                }
            }

            sc_Write(ok ? "case 7: demand paging OK\n" : "case 7: demand paging FAILED\n");
            break;
        }

        case 8:
        {
            //  One page past the heap break is a wild pointer, not a page-in.

            sc_Write("case 8: writing above the heap break\n");
            unsigned long p = sc_Malloc(4096);

            if (p == (unsigned long)-1)
            {
                sc_Write("case 8: MALLOC FAILED\n");    //  otherwise *(-1) would "pass" by dying
                break;
            }

            *(volatile uint64_t *)p = 1;            //  inside the break: faults in, survives
            *(volatile uint64_t *)(p + 4096) = 1;   //  at the break: must be killed
            sc_Write("case 8: NOT KILLED\n");
            break;
        }

        case 9:
        {
            sc_Write("case 9: writing into the guard gap\n");
            *(volatile uint64_t *)USER_GUARD_BOTTOM = 1;
            sc_Write("case 9: NOT KILLED\n");
            break;
        }

        case 10:
        {
            sc_Write(TouchDeepStack() ? "case 10: deep stack OK\n" : "case 10: deep stack FAILED\n");
            break;
        }

        case 11:
        {
            //  The KERNEL is the first to touch a page.  The message sits at the very end of
            //      page one and its terminator is the first byte of page two, which nothing has
            //      touched.  sc_Write's string copy must fault page two in (zero-filled) to
            //      find the '\0'; otherwise the copy fails and the line silently never prints.

            static const char message[] = "case 11: kernel first touch OK\n";
            const unsigned long length = sizeof(message) - 1;

            unsigned long p = sc_Malloc(8192);

            if (p == (unsigned long)-1)
            {
                sc_Write("case 11: MALLOC FAILED\n");
                break;
            }

            char *s = (char *)(p + 4096 - length);

            for (unsigned long i = 0; i < length; i++)
            {
                s[i] = message[i];
            }

            sc_Write(s);
            break;
        }

        default:
            sc_Write("unknown case\n");
            break;
    }

    return 0;
}

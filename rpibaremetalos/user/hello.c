// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  The EL0 side of the address-space tests.  Built as a fixed-address flat binary linked
//      at S_USER_IMAGE_BASE (see user/user.template.ld) and loaded by LoadUserBinary().
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
//      ...
//      12 -> user statistics: sets the four below, then waits for the kernel to zero the gauge (a service)
//      13 -> the statistics system calls, run while a case 12 process (payload: its process id) waits (a service)
//      14 -> an ordinary process: its statistics are private data, never registered
//      15 -> sc_Sleep for payload milliseconds, timed with the system counter
//      16 -> writes to payload, a page the kernel maps but stage-2 doesn't, once the kernel sets the gauge to 2
//      17 -> writes and reads back offset 0x20 of payload, EL2's mediated test device, once the gauge is 2
//                                                         -> killed by the abort EL2 injects (a service)
//      18 -> the device calls on the test timer; payload holds CASE_18_* flags.  Reports 0x1800 + its failure count
//                in the gauge, then waits for the kernel to set it to 2 (a service)
//      19 -> serves the test-echo channel until asked to exit or crash (a service; the driver manager's test-echo
//                driver runs it from the copy embedded in the kernel).  With CASE_19_CHECKS it first reports 0x1900 +
//                its start-up failures in the gauge.  With CASE_19_EXPECT_* it only checks that opening the channel
//                is refused, and reports 0x1900 + its failure count as case 18 does


#include <stdint.h>

#include "system_calls.h"
#include "user_address_space.h"

//  The base of the kernel's linear map, which the fault cases probe.  Kernel addresses are not part of the user ABI,
//      so this is not in include/uapi: it must match S_KERNEL_VA_BASE in include/asm_config.h.

#define KERNEL_VA_BASE    0xFFFFFF8000000000UL

//  Registered by the loader before main() runs in a service loaded from this binary; in an ordinary process they
//      are private data.

#define HELLO_GAUGE_ID      1u
#define HELLO_COUNTER_ID    2u
#define HELLO_STAMP_ID      3u
#define HELLO_PUBLIC_ID     4u

USER_GAUGE_STATISTIC(hello_gauge, USER_STATISTIC_PRIVATE, HELLO_GAUGE_ID, "hello", "gauge", "Set to the case 12 payload", 0);
USER_COUNTER_STATISTIC(hello_counter, USER_STATISTIC_PRIVATE, HELLO_COUNTER_ID, "hello", "counter", "Incremented 1000 times by case 12", 0);
USER_TIMESTAMP_STATISTIC(hello_stamp, USER_STATISTIC_PRIVATE, HELLO_STAMP_ID, "hello", "stamp", "Stamped by case 12");
USER_GAUGE_STATISTIC(hello_public, USER_STATISTIC_PUBLIC, HELLO_PUBLIC_ID, "hello", "public", "Set to HELLO_PUBLIC_VALUE by case 12", 0);

#define HELLO_BINARY_PATH   "hello.bin"                         /* what the kernel tests fork */

#define HELLO_PUBLIC_VALUE  0x7075626CLL                        /* "publ" */
#define CASE_13_OWN_VALUE   0x1313LL
#define CASE_14_VALUE       0x1414LL

static uint32_t ProcessId(void)
{
    uint64_t id;
    __asm__ volatile("mrs %0, tpidrro_el0" : "=r"(id));
    return (uint32_t)id;
}

static int SameString(const char *a, const char *b)
{
    while ((*a != '\0') && (*a == *b))
    {
        a++;
        b++;
    }

    return *a == *b;
}

static uint64_t CounterTicks(void)
{
    uint64_t ticks;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(ticks));
    return ticks;
}

static uint64_t CounterFrequency(void)
{
    uint64_t frequency;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    return frequency;
}

//  Cases 16 and 17: sets the gauge to 1, then waits up to 10 s for the kernel to set it to 2.

static int WaitForTheKernel(const char *timeout_message)
{
    UserGaugeSet(&hello_gauge, 1);

    const uint64_t deadline = CounterTicks() + (10 * CounterFrequency());

    while (UserStatisticGet(&hello_gauge) != 2)
    {
        if (CounterTicks() > deadline)
        {
            sc_Write(timeout_message);
            return 0;
        }

        sc_Yield();
    }

    return 1;
}

//  Cases 18 and 19: sets the gauge to 'report', then waits up to 10 s for the kernel to set it to 2.

#define CASE_18_REPORT          0x1800LL
#define CASE_18_EXPECT_DENIED   0x1             /* forked without the capability */
#define CASE_18_EXPECT_BUSY     0x2             /* forked while another process owns the timer */
#define CASE_18_NO_INTERRUPTS   0x4             /* QEMU's raspi4b: the timer's interrupt never reaches the GIC */
#define CASE_18_INTERRUPTS      10

#define TIMER_CS                0x00
#define TIMER_CLO               0x04
#define TIMER_C3                0x18
#define TIMER_MATCH_3           0x08
#define TIMER_TICKS_PER_MS      1000            /* the system timer runs at 1 MHz */

static void ReportToTheKernel(long long report)
{
    UserGaugeSet(&hello_gauge, report);

    const uint64_t deadline = CounterTicks() + (10 * CounterFrequency());

    while ((UserStatisticGet(&hello_gauge) != 2) && (CounterTicks() <= deadline))
    {
        sc_Yield();
    }
}

static int Check(int passed, const char *failure)
{
    if (!passed)
    {
        sc_Write(failure);
    }

    return passed ? 0 : 1;
}

static int TestTimerInterrupts(uint32_t timer, uint64_t registers)
{
    int failures = 0;

    for (int i = 0; i < CASE_18_INTERRUPTS; i++)
    {
        DeviceWrite32(registers + TIMER_CS, TIMER_MATCH_3);
        DeviceWrite32(registers + TIMER_C3, DeviceRead32(registers + TIMER_CLO) + (2 * TIMER_TICKS_PER_MS));

        failures += Check(sc_InterruptWait(timer, 1000000) == DEVICE_SUCCESS, "case 18: FAIL no timer interrupt within 1 s\n");
        failures += Check((DeviceRead32(registers + TIMER_CS) & TIMER_MATCH_3) != 0, "case 18: FAIL woken without a match\n");

        DeviceWrite32(registers + TIMER_CS, TIMER_MATCH_3);

        failures += Check(sc_InterruptAck(timer) == DEVICE_SUCCESS, "case 18: FAIL the ack was refused\n");
    }

    return failures;
}

static int TestDeviceCalls(unsigned long flags)
{
    uint32_t timer = 0;
    uint64_t registers = 0;
    int failures = 0;

    failures += Check(sc_DeviceOpen("no-such-device", &timer, &registers) == DEVICE_NOT_FOUND, "case 18: FAIL an unknown device was not refused\n");
    failures += Check(sc_InterruptWait(0, 1000) == DEVICE_INVALID_HANDLE, "case 18: FAIL a wait on an unopened device was not refused\n");
    failures += Check(sc_InterruptAck(0x100) == DEVICE_INVALID_HANDLE, "case 18: FAIL a bad handle was not refused\n");

    const int opened = sc_DeviceOpen(DEVICE_NAME_TEST_TIMER, &timer, &registers);

    if (flags & CASE_18_EXPECT_DENIED)
    {
        failures += Check(opened == DEVICE_NOT_PERMITTED, "case 18: FAIL opened without the capability\n");
        return failures;
    }

    if (flags & CASE_18_EXPECT_BUSY)
    {
        failures += Check(opened == DEVICE_BUSY, "case 18: FAIL opened a device another process owns\n");
        return failures;
    }

    if (opened != DEVICE_SUCCESS)
    {
        sc_Write("case 18: FAIL the test timer did not open\n");
        return failures + 1;
    }

    uint32_t reopened = 0xFFFFFFFF;
    uint64_t registers_again = 0;

    failures += Check((sc_DeviceOpen(DEVICE_NAME_TEST_TIMER, &reopened, &registers_again) == DEVICE_SUCCESS) &&
                      (reopened == timer) && (registers_again == registers),
                      "case 18: FAIL opening it again gave a different answer\n");
    failures += Check((registers >= S_USER_DEVICE_BASE) && (registers < S_USER_DEVICE_TOP), "case 18: FAIL mapped outside the device windows\n");
    failures += Check(sc_DeviceOpen(DEVICE_NAME_TEST_TIMER, &timer, (uint64_t *)KERNEL_VA_BASE) == DEVICE_BAD_ADDRESS,
                      "case 18: FAIL a kernel address for the result was not refused\n");

    const uint32_t before = DeviceRead32(registers + TIMER_CLO);
    sc_Sleep(2000);
    failures += Check(DeviceRead32(registers + TIMER_CLO) - before >= 2 * TIMER_TICKS_PER_MS, "case 18: FAIL the timer's counter did not advance\n");

    //  Nothing armed: C3 a full counter wrap away, and any match from before cleared and its ring drained.

    DeviceWrite32(registers + TIMER_CS, TIMER_MATCH_3);
    DeviceWrite32(registers + TIMER_C3, DeviceRead32(registers + TIMER_CLO) - 1);
    sc_InterruptAck(timer);
    sc_InterruptWait(timer, 1000);

    failures += Check(sc_InterruptWait(timer, 20000) == DEVICE_TIMED_OUT, "case 18: FAIL a wait with nothing armed did not time out\n");

    if (!(flags & CASE_18_NO_INTERRUPTS))
    {
        failures += TestTimerInterrupts(timer, registers);

        if (failures == 0)
        {
            sc_Write("case 18: device interrupts OK\n");
        }
    }

    if (failures == 0)
    {
        sc_Write("case 18: device calls OK\n");
    }

    return failures;
}

//  Case 19: the test-echo channel's protocol.  The first byte of a request is its operation; `test drivers` sends
//      them (src/c/cli/test_command.cpp).

#define CASE_19_REPORT          0x1900LL
#define CASE_19_EXPECT_DENIED   0x1             /* forked without the capability */
#define CASE_19_EXPECT_BUSY     0x2             /* forked while another process serves the channel */
#define CASE_19_CHECKS          0x4             /* nobody calls until it reports its start-up checks in the gauge */

#define ECHO_OP_ECHO            1               /* replies with each byte of the request plus one */
#define ECHO_OP_SLOW            2               /* sleeps for the little-endian milliseconds in bytes 1-4, then replies */
#define ECHO_OP_STATUS          3               /* replies with its failure count, then the replies the kernel dropped */
#define ECHO_OP_OVERSIZE        4               /* tries replies too long, then replies with 8 bytes of 0x44 */
#define ECHO_OP_EXIT            5               /* exits without replying */
#define ECHO_OP_DATA            6               /* replies with the data's length, and the data with each byte complemented */
#define ECHO_OP_CRASH           7               /* writes to the kernel's memory, and is killed */

#define ECHO_DATA_SIZE          0x10000         /* the test-echo channel's data area */

#define ECHO_IDLE_US            10000000        /* exits after 10 s with no request */

static int TestChannelRefused(unsigned long flags)
{
    uint32_t channel = 0;
    uint64_t page = 0;
    uint64_t data_size = 0;
    const int opened = sc_ChannelOpen(CHANNEL_NAME_TEST_ECHO, &channel, &page, &data_size);

    if (flags & CASE_19_EXPECT_DENIED)
    {
        return Check(opened == CHANNEL_NOT_PERMITTED, "case 19: FAIL served a channel without the capability\n");
    }

    return Check(opened == CHANNEL_BUSY, "case 19: FAIL served a channel another process serves\n");
}

static void PutWord(volatile uint8_t *bytes, uint32_t value)
{
    for (int i = 0; i < 4; i++)
    {
        bytes[i] = (uint8_t)(value >> (8 * i));
    }
}

static void ServeEchoChannel(unsigned long flags)
{
    uint32_t channel = 0;
    uint64_t page = 0;
    uint64_t data_size = 0;
    uint64_t data_length = 0;
    uint32_t failures = 0;
    uint32_t abandoned = 0;

    failures += Check(sc_ChannelOpen("no-such-channel", &channel, &page, &data_size) == CHANNEL_NOT_FOUND, "case 19: FAIL an unknown channel was not refused\n");
    failures += Check(sc_ChannelReceive(0, 1000, 0) == CHANNEL_INVALID_HANDLE, "case 19: FAIL a receive on an unopened channel was not refused\n");
    failures += Check(sc_ChannelReply(0x100, 0, 0) == CHANNEL_INVALID_HANDLE, "case 19: FAIL a bad handle was not refused\n");

    if (sc_ChannelOpen(CHANNEL_NAME_TEST_ECHO, &channel, &page, &data_size) != CHANNEL_SUCCESS)
    {
        sc_Write("case 19: FAIL the test-echo channel did not open\n");
        return;
    }

    uint32_t reopened = 0xFFFFFFFF;
    uint64_t page_again = 0;
    uint64_t data_size_again = 0;

    failures += Check((sc_ChannelOpen(CHANNEL_NAME_TEST_ECHO, &reopened, &page_again, &data_size_again) == CHANNEL_SUCCESS) &&
                      (reopened == channel) && (page_again == page) && (data_size_again == data_size),
                      "case 19: FAIL opening it again gave a different answer\n");
    failures += Check(data_size == ECHO_DATA_SIZE, "case 19: FAIL the data area is the wrong size\n");
    failures += Check((page >= S_USER_CHANNEL_BASE) && (page + CHANNEL_DATA_OFFSET + data_size <= S_USER_CHANNEL_TOP),
                      "case 19: FAIL mapped outside the channel memory\n");
    failures += Check(sc_ChannelReply(channel, 0, 0) == CHANNEL_NOT_SERVING, "case 19: FAIL a reply with no request was not refused\n");
    failures += Check(sc_ChannelReply(channel, 0, data_size + 1) == CHANNEL_TOO_LONG, "case 19: FAIL a reply with too much data was not refused\n");
    failures += Check(sc_ChannelReceive(channel, 20000, (uint64_t *)KERNEL_VA_BASE) == CHANNEL_BAD_ADDRESS,
                      "case 19: FAIL a kernel address for the data length was not refused\n");

    //  Only while nobody calls: as a managed driver, its first request may already be waiting.

    if (flags & CASE_19_CHECKS)
    {
        failures += Check(sc_ChannelReceive(channel, 20000, &data_length) == CHANNEL_TIMED_OUT, "case 19: FAIL a receive with no caller did not time out\n");
        UserGaugeSet(&hello_gauge, CASE_19_REPORT + failures);
    }

    volatile uint8_t *request = (volatile uint8_t *)(page + CHANNEL_REQUEST_OFFSET);
    volatile uint8_t *reply = (volatile uint8_t *)(page + CHANNEL_REPLY_OFFSET);
    volatile uint8_t *data = (volatile uint8_t *)(page + CHANNEL_DATA_OFFSET);

    for (;;)
    {
        const int length = sc_ChannelReceive(channel, ECHO_IDLE_US, &data_length);

        if (length == CHANNEL_TIMED_OUT)
        {
            sc_Write("case 19: FAIL no request for 10 s\n");
            return;
        }

        if ((length < 1) || (length > CHANNEL_MESSAGE_MAX))
        {
            sc_Write("case 19: FAIL a receive returned an impossible length\n");
            return;
        }

        int result = CHANNEL_SUCCESS;

        switch (request[0])
        {
            case ECHO_OP_ECHO:
                failures += Check(sc_ChannelReceive(channel, 1000, 0) == CHANNEL_REPLY_PENDING, "case 19: FAIL a second receive was not refused\n");
                failures += Check(data_length == 0, "case 19: FAIL an echo came with data\n");

                for (int i = 0; i < length; i++)
                {
                    reply[i] = (uint8_t)(request[i] + 1);
                }

                result = sc_ChannelReply(channel, (uint64_t)length, 0);
                break;

            case ECHO_OP_DATA:
                for (uint64_t i = 0; i < data_length; i++)
                {
                    data[i] = (uint8_t)~data[i];
                }

                PutWord(reply, (uint32_t)data_length);
                result = sc_ChannelReply(channel, 4, data_length);
                break;

            case ECHO_OP_SLOW:
            {
                const uint32_t milliseconds = (uint32_t)request[1] | ((uint32_t)request[2] << 8) | ((uint32_t)request[3] << 16) | ((uint32_t)request[4] << 24);

                sc_Sleep((uint64_t)milliseconds * 1000);

                reply[0] = ECHO_OP_SLOW;
                result = sc_ChannelReply(channel, 1, 0);

                if (result == CHANNEL_ABANDONED)
                {
                    abandoned++;
                    result = CHANNEL_SUCCESS;
                }

                break;
            }

            case ECHO_OP_STATUS:
                PutWord(reply, failures);
                PutWord(reply + 4, abandoned);
                result = sc_ChannelReply(channel, 8, 0);
                break;

            case ECHO_OP_OVERSIZE:
                failures += Check(sc_ChannelReply(channel, CHANNEL_MESSAGE_MAX + 1, 0) == CHANNEL_TOO_LONG, "case 19: FAIL an overlong reply was not refused\n");
                failures += Check(sc_ChannelReply(channel, 16, 0) == CHANNEL_TOO_LONG, "case 19: FAIL a reply longer than the caller takes was not refused\n");
                failures += Check(sc_ChannelReply(channel, 8, 1) == CHANNEL_TOO_LONG, "case 19: FAIL data the caller doesn't take was not refused\n");

                for (int i = 0; i < 8; i++)
                {
                    reply[i] = 0x44;
                }

                result = sc_ChannelReply(channel, 8, 0);
                break;

            case ECHO_OP_EXIT:
                return;

            case ECHO_OP_CRASH:
                *(volatile uint64_t *)KERNEL_VA_BASE = 0;
                sc_Write("case 19: FAIL NOT KILLED\n");
                return;

            default:
                sc_Write("case 19: FAIL an unknown operation\n");
                failures++;
                result = sc_ChannelReply(channel, 0, 0);
                break;
        }

        failures += Check(result == CHANNEL_SUCCESS, "case 19: FAIL a reply was refused\n");
    }
}

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
            volatile uint32_t *text = (volatile uint32_t *)S_USER_IMAGE_BASE;
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
            *(volatile uint64_t *)S_USER_GUARD_BOTTOM = 1;
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

        case 12:
        {
            //  The kernel test reads these through its own mapping of the same page, then zeroes
            //      the gauge to say it has seen them -- which this process must see too.

            UserGaugeSet(&hello_gauge, (int64_t)payload);

            for (uint32_t i = 0; i < 1000; i++)
            {
                UserCounterIncrement(&hello_counter, 1);
            }

            UserTimestampStamp(&hello_stamp);
            UserGaugeSet(&hello_public, HELLO_PUBLIC_VALUE);

            sc_Write("case 12: statistics set\n");

            const uint64_t deadline = CounterTicks() + (10 * CounterFrequency());

            while (UserStatisticGet(&hello_gauge) == (int64_t)payload)
            {
                if (CounterTicks() > deadline)
                {
                    sc_Write("case 12: TIMED OUT waiting for the kernel\n");
                    return 0;
                }

                sc_Yield();
            }

            sc_Write("case 12: kernel write seen\n");
            break;
        }

        case 13:
        {
            //  payload is the process id of a case 12 process that is still running: it has the same four
            //      statistics as this one, and its public one holds HELLO_PUBLIC_VALUE.

            const uint32_t self = ProcessId();
            const uint32_t other = (uint32_t)payload;

            UserStatisticRecord record;

            //  Its own private statistic: found by name, then read again by its fixed id.

            UserGaugeSet(&hello_gauge, CASE_13_OWN_VALUE);

            if ((sc_FindStatistic(USER_STATISTIC_SELF, "hello", "gauge", &record) != USER_STATISTIC_SUCCESS) ||
                (record.process_id_ != self) || (record.id_ != HELLO_GAUGE_ID) || (record.value_ != CASE_13_OWN_VALUE) ||
                (record.kind_ != USER_STATISTIC_GAUGE) || (record.flags_ != USER_STATISTIC_PRIVATE) ||
                !SameString(record.name_, "gauge"))
            {
                sc_Write("case 13: FAIL own statistic not found by name\n");
                return 0;
            }

            UserGaugeSet(&hello_gauge, CASE_13_OWN_VALUE + 1);

            if ((sc_ReadStatistic(USER_STATISTIC_SELF, HELLO_GAUGE_ID, &record) != USER_STATISTIC_SUCCESS) ||
                (record.process_id_ != self) || (record.id_ != HELLO_GAUGE_ID) || (record.value_ != CASE_13_OWN_VALUE + 1) ||
                (sc_ReadStatistic(self, HELLO_GAUGE_ID, &record) != USER_STATISTIC_SUCCESS) ||
                (sc_ReadStatistic(USER_STATISTIC_SELF, HELLO_PUBLIC_ID + 1, &record) != USER_STATISTIC_NOT_FOUND))
            {
                sc_Write("case 13: FAIL own statistic not read by id\n");
                return 0;
            }

            sc_Write("case 13: own statistic readable\n");

            //  Both processes were loaded from this binary, so both are found by its path, in ascending order.

            uint32_t process = 0;
            uint32_t self_found = 0;
            uint32_t other_found = 0;

            for (uint32_t after = 0; sc_FindProcess(HELLO_BINARY_PATH, after, &process) == USER_STATISTIC_SUCCESS; after = process)
            {
                if (process <= after)
                {
                    sc_Write("case 13: FAIL processes were not found in ascending order\n");
                    return 0;
                }

                self_found += (process == self) ? 1 : 0;
                other_found += (process == other) ? 1 : 0;
            }

            if ((self_found != 1) || (other_found != 1) ||
                (sc_FindProcess("no-such-binary.bin", 0, &process) != USER_STATISTIC_NOT_FOUND))
            {
                sc_Write("case 13: FAIL processes not found by binary\n");
                return 0;
            }

            sc_Write("case 13: processes found by binary\n");

            //  The other process's private statistics do not exist as far as this process can tell: not by
            //      name and not by id.

            if ((sc_FindStatistic(other, "hello", "gauge", &record) != USER_STATISTIC_NOT_FOUND) ||
                (sc_FindStatistic(other, "hello", "counter", &record) != USER_STATISTIC_NOT_FOUND) ||
                (sc_ReadStatistic(other, HELLO_GAUGE_ID, &record) != USER_STATISTIC_NOT_FOUND))
            {
                sc_Write("case 13: FAIL another process's private statistic was returned\n");
                return 0;
            }

            sc_Write("case 13: other process private statistics hidden\n");

            //  Its public statistic is readable, by name and by its fixed id.

            if ((sc_FindStatistic(other, "hello", "public", &record) != USER_STATISTIC_SUCCESS) ||
                (record.process_id_ != other) || (record.id_ != HELLO_PUBLIC_ID) || (record.value_ != HELLO_PUBLIC_VALUE) ||
                (sc_ReadStatistic(other, HELLO_PUBLIC_ID, &record) != USER_STATISTIC_SUCCESS) ||
                (record.process_id_ != other) || (record.value_ != HELLO_PUBLIC_VALUE))
            {
                sc_Write("case 13: FAIL another process's public statistic was not returned\n");
                return 0;
            }

            sc_Write("case 13: other process public statistic readable\n");

            //  Kernel addresses: refused, not dereferenced.

            if ((sc_ReadStatistic(USER_STATISTIC_SELF, HELLO_GAUGE_ID, (UserStatisticRecord *)KERNEL_VA_BASE) != USER_STATISTIC_BAD_ADDRESS) ||
                (sc_FindProcess(HELLO_BINARY_PATH, 0, (uint32_t *)KERNEL_VA_BASE) != USER_STATISTIC_BAD_ADDRESS) ||
                (sc_FindProcess((const char *)KERNEL_VA_BASE, 0, &process) != USER_STATISTIC_INVALID_ARGUMENT) ||
                (sc_FindProcess("", 0, &process) != USER_STATISTIC_INVALID_ARGUMENT) ||
                (sc_FindStatistic(USER_STATISTIC_SELF, (const char *)KERNEL_VA_BASE, "gauge", &record) != USER_STATISTIC_INVALID_ARGUMENT) ||
                (sc_FindStatistic(USER_STATISTIC_SELF, "hello", "a name longer than any statistic can have", &record) != USER_STATISTIC_INVALID_ARGUMENT))
            {
                sc_Write("case 13: FAIL a bad argument was not refused\n");
                return 0;
            }

            sc_Write("case 13: bad arguments refused\n");
            break;
        }

        case 14:
        {
            //  Not a service: the statistics work as this process's own memory, and nothing outside it knows of
            //      them -- not even this process through the system calls.

            UserStatisticRecord record;

            UserGaugeSet(&hello_gauge, CASE_14_VALUE);
            UserCounterIncrement(&hello_counter, 1);

            if ((UserStatisticGet(&hello_gauge) != CASE_14_VALUE) || (UserStatisticGet(&hello_counter) != 1) ||
                (sc_FindStatistic(USER_STATISTIC_SELF, "hello", "gauge", &record) != USER_STATISTIC_NOT_FOUND) ||
                (sc_ReadStatistic(USER_STATISTIC_SELF, HELLO_GAUGE_ID, &record) != USER_STATISTIC_NOT_FOUND))
            {
                sc_Write("case 14: FAIL statistics of an ordinary process\n");
                break;
            }

            sc_Write("case 14: statistics private to an ordinary process\n");
            break;
        }

        case 15:
        {
            const uint64_t requested_ticks = (CounterFrequency() * payload) / 1000;
            const uint64_t start = CounterTicks();

            sc_Sleep(payload * 1000);

            const uint64_t elapsed = CounterTicks() - start;

            sc_Write(elapsed >= requested_ticks ? "case 15: sleep OK\n" : "case 15: sleep returned early\n");
            break;
        }

        case 16:
        {
            if (!WaitForTheKernel("case 16: TIMED OUT waiting for the kernel\n"))
            {
                return 0;
            }

            sc_Write("case 16: writing to a page stage-2 blocks\n");
            *(volatile uint64_t *)payload = 0x16;
            sc_Write("case 16: NOT KILLED\n");
            break;
        }

        case 17:
        {
            if (!WaitForTheKernel("case 17: TIMED OUT waiting for the kernel\n"))
            {
                return 0;
            }

            volatile uint32_t *test_register = (volatile uint32_t *)(payload + 0x20);

            *test_register = 0x17171717;
            sc_Write((*test_register == 0x17171717) ? "case 17: mediated echo OK\n" : "case 17: mediated echo WRONG\n");
            break;
        }

        case 18:
        {
            const int failures = TestDeviceCalls(payload);

            if (payload & (CASE_18_EXPECT_DENIED | CASE_18_EXPECT_BUSY))
            {
                sc_Write(failures == 0 ? "case 18: device refused OK\n" : "case 18: device refusal WRONG\n");
            }

            ReportToTheKernel(CASE_18_REPORT + failures);
            break;
        }

        case 19:
        {
            if (payload & (CASE_19_EXPECT_DENIED | CASE_19_EXPECT_BUSY))
            {
                const int failures = TestChannelRefused(payload);

                sc_Write(failures == 0 ? "case 19: channel refused OK\n" : "case 19: channel refusal WRONG\n");
                ReportToTheKernel(CASE_19_REPORT + failures);
                break;
            }

            ServeEchoChannel(payload);
            break;
        }

        default:
            sc_Write("unknown case\n");
            break;
    }

    return 0;
}

// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  The SD card driver: serves the sd-card channel (sd_card.h) with the board's SDHCI controller, by PIO.  EL2 traps
//      every register access and refuses DMA, so each 32-bit access here costs a trip to EL2.
//
//  It brings the card up before it opens the channel, so the kernel's calls wait until the card is ready.  A driver
//      killed mid-transfer leaves the card in its data state; its successor resets the controller and the card.
//
//  Waits poll, briefly at full speed and then sleeping between polls: a block is usually ready within the spin, and
//      the sleeps keep a slow card from holding the core.

#include "sd_card.h"
#include "system_calls.h"

//  SDHCI registers, as the Pi's controllers lay them out (32-bit access).

#define REG_BLOCK_SIZE_COUNT            0x04
#define REG_ARG1                        0x08
#define REG_COMMAND                     0x0C
#define REG_RESPONSE0                   0x10
#define REG_RESPONSE1                   0x14
#define REG_RESPONSE2                   0x18
#define REG_RESPONSE3                   0x1C
#define REG_DATA                        0x20
#define REG_STATUS                      0x24
#define REG_CONTROL0                    0x28
#define REG_CONTROL1                    0x2C
#define REG_INT_FLAGS                   0x30
#define REG_INT_MASK                    0x34
#define REG_INT_ENABLE                  0x38
#define REG_CONTROL2                    0x3C

#define REG_CONTROL1_RESET_BYTE         0x2F    /* BCM2712: the software reset byte alone */

#define BCM2712_SDIO_CFG_CTRL           0x400
#define BCM2712_SDIO_CFG_PIN_SEL        0x444
#define BCM2712_SDCD_N_TEST_EN          (1u << 31)
#define BCM2712_SDCD_N_TEST_LEV         (1u << 30)
#define BCM2712_PIN_SEL_MASK            0x3u
#define BCM2712_PIN_SEL_SD              (1u << 1)

#define CONTROL0_BUS_POWER_3V3          (0x0Fu << 8)
#define CONTROL0_FOUR_DATA_LINES        (1u << 1)

#define CONTROL1_RESET_DATA             0x04000000u
#define CONTROL1_RESET_COMMAND          0x02000000u
#define CONTROL1_RESET_HOST             0x01000000u
#define CONTROL1_RESET_ALL              (CONTROL1_RESET_DATA | CONTROL1_RESET_COMMAND | CONTROL1_RESET_HOST)
#define CONTROL1_CLOCK_ENABLE           0x00000004u
#define CONTROL1_CLOCK_STABLE           0x00000002u
#define CONTROL1_CLOCK_INTERNAL_ENABLE  0x00000001u
#define CONTROL1_DATA_TIMEOUT_MASK      (0xFu << 16)
#define CONTROL1_DATA_TIMEOUT           (11u << 16)

#define STATUS_COMMAND_INHIBIT          0x00000001u
#define STATUS_DATA_INHIBIT             0x00000002u

#define INT_ERROR_MASK                  0xFFFF0000u
#define INT_ALL                         0xFFFFFFFFu
#define INT_COMMAND_TIMEOUT             0x00010000u
#define INT_ERROR                       0x00008000u
#define INT_CARD                        0x00000100u
#define INT_READ_READY                  0x00000020u
#define INT_WRITE_READY                 0x00000010u
#define INT_DATA_DONE                   0x00000002u
#define INT_COMMAND_DONE                0x00000001u

//  The command register: what the kernel's EMMCCommand bit-field table spelled out.

#define CMD_BLOCK_COUNT                 (1u << 1)
#define CMD_AUTO_CMD12                  (1u << 2)
#define CMD_READ                        (1u << 4)
#define CMD_MULTI_BLOCK                 (1u << 5)
#define CMD_RESPONSE_MASK               (3u << 16)
#define CMD_RESPONSE_136                (1u << 16)
#define CMD_RESPONSE_48                 (2u << 16)
#define CMD_RESPONSE_48_BUSY            (3u << 16)
#define CMD_CRC                         (1u << 19)
#define CMD_DATA                        (1u << 21)
#define CMD_ABORT                       (3u << 22)
#define CMD_INDEX(index)                ((uint32_t)(index) << 24)

#define CMD_GO_IDLE                     (CMD_INDEX(0))
#define CMD_SEND_CID                    (CMD_INDEX(2) | CMD_RESPONSE_136 | CMD_CRC)
#define CMD_SEND_RELATIVE_ADDR          (CMD_INDEX(3) | CMD_RESPONSE_48 | CMD_CRC)
#define CMD_APP_SET_BUS_WIDTH           (CMD_INDEX(6) | CMD_RESPONSE_48 | CMD_CRC)
#define CMD_IO_SET_OP_COND              (CMD_INDEX(5) | CMD_RESPONSE_136)
#define CMD_SELECT_CARD                 (CMD_INDEX(7) | CMD_RESPONSE_48_BUSY | CMD_CRC)
#define CMD_SEND_IF_COND                (CMD_INDEX(8) | CMD_RESPONSE_48 | CMD_CRC)
#define CMD_STOP_TRANSMISSION           (CMD_INDEX(12) | CMD_RESPONSE_48_BUSY | CMD_CRC | CMD_ABORT)
#define CMD_SET_BLOCK_LEN               (CMD_INDEX(16) | CMD_RESPONSE_48 | CMD_CRC)
#define CMD_READ_BLOCK                  (CMD_INDEX(17) | CMD_RESPONSE_48 | CMD_CRC | CMD_DATA | CMD_READ)
#define CMD_READ_MULTIPLE               (CMD_INDEX(18) | CMD_RESPONSE_48 | CMD_CRC | CMD_DATA | CMD_READ | CMD_BLOCK_COUNT | CMD_AUTO_CMD12 | CMD_MULTI_BLOCK)
#define CMD_WRITE_BLOCK                 (CMD_INDEX(24) | CMD_RESPONSE_48 | CMD_CRC | CMD_DATA)
#define CMD_WRITE_MULTIPLE              (CMD_INDEX(25) | CMD_RESPONSE_48 | CMD_CRC | CMD_DATA | CMD_BLOCK_COUNT | CMD_AUTO_CMD12 | CMD_MULTI_BLOCK)
#define CMD_APP_OCR_CHECK               (CMD_INDEX(41) | CMD_RESPONSE_48)
#define CMD_APP_SEND_SCR                (CMD_INDEX(51) | CMD_RESPONSE_48 | CMD_CRC | CMD_DATA | CMD_READ)
#define CMD_APP                         (CMD_INDEX(55) | CMD_RESPONSE_48 | CMD_CRC)

//  The SCR arrives most significant byte first, so its SD_BUS_WIDTHS bit for four lines (SCR bit 50) is bit 10 of
//      scr[0].

#define SCR_FOUR_DATA_LINES             (1u << 10)
#define BUS_WIDTH_FOUR                  2u

#define CLOCK_SETUP_HZ                  400000u
#define CLOCK_NORMAL_HZ                 25000000u
#define MAX_CLOCK_DIVIDER               512u

#define CARD_RESET_ATTEMPTS             10
#define DATA_COMMAND_ATTEMPTS           3

#define SPIN_US                         200
#define POLL_SLEEP_US                   500

//  Results inside the driver.  ERROR_WITH_FLAGS carries the interrupt register's error bits in error_flags.

#define OK                              0
#define FAILED                          -1
#define ERROR_WITH_FLAGS                -2

typedef struct Sd
{
    uint64_t registers;
    uint32_t controller;
    uint32_t host_clock_hz;
    uint64_t ticks_per_us;

    uint32_t transfer_blocks;
    uint32_t block_size;
    uint32_t *buffer;
    uint32_t response[4];
    uint32_t error_flags;

    int sdhc;
    uint32_t relative_card_address;
    uint32_t scr[2];

    uint32_t command_retries;
    uint32_t transfer_aborts;
    uint32_t card_resets;
} Sd;

static Sd sd;

static uint32_t Read(uint32_t offset)
{
    return DeviceRead32(sd.registers + offset);
}

static void Write(uint32_t offset, uint32_t value)
{
    DeviceWrite32(sd.registers + offset, value);
}

static uint64_t Ticks(void)
{
    uint64_t ticks;

    __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(ticks));

    return ticks;
}

static void SleepMs(uint32_t milliseconds)
{
    sc_Sleep((uint64_t)milliseconds * 1000);
}

//  True once (register & mask) is non-zero, or zero when !set; false after timeout_ms.

static int WaitFor(uint32_t offset, uint32_t mask, int set, uint32_t timeout_ms)
{
    const uint64_t start = Ticks();
    const uint64_t spin_end = start + (SPIN_US * sd.ticks_per_us);
    const uint64_t deadline = start + ((uint64_t)timeout_ms * 1000 * sd.ticks_per_us);

    for (;;)
    {
        if (((Read(offset) & mask) != 0) == (set != 0))
        {
            return 1;
        }

        const uint64_t now = Ticks();

        if (now >= deadline)
        {
            return 0;
        }

        if (now >= spin_end)
        {
            sc_Sleep(POLL_SLEEP_US);
        }
    }
}

static int TransferData(uint32_t command)
{
    const int write = (command & CMD_READ) == 0;
    const uint32_t ready = write ? INT_WRITE_READY : INT_READ_READY;
    uint32_t *data = sd.buffer;

    for (uint32_t block = 0; block < sd.transfer_blocks; block++)
    {
        WaitFor(REG_INT_FLAGS, ready | INT_ERROR, 1, 2000);

        const uint32_t flags = Read(REG_INT_FLAGS);

        Write(REG_INT_FLAGS, ready | INT_ERROR);

        if ((flags & (INT_ERROR_MASK | ready)) != ready)
        {
            return FAILED;
        }

        if (write)
        {
            for (uint32_t length = sd.block_size; length > 0; length -= 4)
            {
                Write(REG_DATA, *data++);
            }
        }
        else
        {
            for (uint32_t length = sd.block_size; length > 0; length -= 4)
            {
                *data++ = Read(REG_DATA);
            }
        }
    }

    return OK;
}

static int ResetLines(uint32_t lines)
{
    Write(REG_CONTROL1, Read(REG_CONTROL1) | lines);

    return WaitFor(REG_CONTROL1, lines, 0, 10000);
}

static void ResetCommandAndDataLines(void)
{
    ResetLines(CONTROL1_RESET_COMMAND | CONTROL1_RESET_DATA);

    Write(REG_INT_FLAGS, INT_ALL);
}

static int IssueCommand(uint32_t command, uint32_t argument, uint32_t timeout_ms);

//  Auto CMD12 follows only the last block, so a multi-block transfer cut short leaves the card in its data state,
//      where it rejects the next read or write: stop it explicitly.

static void AbortDataTransfer(uint32_t command)
{
    sd.transfer_aborts++;

    ResetCommandAndDataLines();

    if ((command & CMD_MULTI_BLOCK) != 0)
    {
        IssueCommand(CMD_STOP_TRANSMISSION, 0, 500);
        ResetCommandAndDataLines();
    }
}

static int IssueCommand(uint32_t command, uint32_t argument, uint32_t timeout_ms)
{
    sd.error_flags = 0;

    if (sd.transfer_blocks > 0xFFFF)
    {
        return FAILED;
    }

    Write(REG_BLOCK_SIZE_COUNT, sd.block_size | (sd.transfer_blocks << 16));
    Write(REG_ARG1, argument);
    Write(REG_COMMAND, command);

    if (!WaitFor(REG_INT_FLAGS, INT_ERROR | INT_COMMAND_DONE, 1, timeout_ms))
    {
        return FAILED;
    }

    uint32_t flags = Read(REG_INT_FLAGS);

    Write(REG_INT_FLAGS, INT_ERROR_MASK | INT_COMMAND_DONE);

    if ((flags & (INT_ERROR_MASK | INT_COMMAND_DONE)) != INT_COMMAND_DONE)
    {
        sd.error_flags = flags & INT_ERROR_MASK;
        return ERROR_WITH_FLAGS;
    }

    switch (command & CMD_RESPONSE_MASK)
    {
    case CMD_RESPONSE_48:
    case CMD_RESPONSE_48_BUSY:
        sd.response[0] = Read(REG_RESPONSE0);
        break;

    case CMD_RESPONSE_136:
        sd.response[0] = Read(REG_RESPONSE0);
        sd.response[1] = Read(REG_RESPONSE1);
        sd.response[2] = Read(REG_RESPONSE2);
        sd.response[3] = Read(REG_RESPONSE3);
        break;
    }

    const int data = (command & CMD_DATA) != 0;

    if (data && (TransferData(command) != OK))
    {
        AbortDataTransfer(command);
        return FAILED;
    }

    if (((command & CMD_RESPONSE_MASK) == CMD_RESPONSE_48_BUSY) || data)
    {
        if (!WaitFor(REG_INT_FLAGS, INT_ERROR | INT_DATA_DONE, 1, 2000))
        {
            Write(REG_INT_FLAGS, INT_ERROR_MASK | INT_DATA_DONE);

            if (data)
            {
                AbortDataTransfer(command);
            }

            return FAILED;
        }

        flags = Read(REG_INT_FLAGS);

        Write(REG_INT_FLAGS, INT_ERROR_MASK | INT_DATA_DONE);

        //  A command timeout beside data done passes, as it always has with these controllers.

        const uint32_t outcome = flags & (INT_ERROR_MASK | INT_DATA_DONE);

        if ((outcome != INT_DATA_DONE) && (outcome != (INT_COMMAND_TIMEOUT | INT_DATA_DONE)))
        {
            if (data)
            {
                AbortDataTransfer(command);
            }

            sd.error_flags = flags & INT_ERROR_MASK;
            return ERROR_WITH_FLAGS;
        }
    }

    return OK;
}

static int AppCommand(uint32_t command, uint32_t argument)
{
    const int result = IssueCommand(CMD_APP, sd.relative_card_address << 16, 2000);

    if (result != OK)
    {
        return result;
    }

    return IssueCommand(command, argument, 2000);
}

//  A command timeout is what a card that doesn't know the command gives: reset the command line and carry on.

static int RecoverFromCommandTimeout(void)
{
    if (!ResetLines(CONTROL1_RESET_COMMAND))
    {
        return FAILED;
    }

    Write(REG_INT_FLAGS, INT_COMMAND_TIMEOUT);

    return OK;
}

static int IsV2Card(void)
{
    const int result = IssueCommand(CMD_SEND_IF_COND, 0x1AA, 200);

    if (result == OK)
    {
        return (sd.response[0] & 0xFFF) == 0x1AA;
    }

    if ((result == ERROR_WITH_FLAGS) && ((sd.error_flags & INT_COMMAND_TIMEOUT) != 0))
    {
        RecoverFromCommandTimeout();
    }

    return 0;
}

static int IsUsableCard(void)
{
    const int result = IssueCommand(CMD_IO_SET_OP_COND, 0, 1000);

    if (result != ERROR_WITH_FLAGS)
    {
        return OK;
    }

    if ((sd.error_flags & INT_COMMAND_TIMEOUT) != 0)
    {
        return RecoverFromCommandTimeout();
    }

    return FAILED;
}

static int CheckOperatingConditions(void)
{
    for (int i = 0; i < 5; i++)
    {
        if (AppCommand(CMD_APP_OCR_CHECK, 0) != OK)
        {
            return FAILED;
        }
    }

    return OK;
}

static int ProbeSdhc(int v2_card)
{
    for (int attempt = 0; attempt < 1000; attempt++)
    {
        if (AppCommand(CMD_APP_OCR_CHECK, 0x00FF8000 | (v2_card ? (1u << 30) : 0)) != OK)
        {
            return FAILED;
        }

        if ((sd.response[0] >> 31) & 1)
        {
            sd.sdhc = ((sd.response[0] >> 30) & 1) != 0;
            return OK;
        }

        SleepMs(500);
    }

    return FAILED;
}

//  A power of two at or below the target, as the controllers' 10-bit divider field takes it.

static uint32_t ClockDivider(uint32_t base_hz, uint32_t target_hz)
{
    uint32_t divider = 1;

    if (target_hz <= base_hz)
    {
        divider = base_hz / target_hz;

        if ((base_hz % target_hz) != 0)
        {
            divider++;
        }
    }

    if (divider > MAX_CLOCK_DIVIDER)
    {
        divider = MAX_CLOCK_DIVIDER;
    }

    uint32_t power_of_2 = 1;

    while (power_of_2 < divider)
    {
        power_of_2 *= 2;
    }

    const uint32_t high = (power_of_2 & 0x300) >> 2;
    const uint32_t low = power_of_2 & 0xFF;

    return (low << 8) + high;
}

static int SetupClock(void)
{
    Write(REG_CONTROL2, 0);

    uint32_t control1 = Read(REG_CONTROL1);

    control1 |= CONTROL1_CLOCK_INTERNAL_ENABLE;
    control1 |= ClockDivider(sd.host_clock_hz, CLOCK_SETUP_HZ);
    control1 &= ~CONTROL1_DATA_TIMEOUT_MASK;
    control1 |= CONTROL1_DATA_TIMEOUT;

    Write(REG_CONTROL1, control1);

    if (!WaitFor(REG_CONTROL1, CONTROL1_CLOCK_STABLE, 1, 2000))
    {
        return FAILED;
    }

    SleepMs(30);

    Write(REG_CONTROL1, Read(REG_CONTROL1) | CONTROL1_CLOCK_ENABLE);

    SleepMs(30);

    return OK;
}

static int SwitchClockRate(uint32_t target_hz)
{
    const uint32_t divider = ClockDivider(sd.host_clock_hz, target_hz);

    if (!WaitFor(REG_STATUS, STATUS_COMMAND_INHIBIT | STATUS_DATA_INHIBIT, 0, 1000))
    {
        return FAILED;
    }

    const uint32_t stopped = Read(REG_CONTROL1) & ~CONTROL1_CLOCK_ENABLE;

    Write(REG_CONTROL1, stopped);
    SleepMs(5);

    const uint32_t switched = (stopped & 0xFFFF003F) | divider;

    Write(REG_CONTROL1, switched);

    if (!WaitFor(REG_CONTROL1, CONTROL1_CLOCK_STABLE, 1, 2000))
    {
        return FAILED;
    }

    Write(REG_CONTROL1, switched | CONTROL1_CLOCK_ENABLE);
    SleepMs(20);

    return OK;
}

static int ReadRelativeCardAddress(void)
{
    if ((IssueCommand(CMD_SEND_CID, 0, 2000) != OK) || (IssueCommand(CMD_SEND_RELATIVE_ADDR, 0, 2000) != OK))
    {
        return FAILED;
    }

    sd.relative_card_address = (sd.response[0] >> 16) & 0xFFFF;

    return ((sd.response[0] >> 8) & 1) ? OK : FAILED;
}

static int SelectCard(void)
{
    if (IssueCommand(CMD_SELECT_CARD, sd.relative_card_address << 16, 2000) != OK)
    {
        return FAILED;
    }

    const uint32_t status = (sd.response[0] >> 9) & 0xF;

    return ((status == 3) || (status == 4)) ? OK : FAILED;
}

static int ReadConfiguration(void)
{
    if (!sd.sdhc && (IssueCommand(CMD_SET_BLOCK_LEN, SD_CARD_BLOCK_SIZE, 2000) != OK))
    {
        return FAILED;
    }

    Write(REG_BLOCK_SIZE_COUNT, (Read(REG_BLOCK_SIZE_COUNT) & ~0xFFFu) | SD_CARD_BLOCK_SIZE);

    sd.buffer = sd.scr;
    sd.block_size = 8;
    sd.transfer_blocks = 1;

    const int result = AppCommand(CMD_APP_SEND_SCR, 0);

    sd.block_size = SD_CARD_BLOCK_SIZE;

    return result;
}

//  A card that can't take four data lines stays on one.  The card interrupt is masked first: in 4-bit mode DAT1 also
//      carries it, and nothing here uses it.

static int UseFourDataLines(void)
{
    if ((sd.scr[0] & SCR_FOUR_DATA_LINES) == 0)
    {
        return OK;
    }

    Write(REG_INT_MASK, Read(REG_INT_MASK) & ~INT_CARD);

    if (AppCommand(CMD_APP_SET_BUS_WIDTH, BUS_WIDTH_FOUR) != OK)
    {
        return FAILED;
    }

    Write(REG_CONTROL0, Read(REG_CONTROL0) | CONTROL0_FOUR_DATA_LINES);

    return OK;
}

static void PrepareController(void)
{
    if (sd.controller != SD_CARD_CONTROLLER_BCM2712)
    {
        return;
    }

    //  Route the controller to the SD card's pins, and force card-detect to present: the board booted from it.

    const uint32_t control = Read(BCM2712_SDIO_CFG_CTRL);

    Write(BCM2712_SDIO_CFG_CTRL, (control & ~BCM2712_SDCD_N_TEST_LEV) | BCM2712_SDCD_N_TEST_EN);
    Write(BCM2712_SDIO_CFG_PIN_SEL, (Read(BCM2712_SDIO_CFG_PIN_SEL) & ~BCM2712_PIN_SEL_MASK) | BCM2712_PIN_SEL_SD);
}

static void IssueSoftwareReset(void)
{
    //  The BCM2712's reset needs its internal clock running, so it gets the reset byte alone.

    if (sd.controller == SD_CARD_CONTROLLER_BCM2712)
    {
        DeviceWrite8(sd.registers + REG_CONTROL1_RESET_BYTE, 0x01);
        return;
    }

    Write(REG_CONTROL1, CONTROL1_RESET_HOST);
}

static void ApplyBusPower(void)
{
    //  Without this the SDHCI 3.0 parts leave the bus unpowered, and CMD0 is never driven.

    if (sd.controller == SD_CARD_CONTROLLER_BCM2837)
    {
        return;
    }

    Write(REG_CONTROL0, Read(REG_CONTROL0) | CONTROL0_BUS_POWER_3V3);
    SleepMs(3);
}

static int ResetCard(void)
{
    PrepareController();
    IssueSoftwareReset();

    if (!WaitFor(REG_CONTROL1, CONTROL1_RESET_ALL, 0, 2000))
    {
        return FAILED;
    }

    ApplyBusPower();

    if (SetupClock() != OK)
    {
        return FAILED;
    }

    Write(REG_INT_ENABLE, 0);
    Write(REG_INT_FLAGS, INT_ALL);
    Write(REG_INT_MASK, INT_ALL);

    SleepMs(203);

    sd.transfer_blocks = 0;
    sd.block_size = 0;
    sd.relative_card_address = 0;

    if (IssueCommand(CMD_GO_IDLE, 0, 2000) != OK)
    {
        return FAILED;
    }

    const int v2_card = IsV2Card();

    if ((IsUsableCard() != OK) || (CheckOperatingConditions() != OK) || (ProbeSdhc(v2_card) != OK) ||
        (SwitchClockRate(CLOCK_NORMAL_HZ) != OK) || (ReadRelativeCardAddress() != OK) || (SelectCard() != OK) ||
        (ReadConfiguration() != OK) || (UseFourDataLines() != OK))
    {
        return FAILED;
    }

    Write(REG_INT_FLAGS, INT_ALL);

    return OK;
}

static int InitializeCard(void)
{
    for (int attempt = 0; attempt < CARD_RESET_ATTEMPTS; attempt++)
    {
        sd.card_resets++;

        if (ResetCard() == OK)
        {
            return OK;
        }

        SleepMs(100);
    }

    return FAILED;
}

static int DataCommand(int write, uint32_t *buffer, uint32_t block, uint32_t count)
{
    uint32_t command;

    if (write)
    {
        command = (count > 1) ? CMD_WRITE_MULTIPLE : CMD_WRITE_BLOCK;
    }
    else
    {
        command = (count > 1) ? CMD_READ_MULTIPLE : CMD_READ_BLOCK;
    }

    const uint32_t address = sd.sdhc ? block : block * SD_CARD_BLOCK_SIZE;

    for (int attempt = 0; attempt < DATA_COMMAND_ATTEMPTS; attempt++)
    {
        if (attempt > 0)
        {
            sd.command_retries++;
        }

        sd.transfer_blocks = count;
        sd.buffer = buffer;

        if (IssueCommand(command, address, 5000) == OK)
        {
            return SD_CARD_SUCCESS;
        }
    }

    return SD_CARD_IO_FAILED;
}

static int Serve(uint64_t page, uint32_t length, uint64_t data_length, uint32_t *transferred)
{
    const volatile SDCardRequest *request = (const volatile SDCardRequest *)(page + CHANNEL_REQUEST_OFFSET);

    *transferred = 0;

    if (length < sizeof(SDCardRequest))
    {
        return SD_CARD_BAD_REQUEST;
    }

    const uint32_t operation = request->operation;
    const uint32_t block = request->block;
    const uint32_t count = request->count;

    const int write = (operation == SD_CARD_WRITE);

    if (((operation != SD_CARD_READ) && !write) || (count == 0) || (count > SD_CARD_MAX_BLOCKS) || (block + count < block) ||
        (write && (data_length != (uint64_t)count * SD_CARD_BLOCK_SIZE)))
    {
        return SD_CARD_BAD_REQUEST;
    }

    const int result = DataCommand(write, (uint32_t *)(page + CHANNEL_DATA_OFFSET), block, count);

    if (result == SD_CARD_SUCCESS)
    {
        *transferred = count;
    }

    return result;
}

int main(unsigned long arg)
{
    uint32_t device;
    uint32_t channel;
    uint64_t page;
    uint64_t data_size;
    uint64_t frequency;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));

    sd.controller = SD_CARD_ARGUMENT_CONTROLLER(arg);
    sd.host_clock_hz = SD_CARD_ARGUMENT_CLOCK_HZ(arg);
    sd.ticks_per_us = (frequency >= 1000000) ? frequency / 1000000 : 1;

    if (sd.host_clock_hz == 0)
    {
        sc_Write("sd-card driver: no controller clock rate\n");
        return 1;
    }

    if (sc_DeviceOpen(DEVICE_NAME_SD_CARD, &device, &sd.registers) != DEVICE_SUCCESS)
    {
        sc_Write("sd-card driver: the SD controller did not open\n");
        return 1;
    }

    if (InitializeCard() != OK)
    {
        sc_Write("sd-card driver: the SD card did not initialize\n");
        return 1;
    }

    if ((sc_ChannelOpen(CHANNEL_NAME_SD_CARD, &channel, &page, &data_size) != CHANNEL_SUCCESS) || (data_size < SD_CARD_DATA_SIZE))
    {
        sc_Write("sd-card driver: the sd-card channel did not open\n");
        return 1;
    }

    volatile SDCardReply *reply = (volatile SDCardReply *)(page + CHANNEL_REPLY_OFFSET);

    for (;;)
    {
        uint64_t data_length = 0;

        const int length = sc_ChannelReceive(channel, CHANNEL_WAIT_FOREVER, &data_length);

        if (length == CHANNEL_EVENT)
        {
            continue;
        }

        if (length < 0)
        {
            sc_Write("sd-card driver: a receive failed\n");
            return 1;
        }

        uint32_t transferred = 0;
        const int result = Serve(page, (uint32_t)length, data_length, &transferred);
        const volatile SDCardRequest *request = (const volatile SDCardRequest *)(page + CHANNEL_REQUEST_OFFSET);
        const int read = (result == SD_CARD_SUCCESS) && (request->operation == SD_CARD_READ);

        reply->result = result;
        reply->blocks = transferred;
        reply->command_retries = sd.command_retries;
        reply->transfer_aborts = sd.transfer_aborts;
        reply->card_resets = sd.card_resets;

        sd.command_retries = 0;
        sd.transfer_aborts = 0;
        sd.card_resets = 0;

        sc_ChannelReply(channel, sizeof(SDCardReply), read ? (uint64_t)transferred * SD_CARD_BLOCK_SIZE : 0);
    }
}

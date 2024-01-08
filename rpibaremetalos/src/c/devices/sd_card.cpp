// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.


/*
#include "devices/gpio.h"
#include "devices/physical_timer.h"
// #include "uart.h"
// #include "delays.h"
#include "devices/sd_card.h"

#include <minimalstdio.h>

#include "memory.h"

// command flags
#define CMD_NEED_APP 0x80000000
#define CMD_RSPNS_48 0x00020000
#define CMD_ERRORS_MASK 0xfff9c004
#define CMD_RCA_MASK 0xffff0000

// COMMANDs
#define CMD_GO_IDLE 0x00000000
#define CMD_ALL_SEND_CID 0x02010000
#define CMD_SEND_REL_ADDR 0x03020000
#define CMD_CARD_SELECT 0x07030000
#define CMD_SEND_IF_COND 0x08020000
#define CMD_STOP_TRANS 0x0C030000
#define CMD_READ_SINGLE 0x11220010
#define CMD_READ_MULTI 0x12220032
#define CMD_SET_BLOCKCNT 0x17020000
#define CMD_APP_CMD 0x37000000
#define CMD_SET_BUS_WIDTH (0x06020000 | CMD_NEED_APP)
#define CMD_SEND_OP_COND (0x29020000 | CMD_NEED_APP)
#define CMD_SEND_SCR (0x33220010 | CMD_NEED_APP)

// STATUS register settings
#define SR_READ_AVAILABLE 0x00000800
#define SR_DAT_INHIBIT 0x00000002
#define SR_CMD_INHIBIT 0x00000001
#define SR_APP_CMD 0x00000020

// INTERRUPT register settings
#define INT_DATA_TIMEOUT 0x00100000
#define INT_CMD_TIMEOUT 0x00010000
#define INT_READ_RDY 0x00000020
#define INT_CMD_DONE 0x00000001

#define INT_ERROR_MASK 0x017E8000

// CONTROL register settings
#define C0_SPI_MODE_EN 0x00100000
#define C0_HCTL_HS_EN 0x00000004
#define C0_HCTL_DWITDH 0x00000002

#define C1_SRST_DATA 0x04000000
#define C1_SRST_CMD 0x02000000
#define C1_SRST_HC 0x01000000
#define C1_TOUNIT_DIS 0x000f0000
#define C1_TOUNIT_MAX 0x000e0000
#define C1_CLK_GENSEL 0x00000020
#define C1_CLK_EN 0x00000004
#define C1_CLK_STABLE 0x00000002
#define C1_CLK_INTLEN 0x00000001

#define C1_RESET_ALL (C1_SRST_HC | C1_SRST_CMD | C1_SRST_DATA)

// SLOTISR_VER values
#define HOST_SPEC_NUM 0x00ff0000
#define HOST_SPEC_NUM_SHIFT 16
#define HOST_SPEC_V3 2
#define HOST_SPEC_V2 1
#define HOST_SPEC_V1 0

// SCR flags
#define SCR_SD_BUS_WIDTH_4 0x00000400
#define SCR_SUPP_SET_BLKCNT 0x02000000
// added by my driver
#define SCR_SUPP_CCS 0x00000001

#define ACMD41_VOLTAGE 0x00ff8000
#define ACMD41_CMD_COMPLETE 0x80000000
#define ACMD41_CMD_CCS 0x40000000
#define ACMD41_ARG_HC 0x51ff8000

unsigned long sd_scr[2], sd_ocr, sd_rca, sd_err, sd_hv;

static ExternalMassMediaController *__external_mass_media_controller = nullptr;

void wait_cycles(unsigned int n)
{
    if (n)
        while (n--)
        {
            asm volatile("nop");
        }
}

class ExternalMassMediaControllerImpl : public ExternalMassMediaController
{
public:
    ExternalMassMediaControllerImpl();

    int sd_init() override;
    int sd_readblock(unsigned int lba, unsigned char *buffer, unsigned int num) override;

private:
    typedef enum class EMMCRegisters : uint32_t
    {
        ARG2 = 0x00300000,
        BLKSIZECNT = 0x00300004,
        ARG1 = 0x00300008,
        CMDTM = 0x0030000C,
        RESP0 = 0x00300010,
        RESP1 = 0x00300014,
        RESP2 = 0x00300018,
        RESP3 = 0x0030001C,
        DATA = 0x00300020,
        STATUS = 0x00300024,
        CONTROL0 = 0x00300028,
        CONTROL1 = 0x0030002C,
        INTERRUPT = 0x00300030,
        INT_MASK = 0x00300034,
        INT_EN = 0x00300038,
        CONTROL2 = 0x0030003C,
        SLOTISR_VER = 0x003000FC
    } EMMCRegisters;

    
        class EMMCRegisterSet
        {
        public:
            EMMCRegisterSet(const uint8_t *mmio_base)
                : mmio_base_(mmio_base)
            {
            }

            volatile uint32_t &operator[](EMMCRegisters reg)
            {
                return *((volatile uint32_t *)(mmio_base_ + (uint32_t)reg));
            }

        private:
            const uint8_t *mmio_base_;
        };

        EMMCRegisterSet registers_;
    

    const uint8_t *mmio_base_;

    uint32_t GetRegister(EMMCRegisters reg)
    {
        return *((volatile uint32_t *)(mmio_base_ + (uint32_t)reg));
    }

    void SetRegister(EMMCRegisters reg,
                     uint32_t value)
    {
        *((volatile uint32_t *)(mmio_base_ + (uint32_t)reg)) = value;
    }

    bool wait_reg_mask(EMMCRegisters reg, uint32_t mask, bool set, uint32_t retries)
    {
        for (int attempts = 0; attempts <= retries; attempts++)
        {
            if ((*((volatile uint32_t *)(mmio_base_ + (uint32_t)reg)) & mask) ? set : !set)
            {
                return true;
            }

            PhysicalTimer().WaitMsec(1);
        }

        return false;
    }

    int sd_status(unsigned int mask);
    int sd_int(unsigned int mask);
    uint32_t sd_cmd(unsigned int code, unsigned int arg);
    int sd_clk(unsigned int f);
};

ExternalMassMediaControllerImpl::ExternalMassMediaControllerImpl()
    : mmio_base_(GetPlatformInfo().GetMMIOBase())
//      registers_(GetPlatformInfo().GetMMIOBase())
{
}


int ExternalMassMediaControllerImpl::sd_status(uint32_t mask)
{
    PhysicalTimer timer;

    int32_t retries = 500000;

    while ((GetRegister(EMMCRegisters::STATUS) & mask) &&
           !(GetRegister(EMMCRegisters::INTERRUPT) & INT_ERROR_MASK) &&
           (retries-- > 0))
    {
        timer.WaitMsec(1);
    }

    return ((retries <= 0) || (GetRegister(EMMCRegisters::INTERRUPT) & INT_ERROR_MASK)) ? SD_ERROR : SD_OK;
}


int ExternalMassMediaControllerImpl::sd_int(uint32_t mask)
{
    uint32_t r;
    uint32_t m = mask | INT_ERROR_MASK;

    PhysicalTimer timer;

    int32_t retries = 1000000;

    while (!(GetRegister(EMMCRegisters::INTERRUPT) & m) && (retries-- > 0))
    {
        timer.WaitMsec(1);
    }

    r = GetRegister(EMMCRegisters::INTERRUPT);

    if (retries <= 0 || (r & INT_CMD_TIMEOUT) || (r & INT_DATA_TIMEOUT))
    {
        printf("Timeout in sd_int.  r = %x\n", r);
        SetRegister(EMMCRegisters::INTERRUPT, r);
        return SD_TIMEOUT;
    }
    else if (r & INT_ERROR_MASK)
    {
        printf("Error in sd_int\n");
        SetRegister(EMMCRegisters::INTERRUPT, r);
        return SD_ERROR;
    }

    SetRegister(EMMCRegisters::INTERRUPT, mask);

    return 0;
}


uint32_t ExternalMassMediaControllerImpl::sd_cmd(uint32_t code, uint32_t arg)
{
    PhysicalTimer timer;

    uint32_t r = 0;
    sd_err = SD_OK;

    printf("Got code: 0x%08x\n", code);

    if (code & CMD_NEED_APP)
    {
        printf("Command needs APP\n");

        r = sd_cmd(CMD_APP_CMD | (sd_rca ? CMD_RSPNS_48 : 0), sd_rca);

        if (sd_rca && !r)
        {
            printf("ERROR: failed to send SD APP command\n");
            sd_err = SD_ERROR;
            return 0;
        }

        code &= ~CMD_NEED_APP;
    }

    if (sd_status(SR_CMD_INHIBIT))
    {
        printf("ERROR: EMMC busy\n");
        sd_err = SD_TIMEOUT;
        return 0;
    }

    printf("EMMC: Sending command: 0x%08x arg: 0x%08x\n", code, arg);
    //    uart_hex(code);
    //    uart_puts(" arg ");
    //    uart_hex(arg);
    //    uart_puts("\n");
    SetRegister(EMMCRegisters::INTERRUPT, GetRegister(EMMCRegisters::INTERRUPT));
    SetRegister(EMMCRegisters::ARG1, arg);
    SetRegister(EMMCRegisters::CMDTM, code);

    if (code == CMD_SEND_OP_COND)
    {
        printf("Waiting 1 second for emmc command\n");
        timer.WaitMsec(1000);
    }
    else if ((code == CMD_SEND_IF_COND) || (code == CMD_APP_CMD))
    {
        printf("Waiting 1/10 second for emmc command\n");
        timer.WaitMsec(100);
    }

    if ((r = sd_int(INT_CMD_DONE)))
    {
        printf("ERROR: failed to send EMMC command\n");
        sd_err = r;
        return 0;
    }

    r = GetRegister(EMMCRegisters::RESP0);

    if (code == CMD_GO_IDLE || code == CMD_APP_CMD)
    {
        return 0;
    }
    else if (code == (CMD_APP_CMD | CMD_RSPNS_48))
    {
        return r & SR_APP_CMD;
    }
    else if (code == CMD_SEND_OP_COND)
    {
        return r;
    }
    else if (code == CMD_SEND_IF_COND)
    {
        return r == arg ? SD_OK : SD_ERROR;
    }
    else if (code == CMD_ALL_SEND_CID)
    {
        r |= GetRegister(EMMCRegisters::RESP3);
        r |= GetRegister(EMMCRegisters::RESP2);
        r |= GetRegister(EMMCRegisters::RESP1);
        return r;
    }
    else if (code == CMD_SEND_REL_ADDR)
    {
        sd_err = (((r & 0x1fff)) | ((r & 0x2000) << 6) | ((r & 0x4000) << 8) | ((r & 0x8000) << 8)) & CMD_ERRORS_MASK;
        return r & CMD_RCA_MASK;
    }

    return r & CMD_ERRORS_MASK;
    // make gcc happy
    return 0;
}


int ExternalMassMediaControllerImpl::sd_readblock(unsigned int lba, unsigned char *buffer, unsigned int num)
{
    int r, c = 0, d;

    if (num < 1)
    {
        num = 1;
    }

    printf("sd_readblock lba: %u  num: %u\n", lba, num);
    //    uart_hex(lba);
    //    uart_puts(" num ");
    //    uart_hex(num);
    //    uart_puts("\n");

    if (sd_status(SR_DAT_INHIBIT))
    {
        sd_err = SD_TIMEOUT;
        return 0;
    }

    unsigned int *buf = (unsigned int *)buffer;

    if (sd_scr[0] & SCR_SUPP_CCS)
    {
        if (num > 1 && (sd_scr[0] & SCR_SUPP_SET_BLKCNT))
        {
            sd_cmd(CMD_SET_BLOCKCNT, num);
            if (sd_err)
            {
                return 0;
            }
        }

        SetRegister(EMMCRegisters::BLKSIZECNT, (num << 16) | 512);

        sd_cmd(num == 1 ? CMD_READ_SINGLE : CMD_READ_MULTI, lba);

        if (sd_err)
        {
            return 0;
        }
    }
    else
    {
        SetRegister(EMMCRegisters::BLKSIZECNT, (1 << 16) | 512);
    }

    while (c < num)
    {
        if (!(sd_scr[0] & SCR_SUPP_CCS))
        {
            sd_cmd(CMD_READ_SINGLE, (lba + c) * 512);

            if (sd_err)
            {
                return 0;
            }
        }
        if ((r = sd_int(INT_READ_RDY)))
        {
            printf("\rERROR: Timeout waiting for ready to read\n");
            sd_err = r;
            return 0;
        }
        for (d = 0; d < 128; d++)
        {
            buf[d] = GetRegister(EMMCRegisters::DATA);
        }

        c++;
        buf += 128;
    }

    if (num > 1 && !(sd_scr[0] & SCR_SUPP_SET_BLKCNT) && (sd_scr[0] & SCR_SUPP_CCS))
    {
        sd_cmd(CMD_STOP_TRANS, 0);
    }

    return sd_err != SD_OK || c != num ? 0 : num * 512;
}


int ExternalMassMediaControllerImpl::sd_clk(uint32_t f)
{
    PhysicalTimer timer;

    uint32_t d;
    uint32_t c = 41666666 / f;
    uint32_t x;
    uint32_t s = 32;
    uint32_t h = 0;

    int cnt = 100000;

    while ((GetRegister(EMMCRegisters::STATUS) & (SR_CMD_INHIBIT | SR_DAT_INHIBIT)) && (cnt-- > 0))
    {
        timer.WaitMsec(1);
    }

    if (cnt <= 0)
    {
        printf("ERROR: timeout waiting for inhibit flag\n");
        return SD_ERROR;
    }

    SetRegister(EMMCRegisters::CONTROL1, GetRegister(EMMCRegisters::CONTROL1) & ~C1_CLK_EN);

    timer.WaitMsec(10);

    x = c - 1;

    if (!x)
    {
        s = 0;
    }
    else
    {
        if (!(x & 0xffff0000u))
        {
            x <<= 16;
            s -= 16;
        }
        if (!(x & 0xff000000u))
        {
            x <<= 8;
            s -= 8;
        }
        if (!(x & 0xf0000000u))
        {
            x <<= 4;
            s -= 4;
        }
        if (!(x & 0xc0000000u))
        {
            x <<= 2;
            s -= 2;
        }
        if (!(x & 0x80000000u))
        {
            x <<= 1;
            s -= 1;
        }
        if (s > 0)
        {
            s--;
        }
        if (s > 7)
        {
            s = 7;
        }
    }

    if (sd_hv > HOST_SPEC_V2)
    {
        d = c;
    }
    else
    {
        d = (1 << s);
    }

    if (d <= 2)
    {
        d = 2;
        s = 0;
    }

    printf("sd_clk divisor: %u, shift: %u\n", d, s);
    //    uart_hex(d);
    //    uart_puts(", shift ");
    //    uart_hex(s);
    //    uart_puts("\n");

    if (sd_hv > HOST_SPEC_V2)
    {
        h = (d & 0x300) >> 2;
    }

    d = (((d & 0x0ff) << 8) | h);

    SetRegister(EMMCRegisters::CONTROL1, (GetRegister(EMMCRegisters::CONTROL1) & 0xffff003f) | d);

    timer.WaitMsec(10);

    SetRegister(EMMCRegisters::CONTROL1, GetRegister(EMMCRegisters::CONTROL1) | C1_CLK_EN);

    timer.WaitMsec(10);

    cnt = 10000;

    while (!(GetRegister(EMMCRegisters::CONTROL1) & C1_CLK_STABLE) && (cnt-- > 0))
    {
        timer.WaitMsec(10);
    }

    if (cnt <= 0)
    {
        printf("ERROR: failed to get stable clock\n");
        return SD_ERROR;
    }

    return SD_OK;
}


bool check_v2_card()
{
    bool v2Card = false;

    if (!sd_cmd(CMD_SEND_IF_COND, 0x1AA))
    {
        if (device.last_error == 0)
        {
            // timeout.
            printf("EMMC_ERR: SEND_IF_COND Timeout\n");
        }
        else if (device.last_error & (1 << 16))
        {
            // timeout command error
            if (!reset_command())
            {
                return false;
            }

            EMMC->int_flags = sd_error_mask(SDECommandTimeout);
            printf("EMMC_ERR: SEND_IF_COND CMD TIMEOUT\n");
        }
        else
        {
            printf("EMMC_ERR: Failure sending SEND_IF_COND\n");
            return false;
        }
    }
    else
    {
        if ((device.last_response[0] & 0xFFF) != 0x1AA)
        {
            printf("EMMC_ERR: Unusable SD Card: %X\n", device.last_response[0]);
            return false;
        }

        v2Card = true;
    }

    return v2Card;
}



int ExternalMassMediaControllerImpl::sd_init()
{
    PhysicalTimer timer;

    long r;
    long cnt;
    long ccs = 0;

    GPIO gpio;

    // GPIO_CD
    r = gpio.GetRegister(GPIORegister::GPFSEL4);
    r &= ~(7 << (7 * 3));
    gpio.SetRegister(GPIORegister::GPFSEL4, r);
    gpio.SetRegister(GPIORegister::GPPUD, 2);
    wait_cycles(150);
    gpio.SetRegister(GPIORegister::GPPUDCLK1, (1 << 15));
    wait_cycles(150);
    gpio.SetRegister(GPIORegister::GPPUD, 0);
    gpio.SetRegister(GPIORegister::GPPUDCLK1, 0);
    r = gpio.GetRegister(GPIORegister::GPHEN1);
    r |= 1 << 15;
    gpio.SetRegister(GPIORegister::GPHEN1, r);

    // GPIO_CLK, GPIO_CMD
    r = gpio.GetRegister(GPIORegister::GPFSEL4);
    r |= (7 << (8 * 3)) | (7 << (9 * 3));
    gpio.SetRegister(GPIORegister::GPFSEL4, r);
    gpio.SetRegister(GPIORegister::GPPUD, 2);
    wait_cycles(150);
    gpio.SetRegister(GPIORegister::GPPUDCLK1, (1 << 16) | (1 << 17));
    wait_cycles(150);
    gpio.SetRegister(GPIORegister::GPPUD, 0);
    gpio.SetRegister(GPIORegister::GPPUDCLK1, 0);

    // GPIO_DAT0, GPIO_DAT1, GPIO_DAT2, GPIO_DAT3
    r = gpio.GetRegister(GPIORegister::GPFSEL5);
    r |= (7 << (0 * 3)) | (7 << (1 * 3)) | (7 << (2 * 3)) | (7 << (3 * 3));
    gpio.SetRegister(GPIORegister::GPFSEL5, r);
    gpio.SetRegister(GPIORegister::GPPUD, 2);
    wait_cycles(150);
    gpio.SetRegister(GPIORegister::GPPUDCLK1, (1 << 18) | (1 << 19) | (1 << 20) | (1 << 21));
    wait_cycles(150);
    gpio.SetRegister(GPIORegister::GPPUD, 0);
    gpio.SetRegister(GPIORegister::GPPUDCLK1, 0);

    sd_hv = (GetRegister(EMMCRegisters::SLOTISR_VER) & HOST_SPEC_NUM) >> HOST_SPEC_NUM_SHIFT;
    printf("EMMC: GPIO set up\n");

    // Reset the card.

    //    registers_[EMMCRegisters::CONTROL0] = 0;
    //    registers_[EMMCRegisters::CONTROL1] |= C1_SRST_HC;

    SetRegister(EMMCRegisters::CONTROL0, 0);
    SetRegister(EMMCRegisters::CONTROL1, GetRegister(EMMCRegisters::CONTROL1) | C1_SRST_HC);

    cnt = 10000;

    do
    {
        timer.WaitMsec(10);
    } while ((GetRegister(EMMCRegisters::CONTROL1) & C1_SRST_HC) && (cnt-- > 0));

    if (cnt <= 0)
    {
        printf("ERROR: failed to reset EMMC\n");
        return SD_ERROR;
    }

    printf("EMMC: reset OK\n");

#if (RPI_VERSION == 4)
    // This enabled VDD1 bus power for SD card, needed for RPI 4.
    u32 c0 = EMMC->control[0];
    c0 |= 0x0F << 8;
    EMMC->control[0] = c0;
    timer_sleep(3);
#endif

    SetRegister(EMMCRegisters::CONTROL1, GetRegister(EMMCRegisters::CONTROL1) | (C1_CLK_INTLEN | C1_TOUNIT_MAX));
    timer.WaitMsec(10);

    // Set clock to setup frequency.

    if ((r = sd_clk(400000)))
    {
        return r;
    }

    SetRegister(EMMCRegisters::INT_EN, 0xffffffff);
    SetRegister(EMMCRegisters::INT_MASK, 0xffffffff);

    sd_scr[0] = sd_scr[1] = sd_rca = sd_err = 0;
    sd_cmd(CMD_GO_IDLE, 0);

    if (sd_err)
    {
        return sd_err;
    }

    //  Determine if this is a V2 card

    sd_cmd(CMD_SEND_IF_COND, 0x000001AA);

    if (sd_err)
    {
        printf("SD error code: %x\n", sd_err);
        return sd_err;
    }

    cnt = 6;
    r = 0;

    while (!(r & ACMD41_CMD_COMPLETE) && cnt--)
    {
        wait_cycles(400);
        r = sd_cmd(CMD_SEND_OP_COND, ACMD41_ARG_HC);
        printf("EMMC: CMD_SEND_OP_COND returned ");

        if (r & ACMD41_CMD_COMPLETE)
        {
            printf("COMPLETE ");
        }

        if (r & ACMD41_VOLTAGE)
        {
            printf("VOLTAGE ");
        }

        if (r & ACMD41_CMD_CCS)
        {
            printf("CCS ");
        }

        printf("0x%08x 0x%08x\n", r >> 32, r);
        //        uart_hex(r);
        //        uart_puts("\n");

        if (sd_err != SD_TIMEOUT && sd_err != SD_OK)
        {
            printf("ERROR: EMMC ACMD41 returned error\n");
            return sd_err;
        }
    }

    if (!(r & ACMD41_CMD_COMPLETE) || !cnt)
    {
        return SD_TIMEOUT;
    }

    if (!(r & ACMD41_VOLTAGE))
    {
        return SD_ERROR;
    }

    if (r & ACMD41_CMD_CCS)
    {
        ccs = SCR_SUPP_CCS;
    }

    sd_cmd(CMD_ALL_SEND_CID, 0);

    sd_rca = sd_cmd(CMD_SEND_REL_ADDR, 0);

    printf("EMMC: CMD_SEND_REL_ADDR returned: 0x%08x 0x%08x\n", sd_rca >> 32, sd_rca);
    //    uart_hex(sd_rca >> 32);
    //    uart_hex(sd_rca);
    //    uart_puts("\n");

    if (sd_err)
    {
        return sd_err;
    }

    if ((r = sd_clk(25000000)))
    {
        return r;
    }

    printf("Selecting Card\n");
    sd_cmd(CMD_CARD_SELECT, sd_rca);

    if (sd_err)
    {
        return sd_err;
    }

    if (sd_status(SR_DAT_INHIBIT))
    {
        return SD_TIMEOUT;
    }

    printf("Setting SCR\n");
    SetRegister(EMMCRegisters::BLKSIZECNT, (1 << 16) | 8);

    sd_cmd(CMD_SEND_SCR, 0);

    if (sd_err)
    {
        return sd_err;
    }

    printf("Waiting for Read Ready\n");
    if (sd_int(INT_READ_RDY))
    {
        return SD_TIMEOUT;
    }

    r = 0;
    cnt = 100000;

    while (r < 2 && cnt)
    {
        if (GetRegister(EMMCRegisters::STATUS) & SR_READ_AVAILABLE)
        {
            sd_scr[r++] = GetRegister(EMMCRegisters::DATA);
        }
        else
        {
            timer.WaitMsec(1);
        }
    }

    if (r != 2)
    {
        return SD_TIMEOUT;
    }

    if (sd_scr[0] & SCR_SD_BUS_WIDTH_4)
    {
        sd_cmd(CMD_SET_BUS_WIDTH, sd_rca | 2);

        if (sd_err)
        {
            return sd_err;
        }

        SetRegister(EMMCRegisters::CONTROL0, GetRegister(EMMCRegisters::CONTROL0) | C0_HCTL_DWITDH);
    }

    // add software flag

    printf("EMMC: supports ");

    if (sd_scr[0] & SCR_SUPP_SET_BLKCNT)
    {
        printf("SET_BLKCNT ");
    }

    if (ccs)
    {
        printf("CCS ");
    }

    printf("\n");

    sd_scr[0] &= ~SCR_SUPP_CCS;
    sd_scr[0] |= ccs;

    return SD_OK;
}

ExternalMassMediaController *GetExternalMassMediaController()
{
    if (__external_mass_media_controller == nullptr)
    {
        __external_mass_media_controller = static_new<ExternalMassMediaControllerImpl>();
    }

    return __external_mass_media_controller;
}
*/

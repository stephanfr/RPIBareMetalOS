// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stdint.h>

#include <memory>

#include "heaps.h"

#include "asm_utility.h"

#include "devices/gpio.h"
#include "devices/log.h"
#include "devices/mailbox_messages.h"
#include "devices/physical_timer.h"

#include "devices/emmc.h"

#include "devices/emmc/emmc_commands.h"
#include "devices/emmc/emmc_registers.h"
#include "devices/emmc/sd_card_registers.h"

namespace EmmcImpl
{

#define BSWAP32(x) (((x << 24) & 0xFF000000) | \
                    ((x << 8) & 0x00FF0000) |  \
                    ((x >> 8) & 0x0000FF00) |  \
                    ((x >> 24) & 0x000000FF))

    //
    //  SD Clock Frequencies (in Hz)
    //

    typedef enum SDClockSettings : uint32_t
    {
        SDClockSetupRate = 400000,
        SDClockNormalRate = 25000000,
        SDClockHighRate = 50000000,
        SDClock100Mhz = 100000000,
        SDClock208Mhz = 208000000,
    } SDClockEnum;

    //
    //  Control 1 Register flags
    //

    typedef enum ControlReg1Bitmap : uint32_t
    {
        ControlReg1ResetData = 0x04000000,
        ControlReg1ResetCommand = 0x02000000,
        ControlReg1ResetHost = 0x01000000,
        ControlReg1ResetAll = (ControlReg1ResetData | ControlReg1ResetCommand | ControlReg1ResetHost),

        ControlReg1ClockGenSel = 0x0000020,
        ControlReg1ClockEnable = 0x0000004,
        ControlReg1ClockStable = 0x0000002,
        ControlReg1ClockInterruptEnable = 0x0000001
    } ControlReg1Bitmap;

    //
    //  Status Register flags.
    //

    typedef enum StatusRegBitmap : uint32_t
    {
        StatusRegDataInhibit = 0x0000002,
        StatusRegCommandInhibit = 0x0000001
    } StatusRegBitmap;

    //
    //  Interrupt register flags
    //

    typedef enum InterruptRegisterBitmap : uint32_t
    {
        InterruptRegErrorMask = 0xFFFF0000,
        InterruptRegEnableAll = 0xFFFFFFFF,

        InterruptRegAutoCommandError = 0x01000000,
        InterruptRegDataLineEndBitNot1Error = 0x00400000,
        InterruptRegDataCRCError = 0x00200000,
        InterruptRegDataLineTimeoutError = 0x00100000,
        InterruptRegIncorrectCommandIndexError = 0x00080000,
        InterruptRegCommandLineEndBitNot1Error = 0x00040000,
        InterruptRegCommandCRCError = 0x00020000,
        InterruptRegCommandTimeoutError = 0x00010000,
        InterruptRegError = 0x00008000,
        InterruptRegEndBoot = 0x00004000,
        InterruptRegBootAcknowledge = 0x00002000,
        InterruptRegClockRetune = 0x00001000,
        InterruptRegCardInterrupt = 0x00000100,
        InterruptRegReadReady = 0x00000020,
        InterruptRegWriteReady = 0x00000010,
        InterruptRegDataDone = 0x00000002,
        InterruptRegCommandDone = 0x0000001
    } InterruptRegisterBitmap;

    //
    //  Code follows
    //

    bool Failure(BlockIOResultCodes code)
    {
        return code != BlockIOResultCodes::SUCCESS;
    }

#define RETURN_IF_FAILED(cmd) \
    {                         \
        auto result = cmd;    \
        if (Failure(result))  \
        {                     \
            return result;    \
        }                     \
    }

    //
    //  External Mass Media Controller Implementation
    //

    class SDCardController : public ExternalMassMediaController
    {
    public:
        SDCardController(bool permanent,
                         const char *name,
                         const char *alias,
                         const PlatformInfo &platform_info)
            : ExternalMassMediaController(permanent, name, alias),
              platform_info_(platform_info),
              mmio_base_(platform_info.GetMMIOBase()),
              registers_((EMMCRegisters *)platform_info.GetEMMCBase())
        {
        }

        ~SDCardController() {}

        uint32_t BlockSize() const override
        {
            return 512;
        }

        BlockIOResultCodes Initialize() override;

        BlockIOResultCodes Seek(uint64_t offset_in_blocks) override;

        ValueResult<BlockIOResultCodes, uint32_t> ReadFromBlock(uint8_t *buffer, uint32_t block_number, uint32_t blocks_to_read) override;
        ValueResult<BlockIOResultCodes, uint32_t> ReadFromCurrentOffset(uint8_t *buffer, uint32_t blocks_to_read) override;

        ValueResult<BlockIOResultCodes, uint32_t> WriteBlock(uint8_t *buffer, uint32_t block_number, uint32_t blocks_to_write) override;

    private:
        const PlatformInfo &platform_info_;

        const uint8_t *mmio_base_;
        EMMCRegisters *registers_;

        uint32_t emmc_host_clock_rate_;

        uint32_t transfer_blocks_;
        uint32_t block_size_;
        uint32_t last_response_[4];
        bool is_sdhc_card_;
        uint64_t offset_in_blocks_;
        void *buffer_;

        uint16_t operating_conditions_register_;
        uint32_t relative_card_address_register_;
        SDCardConfigurationRegister sd_card_configuration_register_;

        EMMCCommand GetCommand(EMMCCommandTypes command_type) const
        {
            return commands[static_cast<uint32_t>(command_type)];
        }

        bool WaitForInterrupt(volatile uint32_t &reg, uint32_t mask, bool set, uint32_t retries)
        {
            for (uint32_t attempts = 0; attempts <= retries; attempts++)
            {
                if ((reg & mask) ? set : !set)
                {
                    return true;
                }

                PhysicalTimer().WaitMsec(1);
            }

            return false;
        }

        bool IsV2Card();
        BlockIOResultCodes IsUsableCard();
        BlockIOResultCodes SetIsSDHCCard(bool v2_card);

        BlockIOResultCodes ResetCard();

        BlockIOResultCodes SelectCard();
        BlockIOResultCodes CheckOperatingConditionsRegister();
        BlockIOResultCodes CheckRelativeCardAddressRegister();
        BlockIOResultCodes SetSDCardConfigurationRegister();

        ValueResultWithErrorInfo<BlockIOResultCodes, int32_t, uint32_t> Command(EMMCCommandTypes command, uint32_t arg, uint32_t timeout);
        ValueResultWithErrorInfo<BlockIOResultCodes, int32_t, uint32_t> AppCommand(EMMCCommandTypes command, uint32_t arg, uint32_t timeout);
        BlockIOResultCodes ResetCommand();
        ValueResultWithErrorInfo<BlockIOResultCodes, int32_t, uint32_t> IssueCommand(EMMCCommand cmd, uint32_t arg, uint32_t timeout);
        BlockIOResultCodes DataCommand(bool write, uint8_t *buffer, uint32_t bytes_to_process, uint32_t block_number);

        BlockIOResultCodes TransferData(EMMCCommand cmd);

        void ConfigureGPIO();
        BlockIOResultCodes SetupClock();
        uint32_t GetClockDivider(uint32_t base_clock, uint32_t target_rate);
        BlockIOResultCodes SwitchClockRate(uint32_t base_clock, uint32_t target_rate);
    };

    BlockIOResultCodes SDCardController::TransferData(EMMCCommand cmd)
    {
        uint32_t read_or_write_ready_interrupt = 0;

        //  Direction in the command: 0 is host to card or write, 1 is card to host or read

        bool write = (cmd.direction == 0);

        if (write)
        {
            read_or_write_ready_interrupt = InterruptRegWriteReady;
        }
        else
        {
            read_or_write_ready_interrupt = InterruptRegReadReady;
        }

        uint32_t *data = (uint32_t *)buffer_;

        for (uint32_t block = 0; block < transfer_blocks_; block++)
        {
            //  Wait for the card to be ready for the read or write operation

            WaitForInterrupt(registers_->int_flags, read_or_write_ready_interrupt | InterruptRegError, true, 2000);

            uint32_t intr_val = registers_->int_flags;

            registers_->int_flags = read_or_write_ready_interrupt | InterruptRegError;

            if ((intr_val & (InterruptRegErrorMask | read_or_write_ready_interrupt)) != read_or_write_ready_interrupt)
            {
                return BlockIOResultCodes::EMMC_TRANSFER_DATA_FAILED;
            }

            //  Card is ready.
            //
            //  We move full blocks, 4 bytes (32 bits) at a time as the data register is 32 bits wide.

            uint32_t length = block_size_;

            if (write)
            {
                for (; length > 0; length -= 4)
                {
                    registers_->data = *data++;
                }
            }
            else
            {
                for (; length > 0; length -= 4)
                {
                    *data++ = registers_->data;
                }
            }
        }

        return BlockIOResultCodes::SUCCESS;
    }

    ValueResultWithErrorInfo<BlockIOResultCodes, int32_t, uint32_t> SDCardController::IssueCommand(EMMCCommand command, uint32_t arg, uint32_t timeout)
    {
        using Result = ValueResultWithErrorInfo<BlockIOResultCodes, int32_t, uint32_t>;

        PhysicalTimer timer;

        //  The union nonsense below is to avoid the type punning dereferencing warning from gcc when
        //      getting the command_reg from the command.

        union
        {
            EMMCCommand command;
            uint32_t command_reg;
        } command_convertor;

        command_convertor.command = command;

        volatile uint32_t command_reg = command_convertor.command_reg;

        if (transfer_blocks_ > 0xFFFF)
        {
            return Result::Failure(BlockIOResultCodes::EMMC_ISSUE_COMMAND_TRANSFER_BLOCKS_IS_TOO_LARGE);
        }

        registers_->block_size_count = block_size_ | (transfer_blocks_ << 16);
        registers_->arg1 = arg;
        registers_->cmd_xfer_mode = command_reg;

        timer.WaitMsec(10);

        uint32_t times = 0;

        while (times < timeout)
        {
            uint32_t interrupt_flags = registers_->int_flags;

            //  Break the loop if the command is complete or if there is an error.
            //      There appears to be a difference between error signalling from real HW and QEMU - but this appears to be OK.

            if (interrupt_flags & (InterruptRegError | InterruptRegCommandDone))
            {
                break;
            }

            //  Wait for a millsecond and try again

            timer.WaitMsec(1);
            times++;
        }

        //  Exit now if we timed out

        if (times >= timeout)
        {
            return Result::Failure(BlockIOResultCodes::EMMC_TIMEOUT_FOR_ISSUE_COMMAND);
        }

        uint32_t interrupt_flags = registers_->int_flags;

        registers_->int_flags = InterruptRegErrorMask | InterruptRegCommandDone; //  Reset interrupts

        //  We catch any errors flagged in the interrupt register below

        if ((interrupt_flags & (InterruptRegErrorMask | InterruptRegCommandDone)) != InterruptRegCommandDone)
        {
            LogDebug1("Failing with command and error: %X %X\n", command_reg, interrupt_flags);
            return Result::Failure(BlockIOResultCodes::EMMC_ISSUE_COMMAND_ERROR_WAITING_FOR_COMMAND_INTERRUPT_TO_COMPLETE, interrupt_flags & InterruptRegErrorMask);
        }

        //  Get the response, either 48 or 136 bits

        switch (command.response_type)
        {
        case RT_48_BITS:
        case RT_48_BITS_BUSY:
            last_response_[0] = registers_->response[0];
            break;

        case RT_136_BITS:
            last_response_[0] = registers_->response[0];
            last_response_[1] = registers_->response[1];
            last_response_[2] = registers_->response[2];
            last_response_[3] = registers_->response[3];
            break;
        }

        //  If this is a data command, then transfer the data

        if (command.is_data)
        {
            TransferData(command);
        }

        if ((command.response_type == RT_48_BITS_BUSY) || command.is_data)
        {
            WaitForInterrupt(registers_->int_flags, InterruptRegError | InterruptRegDataDone, true, 2000);

            interrupt_flags = registers_->int_flags;

            registers_->int_flags = InterruptRegErrorMask | InterruptRegDataDone; //  Reset the interrupts

            //  Return a failure if the response is in error and it is not a data timeout error.  It is not entirely clear to me
            //      why we ignore data timeout errors - perhaps because of the RT_48_BITS_BUSY.

            if (((interrupt_flags & (InterruptRegErrorMask | InterruptRegDataDone)) != InterruptRegDataDone) &&
                ((interrupt_flags & (InterruptRegErrorMask | InterruptRegDataDone)) != (InterruptRegCommandTimeoutError | InterruptRegDataDone)))
            {
                LogDebug1("Failing with interrupt flags: %X\n", interrupt_flags);
                return Result::Failure(BlockIOResultCodes::EMMC_ISSUE_COMMAND_ERROR_WAITING_FOR_COMMAND_INTERRUPT_TO_COMPLETE, interrupt_flags & InterruptRegErrorMask);
            }
        }

        return Result::Success();
    }

    ValueResultWithErrorInfo<BlockIOResultCodes, int32_t, uint32_t> SDCardController::Command(EMMCCommandTypes command, uint32_t arg, uint32_t timeout)
    {
        using Result = ValueResultWithErrorInfo<BlockIOResultCodes, int32_t, uint32_t>;

        if (GetCommand(command) == INVALID_CMD)
        {
            return Result::Failure(BlockIOResultCodes::EMMC_INVALID_COMMAND);
        }

        return IssueCommand(GetCommand(command), arg, timeout);
    }

    BlockIOResultCodes SDCardController::ResetCommand()
    {
        registers_->control[1] |= ControlReg1ResetCommand;

        for (int i = 0; i < 10000; i++)
        {
            if (!(registers_->control[1] & ControlReg1ResetCommand))
            {
                return BlockIOResultCodes::SUCCESS;
            }

            PhysicalTimer().WaitMsec(1);
        }

        return BlockIOResultCodes::EMMC_COMMAND_LINE_FAILED_TO_RESET_CORRECTLY;
    }

    ValueResultWithErrorInfo<BlockIOResultCodes, int32_t, uint32_t> SDCardController::AppCommand(EMMCCommandTypes command, uint32_t arg, uint32_t timeout)
    {
        using Result = ValueResultWithErrorInfo<BlockIOResultCodes, int32_t, uint32_t>;

        if (GetCommand(command).index >= 60)
        {
            return Result::Failure(BlockIOResultCodes::EMMC_INVALID_APP_COMMAND);
        }

        uint32_t rca = 0;

        if (relative_card_address_register_)
        {
            rca = relative_card_address_register_ << 16;
        }

        //  Send the APP Command first, then the command we were passed

        auto issue_command_result = IssueCommand(GetCommand(EMMCCommandTypes::App), rca, 2000);

        if (issue_command_result.Failed())
        {
            return issue_command_result;
        }

        return IssueCommand(GetCommand(command), arg, 2000);
    }

    bool SDCardController::IsV2Card()
    {
        auto result = Command(EMMCCommandTypes::SendIfCond, 0x1AA, 200);

        if (result.Failed())
        {
            if (!result.ErrorInfo().has_value())
            {
                //  Timeout
                return false;
            }
            else if (result.ErrorInfo().value() & (1 << 16))
            {
                //  Timeout on the command
                if (Failure(ResetCommand()))
                {
                    return false;
                }

                registers_->int_flags = InterruptRegCommandTimeoutError; //  Reset the interrupt flags

                return false;
            }
            else
            {
                return false;
            }
        }
        else
        {
            if ((last_response_[0] & 0xFFF) != 0x1AA)
            {
                return false;
            }

            return true;
        }

        return false;
    }

    BlockIOResultCodes SDCardController::IsUsableCard()
    {
        auto result = Command(EMMCCommandTypes::IOSetOpCond, 0, 1000);

        if (result.Failed())
        {
            if (!result.ErrorInfo().has_value())
            {
                //  Timeout
                LogDebug1("EMMC_ERR: CTIOSetOpCond Timeout\n");
            }
            else if (result.ErrorInfo().value() & (1 << 16))
            {
                //  Timeout command error - this is a normal expected error and calling the reset command will fix it.

                RETURN_IF_FAILED(ResetCommand());

                registers_->int_flags = InterruptRegCommandTimeoutError;
            }
            else
            {
                return BlockIOResultCodes::EMMC_SD_CARD_NOT_SUPPORTED;
            }
        }

        return BlockIOResultCodes::SUCCESS;
    }

    BlockIOResultCodes SDCardController::SetIsSDHCCard(bool v2_card)
    {
        const uint32_t max_retries = 1000;
        uint32_t retries = 0;

        while (true)
        {
            if (retries++ >= max_retries)
            {
                return BlockIOResultCodes::EMMC_TIMEOUT_WHILE_PROBING_FOR_SDHC_CARD;
            }

            uint32_t v2_flags = 0;

            if (v2_card)
            {
                v2_flags |= (1 << 30); // SDHC Support
            }

            if (AppCommand(EMMCCommandTypes::OcrCheck, 0x00FF8000 | v2_flags, 2000).Failed())
            {
                return BlockIOResultCodes::EMMC_APP_COMMAND_41_FAILED;
            }

            if (last_response_[0] >> 31 & 1)
            {
                operating_conditions_register_ = (last_response_[0] >> 8 & 0xFFFF);
                is_sdhc_card_ = ((last_response_[0] >> 30) & 1) != 0;

                LogDebug1(is_sdhc_card_ ? "Is an SDHC Card\n" : "Is not an SDHC Card\n");
                break;
            }
            else
            {
                LogDebug1("Sleeping for 500msec after response: %X\n", last_response_[0]);
                PhysicalTimer().WaitMsec(500);
            }
        }

        return BlockIOResultCodes::SUCCESS;
    }

    BlockIOResultCodes SDCardController::CheckOperatingConditionsRegister()
    {
        BlockIOResultCodes last_result = BlockIOResultCodes::FAILURE;

        for (int i = 0; i < 5; i++)
        {
            last_result = AppCommand(EMMCCommandTypes::OcrCheck, 0, 2000).ResultCode();

            if (Failure(last_result))
            {
                break;
            }
        }

        if (Failure(last_result))
        {
            return BlockIOResultCodes::EMMC_APP_COMMAND_41_FAILED;
        }

        operating_conditions_register_ = (last_response_[0] >> 8 & 0xFFFF);

        return BlockIOResultCodes::SUCCESS;
    }

    BlockIOResultCodes SDCardController::CheckRelativeCardAddressRegister()
    {
        if (Command(EMMCCommandTypes::SendCide, 0, 2000).Failed())
        {
            return BlockIOResultCodes::EMMC_SEND_CID_COMMAND_FAILED;
        }

        if (Command(EMMCCommandTypes::SendRelativeAddr, 0, 2000).Failed())
        {
            return BlockIOResultCodes::EMMC_SEND_RELATIVE_ADDRESS_COMMAND_FAILED;
        }

        relative_card_address_register_ = (last_response_[0] >> 16) & 0xFFFF;

        if (!((last_response_[0] >> 8) & 1))
        {
            return BlockIOResultCodes::EMMC_FAILED_TO_READ_RELATIVE_CARD_ADDRESS;
        }

        return BlockIOResultCodes::SUCCESS;
    }

    BlockIOResultCodes SDCardController::SelectCard()
    {
        if (Command(EMMCCommandTypes::SelectCard, relative_card_address_register_ << 16, 2000).Failed())
        {
            return BlockIOResultCodes::EMMC_SELECT_CARD_FAILED;
        }

        uint32_t status = (last_response_[0] >> 9) & 0xF;

        if (status != 3 && status != 4)
        {
            return BlockIOResultCodes::EMMC_SELECT_CARD_BAD_STATUS;
        }

        return BlockIOResultCodes::SUCCESS;
    }

    BlockIOResultCodes SDCardController::SetSDCardConfigurationRegister()
    {
        if (!is_sdhc_card_)
        {
            if (Command(EMMCCommandTypes::SetBlockLen, 512, 2000).Failed())
            {
                return BlockIOResultCodes::EMMC_FAILED_TO_SET_BLOCK_LENGTH_FOR_NON_SDHC_CARD;
            }
        }

        uint32_t bsc = registers_->block_size_count;
        bsc &= ~0xFFF; // mask off bottom bits
        bsc |= 0x200;  // set bottom bits to 512
        registers_->block_size_count = bsc;

        buffer_ = &sd_card_configuration_register_.scr[0];
        block_size_ = 8;
        transfer_blocks_ = 1;

        if (AppCommand(EMMCCommandTypes::SendSCR, 0, 30000).Failed())
        {
            return BlockIOResultCodes::EMMC_FAILED_TO_SEND_SCR;
        }

        block_size_ = 512;

        uint32_t scr0 = BSWAP32(sd_card_configuration_register_.scr[0]);

        sd_card_configuration_register_.version = 0xFFFFFFFF;

        uint32_t spec = (scr0 >> (56 - 32)) & 0xf;
        uint32_t spec3 = (scr0 >> (47 - 32)) & 0x1;
        uint32_t spec4 = (scr0 >> (42 - 32)) & 0x1;

        if (spec == 0)
        {
            sd_card_configuration_register_.version = 1;
        }
        else if (spec == 1)
        {
            sd_card_configuration_register_.version = 11;
        }
        else if (spec == 2)
        {

            if (spec3 == 0)
            {
                sd_card_configuration_register_.version = 2;
            }
            else if (spec3 == 1)
            {
                if (spec4 == 0)
                {
                    sd_card_configuration_register_.version = 3;
                }
                if (spec4 == 1)
                {
                    sd_card_configuration_register_.version = 4;
                }
            }
        }

        return BlockIOResultCodes::SUCCESS;
    }

    BlockIOResultCodes SDCardController::ResetCard()
    {
        PhysicalTimer timer;

        registers_->control[1] = ControlReg1ResetHost;

        if (!WaitForInterrupt(registers_->control[1], ControlReg1ResetAll, false, 2000))
        {
            return BlockIOResultCodes::EMMC_TIMEOUT_FOR_CARD_RESET;
        }

        if (platform_info_.IsRPI4())
        {
            //  For the RPI4, we need to enable VDD1 bus power for the SD card

            uint32_t c0 = registers_->control[0];
            c0 |= 0x0F << 8;
            registers_->control[0] = c0;

            timer.WaitMsec(3);
        }

        //  Get the current EMMC clock rate from the mailbox service

        Mailbox mbox(GetPlatformInfo().GetMMIOBase());

        GetClockRateTag getEMMCClockRateTag(MailboxClockIdentifiers(1));

        MailboxPropertyMessage getEMMCClockRateMessage(getEMMCClockRateTag);

        mbox.sendMessage(getEMMCClockRateMessage);

        emmc_host_clock_rate_ = getEMMCClockRateTag.GetRateInHz();

        LogDebug1("EMMC Host Clock Rate: %u Hz\n", getEMMCClockRateTag.GetRateInHz());

        //  Setup the clock

        RETURN_IF_FAILED(SetupClock());

        //  Route all interrupts to the Interrupt Register

        registers_->int_enable = 0;
        registers_->int_flags = 0xFFFFFFFF;
        registers_->int_mask = 0xFFFFFFFF;

        timer.WaitMsec(203);

        transfer_blocks_ = 0;
        block_size_ = 0;

        if (Command(EMMCCommandTypes::GoIdle, 0, 2000).Failed())
        {
            return BlockIOResultCodes::EMMC_NO_RESPONSE_TO_GO_IDLE_COMMAND;
        }

        bool v2_card = IsV2Card();

        RETURN_IF_FAILED(IsUsableCard());

        RETURN_IF_FAILED(CheckOperatingConditionsRegister());

        RETURN_IF_FAILED(SetIsSDHCCard(v2_card));

        RETURN_IF_FAILED(SwitchClockRate(emmc_host_clock_rate_, SDClockNormalRate));

        RETURN_IF_FAILED(CheckRelativeCardAddressRegister());

        RETURN_IF_FAILED(SelectCard());

        RETURN_IF_FAILED(SetSDCardConfigurationRegister());

        // enable all interrupts

        registers_->int_flags = InterruptRegEnableAll;

        //  Card is reset and ready to go

        return BlockIOResultCodes::SUCCESS;
    }

    BlockIOResultCodes SDCardController::Initialize()
    {
        transfer_blocks_ = 0;
        block_size_ = 0;
        is_sdhc_card_ = false;
        operating_conditions_register_ = 0;
        relative_card_address_register_ = 0;
        offset_in_blocks_ = 0;

        ConfigureGPIO();

        BlockIOResultCodes last_reset_result;

        for (int i = 0; i < 10; i++)
        {
            last_reset_result = ResetCard();

            if (last_reset_result == BlockIOResultCodes::SUCCESS)
            {
                break;
            }

            PhysicalTimer().WaitMsec(100);
            LogWarning("EMMC_WARN: Failed to reset card, trying again...\n");
        }

        LogDebug1("%s", last_reset_result == BlockIOResultCodes::SUCCESS ? "SD Card Initialized\n" : "SD Card Initialization Failed\n");

        return last_reset_result;
    }

    void SDCardController::ConfigureGPIO()
    {
        GPIO gpio;

        gpio.SetPinFunction(34, GPIOPinFunction::Input);
        gpio.SetPinFunction(35, GPIOPinFunction::Input);
        gpio.SetPinFunction(36, GPIOPinFunction::Input);
        gpio.SetPinFunction(37, GPIOPinFunction::Input);
        gpio.SetPinFunction(38, GPIOPinFunction::Input);
        gpio.SetPinFunction(39, GPIOPinFunction::Input);

        gpio.SetPinFunction(48, GPIOPinFunction::Alt3);
        gpio.SetPinFunction(49, GPIOPinFunction::Alt3);
        gpio.SetPinFunction(50, GPIOPinFunction::Alt3);
        gpio.SetPinFunction(51, GPIOPinFunction::Alt3);
        gpio.SetPinFunction(52, GPIOPinFunction::Alt3);
    }

    uint32_t SDCardController::GetClockDivider(uint32_t base_clock, uint32_t desired_frequency)
    {
        //  Finder a divider which will give us a frequency <= desired_frequency starting with base_clock

        uint32_t closest_integral_divider = 0;

        if (desired_frequency > base_clock)
        {
            closest_integral_divider = 1;
        }
        else
        {
            closest_integral_divider = base_clock / desired_frequency;

            //  If the base clock is an integral multiple of the desired_frequency, then increment the 'closest divider'
            //      by 1 to insure we fall below the desired frequency after division.

            if (base_clock % desired_frequency)
            {
                closest_integral_divider++;
            }
        }

        //  Cap the divider at 1024

        if (closest_integral_divider > 1024)
        {
            closest_integral_divider = 1024;
        }

        //  Find the closest power of 2 larger than the divider

        uint32_t power_of_2_divider = 1;

        for (power_of_2_divider = 1; power_of_2_divider < closest_integral_divider; power_of_2_divider *= 2)
            ;

        //  Cap the power of 2 divider at 15

        //        if (power_of_2_divider > 15)
        //        {
        //            power_of_2_divider = 15;
        //        }

        LogDebug1("SD Card Clock Rate Divider: %u for target: %u\n", power_of_2_divider, desired_frequency);

        uint32_t high_divider = (power_of_2_divider & 0x300) >> 2; //  Will always be zero if div always <= 15
        uint32_t low_divider = (power_of_2_divider & 0x0ff);

        uint32_t control_reg_1_formatted_divider = (low_divider << 8) + high_divider;

        LogDebug1("Hi, Lo, Div: %u, %u, %u\n", high_divider, low_divider, control_reg_1_formatted_divider);

        return control_reg_1_formatted_divider;
    }

    BlockIOResultCodes SDCardController::SwitchClockRate(uint32_t base_clock, uint32_t target_rate)
    {
        LogDebug1("Switching to Target Rate of: %u\n with Base Clock of: %u\n", target_rate, base_clock);

        PhysicalTimer timer;

        uint32_t divider = GetClockDivider(base_clock, target_rate);

        const uint32_t max_retries = 1000;
        uint32_t retries = 0;

        while (((registers_->status & (StatusRegCommandInhibit | StatusRegDataInhibit)) != 0) &&
               (retries < max_retries))
        {
            timer.WaitMsec(1);
            retries++;
        }

        if (retries >= max_retries)
        {
            return BlockIOResultCodes::EMMC_TIMEOUT_WAITING_FOR_INHIBITS_TO_CLEAR;
        }

        uint32_t c1 = registers_->control[1] & ~ControlReg1ClockEnable;

        registers_->control[1] = c1;

        timer.WaitMsec(5);

        registers_->control[1] = (c1 & 0xffff003f) | divider;

        timer.WaitMsec(5);

        registers_->control[1] = c1 | ControlReg1ClockEnable;

        timer.WaitMsec(20); //  Wait a little longer before returning

        return BlockIOResultCodes::SUCCESS;
    }

    BlockIOResultCodes SDCardController::SetupClock()
    {
        registers_->control2 = 0;

        uint32_t control_reg_1 = registers_->control[1];

        control_reg_1 |= ControlReg1ClockInterruptEnable;
        control_reg_1 |= GetClockDivider(emmc_host_clock_rate_, SDClockSetupRate);
        control_reg_1 &= ~(0xf << 16);
        control_reg_1 |= (11 << 16);

        registers_->control[1] = control_reg_1;

        if (!WaitForInterrupt(registers_->control[1], ControlReg1ClockStable, true, 2000))
        {
            LogDebug1("SD Card Clock not stable\n");
            return BlockIOResultCodes::EMMC_CLOCK_IS_NOT_STABLE;
        }

        PhysicalTimer().WaitMsec(30);

        //  Enable the clock

        registers_->control[1] |= 4;

        PhysicalTimer().WaitMsec(30);

        return BlockIOResultCodes::SUCCESS;
    }

    //
    //  Data Transfer - Read/Write methods
    //

    BlockIOResultCodes SDCardController::DataCommand(bool write, uint8_t *buffer, uint32_t block_number, uint32_t blocks_to_transfer)
    {
        if (!is_sdhc_card_)
        {
            block_number *= 512;
        }

        transfer_blocks_ = blocks_to_transfer;

        buffer_ = buffer;

        EMMCCommandTypes command = EMMCCommandTypes::ReadBlock;

        if (write && transfer_blocks_ > 1)
        {
            command = EMMCCommandTypes::WriteMultiple;
        }
        else if (write)
        {
            command = EMMCCommandTypes::WriteBlock;
        }
        else if (!write && transfer_blocks_ > 1)
        {
            command = EMMCCommandTypes::ReadMultiple;
        }

        int retry_count = 0;
        int max_retries = 3;

        while (retry_count < max_retries)
        {
            auto command_result = Command(command, block_number, 5000);

            if (command_result.Successful())
            {
                break;
            }

            LogDebug1("EMMC Data Command Failed with code: %d\n", command_result.ResultCode());

            if (++retry_count >= max_retries)
            {
                return BlockIOResultCodes::EMMC_DATA_COMMAND_MAX_RETRIES;
            }
        }

        return BlockIOResultCodes::SUCCESS;
    }

    BlockIOResultCodes SDCardController::Seek(uint64_t offset_in_blocks)
    {
        offset_in_blocks_ = offset_in_blocks;

        return BlockIOResultCodes::SUCCESS;
    }

    ValueResult<BlockIOResultCodes, uint32_t> SDCardController::ReadFromCurrentOffset(uint8_t *buffer, uint32_t blocks_to_read)
    {
        return ReadFromBlock(buffer, offset_in_blocks_, blocks_to_read);
    }

    ValueResult<BlockIOResultCodes, uint32_t> SDCardController::ReadFromBlock(uint8_t *buffer, uint32_t block_number, uint32_t blocks_to_read)
    {
        using Result = ValueResult<BlockIOResultCodes, uint32_t>;

        BlockIOResultCodes data_command_result = DataCommand(false, buffer, block_number, blocks_to_read);

        if (Failure(data_command_result))
        {
            return Result::Failure(data_command_result);
        }

        return Result::Success(blocks_to_read);
    }

    ValueResult<BlockIOResultCodes, uint32_t> SDCardController::WriteBlock(uint8_t *buffer, uint32_t block_number, uint32_t blocks_to_write)
    {
        using Result = ValueResult<BlockIOResultCodes, uint32_t>;

        BlockIOResultCodes data_command_result = DataCommand(true, buffer, block_number, blocks_to_write);

        if (Failure(data_command_result))
        {
            return Result::Failure(data_command_result);
        }

        return Result::Success(blocks_to_write);
    }
}

//
//  Global instance and accessor
//

static ExternalMassMediaController *__external_mass_media_controller = nullptr;

ExternalMassMediaController &GetExternalMassMediaController()
{
    if (__external_mass_media_controller == nullptr)
    {
        __external_mass_media_controller = static_new<EmmcImpl::SDCardController>(true, "SD CARD", "SD CARD", GetPlatformInfo());
    }

    return *__external_mass_media_controller;
}

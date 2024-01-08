// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stdint.h>

#include "asm_utility.h"
#include "memory.h"

#include "devices/gpio.h"
#include "devices/log.h"
#include "devices/mailbox_messages.h"
#include "devices/physical_timer.h"

#include "devices/emmc.h"

#include "devices/emmc/emmc_commands.h"
#include "devices/emmc/emmc_errors.h"
#include "devices/emmc/emmc_registers.h"
#include "devices/emmc/sd_card_registers.h"

#define BSWAP32(x) (((x << 24) & 0xff000000) | \
                    ((x << 8) & 0x00ff0000) |  \
                    ((x >> 8) & 0x0000ff00) |  \
                    ((x >> 24) & 0x000000ff))

//
//  SD Clock Frequencies (in Hz)
//

#define SD_CLOCK_ID 400000
#define SD_CLOCK_NORMAL 25000000
#define SD_CLOCK_HIGH 50000000
#define SD_CLOCK_100 100000000
#define SD_CLOCK_208 208000000
#define SD_COMMAND_COMPLETE 1
#define SD_TRANSFER_COMPLETE (1 << 1)
#define SD_BLOCK_GAP_EVENT (1 << 2)
#define SD_DMA_INTERRUPT (1 << 3)
#define SD_BUFFER_WRITE_READY (1 << 4)
#define SD_BUFFER_READ_READY (1 << 5)
#define SD_CARD_INSERTION (1 << 6)
#define SD_CARD_REMOVAL (1 << 7)
#define SD_CARD_INTERRUPT (1 << 8)

#define EMMC_CTRL1_RESET_DATA (1 << 26)
#define EMMC_CTRL1_RESET_CMD (1 << 25)
#define EMMC_CTRL1_RESET_HOST (1 << 24)
#define EMMC_CTRL1_RESET_ALL (EMMC_CTRL1_RESET_DATA | EMMC_CTRL1_RESET_CMD | EMMC_CTRL1_RESET_HOST)

#define EMMC_CTRL1_CLK_GENSEL (1 << 5)
#define EMMC_CTRL1_CLK_ENABLE (1 << 2)
#define EMMC_CTRL1_CLK_STABLE (1 << 1)
#define EMMC_CTRL1_CLK_INT_EN (1 << 0)

#define EMMC_CTRL0_ALT_BOOT_EN (1 << 22)
#define EMMC_CTRL0_BOOT_EN (1 << 21)
#define EMMC_CTRL0_SPI_MODE (1 << 20)

#define EMMC_STATUS_DAT_INHIBIT (1 << 1)
#define EMMC_STATUS_CMD_INHIBIT (1 << 0)

constexpr uint32_t INTERRUPT_REGISTER_ERROR_MASK = 0xFFFF0000;

bool Failure(BlockIOResultCodes code)
{
    return code != BlockIOResultCodes::SUCCESS;
}

bool Failure(minstd::pair<BlockIOResultCodes, uint32_t> code_and_error)
{
    return code_and_error.first() != BlockIOResultCodes::SUCCESS;
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

    ValueResult<BlockIOResultCodes, uint32_t> ReadFromBlock(uint8_t *buffer, uint32_t block_number, uint32_t bocks_to_read) override;
    ValueResult<BlockIOResultCodes, uint32_t> ReadFromCurrentOffset(uint8_t *buffer, uint32_t bocks_to_read) override;

private:
    const PlatformInfo &platform_info_;

    const uint8_t *mmio_base_;
    EMMCRegisters *registers_;

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

    uint32_t sd_error_mask(SDError err)
    {
        return 1 << (16 + (uint32_t)err);
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

    minstd::pair<BlockIOResultCodes, uint32_t> Command(EMMCCommandTypes command, uint32_t arg, uint32_t timeout);
    minstd::pair<BlockIOResultCodes, uint32_t> AppCommand(EMMCCommandTypes command, uint32_t arg, uint32_t timeout);
    BlockIOResultCodes ResetCommand();
    minstd::pair<BlockIOResultCodes, uint32_t> IssueCommand(EMMCCommand cmd, uint32_t arg, uint32_t timeout);
    BlockIOResultCodes DataCommand(bool write, uint8_t *buffer, uint32_t bytes_to_process, uint32_t block_number);

    minstd::pair<BlockIOResultCodes, uint32_t> TransferData(EMMCCommand cmd);

    void ConfigureGPIO();
    BlockIOResultCodes SetupClock();
    uint32_t GetClockDivider(uint32_t base_clock, uint32_t target_rate);
    BlockIOResultCodes SwitchClockRate(uint32_t base_clock, uint32_t target_rate);
};

minstd::pair<BlockIOResultCodes, uint32_t> SDCardController::TransferData(EMMCCommand cmd)
{
    uint32_t wrIrpt = 0;
    bool write = false;

    if (cmd.direction)
    {
        wrIrpt = 1 << 5;
    }
    else
    {
        wrIrpt = 1 << 4;
        write = true;
    }

    uint32_t *data = (uint32_t *)buffer_;

    for (uint32_t block = 0; block < transfer_blocks_; block++)
    {
        WaitForInterrupt(registers_->int_flags, wrIrpt | 0x8000, true, 2000);
        uint32_t intr_val = registers_->int_flags;
        registers_->int_flags = wrIrpt | 0x8000;

        if ((intr_val & (INTERRUPT_REGISTER_ERROR_MASK | wrIrpt)) != wrIrpt)
        {
            return minstd::make_pair(BlockIOResultCodes::EMMC_TRANSFER_DATA_FAILED, intr_val & INTERRUPT_REGISTER_ERROR_MASK);
        }

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

    return minstd::make_pair(BlockIOResultCodes::SUCCESS, 0U);
}

minstd::pair<BlockIOResultCodes, uint32_t> SDCardController::IssueCommand(EMMCCommand command, uint32_t arg, uint32_t timeout)
{
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
        return minstd::make_pair(BlockIOResultCodes::EMMC_ISSUE_COMMAND_TRANSFER_BLOCKS_IS_TOO_LARGE, 0U);
    }

    registers_->block_size_count = block_size_ | (transfer_blocks_ << 16);
    registers_->arg1 = arg;
    registers_->cmd_xfer_mode = command_reg;

    timer.WaitMsec(10);

    uint32_t times = 0;

    while (times < timeout)
    {
        uint32_t reg = registers_->int_flags;

        if (reg & 0x8001)
        {
            break;
        }

        timer.WaitMsec(1);
        times++;
    }

    if (times >= timeout)
    {
        //  Timeouts here are OK sometimes, so not necessarily a fatal condition
        return minstd::make_pair(BlockIOResultCodes::EMMC_TIMEOUT_FOR_ISSUE_COMMAND, 0U);
    }

    uint32_t intr_val = registers_->int_flags;

    registers_->int_flags = 0xFFFF0001;

    if ((intr_val & 0xFFFF0001) != 1)
    {
        return minstd::make_pair(BlockIOResultCodes::EMMC_ISSUE_COMMAND_ERROR_WAITING_FOR_COMMAND_INTERRUPT_TO_COMPLETE, intr_val & INTERRUPT_REGISTER_ERROR_MASK);
    }

    switch (command.response_type)
    {
    case RT48:
    case RT48Busy:
        last_response_[0] = registers_->response[0];
        break;

    case RT136:
        last_response_[0] = registers_->response[0];
        last_response_[1] = registers_->response[1];
        last_response_[2] = registers_->response[2];
        last_response_[3] = registers_->response[3];
        break;
    }

    if (command.is_data)
    {
        TransferData(command);
    }

    if (command.response_type == RT48Busy || command.is_data)
    {
        WaitForInterrupt(registers_->int_flags, 0x8002, true, 2000);
        intr_val = registers_->int_flags;

        registers_->int_flags = 0xFFFF0002;

        if ((intr_val & 0xFFFF0002) != 2 && (intr_val & 0xFFFF0002) != 0x100002)
        {
            return minstd::make_pair(BlockIOResultCodes::EMMC_ISSUE_COMMAND_ERROR_WAITING_FOR_COMMAND_INTERRUPT_TO_COMPLETE, intr_val & INTERRUPT_REGISTER_ERROR_MASK);
        }

        registers_->int_flags = 0xFFFF0002;
    }

    //    last_success_ = true;

    return minstd::make_pair(BlockIOResultCodes::SUCCESS, 0U);
}

minstd::pair<BlockIOResultCodes, uint32_t> SDCardController::Command(EMMCCommandTypes command, uint32_t arg, uint32_t timeout)
{
    if (GetCommand(command) == INVALID_CMD)
    {
        return minstd::make_pair(BlockIOResultCodes::EMMC_INVALID_COMMAND, 0U);
    }

    return IssueCommand(GetCommand(command), arg, timeout);
}

BlockIOResultCodes SDCardController::ResetCommand()
{
    registers_->control[1] |= EMMC_CTRL1_RESET_CMD;

    for (int i = 0; i < 10000; i++)
    {
        if (!(registers_->control[1] & EMMC_CTRL1_RESET_CMD))
        {
            return BlockIOResultCodes::SUCCESS;
        }

        PhysicalTimer().WaitMsec(1);
    }

    return BlockIOResultCodes::EMMC_COMMAND_LINE_FAILED_TO_RESET_CORRECTLY;
}

minstd::pair<BlockIOResultCodes, uint32_t> SDCardController::AppCommand(EMMCCommandTypes command, uint32_t arg, uint32_t timeout)
{
    if (GetCommand(command).index >= 60)
    {
        return minstd::make_pair(BlockIOResultCodes::EMMC_INVALID_APP_COMMAND, 0U);
    }

    uint32_t rca = 0;

    if (relative_card_address_register_)
    {
        rca = relative_card_address_register_ << 16;
    }

    //  Send the APP Command first, then the command we were passed

    RETURN_IF_FAILED(IssueCommand(GetCommand(EMMCCommandTypes::App), rca, 2000));

    return IssueCommand(GetCommand(command), arg, 2000);
}

bool SDCardController::IsV2Card()
{
    minstd::pair<BlockIOResultCodes, uint32_t> result = Command(EMMCCommandTypes::SendIfCond, 0x1AA, 200);

    if (result.first() != BlockIOResultCodes::SUCCESS)
    {
        if (result.second() == 0)
        {
            //  Timeout
            return false;
        }
        else if (result.second() & (1 << 16))
        {
            //  Timeout on the command
            if (ResetCommand() != BlockIOResultCodes::SUCCESS)
            {
                return false;
            }

            registers_->int_flags = sd_error_mask(SDError::CommandTimeout);
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
    minstd::pair<BlockIOResultCodes, uint32_t> result = Command(EMMCCommandTypes::IOSetOpCond, 0, 1000);

    if (result.first() != BlockIOResultCodes::SUCCESS)
    {
        if (result.second() == 0)
        {
            //  Timeout
            LogDebug1("EMMC_ERR: CTIOSetOpCond Timeout\n");
        }
        else if (result.second() & (1 << 16))
        {
            //  Timeout command error - this is a normal expected error and calling the reset command will fix it.

            RETURN_IF_FAILED(ResetCommand());

            registers_->int_flags = sd_error_mask(SDError::CommandTimeout);
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

        if (AppCommand(EMMCCommandTypes::OcrCheck, 0x00FF8000 | v2_flags, 2000).first() != BlockIOResultCodes::SUCCESS)
        {
            return BlockIOResultCodes::EMMC_APP_COMMAND_41_FAILED;
        }

        if (last_response_[0] >> 31 & 1)
        {
            operating_conditions_register_ = (last_response_[0] >> 8 & 0xFFFF);
            is_sdhc_card_ = ((last_response_[0] >> 30) & 1) != 0;
            break;
        }
        else
        {
            LogDebug1("EMMC_DEBUG: SLEEPING: %X\n", last_response_[0]);
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
        last_result = AppCommand(EMMCCommandTypes::OcrCheck, 0, 2000).first();

        if (last_result == BlockIOResultCodes::SUCCESS)
        {
            break;
        }
    }

    if (last_result != BlockIOResultCodes::SUCCESS)
    {
        return BlockIOResultCodes::EMMC_APP_COMMAND_41_FAILED;
    }

    operating_conditions_register_ = (last_response_[0] >> 8 & 0xFFFF);

    return BlockIOResultCodes::SUCCESS;
}

BlockIOResultCodes SDCardController::CheckRelativeCardAddressRegister()
{
    if (Command(EMMCCommandTypes::SendCide, 0, 2000).first() != BlockIOResultCodes::SUCCESS)
    {
        return BlockIOResultCodes::EMMC_SEND_CID_COMMAND_FAILED;
    }

    if (Command(EMMCCommandTypes::SendRelativeAddr, 0, 2000).first() != BlockIOResultCodes::SUCCESS)
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
    if (Command(EMMCCommandTypes::SelectCard, relative_card_address_register_ << 16, 2000).first() != BlockIOResultCodes::SUCCESS)
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
        if (Command(EMMCCommandTypes::SetBlockLen, 512, 2000).first() != BlockIOResultCodes::SUCCESS)
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

    if (AppCommand(EMMCCommandTypes::SendSCR, 0, 30000).first() != BlockIOResultCodes::SUCCESS)
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

    registers_->control[1] = EMMC_CTRL1_RESET_HOST;

    if (!WaitForInterrupt(registers_->control[1], EMMC_CTRL1_RESET_ALL, false, 2000))
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

    RETURN_IF_FAILED(SetupClock());

    //  Route all interrupts to the Interrupt Register

    registers_->int_enable = 0;
    registers_->int_flags = 0xFFFFFFFF;
    registers_->int_mask = 0xFFFFFFFF;

    timer.WaitMsec(203);

    transfer_blocks_ = 0;
    block_size_ = 0;

    if (Command(EMMCCommandTypes::GoIdle, 0, 2000).first() != BlockIOResultCodes::SUCCESS)
    {
        return BlockIOResultCodes::EMMC_NO_RESPONSE_TO_GO_IDLE_COMMAND;
    }

    bool v2_card = IsV2Card();

    RETURN_IF_FAILED(IsUsableCard());

    RETURN_IF_FAILED(CheckOperatingConditionsRegister());

    RETURN_IF_FAILED(SetIsSDHCCard(v2_card));

    RETURN_IF_FAILED(SwitchClockRate(0U, SD_CLOCK_NORMAL)); //  Not sure why this is always zero - but it is...

    RETURN_IF_FAILED(CheckRelativeCardAddressRegister());

    RETURN_IF_FAILED(SelectCard());

    RETURN_IF_FAILED(SetSDCardConfigurationRegister());

    // enable all interrupts

    registers_->int_flags = 0xFFFFFFFF;

    return BlockIOResultCodes::SUCCESS;
}

BlockIOResultCodes SDCardController::DataCommand(bool write, uint8_t *buffer, uint32_t block_number, uint32_t blocks_to_read)
{
    if (!is_sdhc_card_)
    {
        block_number *= 512;
    }

    transfer_blocks_ = blocks_to_read;

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
        if (Command(command, block_number, 5000).first() == BlockIOResultCodes::SUCCESS)
        {
            break;
        }

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

    if (data_command_result != BlockIOResultCodes::SUCCESS)
    {
        return Result::Failure(data_command_result);
    }

    return Result::Success(blocks_to_read);
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

uint32_t SDCardController::GetClockDivider(uint32_t base_clock, uint32_t target_rate)
{
    uint32_t target_div = 1;

    if (target_rate <= base_clock)
    {
        target_div = base_clock / target_rate;

        if (base_clock % target_rate)
        {
            target_div = 0;
        }
    }

    int div = -1;
    for (int fb = 31; fb >= 0; fb--)
    {
        uint32_t bt = (1 << fb);

        if (target_div & bt)
        {
            div = fb;
            target_div &= ~(bt);

            if (target_div)
            {
                div++;
            }

            break;
        }
    }

    if (div == -1)
    {
        div = 31;
    }

    if (div >= 32)
    {
        div = 31;
    }

    if (div != 0)
    {
        div = (1 << (div - 1));
    }

    if (div >= 0x400)
    {
        div = 0x3FF;
    }

    uint32_t freqSel = div & 0xff;
    uint32_t upper = (div >> 8) & 0x3;
    uint32_t ret = (freqSel << 8) | (upper << 6) | (0 << 5);

    return ret;
}

BlockIOResultCodes SDCardController::SwitchClockRate(uint32_t base_clock, uint32_t target_rate)
{
    PhysicalTimer timer;

    uint32_t divider = GetClockDivider(base_clock, target_rate);

    const uint32_t max_retries = 1000;
    uint32_t retries = 0;

    while (((registers_->status & (EMMC_STATUS_CMD_INHIBIT | EMMC_STATUS_DAT_INHIBIT)) != 0) &&
           (retries < max_retries))
    {
        timer.WaitMsec(1);
        retries++;
    }

    if (retries >= max_retries)
    {
        return BlockIOResultCodes::EMMC_TIMEOUT_WAITING_FOR_INHIBITS_TO_CLEAR;
    }

    uint32_t c1 = registers_->control[1] & ~EMMC_CTRL1_CLK_ENABLE;

    registers_->control[1] = c1;

    timer.WaitMsec(5);

    registers_->control[1] = (c1 | divider) & ~0xFFE0;

    timer.WaitMsec(5);

    registers_->control[1] = c1 | EMMC_CTRL1_CLK_ENABLE;

    timer.WaitMsec(20); //  Wait a little longer before returning

    return BlockIOResultCodes::SUCCESS;
}

BlockIOResultCodes SDCardController::SetupClock()
{
    registers_->control2 = 0;

    //  Get the current EMMC clock rate from the mailbox service

    Mailbox mbox(GetPlatformInfo().GetMMIOBase());

    GetClockRateTag getEMMCClockRateTag(MailboxClockIdentifiers(1));

    MailboxPropertyMessage getEMMCClockRateMessage(getEMMCClockRateTag);

    mbox.sendMessage(getEMMCClockRateMessage);

    LogDebug1("EMMC SetupClock Rate: %u\n", getEMMCClockRateTag.GetRateInHz());

    uint32_t n = registers_->control[1];
    n |= EMMC_CTRL1_CLK_INT_EN;
    n |= GetClockDivider(getEMMCClockRateTag.GetRateInHz(), SD_CLOCK_ID);
    n &= ~(0xf << 16);
    n |= (11 << 16);

    registers_->control[1] = n;

    if (!WaitForInterrupt(registers_->control[1], EMMC_CTRL1_CLK_STABLE, true, 2000))
    {
        return BlockIOResultCodes::EMMC_CLOCK_IS_NOT_STABLE;
    }

    PhysicalTimer().WaitMsec(30);

    //  Enable the clock

    registers_->control[1] |= 4;

    PhysicalTimer().WaitMsec(30);

    return BlockIOResultCodes::SUCCESS;
}

//
//  Global instance and accessor
//

static ExternalMassMediaController *__external_mass_media_controller = nullptr;

ExternalMassMediaController &GetExternalMassMediaController()
{
    if (__external_mass_media_controller == nullptr)
    {
        __external_mass_media_controller = static_new<SDCardController>(true, "SD CARD", "SD CARD", GetPlatformInfo());
    }

    return *__external_mass_media_controller;
}

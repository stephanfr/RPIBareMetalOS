// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "hypervisor_abort.h"
#include "hypervisor_config.h"
#include "hypervisor_mmio.h"

#include "cpu_part_nums.h"

#include <stdint.h>

namespace
{
    constexpr uint64_t ISV = 1ull << 24;
    constexpr uint64_t SSE = 1ull << 21;
    constexpr uint64_t SF = 1ull << 15;
    constexpr uint64_t FNV = 1ull << 10;
    constexpr uint64_t EA = 1ull << 9;
    constexpr uint64_t CM = 1ull << 8;
    constexpr uint64_t S1PTW = 1ull << 7;
    constexpr uint64_t DFSC_TRANSLATION_LEVEL_3 = 0x07;
    constexpr uint64_t DFSC_PERMISSION_LEVEL_3 = 0x0F;

    //  A stage-2 translation fault from a single-register load or store, as ESR_EL2 reports it.

    uint64_t Esr(uint32_t size, bool write, uint32_t reg, bool sign_extend = false, bool register_64 = false)
    {
        const uint64_t sas = (size == 1) ? 0 : (size == 2) ? 1 : (size == 4) ? 2 : 3;

        return (static_cast<uint64_t>(ESR_EC_DABT_LOWER) << ESR_EC_SHIFT) | ESR_IL | ISV | (sas << 22) |
               (sign_extend ? SSE : 0) | (static_cast<uint64_t>(reg) << 16) | (register_64 ? SF : 0) |
               (write ? ESR_WNR : 0) | DFSC_TRANSLATION_LEVEL_3;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(MmioDecodeTests)
    {
        HypervisorMmioAccess access{};
    };
#pragma GCC diagnostic pop

    TEST(MmioDecodeTests, EverySizeDirectionAndRegisterIsRead)
    {
        const uint32_t sizes[] = {1, 2, 4, 8};

        for (uint32_t size : sizes)
        {
            for (uint32_t reg = 0; reg <= HYP_MMIO_REGISTER_ZERO; reg++)
            {
                CHECK_EQUAL(1U, HypervisorMmioDecode(Esr(size, true, reg), &access));
                CHECK_EQUAL(size, access.size);
                CHECK_EQUAL(1U, access.write);
                CHECK_EQUAL(reg, access.reg);

                CHECK_EQUAL(1U, HypervisorMmioDecode(Esr(size, false, reg), &access));
                CHECK_EQUAL(0U, access.write);
            }
        }
    }

    TEST(MmioDecodeTests, SignExtensionAndRegisterWidthAreRead)
    {
        HypervisorMmioDecode(Esr(1, false, 3, true, true), &access);
        CHECK_EQUAL(1U, access.sign_extend);
        CHECK_EQUAL(1U, access.register_64);

        HypervisorMmioDecode(Esr(2, false, 3, true, false), &access);
        CHECK_EQUAL(1U, access.sign_extend);
        CHECK_EQUAL(0U, access.register_64);

        HypervisorMmioDecode(Esr(4, false, 3), &access);
        CHECK_EQUAL(0U, access.sign_extend);
        CHECK_EQUAL(0U, access.register_64);

        //  A store never sign-extends, whatever SSE holds.

        HypervisorMmioDecode(Esr(4, true, 3, true, true), &access);
        CHECK_EQUAL(0U, access.sign_extend);
    }

    TEST(MmioDecodeTests, FaultsEl2CannotEmulateAreRefused)
    {
        const uint64_t load = Esr(4, false, 1);

        CHECK_EQUAL(0U, HypervisorMmioDecode(load & ~ISV, &access));                     //  ldp, writeback, SIMD
        CHECK_EQUAL(0U, HypervisorMmioDecode(load | FNV, &access));                      //  FAR not valid
        CHECK_EQUAL(0U, HypervisorMmioDecode(load | EA, &access));                       //  external abort
        CHECK_EQUAL(0U, HypervisorMmioDecode(load | CM, &access));                       //  cache maintenance
        CHECK_EQUAL(0U, HypervisorMmioDecode(load | S1PTW, &access));                    //  on a stage-1 walk
        CHECK_EQUAL(0U, HypervisorMmioDecode((load & ~0x3Full) | DFSC_PERMISSION_LEVEL_3, &access));

        const uint64_t instruction_abort = (load & ~(ESR_EC_MASK << ESR_EC_SHIFT)) |
                                           (static_cast<uint64_t>(ESR_EC_IABT_LOWER) << ESR_EC_SHIFT);

        CHECK_EQUAL(0U, HypervisorMmioDecode(instruction_abort, &access));
    }

    TEST(MmioDecodeTests, TranslationFaultsAtEveryLevelAreAccepted)
    {
        for (uint64_t level = 0; level <= 3; level++)
        {
            CHECK_EQUAL(1U, HypervisorMmioDecode((Esr(4, false, 1) & ~0x3Full) | (0x04 + level), &access));
        }
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(MmioValueTests)
    {
        HypervisorMmioAccess access{};
        uint64_t x[31] = {};

        void Decode(uint64_t esr)
        {
            CHECK_EQUAL(1U, HypervisorMmioDecode(esr, &access));
        }
    };
#pragma GCC diagnostic pop

    TEST(MmioValueTests, AStoreWritesTheLowBytesOfItsRegister)
    {
        x[5] = 0x1122334455667788;

        Decode(Esr(1, true, 5));
        CHECK_EQUAL(0x88U, HypervisorMmioStoreValue(&access, x));

        Decode(Esr(2, true, 5));
        CHECK_EQUAL(0x7788U, HypervisorMmioStoreValue(&access, x));

        Decode(Esr(4, true, 5));
        CHECK_EQUAL(0x55667788U, HypervisorMmioStoreValue(&access, x));

        Decode(Esr(8, true, 5, false, true));
        CHECK_EQUAL(0x1122334455667788U, HypervisorMmioStoreValue(&access, x));
    }

    TEST(MmioValueTests, AStoreFromTheZeroRegisterWritesZero)
    {
        for (uint64_t &value : x)
        {
            value = ~0ull;
        }

        Decode(Esr(8, true, HYP_MMIO_REGISTER_ZERO, false, true));
        CHECK_EQUAL(0U, HypervisorMmioStoreValue(&access, x));
    }

    TEST(MmioValueTests, ALoadZeroExtendsUnlessItSignExtends)
    {
        Decode(Esr(1, false, 0, false, false));
        CHECK_EQUAL(0x80U, HypervisorMmioLoadResult(&access, 0xFFFFFF80));

        Decode(Esr(2, false, 0, false, true));
        CHECK_EQUAL(0x8000U, HypervisorMmioLoadResult(&access, 0x8000));

        Decode(Esr(4, false, 0, false, true));
        CHECK_EQUAL(0x80000000U, HypervisorMmioLoadResult(&access, 0x80000000));
    }

    TEST(MmioValueTests, ASignExtendingLoadFillsItsRegisterWidth)
    {
        Decode(Esr(1, false, 0, true, true)); //  ldrsb x0
        CHECK_EQUAL(0xFFFFFFFFFFFFFF80U, HypervisorMmioLoadResult(&access, 0x80));
        CHECK_EQUAL(0x7FU, HypervisorMmioLoadResult(&access, 0x7F));

        Decode(Esr(1, false, 0, true, false)); //  ldrsb w0: the upper half of x0 is zeroed
        CHECK_EQUAL(0xFFFFFF80U, HypervisorMmioLoadResult(&access, 0x80));

        Decode(Esr(2, false, 0, true, true)); //  ldrsh x0
        CHECK_EQUAL(0xFFFFFFFFFFFF8000U, HypervisorMmioLoadResult(&access, 0x8000));

        Decode(Esr(4, false, 0, true, true)); //  ldrsw x0
        CHECK_EQUAL(0xFFFFFFFF80000000U, HypervisorMmioLoadResult(&access, 0x80000000));
        CHECK_EQUAL(0x7FFFFFFFU, HypervisorMmioLoadResult(&access, 0x7FFFFFFF));

        Decode(Esr(8, false, 0, true, true));
        CHECK_EQUAL(0x8000000000000000U, HypervisorMmioLoadResult(&access, 0x8000000000000000));
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(MmioTestDeviceTests)
    {
        uint8_t registers[HYPERVISOR_MMIO_TEST_SIZE] = {};
        HypervisorMmioAccess access{};
        uint64_t value = 0;

        uint32_t Access(uint64_t offset, uint32_t size, bool write)
        {
            CHECK_EQUAL(1U, HypervisorMmioDecode(Esr(size, write, 0, false, size == 8), &access));
            return HypervisorMmioTestDevice(registers, offset, &access, &value);
        }
    };
#pragma GCC diagnostic pop

    TEST(MmioTestDeviceTests, ReadsReturnWhatWasWrittenLittleEndian)
    {
        value = 0x0807060504030201;
        CHECK_EQUAL(1U, Access(0x10, 8, true));

        CHECK_EQUAL(1U, Access(0x10, 8, false));
        CHECK_EQUAL(0x0807060504030201U, value);

        CHECK_EQUAL(1U, Access(0x14, 4, false));
        CHECK_EQUAL(0x08070605U, value);

        CHECK_EQUAL(1U, Access(0x12, 2, false));
        CHECK_EQUAL(0x0403U, value);

        CHECK_EQUAL(1U, Access(0x17, 1, false));
        CHECK_EQUAL(0x08U, value);

        value = 0xAA;
        CHECK_EQUAL(1U, Access(0x11, 1, true));
        CHECK_EQUAL(1U, Access(0x10, 4, false));
        CHECK_EQUAL(0x0403AA01U, value);
    }

    TEST(MmioTestDeviceTests, TheWholeRegisterFileIsReachable)
    {
        for (uint64_t offset = 0; offset < HYPERVISOR_MMIO_TEST_SIZE; offset += 4)
        {
            value = 0xC0DE0000 | offset;
            CHECK_EQUAL(1U, Access(offset, 4, true));
        }

        for (uint64_t offset = 0; offset < HYPERVISOR_MMIO_TEST_SIZE; offset += 4)
        {
            CHECK_EQUAL(1U, Access(offset, 4, false));
            CHECK_EQUAL(0xC0DE0000 | offset, value);
        }
    }

    TEST(MmioTestDeviceTests, AccessesOutsideItOrMisalignedAreRefused)
    {
        value = 0x55;

        CHECK_EQUAL(0U, Access(HYPERVISOR_MMIO_TEST_SIZE, 4, false));
        CHECK_EQUAL(0U, Access(HYPERVISOR_MMIO_TEST_SIZE - 4, 8, true));
        CHECK_EQUAL(0U, Access(0xFFC, 4, true));
        CHECK_EQUAL(0U, Access(0x02, 4, false));
        CHECK_EQUAL(0U, Access(0x01, 2, true));

        for (uint8_t byte : registers)
        {
            CHECK_EQUAL(0, byte);
        }
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(MailboxPageFilterTests)
    {
        HypervisorMmioAccess access{};

        uint32_t Passes(uint32_t board, uint64_t offset, uint32_t size, bool write)
        {
            CHECK_EQUAL(1U, HypervisorMmioDecode(Esr(size, write, 0), &access));
            return HypervisorMailboxPagePassesThrough(board, offset, &access);
        }
    };
#pragma GCC diagnostic pop

    TEST(MailboxPageFilterTests, ThePi3sInterruptRegistersPassThrough)
    {
        for (uint64_t offset = 0x200; offset <= 0x224; offset += 4)
        {
            CHECK_EQUAL(1U, Passes(RPI_BOARD_ENUM_RPI3, offset, 4, false));
            CHECK_EQUAL(1U, Passes(RPI_BOARD_ENUM_RPI3, offset, 4, true));
        }
    }

    TEST(MailboxPageFilterTests, TheMailboxAndOtherBoardsAreRefused)
    {
        const uint64_t mailbox[] = {0x880, 0x890, 0x898, 0x8A0, 0x8B8};

        for (uint64_t offset : mailbox)
        {
            CHECK_EQUAL(0U, Passes(RPI_BOARD_ENUM_RPI3, offset, 4, false));
            CHECK_EQUAL(0U, Passes(RPI_BOARD_ENUM_RPI3, offset, 4, true));
        }

        CHECK_EQUAL(0U, Passes(RPI_BOARD_ENUM_RPI3, 0x1FC, 4, false));
        CHECK_EQUAL(0U, Passes(RPI_BOARD_ENUM_RPI3, 0x228, 4, false));
        CHECK_EQUAL(0U, Passes(RPI_BOARD_ENUM_RPI3, 0x200, 8, false));
        CHECK_EQUAL(0U, Passes(RPI_BOARD_ENUM_RPI3, 0x204, 2, false));
        CHECK_EQUAL(0U, Passes(RPI_BOARD_ENUM_RPI3, 0x201, 1, false));

        CHECK_EQUAL(0U, Passes(RPI_BOARD_ENUM_RPI4, 0x200, 4, false));
        CHECK_EQUAL(0U, Passes(RPI_BOARD_ENUM_RPI5, 0x200, 4, false));
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(SdhciFilterTests)
    {
        HypervisorMmioAccess access{};
        uint64_t value = 0;

        uint32_t Filter(uint64_t offset, uint32_t size, bool write, uint64_t written = 0, uint32_t board = RPI_BOARD_ENUM_RPI3)
        {
            CHECK_EQUAL(1U, HypervisorMmioDecode(Esr(size, write, 0), &access));
            value = written;
            return HypervisorSdhciFilter(board, offset, &access, &value);
        }
    };
#pragma GCC diagnostic pop

    TEST(SdhciFilterTests, ThePioDriversAccessesPassUnchanged)
    {
        CHECK_EQUAL(HYP_SDHCI_PASS, Filter(0x20, 4, false));             //  data port
        CHECK_EQUAL(HYP_SDHCI_PASS, Filter(0x20, 4, true, 0xDEADBEEF));  //  data port
        CHECK_EQUAL(0xDEADBEEFU, value);
        CHECK_EQUAL(HYP_SDHCI_PASS, Filter(0x0C, 4, true, 0x113A0032));  //  CMD17, read, no DMA
        CHECK_EQUAL(0x113A0032U, value);
        CHECK_EQUAL(HYP_SDHCI_PASS, Filter(0x28, 4, true, 0x00000F06));  //  4-bit, power on
        CHECK_EQUAL(0x00000F06U, value);
        CHECK_EQUAL(HYP_SDHCI_PASS, Filter(0x2F, 1, true, 0x01));        //  software reset, a byte write
        CHECK_EQUAL(HYP_SDHCI_PASS, Filter(0x2C, 2, true, 0x0105));
        CHECK_EQUAL(HYP_SDHCI_PASS, Filter(0xFC, 4, false));             //  slot interrupt status
    }

    TEST(SdhciFilterTests, DmaEnableAndDmaSelectAreClearedWhateverTheWidth)
    {
        CHECK_EQUAL(HYP_SDHCI_PASS_DMA_CLEARED, Filter(0x0C, 4, true, 0x113A0033));
        CHECK_EQUAL(0x113A0032U, value);
        CHECK_EQUAL(HYP_SDHCI_PASS_DMA_CLEARED, Filter(0x0C, 2, true, 0x0033));
        CHECK_EQUAL(0x0032U, value);
        CHECK_EQUAL(HYP_SDHCI_PASS_DMA_CLEARED, Filter(0x0C, 1, true, 0x01));
        CHECK_EQUAL(0U, value);

        CHECK_EQUAL(HYP_SDHCI_PASS_DMA_CLEARED, Filter(0x28, 4, true, 0x00000F1E));
        CHECK_EQUAL(0x00000F06U, value);
        CHECK_EQUAL(HYP_SDHCI_PASS_DMA_CLEARED, Filter(0x28, 1, true, 0x10));
        CHECK_EQUAL(0U, value);

        //  The same bits in neighbouring bytes are left alone.

        CHECK_EQUAL(HYP_SDHCI_PASS, Filter(0x0E, 2, true, 0x0001));
        CHECK_EQUAL(HYP_SDHCI_PASS, Filter(0x28, 4, true, 0x18181800));
        CHECK_EQUAL(0x18181800U, value);
    }

    TEST(SdhciFilterTests, WritesToTheDmaAddressRegistersAreIgnored)
    {
        const uint64_t offsets[] = {0x00, 0x58, 0x5C};

        for (uint64_t offset : offsets)
        {
            CHECK_EQUAL(HYP_SDHCI_IGNORE, Filter(offset, 4, true, 0x80000000));
            CHECK_EQUAL(HYP_SDHCI_PASS, Filter(offset, 4, false));
        }

        CHECK_EQUAL(HYP_SDHCI_IGNORE, Filter(0x02, 2, true, 0x8000));
        CHECK_EQUAL(HYP_SDHCI_IGNORE, Filter(0x5F, 1, true, 0x80));
        CHECK_EQUAL(HYP_SDHCI_PASS, Filter(0x04, 4, true, 0x00010200)); //  block size and count
    }

    TEST(SdhciFilterTests, WideMisalignedOrOutOfRangeAccessesAreRefused)
    {
        CHECK_EQUAL(HYP_SDHCI_REFUSE, Filter(0x20, 8, false));
        CHECK_EQUAL(HYP_SDHCI_REFUSE, Filter(0x22, 4, false));
        CHECK_EQUAL(HYP_SDHCI_REFUSE, Filter(0x0D, 2, true, 0));
        CHECK_EQUAL(HYP_SDHCI_REFUSE, Filter(0x100, 4, false));
        CHECK_EQUAL(HYP_SDHCI_REFUSE, Filter(0xFFC, 4, true, 0));
    }

    TEST(SdhciFilterTests, OnlyThePi5ReachesItsSdioConfigBlock)
    {
        CHECK_EQUAL(HYP_SDHCI_PASS, Filter(0x400, 4, false, 0, RPI_BOARD_ENUM_RPI5));
        CHECK_EQUAL(HYP_SDHCI_PASS, Filter(0x400, 4, true, 0x80000000, RPI_BOARD_ENUM_RPI5));
        CHECK_EQUAL(0x80000000U, value);
        CHECK_EQUAL(HYP_SDHCI_PASS, Filter(0x444, 4, true, 0x2, RPI_BOARD_ENUM_RPI5));
        CHECK_EQUAL(HYP_SDHCI_REFUSE, Filter(0x500, 4, false, 0, RPI_BOARD_ENUM_RPI5));
        CHECK_EQUAL(HYP_SDHCI_REFUSE, Filter(0x3FC, 4, false, 0, RPI_BOARD_ENUM_RPI5));

        CHECK_EQUAL(HYP_SDHCI_REFUSE, Filter(0x400, 4, false, 0, RPI_BOARD_ENUM_RPI3));
        CHECK_EQUAL(HYP_SDHCI_REFUSE, Filter(0x444, 4, true, 0x2, RPI_BOARD_ENUM_RPI4));

        //  The standard block is filtered the same on the Pi 5.

        CHECK_EQUAL(HYP_SDHCI_PASS_DMA_CLEARED, Filter(0x0C, 4, true, 0x113A0033, RPI_BOARD_ENUM_RPI5));
        CHECK_EQUAL(HYP_SDHCI_IGNORE, Filter(0x58, 4, true, 0x1000, RPI_BOARD_ENUM_RPI5));
    }
} // namespace

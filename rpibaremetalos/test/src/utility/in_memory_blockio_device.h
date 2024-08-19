// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/block_io.h"

namespace ut_utility
{
    class InMemoryFileBlockIODevice : public BlockIODevice
    {
    public:
        static constexpr size_t BLOCK_SIZE_IN_BYTES = 512;

        InMemoryFileBlockIODevice(const char *name)
            : BlockIODevice(false, name, name)
        {
        }

        ~InMemoryFileBlockIODevice();

        bool Open(const char *filename);

        uint32_t BlockSize() const override
        {
            return BLOCK_SIZE_IN_BYTES;
        }

        void SimulateReadError( uint32_t    requests_before_error = 0 )
        {
            simulate_read_error_ = true;
            requests_before_read_error_ = requests_before_error;
        }

        void SimulateWriteError( uint32_t    requests_before_error = 0 )
        {
            simulate_write_error_ = true;
            requests_before_write_error_ = requests_before_error;
        }

        BlockIOResultCodes Seek(uint64_t offset_in_blocks) override
        {
            return BlockIOResultCodes::FAILURE;
        }

        ValueResult<BlockIOResultCodes, uint32_t> ReadFromBlock(uint8_t *buffer, uint32_t block_number, uint32_t blocks_to_read) override;

        ValueResult<BlockIOResultCodes, uint32_t> ReadFromCurrentOffset(uint8_t *buffer, uint32_t bocks_to_read) override;

        ValueResult<BlockIOResultCodes, uint32_t> WriteBlock(uint8_t *buffer, uint32_t block_number, uint32_t blocks_to_write) override;

    private:
        using Block = uint8_t[512];

        size_t size_in_bytes_ = 0;
        size_t size_in_blocks_ = 0;

        Block *in_memory_file_ = nullptr;

        bool simulate_read_error_ = false;
        uint32_t requests_before_read_error_ = 0;

        bool simulate_write_error_ = false;
        uint32_t requests_before_write_error_ = 0;
    };
}
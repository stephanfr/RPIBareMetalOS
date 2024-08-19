// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "in_memory_blockio_device.h"
#include "heaps.h"

#include <stdio.h>

namespace ut_utility
{

    InMemoryFileBlockIODevice::~InMemoryFileBlockIODevice()
    {
        if (in_memory_file_ != nullptr)
        {
            __os_dynamic_heap.deallocate_block<Block>(in_memory_file_, size_in_blocks_);
        }
    }

    bool InMemoryFileBlockIODevice::Open(const char *filename)
    {
        //  Open the file

        FILE *file_to_read = fopen(filename, "r");

        if (file_to_read == nullptr)
        {
            return false;
        }

        //  Determine the length of the file in bytes

        fseek(file_to_read, 0L, SEEK_END);

        size_in_bytes_ = ftell(file_to_read);

        fseek(file_to_read, 0L, SEEK_SET);

        //  Allocate the buffer and read the entire file into memory

        size_in_blocks_ = size_in_bytes_ / BlockSize();

        in_memory_file_ = __os_dynamic_heap.allocate_block<Block>(size_in_blocks_);

        size_t blocks_read = fread(in_memory_file_, BlockSize(), size_in_blocks_, file_to_read);

        //  Close the file

        fclose(file_to_read);

        //  Success if the number of blocks read matches the number of blocks in the file

        return (size_in_blocks_ == blocks_read);
    }

    ValueResult<BlockIOResultCodes, uint32_t> InMemoryFileBlockIODevice::ReadFromBlock(uint8_t *buffer, uint32_t block_number, uint32_t blocks_to_read)
    {
        using Result = ValueResult<BlockIOResultCodes, uint32_t>;

        if(simulate_read_error_)
        {
            if(requests_before_read_error_ == 0)
            {
                simulate_read_error_ = false;
                return Result::Failure(BlockIOResultCodes::EMMC_READ_FAILED);
            }

            requests_before_read_error_--;
        }

        memmove(buffer, &(in_memory_file_[block_number]), blocks_to_read * BlockSize());

        return Result::Success(blocks_to_read);
    }

    ValueResult<BlockIOResultCodes, uint32_t> InMemoryFileBlockIODevice::ReadFromCurrentOffset(uint8_t *buffer, uint32_t bocks_to_read)
    {
        return ValueResult<BlockIOResultCodes, uint32_t>::Failure(BlockIOResultCodes::FAILURE);
    }

    ValueResult<BlockIOResultCodes, uint32_t> InMemoryFileBlockIODevice::WriteBlock(uint8_t *buffer, uint32_t block_number, uint32_t blocks_to_write)
    {
        using Result = ValueResult<BlockIOResultCodes, uint32_t>;

        if(simulate_write_error_)
        {
            if(requests_before_write_error_ == 0)
            {
                simulate_write_error_ = false;
                return Result::Failure(BlockIOResultCodes::EMMC_DATA_COMMAND_MAX_RETRIES);
            }

            requests_before_write_error_--;
        }

        memmove(&(in_memory_file_[block_number]), buffer, blocks_to_write * BlockSize());

        return Result::Success(blocks_to_write);
    }

}
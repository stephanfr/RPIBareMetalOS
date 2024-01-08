// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

typedef struct EMMCRegisters
{
    volatile uint32_t arg2;
    volatile uint32_t block_size_count;
    volatile uint32_t arg1;
    volatile uint32_t cmd_xfer_mode;
    volatile uint32_t response[4];
    volatile uint32_t data;
    volatile uint32_t status;
    volatile uint32_t control[2];
    volatile uint32_t int_flags;
    volatile uint32_t int_mask;
    volatile uint32_t int_enable;
    volatile uint32_t control2;
    volatile uint32_t cap1;
    volatile uint32_t cap2;
    volatile uint32_t res0[2];
    volatile uint32_t force_int;
    volatile uint32_t res1[7];
    volatile uint32_t boot_timeout;
    volatile uint32_t debug_config;
    volatile uint32_t res2[2];
    volatile uint32_t ext_fifo_config;
    volatile uint32_t ext_fifo_enable;
    volatile uint32_t tune_step;
    volatile uint32_t tune_SDR;
    volatile uint32_t tune_DDR;
    volatile uint32_t res3[23];
    volatile uint32_t spi_int_support;
    volatile uint32_t res4[2];
    volatile uint32_t slot_int_status;
} EMMCRegisters;


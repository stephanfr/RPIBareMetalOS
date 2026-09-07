// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_stdinclude.h"

#include "os_entity.h"

#include "devices/video/console_video_framebuffer.h"
#include "devices/uart_base.h"
#include "devices/gpio.h"
#include "devices/device_registrar.h"
#include "devices/rng_entity.h"

#include "rpi5_uart0.h"
#include "rpi5_uart1.h"
#include "rpi5_hw_rng.h"

/**
 * @brief Device registrar for RPi5.
 * 
 * Registers RPi5 UART0 and RPi5 UART1 (primary UARTs on RPi5).
 * Legacy PL011 UARTs are deprecated but available via wrapper classes.
 */

class RPi5DeviceRegistrar : public DeviceRegistrar
{
public:

    minstd::random_device *CreateHardwareRNG() override
    {
        //  Create and register the Hardware Random Number Generator (HWRNG) for RPi5

        auto rng = make_static_unique<RPi5HardwareRandomNumberGenerator>(GetPlatformInfo());

        if (!rng->Initialize())
        {
            return nullptr;         //  platform.cpp falls back to the SW RNG
        }

        hardware_rng_ = minstd::move(rng);

        return hardware_rng_.get();
    }

    void RegisterDevices(minstd::random_device *hw_rng) override
    {
        //  Create and register the Hardware Random Number Generator (HWRNG) for RPi5

        auto rng_entity = make_static_unique<RandomNumberGeneratorOSEntity<OSEntityTypes::HARDWARE_RNG>>(true, "hw_rng", "HWRNG", *hw_rng);
        GetOSEntityRegistry().AddEntity(rng_entity);

        // Register primary RPi5 UART0 (preferred on RPi5)

        auto rpi5_uart0 = make_static_unique<RPi5UART0>(BaudRates::BAUD_RATE_115200, "CONSOLE");
        GetOSEntityRegistry().AddEntity(rpi5_uart0);

        // Register primary RPi5 UART1
        auto rpi5_uart1 = make_static_unique<RPi5UART1>(BaudRates::BAUD_RATE_9600, "DEBUG");
        GetOSEntityRegistry().AddEntity(rpi5_uart1);

        // Register HDMI framebuffer console
        auto fb_console = make_static_unique<ConsoleVideoFrameBuffer>("HDMI", VideoFrameBuffer::PackColor(0x00, 0xFF, 0x00),
                                                                      VideoFrameBuffer::PackColor(0x00, 0x00, 0x00));
        if (fb_console->IsAllocated())
        {
            GetOSEntityRegistry().AddEntity(fb_console);
        }
    }
};
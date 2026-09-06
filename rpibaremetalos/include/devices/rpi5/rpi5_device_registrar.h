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

#include "rpi5_rp1_uart0.h"
#include "rpi5_rp1_uart1.h"
#include "rpi5_hw_rng.h"

/**
 * @brief Device registrar for RPi5.
 * 
 * Registers RP1 UART0 and RP1 UART1 (primary UARTs on RPi5).
 * Legacy PL011 UARTs are deprecated but available via wrapper classes.
 */

class RPi5DeviceRegistrar : public DeviceRegistrar
{
public:

    minstd::random_device *CreateHardwareRNG(const PlatformInfo &platform_info) override
    {
        //  Create and register the Hardware Random Number Generator (HWRNG) for RPi5

        hardware_rng_ = make_static_unique<RPi5HardwareRandomNumberGenerator>(platform_info);

        return hardware_rng_.get();
    }

    void RegisterDevices(const PlatformInfo &platform_info) override
    {
        //  Create and register the Hardware Random Number Generator (HWRNG) for RPi5

        auto rng_entity = make_static_unique<RandomNumberGeneratorOSEntity<OSEntityTypes::HARDWARE_RNG>>(true, "hw_rng", "HWRNG", *hardware_rng_);
        GetOSEntityRegistry().AddEntity(rng_entity);

        // Register primary RP1 UART0 (preferred on RPi5)

        auto rp1_uart0 = make_static_unique<RP1UART0>(BaudRates::BAUD_RATE_115200, "CONSOLE");
        GetOSEntityRegistry().AddEntity(rp1_uart0);

        // Register primary RP1 UART1
        auto rp1_uart1 = make_static_unique<RP1UART1>(BaudRates::BAUD_RATE_9600, "DEBUG");
        GetOSEntityRegistry().AddEntity(rp1_uart1);

        // Register HDMI framebuffer console
        auto fb_console = make_static_unique<ConsoleVideoFrameBuffer>("HDMI", VideoFrameBuffer::PackColor(0x00, 0xFF, 0x00),
                                                                      VideoFrameBuffer::PackColor(0x00, 0x00, 0x00));
        if (fb_console->IsAllocated())
        {
            GetOSEntityRegistry().AddEntity(fb_console);
        }
    }
};
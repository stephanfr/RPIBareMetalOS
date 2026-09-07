// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_stdinclude.h"

#include "os_entity.h"

#include "devices/device_registrar.h"
#include "devices/gpio.h"
#include "devices/uart_base.h"
#include "devices/video/console_video_framebuffer.h"

#include "rpi3_uart0.h"
#include "rpi3_uart1.h"
#include "rpi3_hw_rng.h"

#include "devices/rng_entity.h"


class RPi3DeviceRegistrar : public DeviceRegistrar
{
public:

    minstd::random_device *CreateHardwareRNG() override
    {
        //  Create and register the Hardware Random Number Generator (HWRNG) for RPi3

        hardware_rng_ = make_static_unique<RPi3HardwareRandomNumberGenerator>(GetPlatformInfo());

        return hardware_rng_.get();
    }


    void RegisterDevices(minstd::random_device *hw_rng) override
    {
        //  Register the Hardware Random Number Generator (HWRNG) for RPi3
        
        auto rng_entity = make_static_unique<RandomNumberGeneratorOSEntity<OSEntityTypes::HARDWARE_RNG>>(
                              true, "hw_rng", "HWRNG", *hw_rng);
        GetOSEntityRegistry().AddEntity(rng_entity);
        
        // Register UART0 (PL011 at 4MHz)

        auto uart0 = make_static_unique<RPi3UART0>(BaudRates::BAUD_RATE_115200, "CONSOLE", 4000000);
        uart0->Initialize();
        GetOSEntityRegistry().AddEntity(uart0);

        // Register UART1 (mini-UART with 25MHz crystal)

//        auto uart1 = make_static_unique<RPi3UART1>(BaudRates::BAUD_RATE_9600, "DEBUG", 25000000);
//        uart1->Initialize();
//        GetOSEntityRegistry().AddEntity(uart1);

        // Register HDMI framebuffer console

        auto fb_console = make_static_unique<ConsoleVideoFrameBuffer>( "HDMI",
                                                                       VideoFrameBuffer::PackColor(0x00, 0xFF, 0x00),
                                                                       VideoFrameBuffer::PackColor(0x00, 0x00, 0x00) );

        if (fb_console->IsAllocated())
        {
            GetOSEntityRegistry().AddEntity(fb_console);
        }
    }
};

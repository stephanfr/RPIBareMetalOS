#pragma once

#include "../device_registrar.h"
#include "rpi3_hw_rng.h"
#include "../../rpi3/rpi3_uart0.h"
#include "../../video/console_video_framebuffer.h"

class RPi3DeviceRegistrar : public DeviceRegistrar
{
public:
    void RegisterDevices() override
    {
        // Register serial console (UART0)
        auto uart0 = std::make_unique<UART0>(BaudRates::BAUD_RATE_115200, "CONSOLE");
        GetOSEntityRegistry().AddEntity(std::move(uart0));
        
        // Register framebuffer console
        auto fb_console = std::make_unique<ConsoleVideoFrameBuffer>(
            "HDMI",
            VideoFrameBuffer::PackColor(0x00, 0xFF, 0x00),
            VideoFrameBuffer::PackColor(0x00, 0x00, 0x00));
        if (fb_console->IsAllocated())
        {
            GetOSEntityRegistry().AddEntity(std::move(fb_console));
        }
        
        // Register Hardware RNG
        auto hw_rng = RPi3HWRNGOSEntity::Create(rpi3_hw_rng_instance);
        GetOSEntityRegistry().AddEntity(std::move(hw_rng));
    }
    
private:
    static RPi3HardwareRandomNumberGenerator rpi3_hw_rng_instance;
};

RPi3HardwareRandomNumberGenerator RPi3DeviceRegistrar::rpi3_hw_rng_instance{ /* MMIO_BASE */ };
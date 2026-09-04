// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/platform_info.h"

#include <random>

//  BCM2712's HW RNG register layout is NOT confirmed from source in this
//      plan (RPi3/RPi4's iproc_rng200-style layout is not guaranteed to
//      carry over -- BCM2712 already changed the mailbox, power manager, and
//      SD controller offsets relative to RPi3/RPi4, so assuming this one is
//      unchanged would be exactly the kind of invented value this plan's
//      guidance rules warn against). Initialize() always fails, so this
//      always falls through to the existing SW RNG fallback in
//      platform.cpp -- same behavior QEMU already exercises today. Replace
//      this with a real register-based implementation once BCM2712's RNG
//      binding/register map is confirmed from a primary source.

class RPi5HardwareRandomNumberGenerator : public minstd::random_device
{
public:
    RPi5HardwareRandomNumberGenerator() = delete;

    RPi5HardwareRandomNumberGenerator(const PlatformInfo &platform_info)
    {
        (void)platform_info;
    }

    ~RPi5HardwareRandomNumberGenerator() {}

    bool Initialize()
    {
        return false;
    }

    result_type operator()() override
    {
        //  Never reached -- Initialize() always fails, so platform.cpp never
        //      installs this as __hw_random_number_generator.

        return 0;
    }

    double entropy() const noexcept override { return 0.0; }
};


class RPi5HardwareRandomNumberGeneratorOSEntity : public RandomNumberGeneratorProxyOSEntity<HARDWARE_RNG>
{
public:
    RPi5HardwareRandomNumberGeneratorOSEntity( bool permanent,
                                               const char* name,
                                               const char* alias, 
                                               RPi5HardwareRandomNumberGenerator &wrapped_generator)
        : RandomNumberGeneratorProxyOSEntity<HARDWARE_RNG>(permanent, name, alias, wrapped_generator)
    {
    }
    
    static std::unique_ptr<RPi5HardwareRandomNumberGeneratorOSEntity> Create(RPi5HardwareRandomNumberGenerator &generator)
    {
        return std::make_unique<RPi5HardwareRandomNumberGeneratorOSEntity>(true, "hw_rng", "HWRNG", generator);
    }
};

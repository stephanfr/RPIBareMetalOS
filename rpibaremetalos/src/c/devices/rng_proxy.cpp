// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/rng_proxy.h"

#include "devices/rng_entity.h"
#include "heaps.h"
#include "os_entity.h"
#include "services/driver_manager.h"
#include "task/channels.h"
#include "task/task_impl.h"
#include "uapi/hw_rng.h"

namespace
{
    using minstd::chrono::milliseconds;

    //  The driver answers from a FIFO, or gives up on a word after about 10 ms, so a call that takes this long means it
    //      has hung.

    constexpr milliseconds CALL_TIMEOUT(100);

    bool hardware_rng_present = false;

    class DriverRandomNumberGenerator : public RandomNumberGeneratorOSEntity
    {
    public:
        DriverRandomNumberGenerator(const char *name, const char *alias)
            : RandomNumberGeneratorOSEntity(name, alias)
        {
        }

        //  The reply is untrusted: only its length is checked, as any 32-bit words are equally valid.

        bool Read(uint32_t *words, size_t count) override
        {
            while (count > 0)
            {
                const uint32_t wanted = (count < HW_RNG_MAX_WORDS) ? static_cast<uint32_t>(count) : HW_RNG_MAX_WORDS;
                size_t reply_length = 0;

                const int result = GetChannel(ChannelId::HW_RNG).Call(&wanted, sizeof(wanted), words, wanted * sizeof(uint32_t), reply_length, CALL_TIMEOUT);

                if (result == CHANNEL_TIMED_OUT)
                {
                    RestartDriver(DriverId::HW_RNG);
                }

                if ((result != CHANNEL_SUCCESS) || (reply_length != wanted * sizeof(uint32_t)))
                {
                    return false;
                }

                words += wanted;
                count -= wanted;
            }

            return true;
        }

        bool FromHardware() const override
        {
            return true;
        }
    };
}

void RegisterRandomNumberGenerator(bool hardware_present, minstd::random_device &local)
{
    hardware_rng_present = hardware_present;

    if (hardware_present)
    {
        auto entity = make_static_unique<DriverRandomNumberGenerator>("hw_rng", "HWRNG");
        GetOSEntityRegistry().AddEntity(entity);
        return;
    }

    auto entity = make_static_unique<LocalRandomNumberGenerator>("sw_rng", "HWRNG", local);
    GetOSEntityRegistry().AddEntity(entity);
}

bool HardwareRNGPresent()
{
    return hardware_rng_present;
}

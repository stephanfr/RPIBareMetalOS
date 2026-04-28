// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <array>

#include "os_entity.h"
#include "result.h"

typedef enum class EthernetResultCodes
{
    SUCCESS = 0,
    FAILURE,
    NOT_INITIALIZED,
    LINK_DOWN,
    NO_FRAME_AVAILABLE,
    FRAME_TOO_LARGE,
    TX_TIMEOUT,
    PHY_RESET_TIMEOUT,
    PHY_AUTONEG_TIMEOUT,
    DMA_INIT_FAILED,
    MDIO_TIMEOUT,
    MDIO_READ_FAILED,
} EthernetResultCodes;

inline bool Successful(EthernetResultCodes code)
{
    return code == EthernetResultCodes::SUCCESS;
}

inline bool Failed(EthernetResultCodes code)
{
    return code != EthernetResultCodes::SUCCESS;
}

static constexpr uint32_t ETHERNET_MAX_FRAME_SIZE = 1518;
static constexpr uint32_t ETHERNET_MIN_FRAME_SIZE = 64;

class EthernetDevice : public OSEntity
{
public:
    EthernetDevice() = delete;

    EthernetDevice(bool permanent,
                   const char *name,
                   const char *alias)
        : OSEntity(permanent, name, alias)
    {
    }

    virtual ~EthernetDevice() {}

    OSEntityTypes OSEntityType() const noexcept override
    {
        return OSEntityTypes::NETWORK_DEVICE;
    }

    virtual EthernetResultCodes Initialize() = 0;

    virtual bool IsLinkUp() = 0;

    virtual void GetMACAddress(minstd::array<uint8_t, 6> &mac) const = 0;

    virtual EthernetResultCodes SendFrame(const uint8_t *data, uint32_t length) = 0;

    virtual ValueResult<EthernetResultCodes, uint32_t> ReceiveFrame(uint8_t *buffer, uint32_t buffer_size) = 0;
};

EthernetDevice &GetEthernetDevice();

// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_entity.h"

#include <memory>
#include <string.h>

//  Provides a uniform interface for board-specific device registration.
//      Each platform implements RegisterDevices() to construct and register
//      its hardware entities (serial console, framebuffer, HW RNG, etc.).

class DeviceRegistrar
{
public:
    virtual ~DeviceRegistrar() = default;

    virtual void RegisterDevices(const PlatformInfo &platform_info) = 0;
};
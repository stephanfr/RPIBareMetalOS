// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "hypervisor_hvc.h"

uint8_t HypConsoleByte(const uint64_t *words, uint32_t index)
{
    return (uint8_t)(words[index / 8] >> (8 * (index % 8)));
}

static int64_t ConsoleWrite(const HypervisorFrame *frame, HypConsolePutc putc, void *context)
{
    uint64_t length = frame->x[1];

    if (length > HVC_CONSOLE_WRITE_MAX)
    {
        return HVC_INVALID_PARAMETER;
    }

    for (uint32_t i = 0; i < length; i++)
    {
        putc(context, (char)HypConsoleByte(&frame->x[2], i));
    }

    return HVC_SUCCESS;
}

void HypervisorHvcDispatch(HypervisorFrame *frame, HypConsolePutc putc, void *context)
{
    switch ((uint32_t)frame->x[0])
    {
    case HVC_FN_VERSION:
        frame->x[0] = HYPERVISOR_BUILD_VERSION;
        frame->x[1] = (HYPERVISOR_ABI_MAJOR << 16) | HYPERVISOR_ABI_MINOR;
        frame->x[2] = 0; //  feature bits, none yet
        break;

    case HVC_FN_CONSOLE_WRITE:
        frame->x[0] = (uint64_t)ConsoleWrite(frame, putc, context);
        break;

    default:
        frame->x[0] = (uint64_t)(int64_t)HVC_NOT_SUPPORTED;
        break;
    }
}

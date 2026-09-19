// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once


#define PSCI_VERSION            0x84000000u
#define PSCI_CPU_SUSPEND_64     0xC4000001u
#define PSCI_CPU_OFF            0x84000002u
#define PSCI_CPU_ON_64          0xC4000003u
#define PSCI_AFFINITY_INFO_64   0xC4000004u
#define PSCI_MIGRATE_INFO_TYPE  0x84000006u
#define PSCI_SYSTEM_OFF         0x84000008u
#define PSCI_SYSTEM_RESET       0x84000009u
#define PSCI_FEATURES           0x8400000Au

#define PSCI_VERSION_1_1        0x00010001

#define PSCI_RET_SUCCESS              0
#define PSCI_RET_NOT_SUPPORTED      (-1)
#define PSCI_RET_INVALID_PARAMETERS (-2)
#define PSCI_RET_DENIED             (-3)
#define PSCI_RET_ALREADY_ON         (-4)
#define PSCI_RET_ON_PENDING         (-5)
#define PSCI_RET_INTERNAL_FAILURE   (-6)
#define PSCI_RET_NOT_PRESENT        (-7)
#define PSCI_RET_DISABLED           (-8)
#define PSCI_RET_INVALID_ADDRESS    (-9)

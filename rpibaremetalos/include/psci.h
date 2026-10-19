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

//  Supervisor-specific calls, in the SMCCC SiP (silicon provider) range.  Any other EL3 - the
//      RPi firmware's TF-A, an older supervisor - answers them NOT_SUPPORTED, so a successful
//      SUPERVISOR_SIP_VERSION proves which build owns EL3.  Bump SUPERVISOR_VERSION whenever the
//      supervisor's behaviour changes.

#define SUPERVISOR_SIP_VERSION  0xC2000000u     //  returns SUPERVISOR_VERSION
#define SUPERVISOR_SIP_PM_BASE  0xC2000001u     //  returns the PM block base used for OFF/RESET, 0 if none

#define SUPERVISOR_VERSION      2

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

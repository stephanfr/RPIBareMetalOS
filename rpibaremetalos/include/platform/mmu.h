// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#define MEMORY_ATTRIBUTE_DEVICE_NO_GATHER_NO_REORDER_NO_EARLY_WRITE_ACK	    0
#define MEMORY_ATTRIBUTE_DEVICE_NO_GATHER_NO_REORDER_EARLY_WRITE_ACK		1
#define MEMORY_ATTRIBUTE_DEVICE_GATHER_REORDER_EARLY_WRITE_ACK		        2
#define MEMORY_ATTRIBUTE_NORMAL_NO_CACHING		                            3
#define MEMORY_ATTRIBUTE_NORMAL		                                        4


/*-[ MMU_setup_pagetable ]--------------------------------------------------}
.  Sets up a default TLB table. This needs to be called by only once by one 
.  core on a multicore system. Each core can use the same default table.
.--------------------------------------------------------------------------*/
void MMU_setup_pagetable (void);

/*-[ MMU_enable ]-----------------------------------------------------------}
.  Enables the MMU system to the previously created TLB tables. This needs 
.  to be called by each individual core on a multicore system.
.--------------------------------------------------------------------------*/
extern "C" void MMU_enable(void);


uint64_t  virtualmap (uint32_t phys_addr, uint8_t memattrs);



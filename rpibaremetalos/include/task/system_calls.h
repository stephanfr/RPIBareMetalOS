// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#define SYS_WRITE_NUMBER    0 
#define SYS_MALLOC_NUMBER   1 	
#define SYS_CLONE_NUMBER    2 	
#define SYS_EXIT_NUMBER     3 	

#define NUMBER_OF_SYSTEM_CALLS 4

#define SYS_CLONE_FAILURE   -1
#define SYS_CLONE_SUCCESS   1
#define SYS_CLONE_NEW_TASK  0x0ccc

#ifndef __ASSEMBLER__

#include "services/uuid.h"
#include "task/task_errors.h"

extern "C" int sc_CloneTask(const char* name, unsigned long fn, unsigned long arg, unsigned long stack, task::TaskResultCodes &result_code, UUID &result);
extern "C" unsigned long sc_Malloc();
extern "C" void sc_Exit();

extern "C" void sc_Write(char * buf);

#endif

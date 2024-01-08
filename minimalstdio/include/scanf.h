// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "minimalstdio_constants.h"

#ifdef __cplusplus
# include <cstdarg>
# include <cstddef>
extern "C" {
#else
# include <stdarg.h>
# include <stddef.h>
#endif

int __sscanf(const char *buff, const char *format, ...);

#ifdef __cplusplus
} // extern "C"
#endif


#define sscanf __sscanf

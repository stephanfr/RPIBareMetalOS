// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

extern "C" void __assert (const char *msg, const char *file, int line) noexcept;

#define MINIMAL_STD_NAMESPACE minstd

#define MINIMAL_STD_ASSERT( expression ) { if(!(expression)) __assert( #expression, __FILE__, __LINE__ ); }

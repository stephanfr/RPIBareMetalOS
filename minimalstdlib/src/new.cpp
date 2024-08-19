// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stddef.h>

#include "new"

//
//  Placement new operator
//

void *operator new(size_t size, void *ptr)
{
    return ptr;
}

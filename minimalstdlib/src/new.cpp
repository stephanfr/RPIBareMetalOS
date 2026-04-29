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

#if !defined(__MINIMAL_STD_TEST__) && !defined(__linux__)
void* operator new(size_t size, std::align_val_t al) { return operator new(size); }
void operator delete(void* ptr, std::align_val_t al) { operator delete(ptr); }
void operator delete(void* ptr, size_t size, std::align_val_t al) { operator delete(ptr); }
void operator delete[](void* ptr, std::align_val_t al) { operator delete(ptr); }
void operator delete[](void* ptr, size_t size, std::align_val_t al) { operator delete(ptr); }
void* operator new(size_t size) { return nullptr; }
void operator delete(void* ptr) {}
void* operator new[](size_t size) { return nullptr; }
void operator delete[](void* ptr) {}
void operator delete(void* ptr, size_t size) {}
void operator delete[](void* ptr, size_t size) {}
#endif

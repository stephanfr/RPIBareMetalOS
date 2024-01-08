// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//------------------------------------------------------------------------------------------
//  Stubs for C++ runtime
//------------------------------------------------------------------------------------------

void operator delete(void *, unsigned long)
{
}

void *__dso_handle = 0;

extern "C" int __cxa_atexit(void (*f)(void *), void *objptr, void *dso)
{
    return 0;
}

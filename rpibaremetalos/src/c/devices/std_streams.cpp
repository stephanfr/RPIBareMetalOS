// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/std_streams.h"

#include <stddef.h>

#include "platform/hypervisor.h"

CharacterIODevice *stdout = nullptr;
CharacterIODevice *stdin = nullptr;


void SetStandardStreams( CharacterIODevice* new_stdout, CharacterIODevice* new_stdin )
{
    stdout = new_stdout;
    stdin = new_stdin;
}
//
//  putchar_ is required for the minimalstdio implementation of 'printf' to output characters.  Until the console is
//      set up, or if it never is, output goes through EL2's console instead, a character at a time: early boot and the
//      reason a console fails are not lost.
//

extern "C" void putchar_(char c)
{
    if (stdout != NULL)
    {
        stdout->putc(c);
        return;
    }

    Hypervisor::ConsoleWrite(&c, 1);
}


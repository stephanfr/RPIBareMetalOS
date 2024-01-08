// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/std_streams.h"

#include <stddef.h>

CharacterIODevice *stdout = nullptr;
CharacterIODevice *stdin = nullptr;


void SetStandardStreams( CharacterIODevice* new_stdout, CharacterIODevice* new_stdin )
{
    stdout = new_stdout;
    stdin = new_stdin;
}

//
//  putchar_ is required for the minimalstdio implementation of 'printf' to output characters.
//

extern "C" void putchar_(char c)
{
    if (stdout != NULL)
    {
        stdout->putc(c);
    }
}


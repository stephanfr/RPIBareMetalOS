// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/std_streams.h"

#include <stddef.h>

CharacterIODevice *stdout = nullptr;
CharacterIODevice *stdin = nullptr;
CharacterIODevice *secondary_stdout = nullptr;


void SetStandardStreams( CharacterIODevice* new_stdout, CharacterIODevice* new_stdin )
{
    stdout = new_stdout;
    stdin = new_stdin;
}

void SetSecondaryStdout( CharacterIODevice* device )
{
    secondary_stdout = device;
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

    if (secondary_stdout != NULL)
    {
        secondary_stdout->putc(c);
    }
}


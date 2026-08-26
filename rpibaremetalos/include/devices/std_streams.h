// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "character_io.h"

extern CharacterIODevice *stdout;
extern CharacterIODevice *stdin;

void SetStandardStreams( CharacterIODevice* new_stdout, CharacterIODevice* new_stdin );

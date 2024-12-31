<!--
 Copyright 2024 Stephan Friedl. All rights reserved.
 Use of this source code is governed by a BSD-style
 license that can be found in the LICENSE file.
-->

This library is intended to capture a 'minimal' implementation of the C++ standard library
with optimizations for embedded, low-dynamic memory allocation applications.  It was developed
in tandem with an AArch64 OS for the RPi and in general, classes have been written and supporting
capabilities like concepts have been pulled in as required.  Initially this was a modest amount
of code but as the OS grew in complexity, the number of supporting classes and capabilities in 
the standard libary grew as well.

Boilerplate standard code is mostly drawn from the LLVM standard c++ library.  Earlier versions of
this library had hand-written type traits, etc. but the size and increasingly coupled nature of the
standard library has caused me to pull in more external 'supporting' code over time.  Most of the
main container classes and template classes I have hand-coded.

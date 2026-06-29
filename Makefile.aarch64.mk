# Copyright 2023 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

# Resolve the toolchain file relative to this file's own directory so that
# includes work correctly regardless of the invoking sub-project's CWD.
include $(dir $(lastword $(MAKEFILE_LIST)))Makefile.toolchain.aarch64.mk

# Copyright 2024 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

TEST_TARGETS := test test-coverage

ifeq (,$(filter $(TEST_TARGETS), $(MAKECMDGOALS)))
include Makefile.aarch64.mk
else
include Makefile.test.mk
endif

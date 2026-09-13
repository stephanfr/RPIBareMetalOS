# Copyright 2024 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

# Target: Check for unused includes in header files
# This target runs the unused-includes tool against all header files
# in the rpibaremetalos/include/ directory.
#
# Usage: make check-unused-includes
#       make check-unused-includes-fix (automatically fixes detected issues)

.PHONY: check-unused-includes check-unused-includes-fix

PYTHON ?= python3

check-unused-includes:
	$(PYTHON) -m pipx.run unused-includes --include-all rpibaremetalos/include/

check-unused-includes-fix:
	@echo "Fixing unused includes..."
	$(PYTHON) -m pipx.run unused-includes --fix rpibaremetalos/include/
	@echo "Done!"

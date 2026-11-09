# Copyright 2026 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

"""QEMU flags that make an emulated board match the real one.  Keep in step with the
QEMU_RPI*_CPU_FLAGS in Makefile.aarch64.mk."""

# The system counter runs at the board's crystal frequency, which is what the kernel writes to
# CNTFRQ_EL0.  QEMU's ARM CPUs count at 62.5 MHz unless told otherwise, and then every duration the
# kernel measures runs 3.26x long on raspi3b and 1.16x long on raspi4b.
_CPU_FLAGS = {
    'raspi3b': '-global cortex-a53-arm-cpu.cntfrq=19200000',
    'raspi4b': '-global cortex-a72-arm-cpu.cntfrq=54000000',
}


def cpu_flags(machine: str) -> str:
    """The flags for machine, with a leading space, or '' for a machine not listed."""
    flags = _CPU_FLAGS.get(machine, '')
    return f' {flags}' if flags else ''

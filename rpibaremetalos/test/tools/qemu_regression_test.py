#!/usr/bin/env python3
# Copyright 2024 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be found
# in the LICENSE file.
#
# QEMU regression test for RPIBareMetalOS.
# Launches the OS under qemu-system-aarch64, exercises the CLI and asserts
# expected output for each command, then halts cleanly.
#
# Runs the suite four times for the requested machine -- both memory models
# (kernel_only_1_to_1 and kernel_high_user_low) each paired with the default
# (relaxed) alignment policy and strict_align=1 -- so regressions in any
# combination are caught. Board selection in the OS is runtime (MIDR_EL1
# PARTNUM), so the same kernel8.elf and sd.img serve every machine.
#
# Usage:
#   python3 qemu_regression_test.py --qemu <qemu-binary> \
#                                   --kernel <kernel8.elf> \
#                                   --sdimage <sd.img> \
#                                   [--machine raspi3b] [--memory 2G]

import argparse
import sys
import pexpect


PROMPT = '> '
BOOT_READY_MARKER = 'Command Line Interface'
TIMEOUT = 60  # seconds to wait for each response


def run(qemu: str, kernel: str, sdimage: str,
        machine: str = 'raspi3b',
        memory: str = '',
        memory_model: str = 'kernel_only_1_to_1',
        extra_cmdline: str = '') -> int:
    cmd = (
        f'{qemu} -M {machine}'
        f'{f" -m {memory}" if memory else ""}'
        f' -kernel {kernel}'
        f' -drive file={sdimage},if=sd,format=raw'
        f' -serial stdio'
        f' -display none'
        f' -no-reboot'
        f' -append "console=ttys0,57600 memory_model={memory_model}{extra_cmdline}"'
    )

    print(f'Launching: {cmd}')
    child = pexpect.spawn(cmd, encoding='utf-8', timeout=TIMEOUT)
    child.logfile = sys.stdout

    failures = 0

    def check(label: str, output: str, *expected: str) -> None:
        nonlocal failures
        for text in expected:
            if text not in output:
                print(f'\nFAIL [{label}]: expected "{text}" not found in output')
                failures += 1
            else:
                print(f'\nPASS [{label}]: found "{text}"')

    def send_command(command: str, timeout: int = TIMEOUT) -> str:
        child.sendline(command)
        child.expect(PROMPT, timeout=timeout)
        return child.before

    try:
        # Wait for the OS to boot and reach the CLI prompt
        child.expect(BOOT_READY_MARKER, timeout=TIMEOUT)
        child.expect(PROMPT, timeout=TIMEOUT)  # consume the initial prompt

        # list filesystems
        output = send_command('list filesystems')
        check('list filesystems', output, 'Filesystem:')

        # list tasks
        output = send_command('list tasks')
        check('list tasks', output, 'Tasks:', 'Kernel Main Task', 'Idle Task', 'CLI')

        # show diagnostics
        output = send_command('show diagnostics')
        check('show diagnostics', output, 'Board Info:', 'RPI Version:')

        # test memory — allocator correctness + the "did we actually unlock
        # the real amount of RAM" regression guard
        output = send_command('test memory')
        check('test memory', output, 'PASS: memory allocator correctness test')

        # test memorysoak — short duration here; this is a regression smoke
        # check that the churn path itself works, not a real soak run. Extra
        # pexpect timeout headroom for QEMU being slower than real hardware,
        # on top of the 10s of in-OS wall-clock time the test itself requests.
        output = send_command('test memorysoak --seconds=10', timeout=90)
        check('test memorysoak', output, 'PASS: memory soak test')

        # halt
        child.sendline('halt')
        child.expect('Halting', timeout=TIMEOUT)

    except pexpect.TIMEOUT:
        print('\nFAIL: timed out waiting for expected output')
        failures += 1
    except pexpect.EOF:
        print('\nFAIL: QEMU exited unexpectedly')
        failures += 1
    finally:
        child.terminate(force=True)

    print(f'\n{"PASSED" if failures == 0 else "FAILED"} — {failures} failure(s)')
    return 0 if failures == 0 else 1


def main() -> int:
    parser = argparse.ArgumentParser(description='RPIBareMetalOS QEMU regression test')
    parser.add_argument('--qemu',    required=True, help='Path to qemu-system-aarch64')
    parser.add_argument('--kernel',  required=True, help='Path to kernel8.elf')
    parser.add_argument('--sdimage', required=True, help='Path to sd.img')
    parser.add_argument('--machine', default='raspi3b',
                        help='QEMU machine model (default: raspi3b)')
    parser.add_argument('--memory',  default='',
                        help='RAM size passed to QEMU -m (default: machine default)')
    args = parser.parse_args()

    print(f'=== {args.machine} Pass 1: kernel_only_1_to_1, default alignment ===')
    result = run(args.qemu, args.kernel, args.sdimage,
                 machine=args.machine, memory=args.memory,
                 memory_model='kernel_only_1_to_1')
    if result != 0:
        return result

    print(f'\n=== {args.machine} Pass 2: kernel_only_1_to_1, strict_align=1 ===')
    result = run(args.qemu, args.kernel, args.sdimage,
                 machine=args.machine, memory=args.memory,
                 memory_model='kernel_only_1_to_1',
                 extra_cmdline=' strict_align=1')
    if result != 0:
        return result

    print(f'\n=== {args.machine} Pass 3: kernel_high_user_low, default alignment ===')
    result = run(args.qemu, args.kernel, args.sdimage,
                 machine=args.machine, memory=args.memory,
                 memory_model='kernel_high_user_low')
    if result != 0:
        return result

    print(f'\n=== {args.machine} Pass 4: kernel_high_user_low, strict_align=1 ===')
    return run(args.qemu, args.kernel, args.sdimage,
               machine=args.machine, memory=args.memory,
               memory_model='kernel_high_user_low',
               extra_cmdline=' strict_align=1')


if __name__ == '__main__':
    sys.exit(main())
    
#!/usr/bin/env python3
# Copyright 2024 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be found
# in the LICENSE file.
#
# QEMU regression test for RPIBareMetalOS.
# Launches the OS under qemu-system-aarch64, exercises the CLI and asserts
# expected output for each command, then halts cleanly.
#
# Runs the suite twice for the requested machine -- the default (relaxed)
# alignment policy and strict_align=1 -- so regressions in either are caught.
# The kernel has a single memory model, so the command line does not name
# one. Board selection in the OS is runtime (MIDR_EL1
# PARTNUM), so the same kernel8.img and sd.img serve every machine.
#
# The armstub is the -kernel image and the kernel is loaded beside it: an ELF keeps QEMU's
# is_linux flag clear, which is what starts the cores at EL3 (hw/arm/boot.c:1238-1249).
#
# Usage:
#   python3 qemu_regression_test.py --qemu <qemu-binary> \
#                                   --armstub <supervisor.elf> \
#                                   --kernel <kernel8.img> \
#                                   --sdimage <sd.img> \
#                                   [--machine raspi3b] [--memory 2G]

import argparse
import sys
import re
import pexpect


PROMPT = '> '
BOOT_READY_MARKER = 'Command Line Interface'
TIMEOUT = 60  # seconds to wait for each response


def is_interleaving_of_two(text: str, expected: str) -> bool:
    """True if text can be split into two copies of expected, characters in order.

    Two tasks writing to the console at once have no lock between them, so their lines come
    out shuffled together character by character.  Each task's own characters stay in order,
    which is exactly what this checks (a small DP over how much of each copy has been seen).
    Anything else -- a different message such as ALIASED, one task missing, extra output -- is
    not a shuffle of two copies and returns False.
    """
    n = len(expected)

    if len(text) != 2 * n:
        return False

    states = {(0, 0)}

    for ch in text:
        states = ({(i + 1, j) for i, j in states if i < n and expected[i] == ch} |
                  {(i, j + 1) for i, j in states if j < n and expected[j] == ch})

        if not states:
            return False

    return (n, n) in states


def case4_result_ok(output: str) -> bool:
    """Both case 4 tasks reported 'isolated', whether or not their lines interleaved."""
    match = re.search(r'-- case 4 \(heap isolation, x2\)(.*?)(?:PASS|FAIL): user task test',
                      output, re.S)

    if match is None:
        return False

    # Console output is CR/LF-terminated and the two lines can split anywhere, so compare with
    # all whitespace removed.
    return is_interleaving_of_two(re.sub(r'\s+', '', match.group(1)), 'case4:isolated')


def run(qemu: str, armstub: str, kernel: str, sdimage: str,
        machine: str = 'raspi3b',
        memory: str = '',
        extra_cmdline: str = '') -> int:
    cmd = (
        f'{qemu} -M {machine}'
        f'{f" -m {memory}" if memory else ""}'
        f' -kernel {armstub}'
        f' -device loader,file={kernel},addr=0x80000,force-raw=on'
        f' -drive file={sdimage},if=sd,format=raw'
        f' -serial stdio'
        f' -display none'
        f' -no-reboot'
        f' -append "console=ttys0,57600 host=qemu{extra_cmdline}"'
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

        # test usertask — verifies user-space task execution, fault handling,
        # and heap isolation across different argument scenarios. Exercises the
        # complete user-task lifecycle from EL1 fork to EL0 execution and back.
        output = send_command('test usertask', timeout=240)
        check('test usertask', output, 'PASS: user task test')

        # "hello from EL0" also appears inside case 0's own "[expect: ... 'hello from EL0']"
        # label, so a plain substring test passes even when case 0 never ran.  Require it as
        # a whole line, which only the task itself produces.
        if re.search(r'^hello from EL0\s*$', output, re.M):
            print('\nPASS [test usertask]: found line "hello from EL0"')
        else:
            print('\nFAIL [test usertask]: line "hello from EL0" not found in output')
            failures += 1

        # Cases 1-3 are SUPPOSED to be killed, so the real pass condition is the ABSENCE of
        # the "NOT KILLED" line hello.c prints when an expected fault did not happen. The
        # kernel counts forks, not arrivals, which is what let this command report PASS on a
        # run where a case-4 task never reached EL0.

        # Case 4 runs two tasks at once and the console has no lock, so their two lines can
        # come out interleaved character by character.  Accept any such shuffle of exactly
        # two "case 4: isolated" lines; ALIASED, MALLOC FAILED or a missing task are not.

        if not case4_result_ok(output):
            print('\nFAIL [test usertask]: case 4 did not report "isolated" from both tasks')
            failures += 1

        # The demand-paging cases must report OK in every pass.
        # Cases 8 and 9 (wild heap pointer, guard gap) are caught by the 'NOT KILLED' scan below.
        for good in ('case 7: demand paging OK',
                     'case 10: deep stack OK',
                     'case 11: kernel first touch OK'):
            if good not in output:
                print(f'\nFAIL [test usertask]: missing "{good}"')
                failures += 1

        # Seven cases are SUPPOSED to be killed (args 1, 2, 3, 4, 6, 8, 9).  Each announces
        # itself just before the faulting access and prints "NOT KILLED" if it survives, so
        # require every announcement exactly once; the 'NOT KILLED' scan below then proves each
        # one died.  (The kernel's own "task killed" message is a warning and the OS runs at
        # log level ERROR, so it can't be counted here.)  A positive case dying early is caught
        # by the "OK" lines above, 'hello from EL0', and the case 4 check.
        for announced in ('case 1: reading the kernel linear map',
                          'case 1b: reading 0x80000',
                          'case 2: writing to user text',
                          'case 3: executing from the stack',
                          'case 5: invalid syscall',
                          'case 8: writing above the heap break',
                          'case 9: writing into the guard gap'):
            if output.count(announced) != 1:
                print(f'\nFAIL [test usertask]: expected exactly one "{announced}"')
                failures += 1

        for bad in ('Failed to move task to user space', 'NOT KILLED', 'FAILED'):
            if bad in output:
                print(f'\nFAIL [test usertask]: unexpected "{bad}" in output')
                failures += 1

        # test addrspace — checks the address space / page table layer, including
        # that the syscall boundary range-checks before it walks.
        output = send_command('test addrspace')
        check('test addrspace', output, 'PASS: address space test')

    except pexpect.TIMEOUT:
        print('\nFAIL: timed out waiting for expected output')
        failures += 1
    except pexpect.EOF:
        print('\nFAIL: QEMU exited unexpectedly')
        failures += 1
    finally:
        child.terminate(force=True)

    try:
        # halt
        child.sendline('halt')
        child.expect(pexpect.EOF)
    except pexpect.TIMEOUT:
        print('\nFAIL: timed out waiting for halt')
        failures += 1
    finally:
        child.terminate(force=True)

    print(f'\n{"PASSED" if failures == 0 else "FAILED"} — {failures} failure(s)')
    return 0 if failures == 0 else 1


def main() -> int:
    parser = argparse.ArgumentParser(description='RPIBareMetalOS QEMU regression test')
    parser.add_argument('--qemu',    required=True, help='Path to qemu-system-aarch64')
    parser.add_argument('--armstub', required=True, help='Path to armstub_minimal.elf')
    parser.add_argument('--kernel',  required=True, help='Path to kernel8.img')
    parser.add_argument('--sdimage', required=True, help='Path to sd.img')
    parser.add_argument('--machine', default='raspi3b',
                        help='QEMU machine model (default: raspi3b)')
    parser.add_argument('--memory',  default='',
                        help='RAM size passed to QEMU -m (default: machine default)')
    args = parser.parse_args()

    print(f'=== {args.machine} Pass 1: default alignment ===')
    result = run(args.qemu, args.armstub, args.kernel, args.sdimage,
                 machine=args.machine, memory=args.memory)
    if result != 0:
        return result

    print(f'\n=== {args.machine} Pass 2: strict_align=1 ===')
    return run(args.qemu, args.armstub, args.kernel, args.sdimage,
               machine=args.machine, memory=args.memory,
               extra_cmdline=' strict_align=1')

if __name__ == '__main__':
    sys.exit(main())
    
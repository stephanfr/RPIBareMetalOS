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
# alignment policy with stage-2's device allow-list enforced, then
# strict_align=1 with the allow-list in audit mode -- so regressions in either
# are caught.
# The kernel has a single memory model, so the command line does not name
# one. Board selection in the OS is runtime (MIDR_EL1
# PARTNUM), so the same kernel8.img and sd.img serve every machine.
#
# The armstub is the -kernel image and the kernel is loaded beside it: QEMU boots an ELF
# -kernel as a bare-metal image, which is what starts the cores at EL3 (hw/arm/boot.c:1238-1249).
#
# Usage:
#   python3 qemu_regression_test.py --qemu <qemu-binary> \
#                                   --armstub <supervisor.elf> \
#                                   --kernel <kernel8.img> \
#                                   --sdimage <sd.img> \
#                                   [--machine raspi3b] [--memory 2G]

import argparse
import os
import sys
import re
import pexpect

from qemu_machines import cpu_flags


PROMPT = '> '


def hypervisor_abi() -> str:
    """'major.minor' from include/hypervisor_config.h, as the hypervisor reports it."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'include', 'hypervisor_config.h')

    with open(path) as config:
        text = config.read()

    major = re.search(r'#define HYPERVISOR_ABI_MAJOR (\d+)', text).group(1)
    minor = re.search(r'#define HYPERVISOR_ABI_MINOR (\d+)', text).group(1)

    return f'{major}.{minor}'


ABI = hypervisor_abi()
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
    match = re.search(r'-- case 4 \(heap isolation, x2\)(.*?)(?:-- case |(?:PASS|FAIL): user task test)',
                      output, re.S)

    if match is None:
        return False

    # Console output is CR/LF-terminated and the two lines can split anywhere, so compare with
    # all whitespace removed.
    return is_interleaving_of_two(re.sub(r'\s+', '', match.group(1)), 'case4:isolated')


def run(qemu: str, armstub: str, kernel: str, hypervisor: str, sdimage: str,
        machine: str = 'raspi3b',
        memory: str = '',
        extra_cmdline: str = '') -> int:
    audit = 'stage2=audit' in extra_cmdline
    cmd = (
        f'{qemu} -M {machine}'
        f'{cpu_flags(machine)}'
        f'{f" -m {memory}" if memory else ""}'
        f' -kernel {armstub}'
        f' -device loader,file={kernel},addr=0x80000,force-raw=on'
        f' -device loader,file={hypervisor},addr=0x40000,force-raw=on'
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
        boot_output = child.before
        child.expect(PROMPT, timeout=TIMEOUT)  # consume the initial prompt

        # The hypervisor's banner is the first output, ahead of the kernel's
        hyp_banner = boot_output.find('HYP: hypervisor v0.')
        kernel_banner = boot_output.find('SEF RPI Bare Metal OS')
        if hyp_banner < 0 or kernel_banner < 0 or hyp_banner > kernel_banner:
            print('\nFAIL [boot]: expected "HYP: hypervisor v0." before "SEF RPI Bare Metal OS"')
            failures += 1
        else:
            print('\nPASS [boot]: hypervisor banner precedes the kernel banner')

        # The kernel's first calls into EL2: the version query and a console write through hvc
        check('boot', boot_output, 'Hypervisor v0.', 'HYP console: hello from EL1', 'Stage-2 protection enabled',
              'HYP S2: VideoCore memory 0x',
              'Stage-2 lockdown done', *(['Stage-2 AUDIT mode'] if audit else []))

        # list filesystems
        output = send_command('list filesystems')
        check('list filesystems', output, 'Filesystem:')

        # list tasks
        output = send_command('list tasks')
        check('list tasks', output, 'Tasks:', 'Kernel Main Task', 'Idle Task', 'CLI')

        # show diagnostics
        output = send_command('show diagnostics')
        check('show diagnostics', output, 'Board Info:', 'RPI Version:')

        # hypervisor version / test hypervisor — the HVC ABI from EL1.  The 100-byte line goes through EL2 in three
        # chunks, so finding it whole shows they arrived intact and in order; nothing may print for the rejected
        # 49-byte write or the empty one.
        output = send_command('hypervisor version')
        check('hypervisor version', output, 'Hypervisor v0.', '(ABI 1.')

        output = send_command('test hypervisor')
        long_line = 'HYP console 100: ' + ''.join(str(i % 10) for i in range(82))
        check('test hypervisor', output, 'PASS: hypervisor test', long_line, 'mailbox call through EL2: ')
        if 'FAIL!!!' in output:
            print('\nFAIL [test hypervisor]: a rejected or empty console write printed')
            failures += 1

        # test stage2 -- the hypervisor's memory faults from EL1 and from EL0, and EL2 reports each fault.  Run again
        # after the live update, which must keep stage-2 on.
        def check_stage2(label: str) -> None:
            nonlocal failures
            output = send_command('test stage2')
            check(label, output, 'HYP S2 AUDIT core=' if audit else 'reason=blocked',
                  'stage-2 allow-list in AUDIT mode' if audit else 'stage-2 allow-list enforced')
            check(label, output, 'PASS: stage2 test', 'reason=firmware', 'reason=videocore', 'framebuffer: 2 buffer(s)', 'reason=hypervisor', 'reason=pool',
                  'HYP S2 FAULT core=', ' el=0 write ipa=0x7F000', 'reason=mediated refused',
                  'reason=mediated device-fault', 'reason=mediated not-emulable', 'case 17: mediated echo OK', 'mediated access: ',
                  'SD read through EL2: ',
                  'ipa=0x' + ('3F00B898' if machine == 'raspi3b' else 'FE00B898') + ' ')
            if 'case 16: NOT KILLED' in output:
                print(f'\nFAIL [{label}]: a user task wrote to a page stage-2 blocks')
                failures += 1
            if 'HYP SDHCI' in output:
                print(f'\nFAIL [{label}]: the SD driver asked for DMA')
                failures += 1

        check_stage2('test stage2')

        # test drivers -- a service process opens the test timer, has its interrupts forwarded, and gives it back on
        # exit; another serves the test-echo channel for the kernel's calls, and exits holding one.  Second owners and
        # processes without the capability are refused.  Then the driver manager runs the embedded test-echo driver,
        # restarts it after two crashes and a kill when it hangs, and kills it to stop it.  Last, the HWRNG entity reads
        # through the hw-rng driver, which is killed and restarted.  QEMU's raspi4b has no RNG200, so there the kernel's
        # software generator serves and no hw-rng driver runs.  Then the console driver is killed: what the CLI writes
        # while it restarts goes out through EL2, and every command after this one is typed through its successor.  Last,
        # the sd-card driver is killed during a 1 MB read, which fails; the next read waits for its successor, and every
        # file read after this goes through it.
        output = send_command('test drivers', timeout=120)
        check('test drivers', output, 'PASS: drivers test', 'case 18: device calls OK', 'case 18: device refused OK',
              'channel calls OK', 'case 19: channel refused OK', 'driver manager OK',
              'console: written before its driver is killed', 'console: written while its driver restarts',
              'console: written after its driver restarted', 'console OK',
              'sd-card: a read failed when its driver was killed', 'sd-card OK',
              *(['case 18: device interrupts OK', 'hw-rng OK'] if machine == 'raspi3b'
                else ['hw-rng: no hardware RNG']))

        # hypervisor update — live update.  Failures first, each leaving v0.1 running.  Then the v0.2 build on the SD image
        # replaces it on every core without a reboot, and every test after this runs on the updated hypervisor.
        output = send_command('hypervisor update /nonexistent.bin')
        check('hypervisor update, missing file', output, 'Hypervisor update failed: file not found')
        output = send_command('hypervisor update /hello.bin')
        check('hypervisor update, not a hypervisor', output, 'Hypervisor update failed: image rejected')
        output = send_command('hypervisor version')
        check('hypervisor version after failed updates', output, f'Hypervisor v0.1 (ABI {ABI})')

        output = send_command('hypervisor update /hypervisor_v0_2.bin')
        check('hypervisor update', output, 'HYP: live update from v0.1 to v0.2', f'Hypervisor updated: now v0.2 (ABI {ABI})')
        output = send_command('hypervisor version')
        check('hypervisor version after update', output, f'Hypervisor v0.2 (ABI {ABI})')
        output = send_command('test hypervisor')
        check('test hypervisor after update', output, 'PASS: hypervisor test', long_line)
        check_stage2('test stage2 after update')

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

        # Case 12: statistics a user program defines are registered at load, read by the kernel
        # through its own mapping, written back to the process, and removed and freed once the
        # task is reaped.  The process prints TIMED OUT if it never sees the kernel's write.
        check('test usertask', output,
              'case 12: statistics set',
              'user statistics read through the kernel mapping',
              'case 12: kernel write seen',
              'user statistics removed and freed after the task was reaped')

        # Case 13: a second process, run while case 12 waits, uses the statistics system calls.
        # It reads its own statistics, finds both processes by binary path, cannot see the other
        # process's private statistics, can read its public one, and has kernel pointers refused.
        # Any failure prints "case 13: FAIL".
        check('test usertask', output,
              'case 13: own statistic readable',
              'case 13: processes found by binary',
              'case 13: other process private statistics hidden',
              'case 13: other process public statistic readable',
              'case 13: bad arguments refused')

        if 'case 13: FAIL' in output:
            print('\nFAIL [test usertask]: case 13 reported a failure')
            failures += 1

        # Case 14: an ordinary process, not a service.  Its statistics are writable private data,
        # and none of them is registered, so the system calls do not find them.
        check('test usertask', output, 'case 14: statistics private to an ordinary process')

        if 'case 14: FAIL' in output:
            print('\nFAIL [test usertask]: case 14 reported a failure')
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
        # log level ERROR, so it is counted by the tasks.user_faults statistic instead, below.)
        # A positive case dying early is caught by the "OK" lines above, 'hello from EL0', and
        # the case 4 check.
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

        # test sleep — kernel and user tasks block until a deadline instead of spinning: no sleep
        # ends early or more than the scheduler-tick bound late, and a sleeping task is not scheduled.
        output = send_command('test sleep', timeout=60)
        check('test sleep', output, 'PASS: sleep test', 'case 15: sleep OK')

        # test wait / test mutex — the wait queue and the blocking mutex across every core: each
        # posted token taken exactly once, mutual exclusion, and a waiter that a re-locking hog
        # cannot starve.
        output = send_command('test wait', timeout=60)
        check('test wait', output, 'PASS: wait test')

        output = send_command('test mutex', timeout=60)
        check('test mutex', output, 'PASS: mutex test')

        # list statistics — the kernel counts every user task a fault killed: the seven test usertask cases
        # above, the case 16 task each of the two test stage2 runs kills, and the two crashes of the test-echo
        # driver in test drivers.
        expected_user_faults = 7 + 2 + 2
        output = send_command('list statistics')
        check('list statistics', output, 'Statistics:', 'tasks.user_faults')

        faults = re.search(r'^\s*0\s+1\s+counter\s+tasks\.user_faults\s+(\d+)\s*$', output, re.MULTILINE)
        if faults is None or int(faults.group(1)) != expected_user_faults:
            print(f'\nFAIL [list statistics]: expected tasks.user_faults to be {expected_user_faults}, got '
                  f'{faults.group(1) if faults else "no tasks.user_faults line"}')
            failures += 1
        else:
            print(f'\nPASS [list statistics]: tasks.user_faults is {expected_user_faults}')

        # The driver manager started the test-echo driver once and restarted it after two crashes and a kill, and
        # killed it again to stop it.  It started the console and sd-card drivers at boot and restarted each after test
        # drivers killed it.  On raspi3b the same goes for the hw-rng driver.
        hw_rng = 1 if machine == 'raspi3b' else 0
        for stat_id, name, expected in ((48, 'drivers.starts', 8 + 2 * hw_rng), (49, 'drivers.restarts', 5 + hw_rng),
                                        (50, 'tasks.kills', 4 + hw_rng)):
            found = re.search(rf'^\s*0\s+{stat_id}\s+counter\s+{re.escape(name)}\s+(\d+)\s*$', output, re.MULTILINE)
            if found is None or int(found.group(1)) != expected:
                print(f'\nFAIL [list statistics]: expected {name} to be {expected}, got '
                      f'{found.group(1) if found else "no " + name + " line"}')
                failures += 1
            else:
                print(f'\nPASS [list statistics]: {name} is {expected}')

        # The memory metrics are maintained by the page allocator itself.  free_pages is a running
        # gauge, so it must be a plausible nonzero count; invalid_releases and last_invalid_release
        # are correctness canaries that must stay zero -- a nonzero value means a double free or a
        # stray pointer freed a page the kernel did not own.  (The console wraps the 82-column
        # listing lines, so the value lands on the line after the name; \s+ spans that.)
        def stat_value(stat_id: int, kind: str, name: str) -> int | None:
            m = re.search(rf'^\s*0\s+{stat_id}\s+{kind}\s+{re.escape(name)}\s+(\d+)\s*$',
                          output, re.MULTILINE)
            return int(m.group(1)) if m else None

        free_pages = stat_value(2, 'gauge', 'memory.free_pages')
        if free_pages is None or free_pages <= 0:
            print(f'\nFAIL [list statistics]: expected memory.free_pages > 0, got {free_pages}')
            failures += 1
        else:
            print(f'\nPASS [list statistics]: memory.free_pages is {free_pages}')

        invalid_releases = stat_value(3, 'counter', 'memory.invalid_releases')
        if invalid_releases != 0:
            print(f'\nFAIL [list statistics]: expected memory.invalid_releases to be 0, got {invalid_releases}')
            failures += 1
        else:
            print('\nPASS [list statistics]: memory.invalid_releases is 0')

        if stat_value(4, 'counter', 'memory.alloc_failures') is None:
            print('\nFAIL [list statistics]: memory.alloc_failures line missing')
            failures += 1
        else:
            print('\nPASS [list statistics]: memory.alloc_failures present')

        last_invalid_release = stat_value(5, 'timestamp', 'memory.last_invalid_release')
        if last_invalid_release != 0:
            print(f'\nFAIL [list statistics]: expected memory.last_invalid_release to be 0 (never), got {last_invalid_release}')
            failures += 1
        else:
            print('\nPASS [list statistics]: memory.last_invalid_release is 0 (never)')

        # The scheduling metrics are maintained in the scheduler itself.  By now every one must
        # have moved: the run exercised voluntary yields, timer-tick preemption, and dozens of
        # short-lived user tasks.  running counts the cores whose current task is RUNNING -- it
        # can briefly dip below the core count while a zombie spins awaiting its next switch,
        # but it can never exceed it.  Each core's run list keeps its idle task forever, so its
        # depth never reaches zero.  dropped_zombies is a correctness canary that must stay zero
        # -- a nonzero value means a zombie leaked its stack and address space.
        def expect_stat(stat_id: int, kind: str, name: str, ok, describe: str) -> None:
            nonlocal failures
            value = stat_value(stat_id, kind, name)
            if value is None or not ok(value):
                print(f'\nFAIL [list statistics]: {name} {describe}, got {value}')
                failures += 1
            else:
                print(f'\nPASS [list statistics]: {name} {describe} ({value})')

        expect_stat(6, 'counter', 'tasks.voluntary_switches', lambda v: v > 0, '> 0')
        expect_stat(7, 'counter', 'tasks.preemptive_switches', lambda v: v > 0, '> 0')
        expect_stat(8, 'counter', 'tasks.timeslices_granted', lambda v: v > 0, '> 0')
        expect_stat(9, 'gauge', 'tasks.running', lambda v: 1 <= v <= 4, 'in 1..4')
        expect_stat(10, 'gauge', 'tasks.zombies', lambda v: v > 0, '> 0')
        expect_stat(11, 'counter', 'tasks.dropped_zombies', lambda v: v == 0, '== 0')
        # One line per core: each run list always holds its idle task, and the emulated boards have four cores.
        for core in range(4):
            expect_stat(12, 'array', f'tasks.runlist_depth[{core}]', lambda v: v >= 1, '>= 1')
        if stat_value(12, 'array', 'tasks.runlist_depth[4]') is not None:
            print('\nFAIL [list statistics]: tasks.runlist_depth has an element past the last core')
            failures += 1
        else:
            print('\nPASS [list statistics]: tasks.runlist_depth has one element per core')
        expect_stat(16, 'counter', 'scheduler.missed_interrupts', lambda v: v >= 0, 'present')

        # The wait metrics.  test sleep, test wait, test mutex and the reaper block, and every sleep
        # ends by its timeout.
        # Nothing waits for storage I/O yet, so waiting_io must stay zero.
        expect_stat(34, 'gauge', 'tasks.waiting', lambda v: v >= 0, '>= 0')
        expect_stat(35, 'gauge', 'tasks.waiting_io', lambda v: v == 0, '== 0')
        expect_stat(36, 'counter', 'tasks.blocks', lambda v: v > 0, '> 0')
        expect_stat(37, 'counter', 'tasks.wakeups', lambda v: v >= 0, 'present')
        expect_stat(38, 'counter', 'tasks.wait_timeouts', lambda v: v > 0, '> 0')
        expect_stat(39, 'counter', 'mutex.handoffs', lambda v: v >= 0, 'present')
        expect_stat(40, 'counter', 'scheduler.deadline_interrupts', lambda v: v > 0, '> 0')

        # The address-space metrics.  demand_page_faults must have moved -- test usertask
        # demand-pages heap and stack pages in several cases.  demand_page_failures is a
        # correctness canary that must stay zero: a nonzero value means a page-in lost a
        # frame or table allocation.  address_spaces is racy in the instant sense (a zombie
        # keeps its space for the reaper's one-second grace), but it must never accumulate:
        # a space not returned once its task is gone is an ASID leak, which no page count
        # would show.
        expect_stat(17, 'gauge', 'mmu.address_spaces', lambda v: v <= 8, '<= 8')
        expect_stat(18, 'counter', 'mmu.demand_page_faults', lambda v: v > 0, '> 0')
        expect_stat(19, 'counter', 'mmu.demand_page_failures', lambda v: v == 0, '== 0')

        # The SD card metrics.  Mounting the filesystem and loading every user binary read the
        # card, so reads and blocks_read must have moved, and the time spent must be nonzero.
        # The run never writes, so the write statistics need only be present.  io_failures and
        # last_io_failure are correctness canaries: test drivers fails exactly one read by
        # killing the driver in it, and any other failure means a read gave up after all its
        # retries; transfer_aborts means a data command hit an error at all.  The card must
        # have come up, so there was at least one reset.
        expect_stat(21, 'counter', 'emmc.reads', lambda v: v > 0, '> 0')
        expect_stat(22, 'counter', 'emmc.writes', lambda v: v >= 0, 'present')
        expect_stat(23, 'counter', 'emmc.blocks_read', lambda v: v >= stat_value(21, 'counter', 'emmc.reads'), '>= reads')
        expect_stat(24, 'counter', 'emmc.blocks_written', lambda v: v >= 0, 'present')
        expect_stat(25, 'counter', 'emmc.io_failures', lambda v: v == 1, '== 1')
        expect_stat(26, 'timestamp', 'emmc.last_io_failure', lambda v: v > 0, '> 0')
        expect_stat(27, 'counter', 'emmc.command_retries', lambda v: v >= 0, 'present')
        expect_stat(28, 'counter', 'emmc.io_time_us', lambda v: v > 0, '> 0')
        expect_stat(29, 'gauge', 'emmc.max_io_time_us', lambda v: 0 < v <= stat_value(28, 'counter', 'emmc.io_time_us'), 'in 1..io_time_us')
        expect_stat(30, 'counter', 'emmc.controller_waits', lambda v: v >= 0, 'present')
        expect_stat(31, 'counter', 'emmc.card_resets', lambda v: v >= 1, '>= 1')
        expect_stat(32, 'counter', 'emmc.transfer_aborts', lambda v: v == 0, '== 0')

        # test addrspace — checks the address space / page table layer, including
        # that the syscall boundary range-checks before it walks.
        output = send_command('test addrspace')
        check('test addrspace', output, 'PASS: address space test', 'loaded statistics: 4 registered')

        # test soak — two rounds of the scheduling, memory and concurrency soak that
        # make qemu-cli-soak and make hw-cli-soak run for an hour.  Last, so its tasks and statistics cannot
        # disturb the exact checks above.
        output = send_command('test soak --seconds=10', timeout=180)
        check('test soak', output, 'PASS: soak test')

        # Back to v0.1, through the v0.2 image's own trampoline.
        output = send_command('hypervisor update /hypervisor.bin')
        check('hypervisor update back', output, 'HYP: live update from v0.2 to v0.1', f'Hypervisor updated: now v0.1 (ABI {ABI})')
        output = send_command('test sleep', timeout=60)
        check('test sleep after the second update', output, 'PASS: sleep test')

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
    parser.add_argument('--hypervisor', required=True, help='Path to hypervisor.bin, loaded at 0x40000')
    parser.add_argument('--sdimage', required=True, help='Path to sd.img')
    parser.add_argument('--machine', default='raspi3b',
                        help='QEMU machine model (default: raspi3b)')
    parser.add_argument('--memory',  default='',
                        help='RAM size passed to QEMU -m (default: machine default)')
    args = parser.parse_args()

    print(f'=== {args.machine} Pass 1: default alignment ===')
    result = run(args.qemu, args.armstub, args.kernel, args.hypervisor, args.sdimage,
                 machine=args.machine, memory=args.memory)
    if result != 0:
        return result

    print(f'\n=== {args.machine} Pass 2: strict_align=1, stage-2 audit mode ===')
    return run(args.qemu, args.armstub, args.kernel, args.hypervisor, args.sdimage,
               machine=args.machine, memory=args.memory,
               extra_cmdline=' strict_align=1 stage2=audit')

if __name__ == '__main__':
    sys.exit(main())
    
#!/usr/bin/env python3
# Copyright 2026 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be found
# in the LICENSE file.
#
# Long-running CLI soak test for RPIBareMetalOS.  Drives the CLI either of an OS
# it launches under qemu-system-aarch64, or of a board on a serial port
# (--serial), running random CLI commands serially for a configurable duration.
# 'test soak' is in the pool, so most of the run is spent in the kernel's
# scheduling, memory and concurrency soak.

import argparse
import os
import random
import sys
import termios
import time
import tty
from collections import Counter
from dataclasses import dataclass

import pexpect
import pexpect.fdpexpect

from qemu_machines import cpu_flags


PROMPT = '> '
BOOT_READY_MARKER = 'Command Line Interface'
TIMEOUT = 60
DEFAULT_BOOT_TIMEOUT = 300

# Printed by a failing kernel test or by a hello.bin case that should not have survived.
FAILURE_MARKERS = ('FAIL', 'CORRUPTION', 'NOT KILLED', 'ALIASED', 'SYNC_ERROR')


@dataclass(frozen=True)
class cli_command_spec:
    command: str
    expected_markers: tuple[str, ...]
    timeout: int = TIMEOUT


COMMAND_POOL = (
    cli_command_spec('list filesystems', ('Filesystem:',)),
    cli_command_spec('list tasks', ('Tasks:', 'Kernel Main Task', 'Idle Task', 'CLI')),
    cli_command_spec('show diagnostics', ('Board Info:', 'RPI Version:')),
    cli_command_spec('test soak --seconds=30', ('PASS: soak test',), timeout=300),
)


class tee_writer:
    """Writes console output to every target: stdout for --verbose, a file for --log."""

    def __init__(self, *targets):
        self.targets = [target for target in targets if target is not None]

    def write(self, text: str) -> None:
        for target in self.targets:
            target.write(text)

    def flush(self) -> None:
        for target in self.targets:
            target.flush()


def open_qemu(args: argparse.Namespace) -> pexpect.spawn:
    cmd = (
        f'{args.qemu} -M {args.machine}'
        f'{cpu_flags(args.machine)}'
        f' -kernel {args.armstub}'
        f' -device loader,file={args.kernel},addr=0x80000,force-raw=on'
        f' -drive file={args.sdimage},if=sd,format=raw'
        f' -serial stdio'
        f' -display none'
        f' -no-reboot'
        f' -append "console=ttys0,57600 host=qemu"'
    )

    print(f'Launching: {cmd}')
    return pexpect.spawn(cmd, encoding='utf-8', codec_errors='replace', timeout=TIMEOUT)


def open_serial(port: str, baud: int) -> tuple[pexpect.fdpexpect.fdspawn, int]:
    speed = getattr(termios, f'B{baud}', None)
    if speed is None:
        raise ValueError(f'unsupported baud rate {baud}')

    fd = os.open(port, os.O_RDWR | os.O_NOCTTY)

    # Raw 8N1, no flow control, ignore modem lines.
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    attrs[2] |= termios.CLOCAL | termios.CREAD
    attrs[2] &= ~(termios.CSTOPB | termios.PARENB | getattr(termios, 'CRTSCTS', 0))
    attrs[4] = speed
    attrs[5] = speed
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)

    print(f'Opened {port} at {baud} baud')
    return pexpect.fdpexpect.fdspawn(fd, encoding='utf-8', codec_errors='replace', timeout=TIMEOUT), fd


def wait_for_cli(child, serial: bool, wait_for_boot: bool, boot_timeout: int) -> None:
    if not serial or wait_for_boot:
        if serial:
            print(f'Waiting up to {boot_timeout}s for the board to boot -- power it on or reset it now')
        child.expect(BOOT_READY_MARKER, timeout=boot_timeout)
        child.expect(PROMPT, timeout=TIMEOUT)
        return

    # Already booted: an empty line makes the CLI print a fresh prompt.
    child.send('\n')
    child.expect(PROMPT, timeout=TIMEOUT)


def run(args: argparse.Namespace) -> int:
    if args.duration_seconds <= 0:
        print('FAIL: duration_seconds must be > 0')
        return 1
    if args.min_interval_seconds < 0 or args.max_interval_seconds < 0:
        print('FAIL: interval values must be >= 0')
        return 1
    if args.min_interval_seconds > args.max_interval_seconds:
        print('FAIL: min_interval_seconds must be <= max_interval_seconds')
        return 1

    serial = args.serial is not None
    rng = random.Random(args.seed)

    if args.seed is not None:
        print(f'Random seed: {args.seed}')
    print(
        'Soak config: '
        f'duration={args.duration_seconds:.2f}s, '
        f'interval=[{args.min_interval_seconds:.2f},{args.max_interval_seconds:.2f}]s, '
        f'progress={args.progress_interval_seconds:.2f}s'
    )

    log_file = open(args.log, 'w', encoding='utf-8') if args.log else None
    serial_fd = None

    try:
        if serial:
            child, serial_fd = open_serial(args.serial, args.baud)
        else:
            child = open_qemu(args)
    except (OSError, ValueError) as error:
        print(f'FAIL: {error}')
        return 1

    child.logfile_read = tee_writer(sys.stdout if args.verbose else None, log_file)

    failures = 0
    command_counts: Counter[str] = Counter()
    iterations = 0
    start = time.monotonic()
    end_time = start + args.duration_seconds
    next_progress_time = start + args.progress_interval_seconds

    def send_command(command: str, timeout: int) -> str:
        # The CLI ends a line at '\n'; a terminal's '\r' would be read as part of the command.
        child.send(command + '\n')
        child.expect(PROMPT, timeout=timeout)
        return child.before

    try:
        wait_for_cli(child, serial, args.wait_for_boot, args.boot_timeout)

        start = time.monotonic()
        end_time = start + args.duration_seconds
        next_progress_time = start + args.progress_interval_seconds

        while time.monotonic() < end_time:
            spec = rng.choice(COMMAND_POOL)
            output = send_command(spec.command, spec.timeout)
            iterations += 1
            command_counts[spec.command] += 1

            missing_markers = [marker for marker in spec.expected_markers if marker not in output]
            failure_lines = [line.strip() for line in output.splitlines()
                             if any(marker in line for marker in FAILURE_MARKERS)]
            if missing_markers or failure_lines:
                failures += 1
                if missing_markers:
                    print(
                        f'FAIL [{spec.command}] missing markers: '
                        + ', '.join(f'"{marker}"' for marker in missing_markers)
                    )
                for line in failure_lines:
                    print(f'FAIL [{spec.command}] {line}')
                if args.fail_fast:
                    break

            now = time.monotonic()
            if args.progress_interval_seconds > 0 and now >= next_progress_time:
                elapsed = now - start
                print(
                    f'Progress: elapsed={elapsed:.1f}s '
                    f'iterations={iterations} failures={failures}'
                )
                next_progress_time = now + args.progress_interval_seconds

            if time.monotonic() < end_time and args.max_interval_seconds > 0:
                time.sleep(rng.uniform(args.min_interval_seconds, args.max_interval_seconds))

        # Halt QEMU so it exits cleanly; a board is left at the prompt for inspection.
        if not serial:
            child.send('halt\n')
            child.expect('Halting', timeout=TIMEOUT)

    except pexpect.TIMEOUT:
        print('FAIL: timed out waiting for expected output')
        failures += 1
    except pexpect.EOF:
        print('FAIL: QEMU exited unexpectedly' if not serial else 'FAIL: serial port closed unexpectedly')
        failures += 1
    finally:
        if serial:
            child.close()
        else:
            child.terminate(force=True)
        if log_file is not None:
            log_file.close()

    elapsed_total = time.monotonic() - start
    print('--- Soak Summary ---')
    print(f'Target: {args.serial if serial else "QEMU " + args.machine}')
    print(f'Elapsed: {elapsed_total:.2f}s')
    print(f'Iterations: {iterations}')
    for command_spec in COMMAND_POOL:
        print(f'  {command_spec.command}: {command_counts[command_spec.command]}')
    if args.log:
        print(f'Console log: {args.log}')

    print(f'{"PASSED" if failures == 0 else "FAILED"} - {failures} failure(s)')
    return 0 if failures == 0 else 1


def main() -> int:
    parser = argparse.ArgumentParser(
        description='RPIBareMetalOS long-running random CLI soak test, under QEMU or on a board over a serial port')

    qemu = parser.add_argument_group('QEMU target (the default)')
    qemu.add_argument('--qemu', help='Path to qemu-system-aarch64')
    qemu.add_argument('--armstub', help='Path to supervisor.elf')
    qemu.add_argument('--kernel', help='Path to kernel8.img')
    qemu.add_argument('--sdimage', help='Path to sd.img')
    qemu.add_argument('--machine', default='raspi3b', help='QEMU machine model (default: raspi3b)')

    board = parser.add_argument_group('Board target')
    board.add_argument('--serial', metavar='PORT', help='Serial port of the board, e.g. /dev/ttyUSB0; replaces the QEMU arguments')
    board.add_argument('--baud', type=int, default=57600, help='Serial baud rate (default: 57600, as in cmdline.txt)')
    board.add_argument('--wait-for-boot', action='store_true',
                       help='Wait for the board to boot instead of expecting it already at the prompt')
    board.add_argument('--boot-timeout', type=int, default=DEFAULT_BOOT_TIMEOUT,
                       help=f'Seconds to wait for the boot banner (default: {DEFAULT_BOOT_TIMEOUT})')

    parser.add_argument(
        '--duration-seconds',
        type=float,
        default=3600,
        help='How long to run the random serial CLI command loop',
    )
    parser.add_argument(
        '--min-interval-seconds',
        type=float,
        default=0.2,
        help='Minimum delay between commands',
    )
    parser.add_argument(
        '--max-interval-seconds',
        type=float,
        default=1.0,
        help='Maximum delay between commands',
    )
    parser.add_argument(
        '--progress-interval-seconds',
        type=float,
        default=30,
        help='Progress print interval; use 0 to disable',
    )
    parser.add_argument('--seed', type=int, default=None, help='Random seed for deterministic command order')
    parser.add_argument('--log', metavar='FILE', help='Write the full console output to FILE')
    parser.add_argument('--verbose', action='store_true', help='Stream the full console output to stdout')
    parser.add_argument('--fail-fast', action='store_true', help='Stop immediately on first CLI response validation failure')

    args = parser.parse_args()

    if args.serial is None:
        missing = [name for name in ('qemu', 'armstub', 'kernel', 'sdimage') if getattr(args, name) is None]
        if missing:
            parser.error('without --serial these are required: ' + ', '.join(f'--{name}' for name in missing))
    elif args.wait_for_boot is False and args.boot_timeout != DEFAULT_BOOT_TIMEOUT:
        parser.error('--boot-timeout applies only with --wait-for-boot')

    return run(args)


if __name__ == '__main__':
    sys.exit(main())

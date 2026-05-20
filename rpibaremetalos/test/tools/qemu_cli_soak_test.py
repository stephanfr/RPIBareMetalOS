#!/usr/bin/env python3
# Copyright 2026 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be found
# in the LICENSE file.
#
# Long-running CLI soak test for RPIBareMetalOS.
# Launches the OS under qemu-system-aarch64, then executes random CLI commands
# serially for a configurable duration.

import argparse
import random
import sys
import time
from collections import Counter
from dataclasses import dataclass

import pexpect


PROMPT = '> '
BOOT_READY_MARKER = 'Command Line Interface'
TIMEOUT = 60


@dataclass(frozen=True)
class cli_command_spec:
    command: str
    expected_markers: tuple[str, ...]


COMMAND_POOL = (
    cli_command_spec('list filesystems', ('Filesystem:',)),
    cli_command_spec('list tasks', ('Tasks:', 'Kernel Main Task', 'Idle Task', 'CLI')),
    cli_command_spec('show diagnostics', ('Board Info:', 'RPI Version:')),
)


def run(
    qemu: str,
    kernel: str,
    sdimage: str,
    duration_seconds: float,
    min_interval_seconds: float,
    max_interval_seconds: float,
    progress_interval_seconds: float,
    seed: int | None,
    verbose: bool,
    fail_fast: bool,
) -> int:
    if duration_seconds <= 0:
        print('FAIL: duration_seconds must be > 0')
        return 1
    if min_interval_seconds < 0 or max_interval_seconds < 0:
        print('FAIL: interval values must be >= 0')
        return 1
    if min_interval_seconds > max_interval_seconds:
        print('FAIL: min_interval_seconds must be <= max_interval_seconds')
        return 1

    rng = random.Random(seed)
    cmd = (
        f'{qemu} -M raspi3b'
        f' -kernel {kernel}'
        f' -drive file={sdimage},if=sd,format=raw'
        f' -serial stdio'
        f' -display none'
        f' -no-reboot'
        f' -append "console=ttys0,57600 memory_model=kernel_only_1_to_1"'
    )

    print(f'Launching: {cmd}')
    if seed is not None:
        print(f'Random seed: {seed}')
    print(
        'Soak config: '
        f'duration={duration_seconds:.2f}s, '
        f'interval=[{min_interval_seconds:.2f},{max_interval_seconds:.2f}]s, '
        f'progress={progress_interval_seconds:.2f}s'
    )

    child = pexpect.spawn(cmd, encoding='utf-8', timeout=TIMEOUT)
    child.logfile = sys.stdout if verbose else None

    failures = 0
    command_counts: Counter[str] = Counter()
    iterations = 0
    start = time.monotonic()
    end_time = start + duration_seconds
    next_progress_time = start + progress_interval_seconds

    def send_command(command: str) -> str:
        child.sendline(command)
        child.expect(PROMPT, timeout=TIMEOUT)
        return child.before

    try:
        child.expect(BOOT_READY_MARKER, timeout=TIMEOUT)
        child.expect(PROMPT, timeout=TIMEOUT)

        while time.monotonic() < end_time:
            spec = rng.choice(COMMAND_POOL)
            output = send_command(spec.command)
            iterations += 1
            command_counts[spec.command] += 1

            missing_markers = [marker for marker in spec.expected_markers if marker not in output]
            if missing_markers:
                failures += 1
                print(
                    f'FAIL [{spec.command}] missing markers: '
                    + ', '.join(f'"{marker}"' for marker in missing_markers)
                )
                if fail_fast:
                    break

            now = time.monotonic()
            if progress_interval_seconds > 0 and now >= next_progress_time:
                elapsed = now - start
                print(
                    f'Progress: elapsed={elapsed:.1f}s '
                    f'iterations={iterations} failures={failures}'
                )
                next_progress_time = now + progress_interval_seconds

            if time.monotonic() < end_time and max_interval_seconds > 0:
                time.sleep(rng.uniform(min_interval_seconds, max_interval_seconds))

        child.sendline('halt')
        child.expect('Halting', timeout=TIMEOUT)

    except pexpect.TIMEOUT:
        print('FAIL: timed out waiting for expected output')
        failures += 1
    except pexpect.EOF:
        print('FAIL: QEMU exited unexpectedly')
        failures += 1
    finally:
        child.terminate(force=True)

    elapsed_total = time.monotonic() - start
    print('--- Soak Summary ---')
    print(f'Elapsed: {elapsed_total:.2f}s')
    print(f'Iterations: {iterations}')
    for command_spec in COMMAND_POOL:
        print(f'  {command_spec.command}: {command_counts[command_spec.command]}')

    print(f'{"PASSED" if failures == 0 else "FAILED"} - {failures} failure(s)')
    return 0 if failures == 0 else 1


def main() -> int:
    parser = argparse.ArgumentParser(description='RPIBareMetalOS long-running random CLI soak test')
    parser.add_argument('--qemu', required=True, help='Path to qemu-system-aarch64')
    parser.add_argument('--kernel', required=True, help='Path to kernel8.elf')
    parser.add_argument('--sdimage', required=True, help='Path to sd.img')
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
    parser.add_argument('--verbose', action='store_true', help='Stream full QEMU serial output')
    parser.add_argument('--fail-fast', action='store_true', help='Stop immediately on first CLI response validation failure')

    args = parser.parse_args()
    return run(
        qemu=args.qemu,
        kernel=args.kernel,
        sdimage=args.sdimage,
        duration_seconds=args.duration_seconds,
        min_interval_seconds=args.min_interval_seconds,
        max_interval_seconds=args.max_interval_seconds,
        progress_interval_seconds=args.progress_interval_seconds,
        seed=args.seed,
        verbose=args.verbose,
        fail_fast=args.fail_fast,
    )


if __name__ == '__main__':
    sys.exit(main())
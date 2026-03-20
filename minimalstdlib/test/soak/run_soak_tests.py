#!/usr/bin/env python3
"""
run_soak_tests.py — discover and run soak test groups with optional GDB.

Usage:
    python3 test/soak/run_soak_tests.py [options]

Options:
    --soak-exe <path>   Path to the soak test executable.
                        Default: test/build/soak/cpputest_soak.exe
    --no-gdb            Run tests directly without GDB.
    --runs <N>          Number of times to run each test group (default: 1).
    --timeout <secs>    Per-run timeout in seconds (default: 600).
    --group <name>      Run only this group (may be repeated).
    --list              List discovered groups then exit.

Environment variables passed through to the soak executable:
    ALLOCATOR_SOAK_DURATION, SKIPLIST_SOAK_DURATION, etc.
"""

import argparse
import os
import subprocess
import sys
import time


DEFAULT_EXE = "test/build/soak/cpputest_soak.exe"
GDB_PREAMBLE = [
    "handle SIGUSR1 noprint nostop pass",
    "run",
    "bt",
]


def discover_groups(exe: str) -> list[str]:
    """Return the list of TEST_GROUPs reported by the executable via -lg."""
    try:
        result = subprocess.run(
            [exe, "-lg"],
            capture_output=True,
            text=True,
            timeout=30,
        )
    except FileNotFoundError:
        print(f"error: executable not found: {exe}", file=sys.stderr)
        sys.exit(1)
    except subprocess.TimeoutExpired:
        print("error: timed out listing groups", file=sys.stderr)
        sys.exit(1)

    groups = []
    for line in result.stdout.splitlines():
        line = line.strip()
        # CppUTest prints group names prefixed with a space or directly
        if line and not line.startswith("TEST") and not line.startswith("-"):
            groups.append(line)

    return groups


def build_gdb_cmd(exe: str, group: str) -> list[str]:
    ex_args = []
    for ex in GDB_PREAMBLE:
        ex_args += ["-ex", ex]
    return ["gdb", "-batch"] + ex_args + ["--args", exe, "-g", group]


def build_direct_cmd(exe: str, group: str) -> list[str]:
    return [exe, "-g", group]


def run_group(cmd: list[str], group: str, run_index: int, timeout: int) -> bool:
    """Run one group in a subprocess.  Returns True on success."""
    label = f"[{group} run {run_index}]"
    print(f"\n{'='*70}")
    print(f"{label}  cmd: {' '.join(cmd)}")
    print(f"{'='*70}", flush=True)

    start = time.monotonic()
    try:
        proc = subprocess.run(cmd, timeout=timeout)
        elapsed = time.monotonic() - start
        ok = proc.returncode == 0
        status = "PASS" if ok else f"FAIL (rc={proc.returncode})"
        print(f"{label}  {status}  elapsed={elapsed:.1f}s", flush=True)
        return ok
    except subprocess.TimeoutExpired:
        elapsed = time.monotonic() - start
        print(f"{label}  TIMEOUT after {elapsed:.0f}s", flush=True)
        return False


def main() -> int:
    parser = argparse.ArgumentParser(description="Run soak tests with optional GDB harness.")
    parser.add_argument("--soak-exe", default=DEFAULT_EXE,
                        help=f"Path to soak executable (default: {DEFAULT_EXE})")
    parser.add_argument("--no-gdb", action="store_true",
                        help="Run directly without GDB")
    parser.add_argument("--runs", type=int, default=1,
                        help="Number of runs per group (default: 1)")
    parser.add_argument("--timeout", type=int, default=600,
                        help="Per-run timeout in seconds (default: 600)")
    parser.add_argument("--group", action="append", dest="groups", default=[],
                        help="Run only this group (may be repeated)")
    parser.add_argument("--list", action="store_true",
                        help="List discovered groups and exit")
    args = parser.parse_args()

    exe = args.soak_exe
    if not os.path.isfile(exe):
        print(f"error: soak executable not found: {exe}", file=sys.stderr)
        print("Build it first:  make -f Makefile.test.mk test-soak", file=sys.stderr)
        return 1

    all_groups = discover_groups(exe)
    if not all_groups:
        print("No soak test groups discovered.  Is the executable built?", file=sys.stderr)
        return 1

    if args.list:
        print("Discovered soak test groups:")
        for g in all_groups:
            print(f"  {g}")
        return 0

    target_groups = args.groups if args.groups else all_groups

    # Validate requested groups
    unknown = [g for g in target_groups if g not in all_groups]
    if unknown:
        print(f"error: unknown group(s): {', '.join(unknown)}", file=sys.stderr)
        print(f"Available: {', '.join(all_groups)}", file=sys.stderr)
        return 1

    use_gdb = not args.no_gdb

    total = 0
    passed = 0
    failed_list: list[str] = []

    for group in target_groups:
        for run_idx in range(1, args.runs + 1):
            if use_gdb:
                cmd = build_gdb_cmd(exe, group)
            else:
                cmd = build_direct_cmd(exe, group)

            total += 1
            ok = run_group(cmd, group, run_idx, args.timeout)
            if ok:
                passed += 1
            else:
                failed_list.append(f"{group} run {run_idx}")

    print(f"\n{'='*70}")
    print(f"Soak test summary: {passed}/{total} passed")
    if failed_list:
        print("Failed:")
        for f in failed_list:
            print(f"  {f}")
    print(f"{'='*70}")

    return 0 if not failed_list else 1


if __name__ == "__main__":
    sys.exit(main())

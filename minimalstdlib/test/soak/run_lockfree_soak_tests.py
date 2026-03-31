#!/usr/bin/env python3
"""
run_lockfree_soak_tests.py — discover and run lockfree soak test groups with optional GDB.

Usage:
    python3 test/soak/run_lockfree_soak_tests.py [options]

Options:
    --soak-exe <path>   Path to the soak test executable.
                        Default: test/build/soak/cpputest_soak.exe
    --no-gdb            Run tests directly without GDB.
    --runs <N>          Number of times to run each test group (default: 1).
    --allocator-soak-duration <secs>
                        Sets ALLOCATOR_SOAK_DURATION for each run.
    --timeout <secs>    Per-run timeout in seconds (default: 600).
    --group <name>      Run only this group (may be repeated).
    --list              List discovered groups then exit.

Environment variables passed through to the soak executable:
    ALLOCATOR_SOAK_DURATION, SKIPLIST_SOAK_DURATION, etc.
"""

import argparse
import os
import select
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

    # CppUTest output formatting can vary by version/build and may print
    # groups on one line. Tokenize by whitespace and keep unique entries.
    groups: list[str] = []
    for token in result.stdout.split():
        token = token.strip()
        if not token:
            continue
        if token.startswith("TEST") or token.startswith("-"):
            continue
        if token not in groups:
            groups.append(token)

    return groups


def build_gdb_cmd(exe: str, group: str) -> list[str]:
    ex_args = []
    for ex in GDB_PREAMBLE:
        ex_args += ["-ex", ex]
    return ["gdb", "-batch"] + ex_args + ["--args", exe, "-g", group]


def build_direct_cmd(exe: str, group: str) -> list[str]:
    return [exe, "-g", group]


def run_group(cmd: list[str], group: str, run_index: int, timeout: int, run_env: dict[str, str]) -> bool:
    """Run one group in a subprocess.  Returns True on success."""
    label = f"[{group} run {run_index}]"
    print(f"\n{'='*70}")
    print(f"{label}  cmd: {' '.join(cmd)}")
    print(f"{'='*70}", flush=True)

    start = time.monotonic()
    try:
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            env=run_env,
        )

        output_chunks: list[str] = []
        deadline = start + timeout

        assert proc.stdout is not None

        while True:
            now = time.monotonic()
            if now >= deadline:
                proc.kill()
                proc.wait()
                elapsed = time.monotonic() - start
                print(f"{label}  TIMEOUT after {elapsed:.0f}s", flush=True)
                return False

            ready, _, _ = select.select([proc.stdout], [], [], 0.2)
            if ready:
                line = proc.stdout.readline()
                if line:
                    output_chunks.append(line)
                    print(line, end="", flush=True)

            if proc.poll() is not None:
                # Drain any remaining buffered output.
                rest = proc.stdout.read()
                if rest:
                    output_chunks.append(rest)
                    print(rest, end="", flush=True)
                break

        elapsed = time.monotonic() - start
        output = "".join(output_chunks)
        # gdb -batch with a trailing "bt" can return 1 after a normal test
        # exit because there is no active stack frame ("No stack.").
        # Treat this as success when the inferior exited normally.
        exited_normally = "exited normally" in output
        no_stack = "No stack." in output
        return_code = proc.returncode if proc.returncode is not None else -1
        ok = (return_code == 0) or (return_code == 1 and exited_normally and no_stack)
        status = "PASS" if ok else f"FAIL (rc={return_code})"
        print(f"{label}  {status}  elapsed={elapsed:.1f}s", flush=True)
        return ok
    except subprocess.TimeoutExpired:
        elapsed = time.monotonic() - start
        print(f"{label}  TIMEOUT after {elapsed:.0f}s", flush=True)
        return False


def main() -> int:
    parser = argparse.ArgumentParser(description="Run lockfree soak tests with optional GDB harness.")
    parser.add_argument("--soak-exe", default=DEFAULT_EXE,
                        help=f"Path to soak executable (default: {DEFAULT_EXE})")
    parser.add_argument("--no-gdb", action="store_true",
                        help="Run directly without GDB")
    parser.add_argument("--runs", type=int, default=1,
                        help="Number of runs per group (default: 1)")
    parser.add_argument("--allocator-soak-duration", type=int, default=None,
                        help="Set ALLOCATOR_SOAK_DURATION in seconds for each run")
    parser.add_argument("--timeout", type=int, default=None,
                        help="Per-run timeout in seconds (default: max(600, allocator_duration+120))")
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

    run_env = os.environ.copy()
    if args.allocator_soak_duration is not None:
        run_env["ALLOCATOR_SOAK_DURATION"] = str(args.allocator_soak_duration)

    effective_timeout = args.timeout if args.timeout is not None else 600
    if args.allocator_soak_duration is not None:
        min_timeout = args.allocator_soak_duration + 120
        if effective_timeout < min_timeout:
            effective_timeout = min_timeout

    print(f"Using timeout={effective_timeout}s")
    if "ALLOCATOR_SOAK_DURATION" in run_env:
        print(f"Using ALLOCATOR_SOAK_DURATION={run_env['ALLOCATOR_SOAK_DURATION']}s")

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
            ok = run_group(cmd, group, run_idx, effective_timeout, run_env)
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

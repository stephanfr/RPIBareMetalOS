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
    --test-duration <secs> Total duration to keep running tests (overrides --runs).
    --allocator-soak-duration <secs>
                        Sets ALLOCATOR_SOAK_DURATION for each run.
    --phase-base <secs> Sets ALLOCATOR_SOAK_PHASE_BASE in seconds.
    --phase-range <secs> Sets ALLOCATOR_SOAK_PHASE_RANGE in seconds.
    --progress-file <path> Output file for progress logic (default: /tmp/soak_progress.log).
    --status-file <path>   Output file for overall test status (default: /tmp/soak_status.log).
    --timeout <secs>    Per-run timeout in seconds.
    --group <name>      Run only this group (may be repeated).
    --list              List discovered groups then exit.
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

progress_file_path = None
status_file_path = None

def log_progress(text: str):
    print(text, end="", flush=True)
    if progress_file_path:
        with open(progress_file_path, "a") as f:
            f.write(text)

def log_status(text: str):
    if status_file_path:
        with open(status_file_path, "a") as f:
            f.write(text)

def discover_groups(exe: str) -> list[str]:
    try:
        result = subprocess.run([exe, "-lg"], capture_output=True, text=True, timeout=30)
    except FileNotFoundError:
        print(f"error: executable not found: {exe}", file=sys.stderr)
        sys.exit(1)
    except subprocess.TimeoutExpired:
        print("error: timed out listing groups", file=sys.stderr)
        sys.exit(1)

    groups: list[str] = []
    for token in result.stdout.split():
        token = token.strip()
        if not token or token.startswith("TEST") or token.startswith("-"):
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
    label = f"[{group} run {run_index}]"
    log_progress(f"\n{'='*70}\n")
    log_progress(f"{label}  cmd: {' '.join(cmd)}\n")
    log_progress(f"{'='*70}\n")

    start = time.monotonic()
    try:
        proc = subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1, env=run_env
        )
        output_chunks = []
        deadline = start + timeout
        assert proc.stdout is not None

        while True:
            now = time.monotonic()
            if now >= deadline:
                proc.kill()
                proc.wait()
                elapsed = time.monotonic() - start
                log_progress(f"{label}  TIMEOUT after {elapsed:.0f}s\n")
                log_status(f"{label} TIMEOUT (elapsed: {elapsed:.0f}s)\n")
                return False

            ready, _, _ = select.select([proc.stdout], [], [], 0.2)
            if ready:
                line = proc.stdout.readline()
                if line:
                    output_chunks.append(line)
                    log_progress(line)

            if proc.poll() is not None:
                rest = proc.stdout.read()
                if rest:
                    output_chunks.append(rest)
                    log_progress(rest)
                break

        elapsed = time.monotonic() - start
        output = "".join(output_chunks)
        exited_normally = "exited normally" in output
        no_stack = "No stack." in output
        return_code = proc.returncode if proc.returncode is not None else -1
        ok = (return_code == 0) or (return_code == 1 and exited_normally and no_stack)
        status = "PASS" if ok else f"FAIL (rc={return_code})"
        
        log_progress(f"{label}  {status}  elapsed={elapsed:.1f}s\n")
        log_status(f"{label} {status} (rc={return_code}, elapsed={elapsed:.1f}s)\n")
        return ok
    except subprocess.TimeoutExpired:
        elapsed = time.monotonic() - start
        log_progress(f"{label}  TIMEOUT after {elapsed:.0f}s\n")
        log_status(f"{label} TIMEOUT (elapsed: {elapsed:.0f}s)\n")
        return False

def main() -> int:
    global progress_file_path, status_file_path
    parser = argparse.ArgumentParser(description="Run lockfree soak tests with optional GDB harness.")
    parser.add_argument("--soak-exe", default=DEFAULT_EXE)
    parser.add_argument("--no-gdb", action="store_true")
    parser.add_argument("--runs", type=int, default=1)
    parser.add_argument("--test-duration", type=int, default=None)
    parser.add_argument("--allocator-soak-duration", type=int, default=None)
    parser.add_argument("--phase-base", type=int, default=None)
    parser.add_argument("--phase-range", type=int, default=None)
    parser.add_argument("--progress-file", default="/tmp/soak_progress.log")
    parser.add_argument("--status-file", default="/tmp/soak_status.log")
    parser.add_argument("--timeout", type=int, default=None)
    parser.add_argument("--group", action="append", dest="groups", default=[])
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()

    # Clear status file on start
    status_file_path = args.status_file
    if status_file_path:
        with open(status_file_path, "w") as f:
            f.write("=== Soak Test Status ===\n")
            
    progress_file_path = args.progress_file
    if progress_file_path:
        with open(progress_file_path, "w") as f:
            f.write("=== Soak Test Progress ===\n")

    exe = args.soak_exe
    if not os.path.isfile(exe):
        print(f"error: soak executable not found: {exe}", file=sys.stderr)
        return 1

    all_groups = discover_groups(exe)
    if not all_groups:
        print("No soak test groups discovered.", file=sys.stderr)
        return 1

    if args.list:
        for g in all_groups: print(f"  {g}")
        return 0

    target_groups = args.groups if args.groups else all_groups
    run_env = os.environ.copy()

    if args.allocator_soak_duration is not None:
        run_env["ALLOCATOR_SOAK_DURATION"] = str(args.allocator_soak_duration)
    if args.phase_base is not None:
        run_env["ALLOCATOR_SOAK_PHASE_BASE"] = str(args.phase_base)
    if args.phase_range is not None:
        run_env["ALLOCATOR_SOAK_PHASE_RANGE"] = str(args.phase_range)

    effective_timeout = args.timeout if args.timeout is not None else 600
    if args.allocator_soak_duration is not None:
        effective_timeout = max(effective_timeout, args.allocator_soak_duration + 120)

    log_progress(f"Using timeout={effective_timeout}s\n")
    
    total = 0
    passed = 0
    start_time = time.time()
    
    # Loop over runs or duration
    run_idx = 1
    keep_running = True
    while keep_running:
        for group in target_groups:
            cmd = build_direct_cmd(exe, group) if args.no_gdb else build_gdb_cmd(exe, group)
            total += 1
            if run_group(cmd, group, run_idx, effective_timeout, run_env):
                passed += 1

        if args.test_duration is not None:
            if time.time() - start_time >= args.test_duration:
                keep_running = False
        else:
            if run_idx >= args.runs:
                keep_running = False
        run_idx += 1

    summary = f"\n=== FINAL SUMMARY ===\nPassed {passed}/{total} runs.\n"
    log_progress(summary)
    log_status(summary)

    return 0 if passed == total else 1

if __name__ == "__main__":
    sys.exit(main())

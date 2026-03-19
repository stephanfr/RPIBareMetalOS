import subprocess
import sys

TEST_BINARY = './test/build/cpputest_stdlib_main.exe'
TEST_GROUP = 'LockfreeSingleBlockMemoryResourceTests'
TEST_NAME = 'SoakTest'


def parse_args(argv):
    iterations = 10
    mode = 'full'
    timeout_seconds = 180

    for arg in argv:
        if arg in ('full', '--full'):
            mode = 'full'
        elif arg in ('focused', '--focused'):
            mode = 'focused'
        elif arg.startswith('--timeout='):
            timeout_seconds = int(arg.split('=', 1)[1])
        else:
            iterations = int(arg)

    return iterations, mode, timeout_seconds


def build_test_command(mode):
    if mode == 'focused':
        return ['gdb', '-batch', '-ex', 'handle SIGUSR1 noprint nostop pass', '-ex', 'run', '--args', TEST_BINARY, '-g', TEST_GROUP, '-n', TEST_NAME]

    return ['gdb', '-batch', '-ex', 'handle SIGUSR1 noprint nostop pass', '-ex', 'run', '-ex', 'bt', '--args', TEST_BINARY, '-g', TEST_GROUP]


def run_soak_test(iterations, mode, timeout_seconds):
    command = build_test_command(mode)
    print(f"Starting {mode} soak test for {iterations} iterations...")
    print(f"Command: {' '.join(command)}")
    print(f"Timeout per run: {timeout_seconds}s")

    for i in range(iterations):
        print(f"=== Run {i+1}/{iterations} ===", flush=True)
        try:
            result = subprocess.run(
                command,
                text=True,
                timeout=timeout_seconds,
            )
            if result.returncode != 0:
                print(f"FAILED (ret {result.returncode})")
                return False
            print("OK", flush=True)
        except subprocess.TimeoutExpired:
            print("TIMEOUT (possible livelock/deadlock)")
            return False
            
    print("All runs successful!")
    return True

if __name__ == "__main__":
    runs, mode, timeout_seconds = parse_args(sys.argv[1:])
    success = run_soak_test(runs, mode, timeout_seconds)
    sys.exit(0 if success else 1)

#!/usr/bin/env python3
"""Runs every tests/**/*.lix file and checks its output.

Expectations are written as comments in the test file:

    print 1 + 2;            // expect: 3
    int x = "a";            // expect error: Cannot convert 'string' to 'int'
    print v[10];            // expect runtime error: out of range

'expect:' lines must match stdout line by line, in order. 'expect error:' means
compilation must fail and stderr must contain the text. 'expect runtime error:' means
the program must fail at runtime and stderr must contain the text.

A file whose first line is '// mode: repl' is fed to the REPL on stdin instead, one line
per entry. Then errors do not end the session: the REPL must exit with 0, every expected
error text must appear in stderr, and stdout must match the 'expect:' lines.

Usage: run_tests.py path/to/lix [--gc-stress] [filter]
"""

import pathlib
import re
import subprocess
import sys

EXPECT = re.compile(r"// expect: ?(.*)$")
EXPECT_ERROR = re.compile(r"// expect error: ?(.*)$")
EXPECT_RUNTIME_ERROR = re.compile(r"// expect runtime error: ?(.*)$")


def run_test(lix, path, extra_args):
    expected_output = []
    expected_errors = []
    expected_runtime_error = None
    source = path.read_text()
    repl_mode = source.startswith("// mode: repl")
    for line in source.splitlines():
        if m := EXPECT_ERROR.search(line):
            expected_errors.append(m.group(1).strip())
        elif m := EXPECT_RUNTIME_ERROR.search(line):
            expected_runtime_error = m.group(1).strip()
            if repl_mode:
                expected_errors.append(expected_runtime_error)
        elif m := EXPECT.search(line):
            expected_output.append(m.group(1))

    try:
        if repl_mode:
            result = subprocess.run([lix, *extra_args], input=source, capture_output=True, text=True, timeout=60)
        else:
            result = subprocess.run([lix, *extra_args, str(path)], capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        return ["timed out after 60 seconds"]
    actual_output = result.stdout.splitlines()
    failures = []

    if repl_mode:
        if result.returncode != 0:
            failures.append(f"REPL exited with {result.returncode}")
        for error in expected_errors:
            if error not in result.stderr:
                failures.append(f"missing error: {error}")
        expected_errors = []
        expected_runtime_error = None

    if expected_errors:
        if result.returncode != 65:
            failures.append(f"expected compile error (exit 65), got exit {result.returncode}")
        for error in expected_errors:
            if error not in result.stderr:
                failures.append(f"missing compile error: {error}")
    elif expected_runtime_error is not None:
        if result.returncode != 70:
            failures.append(f"expected runtime error (exit 70), got exit {result.returncode}")
        if expected_runtime_error not in result.stderr:
            failures.append(f"missing runtime error: {expected_runtime_error}")
    elif result.returncode != 0:
        failures.append(f"exit code {result.returncode}")

    if not expected_errors and actual_output != expected_output:
        failures.append("output mismatch")
        for i in range(max(len(actual_output), len(expected_output))):
            want = expected_output[i] if i < len(expected_output) else "<nothing>"
            got = actual_output[i] if i < len(actual_output) else "<nothing>"
            marker = "  " if want == got else "!!"
            failures.append(f"  {marker} expected {want!r:30} got {got!r}")

    if failures and result.stderr:
        failures.append("stderr:\n" + result.stderr.rstrip())
    return failures


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    lix = sys.argv[1]
    extra_args = [a for a in sys.argv[2:] if a.startswith("--")]
    filters = [a for a in sys.argv[2:] if not a.startswith("--")]

    root = pathlib.Path(__file__).parent
    tests = sorted(p for p in root.rglob("*.lix") if not filters or any(f in str(p) for f in filters))
    failed = 0
    for path in tests:
        failures = run_test(lix, path, extra_args)
        if failures:
            failed += 1
            print(f"FAIL {path.relative_to(root)}")
            for failure in failures:
                print("     " + failure.replace("\n", "\n     "))
    print(f"{len(tests) - failed} passed, {failed} failed ({len(tests)} total)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

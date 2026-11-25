"""Run the three native smoke suites and report observed ChucK check counts."""
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
SUITES = ("raster/test_raster.py", "audio/test_audio.py", "test_multimedia.py")
MARKER = re.compile(r"SMOKE_COUNTS (\d+) (\d+) (\d+)")
START = re.compile(r"SMOKE_EXPECTED (\d+)")

def main():
    expected = 0
    reported = passed = failed = missing = extra = completed = 0
    status = "FAIL"
    try:
        for name in SUITES:
            print(f"Running {name}", flush=True)
            with subprocess.Popen([sys.executable, "-u", str(HERE / name)],
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  text=True, errors="replace") as proc:
                for line in proc.stdout:
                    start = START.fullmatch(line.strip())
                    if start:
                        expected += int(start.group(1))
                        continue
                    match = MARKER.fullmatch(line.strip())
                    if match:
                        e, p, f = map(int, match.groups())
                        reported += e
                        passed += p
                        failed += f
                        missing += max(0, e - p - f)
                        extra += max(0, p + f - e)
                    else:
                        print(line, end="", flush=True)
                code = proc.wait()
            if code:
                print(f"Suite failed: {name} (exit {code})", flush=True)
                return 1
            completed += 1
        if expected > 0 and reported == expected and passed == expected and not failed and not missing and not extra:
            status = "PASS"
            return 0
        return 1
    finally:
        unreported = max(0, expected - reported)
        print(f"MULTIMEDIA SUMMARY | expected={expected} | PASS={passed} | FAIL={failed} | "
              f"missing={missing} | unreported={unreported} | extra={extra} | "
              f"suites={completed}/{len(SUITES)} | status={status}", flush=True)

if __name__ == "__main__":
    sys.exit(main())

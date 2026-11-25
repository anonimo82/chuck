"""Run the three native smoke suites and report observed ChucK check counts."""
import ast
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
SUITES = ("raster/test_raster.py", "audio/test_audio.py", "test_multimedia.py")
MARKER = re.compile(r"SMOKE_COUNTS (\d+) (\d+) (\d+)")

def planned_checks(path):
    tree = ast.parse(path.read_text(encoding="utf-8"))
    if path.name == "test_multimedia.py":
        calls = [n for n in ast.walk(tree)
                 if isinstance(n, ast.Call) and isinstance(n.func, ast.Name)
                 and n.func.id == "run"]
        return sum(ast.literal_eval(n.args[2]) for n in calls)
    markers = [n for n in ast.walk(tree)
               if isinstance(n, ast.JoinedStr) and n.values
               and isinstance(n.values[0], ast.Constant)
               and n.values[0].value == "SMOKE_COUNTS "]
    if len(markers) != 1:
        raise ValueError(f"Expected one count marker in {path}")
    return ast.literal_eval(markers[0].values[1].value)

def main():
    expected = sum(planned_checks(HERE / name) for name in SUITES)
    reported = passed = failed = missing = extra = completed = 0
    status = "FAIL"
    try:
        for name in SUITES:
            print(f"Running {name}", flush=True)
            with subprocess.Popen([sys.executable, "-u", str(HERE / name)],
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  text=True, errors="replace") as proc:
                for line in proc.stdout:
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
        if reported == expected and passed == expected and not failed and not missing and not extra:
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

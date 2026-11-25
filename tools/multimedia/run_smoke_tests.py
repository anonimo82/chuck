"""Run native smoke suites, with conservative incremental selection."""
import argparse
import json
import os
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SUITES = ("raster/test_raster.py", "audio/test_audio.py", "test_multimedia.py")
GROUPS = ("raster", "audio", "legacy", "text-core", "text-recent")
MARKER = re.compile(r"SMOKE_COUNTS (\d+) (\d+) (\d+)")
START = re.compile(r"SMOKE_EXPECTED (\d+)")
PREFIX = "tools/multimedia/"
# Explicit migration anchor: the last reported 3153-check full baseline.
MIGRATION = "92933d3894d2c844178e04e2540e1e4524750c9f"

def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args], text=True).strip()

def read_state(path):
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}

def save_state(path, state):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(".tmp")
    tmp.write_text(json.dumps(state, indent=2), encoding="utf-8")
    tmp.replace(path)

def select(base, head):
    if not base:
        return list(GROUPS)
    try:
        if subprocess.run(["git", "-C", str(ROOT), "merge-base", "--is-ancestor", base, head],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
            return list(GROUPS)
        paths = git("diff", "--name-only", base, head).splitlines()
        if not paths:
            return ["text-recent"]
        selected = set()
        for path in paths:
            if path.endswith(".md"):
                continue
            if path.startswith(PREFIX + "text/"):
                selected.update(("text-core", "text-recent"))
            elif path == PREFIX + "test_multimedia.py":
                if base == MIGRATION:
                    # The pre-group baseline is known; only the new text cycle
                    # plus its directly affected text regressions need running.
                    selected.update(("text-core", "text-recent"))
                else:
                    old = git("show", base + ":" + path)
                    new = git("show", head + ":" + path)
                    def split(source):
                        a = source.index("\ndef legacy():")
                        b = source.index("\ndef text_core():")
                        c = source.index("\ndef text_recent():")
                        d = source.index('\nif __name__ == "__main__":')
                        return (source[:a] + source[d:], source[a:b], source[b:c], source[c:d])
                    before, after = split(old), split(new)
                    if before[0] != after[0]:
                        return list(GROUPS)
                    for group, x, y in zip(GROUPS[2:], before[1:], after[1:]):
                        if x != y:
                            selected.add(group)
            elif base == MIGRATION and path in (PREFIX + "run_smoke_tests.py", PREFIX + "build-and-test.ps1"):
                # Migration changes only the runner, with full mode retained.
                continue
            else:
                # Shared native code, other modules or unknown changes:
                # do not guess their cross-module dependencies.
                return list(GROUPS)
        return [g for g in GROUPS if g in selected] or ["text-recent"]
    except (OSError, subprocess.SubprocessError, ValueError):
        return list(GROUPS)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("Full", "Incremental", "Failed"), default="Full")
    parser.add_argument("--state", type=pathlib.Path, default=ROOT / "build" / "multimedia-smoke-state.json")
    parser.add_argument("--since", help="Explicit previously validated commit (Incremental only)")
    parser.add_argument("--group", choices=GROUPS, action="append", help="Explicit diagnostic group; does not advance baseline")
    args = parser.parse_args()
    if args.since and (args.mode != "Incremental" or args.group):
        parser.error("--since requires Incremental and no explicit group")
    if args.group and args.mode == "Failed":
        parser.error("--group cannot be combined with Failed")
    state = read_state(args.state)
    try:
        head = git("rev-parse", "HEAD")
        dirty = bool(git("status", "--porcelain", "--untracked-files=no"))
    except (OSError, subprocess.SubprocessError):
        head, dirty = "", True
    if args.group:
        groups = [g for g in GROUPS if g in args.group]
    elif args.mode == "Failed":
        if not state.get("pending"):
            parser.error("No failed or interrupted run recorded")
        groups = state["pending"]
        if (not isinstance(groups, list) or any(g not in GROUPS for g in groups)
                or dirty or state.get("attempt_dirty", True)):
            groups = list(GROUPS)
        elif state.get("attempt") != head:
            changed = select(state.get("attempt"), head)
            groups = [g for g in GROUPS if g in groups or g in changed]
    elif args.mode == "Incremental" and not dirty:
        groups = select(args.since or state.get("validated"), head)
    else:
        groups = list(GROUPS)
    plan = []
    for group, name in zip(GROUPS[:2], SUITES[:2]):
        if group in groups:
            plan.append((name, [group]))
    native = [g for g in GROUPS[2:] if g in groups]
    if native:
        plan.append((SUITES[2], native))
    print("SMOKE SELECTION | mode=" + args.mode + " | groups=" + ",".join(groups), flush=True)
    # Record the entire attempted plan before launching it. Failed retries
    # repeat that plan, including fixture producers and Python assertions.
    state.update(attempt=head, attempt_dirty=dirty, pending=groups, diagnostic=bool(args.group) or (args.mode == "Failed" and state.get("diagnostic", False)))
    save_state(args.state, state)
    expected = 0
    reported = passed = failed = missing = extra = completed = 0
    status = "FAIL"
    try:
        for name, chosen in plan:
            print(f"Running {name} ({','.join(chosen)})", flush=True)
            env = dict(os.environ, MULTIMEDIA_TEST_GROUPS=",".join(chosen))
            with subprocess.Popen([sys.executable, "-u", str(HERE / name)],
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  text=True, errors="replace", env=env) as proc:
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
            if head and not dirty and not state["diagnostic"]:
                if git("rev-parse", "HEAD") == head and not git("status", "--porcelain", "--untracked-files=no"):
                    state["validated"] = head
                    if groups == list(GROUPS):
                        state["full_validated"] = head
            state["pending"] = []
            save_state(args.state, state)
            status = "PASS"
            return 0
        return 1
    finally:
        unreported = max(0, expected - reported)
        print(f"MULTIMEDIA SUMMARY | expected={expected} | PASS={passed} | FAIL={failed} | "
              f"missing={missing} | unreported={unreported} | extra={extra} | "
              f"suites={completed}/{len(plan)} | status={status}", flush=True)

if __name__ == "__main__":
    sys.exit(main())

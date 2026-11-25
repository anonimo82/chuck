"""Run native smoke suites using versioned global/local manifests."""
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
GROUPS = ("raster", "audio", "legacy", "text-core", "text-recent", "text-page-breaks", "text-page-furniture", "text-page-numbers", "text-split-paragraphs", "text-split-tables", "text-frame-foundation", "text-frame-flow")
MARKER = re.compile(r"SMOKE_COUNTS (\d+) (\d+) (\d+)")
START = re.compile(r"SMOKE_EXPECTED (\d+)")

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

def load_manifest(path):
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or data.get("version") != 1:
        raise ValueError(f"Unsupported test manifest: {path}")
    groups = data.get("groups")
    if (not isinstance(groups, list) or any(not isinstance(g, str) for g in groups)
            or len(groups) != len(set(groups)) or any(g not in GROUPS for g in groups)):
        raise ValueError(f"Invalid groups in {path}")
    return groups

def manifests():
    global_groups = load_manifest(HERE / "global-tests.json")
    local_groups = load_manifest(HERE / "local-tests.json")
    if set(global_groups) != set(GROUPS):
        raise ValueError("Global manifest must cover every registered group")
    if not set(local_groups).issubset(global_groups):
        raise ValueError("Local manifest must be a subset of global")
    return global_groups, local_groups

def select_groups(mode, global_groups, local_groups, explicit, pending):
    if explicit:
        return [g for g in global_groups if g in explicit]
    if mode == "Full":
        return global_groups
    if mode in ("Local", "Incremental"):
        return [g for g in global_groups if g in local_groups]
    if mode == "Failed":
        if (not isinstance(pending, list) or not pending
                or any(g not in global_groups for g in pending)):
            raise ValueError("No valid failed or interrupted selection recorded")
        return [g for g in global_groups if g in pending or g in local_groups]
    raise ValueError("Unknown validation mode")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("Full", "Local", "Incremental", "Failed"), default="Full")
    parser.add_argument("--state", type=pathlib.Path, default=ROOT / "build" / "multimedia-smoke-state.json")
    parser.add_argument("--since", help="Legacy option; uses conservative full coverage when supplied")
    parser.add_argument("--group", choices=GROUPS, action="append", help="Explicit diagnostic group; does not advance baseline")
    parser.add_argument("--list", action="store_true", help="Validate manifests and print selection without native execution")
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
    try:
        global_groups, local_groups = manifests()
        mode = "Full" if args.since else args.mode
        groups = select_groups(mode, global_groups, local_groups, args.group, state.get("pending"))
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print("SMOKE SELECTION | mode=" + args.mode + " | groups=" + ",".join(groups), flush=True)
    if args.list:
        print("Global groups: " + ",".join(global_groups))
        print("Local groups: " + ",".join(local_groups))
        return 0
    if not groups:
        print("No local tests scheduled; nothing executed.", flush=True)
        print("MULTIMEDIA SUMMARY | expected=0 | PASS=0 | FAIL=0 | missing=0 | unreported=0 | extra=0 | suites=0/0 | status=SKIP", flush=True)
        return 0
    plan = []
    for group, name in zip(GROUPS[:2], SUITES[:2]):
        if group in groups:
            plan.append((name, [group]))
    native = [g for g in GROUPS[2:] if g in groups]
    if native:
        plan.append((SUITES[2], native))
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
                    if set(groups) == set(global_groups):
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

"""Smoke test; run after compiling ChucK and AudioIO."""
import os
import pathlib
import subprocess
import tempfile
import wave
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
with tempfile.TemporaryDirectory() as d:
    p = pathlib.Path(d)
    with wave.open(str(p / "input.wav"), "wb") as w:
        w.setparams((1, 2, 8000, 0, "NONE", "not compressed"))
        w.writeframes(b"\x00\x00" * 8000)
    (p / "test.ck").write_text("""fun void check(int ok) { if(ok) <<< "PASS" >>>; else <<< "FAIL" >>>; }
check(AudioIO.sampleRate("input.wav") == 8000);
check(AudioIO.channels("input.wav") == 1);
check(AudioIO.seconds("input.wav") == 1.0);
check(AudioIO.convert("input.wav", "output.flac") == 1);
check(AudioIO.sampleRate("output.flac") == 8000);
check(AudioIO.convert("input.wav", "output.flac") == 0);
check(AudioIO.channels("missing.wav") == -1);
""")
    r = subprocess.run([os.environ.get("CHUCK_EXE", str(ROOT / "src/chuck")), "--silent", "--chugin-path:" + os.environ.get("CHUGIN_DIR", str(HERE)), "test.ck"], cwd=p, capture_output=True, text=True, timeout=30)
    log = r.stdout + r.stderr
    print(log)
    if r.returncode or "FAIL" in log or log.count("PASS") != 7:
        raise SystemExit("AudioIO smoke test failed")
    if (p / "output.flac").read_bytes()[:4] != b"fLaC":
        raise SystemExit("Invalid FLAC signature")

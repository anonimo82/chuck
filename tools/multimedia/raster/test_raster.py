"""Run with Python 3 after building ChucK and RasterIO."""
from pathlib import Path
import os
import struct
import subprocess
import tempfile
import time
import re
import zlib

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]

def chunk(kind, payload):
    return (struct.pack(">I", len(payload)) + kind + payload
            + struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff))

def fixture(path):
    raw = (b"\x00" + b"\xff\x00\x00" * 2) * 3
    path.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 3, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))

def count_checks(log):
    # Multi-value ChucK output may be unquoted; count only result tokens.
    return (len(re.findall(r'(?m)^[ \t]*"?PASS"?(?=[ \t,]|$)', log)),
            len(re.findall(r'(?m)^[ \t]*"?FAIL"?(?=[ \t,]|$)', log)))

def main():
    try:
        timeout = int(os.environ.get("MULTIMEDIA_RASTER_TIMEOUT", "180"))
        if not 1 <= timeout <= 900:
            raise ValueError
    except ValueError:
        raise SystemExit("MULTIMEDIA_RASTER_TIMEOUT must be an integer from 1 to 900 seconds")
    with tempfile.TemporaryDirectory() as directory:
        tmp = Path(directory)
        fixture(tmp / "input.png")
        script = tmp / "test.ck"
        script.write_text("""@import "RasterIO";
<<< "RASTER_STAGE", "script entered" >>>;
fun void check(int ok, string name) {
    if(!ok) <<< "FAIL", name >>>;
    else <<< "PASS", name >>>;
}
<<< "RASTER_STAGE", "width" >>>;
check(RasterIO.width("input.png") == 2, "width");
<<< "RASTER_STAGE", "height" >>>;
check(RasterIO.height("input.png") == 3, "height");
<<< "RASTER_STAGE", "convert" >>>;
check(RasterIO.convert("input.png", "output.bmp") == 1, "convert");
<<< "RASTER_STAGE", "output width" >>>;
check(RasterIO.width("output.bmp") == 2, "output width");
<<< "RASTER_STAGE", "output height" >>>;
check(RasterIO.height("output.bmp") == 3, "output height");
<<< "RASTER_STAGE", "no overwrite" >>>;
check(RasterIO.convert("input.png", "output.bmp") == 0, "no overwrite");
<<< "RASTER_STAGE", "missing input" >>>;
check(RasterIO.width("missing.png") == -1, "missing input");
<<< "RASTER_STAGE", "unsupported output" >>>;
check(RasterIO.convert("input.png", "output.unknown") == 0, "unsupported output");
""")
        print("SMOKE_EXPECTED 8", flush=True)
        command = [os.environ.get("CHUCK_EXE", str(ROOT / "src/chuck")), "--silent",
                   "--chugin-path:" + os.environ.get("CHUGIN_DIR", str(HERE)), script.name]
        env = dict(os.environ, CHUCK_RASTER_TRACE="1")
        print("Raster thread settings: MAGICK_THREAD_LIMIT=" + env.get("MAGICK_THREAD_LIMIT", "<unset>") +
              " OMP_NUM_THREADS=" + env.get("OMP_NUM_THREADS", "<unset>"), flush=True)
        print(f"Raster smoke timeout: {timeout} seconds", flush=True)
        started = time.monotonic()
        try:
            result = subprocess.run(command, cwd=tmp, env=env, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, errors="replace", timeout=timeout)
        except subprocess.TimeoutExpired as exc:
            partial = exc.stdout or ""
            if isinstance(partial, bytes):
                partial = partial.decode("utf-8", errors="replace")
            print(partial, flush=True)
            stages = [line for line in partial.splitlines() if "RASTER_STAGE" in line]
            print(f"RasterIO timeout after {timeout} seconds; last stage: " +
                  (stages[-1] if stages else "no script stage reached"), flush=True)
            native = [line for line in partial.splitlines() if line.startswith("[RasterIO native]") or line.startswith("[RasterIO convert]")]
            print("Last native stage: " + (native[-1] if native else "not reached"), flush=True)
            passed, failed = count_checks(partial)
            print(f"SMOKE_COUNTS 8 {passed} {failed}", flush=True)
            raise SystemExit("RasterIO smoke test timed out")
        print(f"Raster smoke elapsed: {time.monotonic() - started:.3f} seconds", flush=True)
        log = result.stdout
        print(log)
        passed, failed = count_checks(log)
        print(f"SMOKE_COUNTS 8 {passed} {failed}", flush=True)
        if result.returncode or failed or passed != 8:
            raise SystemExit("RasterIO smoke test failed")
        header = (tmp / "output.bmp").read_bytes()[:16]
        if header[:2] != b"BM":
            raise SystemExit(f"Invalid BMP signature: {header.hex()}")
        print("BMP signature verified")

if __name__ == "__main__":
    main()

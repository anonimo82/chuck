"""Run with Python 3 after building ChucK and RasterIO."""
from pathlib import Path
import struct
import subprocess
import tempfile
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

def main():
    with tempfile.TemporaryDirectory() as directory:
        tmp = Path(directory)
        fixture(tmp / "input.png")
        script = tmp / "test.ck"
        script.write_text("""fun void check(int ok, string name) {
    if(!ok) <<< "FAIL", name >>>;
    else <<< "PASS", name >>>;
}
check(RasterIO.width("input.png") == 2, "width");
check(RasterIO.height("input.png") == 3, "height");
check(RasterIO.convert("input.png", "output.bmp") == 1, "convert");
check(RasterIO.width("output.bmp") == 2, "output width");
check(RasterIO.height("output.bmp") == 3, "output height");
check(RasterIO.convert("input.png", "output.bmp") == 0, "no overwrite");
check(RasterIO.width("missing.png") == -1, "missing input");
check(RasterIO.convert("input.png", "output.unknown") == 0, "unsupported output");
""")
        result = subprocess.run(
            [str(ROOT / "src/chuck"), "--silent", "--chugin-path:" + str(HERE), str(script)],
            cwd=tmp, capture_output=True, text=True, timeout=30)
        log = result.stdout + result.stderr
        print(log)
        if result.returncode or "FAIL" in log or log.count("PASS") != 8:
            raise SystemExit("RasterIO smoke test failed")
        if (tmp / "output.bmp").read_bytes()[:2] != b"BM":
            raise SystemExit("Invalid BMP signature")

if __name__ == "__main__":
    main()

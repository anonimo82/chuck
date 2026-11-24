"""Run with Python 3 and FFmpeg CLI after building ChucK and all Chugins."""
import os
import pathlib
import subprocess
import tempfile
HERE = pathlib.Path(__file__).resolve().parent
CHUCK = HERE.parents[1] / "src/chuck"

def run(module, script, expected, directory):
    path = directory / "test.ck"
    name = {"vector": "VectorIO", "model": "ModelIO", "midi": "MidiFileIO", "video": "VideoIO"}[module]
    path.write_text(f'@import "{name}";\n' + 'fun void check(int ok) { if(ok) <<< "PASS" >>>; else <<< "FAIL" >>>; }\n' + script)
    r = subprocess.run([os.environ.get("CHUCK_EXE", str(CHUCK)), "--silent", "--chugin-path:" + os.environ.get("CHUGIN_DIR", str(HERE / module)), path.name], cwd=directory, capture_output=True, text=True, timeout=60)
    log = r.stdout + r.stderr
    print(module, log)
    if r.returncode or "FAIL" in log or log.count("PASS") != expected:
        raise RuntimeError(module + " smoke test failed")

with tempfile.TemporaryDirectory() as d:
    p = pathlib.Path(d)
    run("vector", """
check(VectorIO.save("<svg xmlns='http://www.w3.org/2000/svg' width='4' height='4'><rect width='4' height='4' fill='red'/></svg>", "test.svg"));
check(VectorIO.valid("test.svg"));
check(VectorIO.render("test.svg", "test.png", 4, 4));
check(!VectorIO.render("test.svg", "test.png", 4, 4));
""", 4, p)
    assert (p / "test.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
    (p / "triangle.obj").write_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n")
    run("model", """
check(ModelIO.valid("triangle.obj"));
check(ModelIO.meshes("triangle.obj") == 1);
check(ModelIO.vertices("triangle.obj") == 3);
check(ModelIO.convert("triangle.obj", "converted/triangle.stl"));
check(ModelIO.valid("converted/triangle.stl"));
check(!ModelIO.convert("triangle.obj", "converted/triangle.stl"));
""", 6, p)
    run("midi", """
check(MidiFileIO.writeNote("note.mid", 60, 100, 480));
check(MidiFileIO.tracks("note.mid") == 1);
check(MidiFileIO.events("note.mid", 0) >= 2);
check(MidiFileIO.copy("note.mid", "copy.mid"));
check(!MidiFileIO.copy("note.mid", "copy.mid"));
""", 5, p)
    assert (p / "copy.mid").read_bytes().startswith(b"MThd")
    subprocess.run(["ffmpeg", "-nostdin", "-v", "error", "-f", "lavfi", "-i", "color=c=red:s=16x16:r=2:d=1", "-c:v", "ffv1", str(p / "clip.mkv")], check=True, timeout=30)
    run("video", """
check(VideoIO.width("clip.mkv") == 16);
check(VideoIO.height("clip.mkv") == 16);
check(VideoIO.fps("clip.mkv") == 2.0);
check(VideoIO.firstFrame("clip.mkv", "frame.ppm"));
check(!VideoIO.firstFrame("clip.mkv", "frame.ppm"));
check(VideoIO.remux("clip.mkv", "copy.mkv"));
check(VideoIO.width("copy.mkv") == 16);
check(VideoIO.height("copy.mkv") == 16);
check(!VideoIO.remux("clip.mkv", "copy.mkv"));
check(!VideoIO.remux("clip.mkv", "bad.unknown"));
check(!VideoIO.remux("clip.mkv", "bad.webm"));
""", 11, p)
    assert not (p / "bad.webm").exists()
    assert not (p / "bad.unknown").exists()
    for name in ("clip.mkv", "copy.mkv"):
        subprocess.run(["ffmpeg", "-nostdin", "-v", "error", "-i", str(p / name), "-f", "null", "-"], check=True, timeout=30)
    def decoded(name):
        return subprocess.check_output(["ffmpeg", "-nostdin", "-v", "error", "-i", str(p / name), "-map", "0:v:0", "-pix_fmt", "rgb24", "-f", "rawvideo", "-"], timeout=30)
    assert decoded("clip.mkv") == decoded("copy.mkv")
    assert (p / "frame.ppm").read_bytes().startswith(b"P6\n16 16\n255\n")

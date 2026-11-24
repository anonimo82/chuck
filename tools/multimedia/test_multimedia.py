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

    # Two distinct RGB frames, and one wrong-sized frame for error cleanup.
    red = bytes((255, 0, 0)) * (16 * 16)
    blue = bytes((0, 0, 255)) * (16 * 16)
    for i, pixels in enumerate((red, blue)):
        (p / f"seq{i:06d}.ppm").write_bytes(b"P6\n16 16\n255\n" + pixels)
    (p / "seq000002.ppm").write_bytes(b"P6\n1 1\n255\n" + b"\x00\x00\x00")
    run("video", """
check(VideoIO.encodeSequence("seq", "generated.mkv", 2, 2));
check(VideoIO.width("generated.mkv") == 16);
check(VideoIO.height("generated.mkv") == 16);
check(VideoIO.fps("generated.mkv") == 2.0);
check(!VideoIO.encodeSequence("seq", "generated.mkv", 2, 2));
check(!VideoIO.encodeSequence("seq", "invalid.mkv", 3, 2));
check(!VideoIO.encodeSequence("missing", "missing.mkv", 1, 2));
check(!VideoIO.encodeSequence("seq", "zero.mkv", 0, 2));
check(!VideoIO.encodeSequence("seq", "bad-rate.mkv", 2, 0));
check(!VideoIO.encodeSequence("seq", "wrong.mp4", 2, 2));
""", 10, p)
    for name in ("invalid.mkv", "missing.mkv", "zero.mkv", "bad-rate.mkv", "wrong.mp4"):
        assert not (p / name).exists(), name
    assert decoded("generated.mkv") == red + blue

    # Optional encoders: availability is determined by the linked FFmpeg build.
    for mode, ext, quality, codec in (("h264", "mp4", 23, "h264"), ("vp9", "webm", 32, "vp9")):
        target = "compressed." + ext
        run("video", f"""
if(VideoIO.encoderAvailable("{mode}")) {{
    check(VideoIO.encodeSequence("seq", "{target}", 2, 2, "{mode}", {quality}));
    check(VideoIO.width("{target}") == 16);
    check(VideoIO.height("{target}") == 16);
    check(!VideoIO.encodeSequence("seq", "{target}", 2, 2, "{mode}", {quality}));
}} else {{
    <<< "SKIP encoder {mode} unavailable" >>>;
    check(!VideoIO.encodeSequence("seq", "{target}", 2, 2, "{mode}", {quality}));
    check(!VideoIO.encoderAvailable("{mode}"));
    check(!VideoIO.encodeSequence("seq", "wrong.bin", 2, 2, "{mode}", {quality}));
    check(!VideoIO.encodeSequence("seq", "{target}", 2, 2, "{mode}", -1));
}}
""", 4, p)
        if (p / target).exists():
            actual = subprocess.check_output(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=codec_name", "-of", "default=nw=1:nk=1", str(p / target)], timeout=30).decode().strip()
            assert actual == codec, actual
            pixels = decoded(target)
            assert len(pixels) == len(red + blue)
            # Lossy RGB conversion: check color dominance and frame order.
            n = len(red)
            assert pixels[0] > 200 and pixels[2] < 50
            assert pixels[n] < 50 and pixels[n + 2] > 200
    run("video", """
check(!VideoIO.encoderAvailable("unknown"));
check(!VideoIO.encodeSequence("seq", "bad-codec.mkv", 2, 2, "unknown", 20));
check(!VideoIO.encodeSequence("seq", "bad-quality.mp4", 2, 2, "h264", 52));
check(!VideoIO.encodeSequence("seq", "bad-container.webm", 2, 2, "h264", 23));
""", 4, p)
    for name in ("bad-codec.mkv", "bad-quality.mp4", "bad-container.webm"):
        assert not (p / name).exists(), name

    # Independent PCM fixture, one second, matching the two-frame video.
    import wave
    pcm = b"\x00\x10\x00\xf0" * 4000
    with wave.open(str(p / "sound.wav"), "wb") as wav:
        wav.setparams((1, 2, 8000, 0, "NONE", "not compressed"))
        wav.writeframes(pcm)
    run("video", """
check(VideoIO.muxAudio("generated.mkv", "sound.wav", "audiovisual.mkv"));
check(VideoIO.width("audiovisual.mkv") == 16);
check(VideoIO.height("audiovisual.mkv") == 16);
check(!VideoIO.muxAudio("generated.mkv", "sound.wav", "audiovisual.mkv"));
check(!VideoIO.muxAudio("generated.mkv", "missing.wav", "missing-audio.mkv"));
check(!VideoIO.muxAudio("generated.mkv", "generated.mkv", "no-audio.mkv"));
check(!VideoIO.muxAudio("sound.wav", "sound.wav", "no-video.mkv"));
check(!VideoIO.muxAudio("generated.mkv", "sound.wav", "unsupported.bin"));
""", 8, p)
    for name in ("missing-audio.mkv", "no-audio.mkv", "no-video.mkv", "unsupported.bin"):
        assert not (p / name).exists(), name
    assert decoded("audiovisual.mkv") == red + blue
    audio_bytes = subprocess.check_output(["ffmpeg", "-nostdin", "-v", "error", "-i", str(p / "audiovisual.mkv"), "-map", "0:a:0", "-c:a", "pcm_s16le", "-f", "s16le", "-"], timeout=30)
    assert audio_bytes == pcm

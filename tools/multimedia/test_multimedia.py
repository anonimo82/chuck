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

    run("video", """
check(VideoIO.transcode("generated.mkv", "resized.mkv", 32, 24, "ffv1", 0));
check(VideoIO.width("resized.mkv") == 32);
check(VideoIO.height("resized.mkv") == 24);
check(!VideoIO.transcode("generated.mkv", "resized.mkv", 32, 24, "ffv1", 0));
check(VideoIO.transcode("audiovisual.mkv", "preserved-audio.mkv", 32, 24, "ffv1", 0));
check(!VideoIO.transcode("generated.mkv", "invalid-size.mkv", 0, 24, "ffv1", 0));
check(!VideoIO.transcode("generated.mkv", "invalid-codec.mkv", 32, 24, "unknown", 0));
check(!VideoIO.transcode("missing.mkv", "missing-source.mkv", 32, 24, "ffv1", 0));
""", 8, p)
    assert decoded("resized.mkv") == bytes((255, 0, 0)) * (32 * 24) + bytes((0, 0, 255)) * (32 * 24)
    for name in ("invalid-size.mkv", "invalid-codec.mkv", "missing-source.mkv"):
        assert not (p / name).exists(), name
    for mode, ext, quality in (("h264", "mp4", 23), ("vp9", "webm", 32)):
        name = "transcoded." + ext
        run("video", f"""
if(VideoIO.encoderAvailable("{mode}")) {{
    check(VideoIO.transcode("generated.mkv", "{name}", 32, 24, "{mode}", {quality}));
    check(VideoIO.width("{name}") == 32);
    check(VideoIO.height("{name}") == 24);
}} else {{
    <<< "SKIP encoder {mode} unavailable" >>>;
    check(!VideoIO.transcode("generated.mkv", "{name}", 32, 24, "{mode}", {quality}));
    check(!VideoIO.encoderAvailable("{mode}"));
    check(!VideoIO.transcode("generated.mkv", "wrong.bin", 32, 24, "{mode}", {quality}));
}}
""", 3, p)
        if (p / name).exists():
            rgb = decoded(name)
            assert len(rgb) == 32 * 24 * 3 * 2
            assert rgb[0] > 200 and rgb[2] < 50
            assert rgb[32 * 24 * 3 + 2] > 200

    # Frame extraction: exact pixels, range selection and rollback on collision.
    (p / "collision000001.ppm").write_bytes(b"sentinel")
    run("video", """
check(VideoIO.extractSequence("generated.mkv", "extracted", 0, 10) == 2);
check(VideoIO.extractSequence("generated.mkv", "second", 1, 1) == 1);
check(VideoIO.extractSequence("generated.mkv", "beyond", 2, 1) == 0);
check(VideoIO.extractSequence("generated.mkv", "extracted", 0, 2) == -1);
check(VideoIO.extractSequence("generated.mkv", "collision", 0, 2) == -1);
check(VideoIO.extractSequence("missing.mkv", "missing-frame", 0, 1) == -1);
check(VideoIO.extractSequence("generated.mkv", "invalid-frame", -1, 1) == -1);
check(VideoIO.extractSequence("generated.mkv", "invalid-frame", 0, 0) == -1);
check(VideoIO.encodeSequence("extracted", "roundtrip.mkv", 2, 2));
""", 9, p)
    header = b"P6\n16 16\n255\n"
    assert (p / "extracted000000.ppm").read_bytes() == header + red
    assert (p / "extracted000001.ppm").read_bytes() == header + blue
    assert (p / "second000000.ppm").read_bytes() == header + blue
    assert not (p / "extracted000002.ppm").exists()
    assert not (p / "beyond000000.ppm").exists()
    assert not (p / "collision000000.ppm").exists()
    assert (p / "collision000001.ppm").read_bytes() == b"sentinel"
    assert not (p / "missing-frame000000.ppm").exists()
    assert not (p / "invalid-frame000000.ppm").exists()
    assert decoded("roundtrip.mkv") == red + blue

    # Preserve two independent audio tracks, their metadata and their decoded samples.
    pcm2 = b"\x00\x20\x00\xe0" * 4000
    with wave.open(str(p / "sound2.wav"), "wb") as wav:
        wav.setparams((1, 2, 8000, 0, "NONE", "not compressed"))
        wav.writeframes(pcm2)
    subprocess.run(["ffmpeg", "-nostdin", "-v", "error", "-i", str(p / "generated.mkv"),
                    "-i", str(p / "sound.wav"), "-i", str(p / "sound2.wav"),
                    "-map", "0:v:0", "-map", "1:a:0", "-map", "2:a:0", "-c", "copy",
                    "-metadata:s:a:0", "language=ita", "-metadata:s:a:1", "language=eng",
                    str(p / "multi-audio.mkv")], check=True, timeout=30)
    run("video", """
check(VideoIO.width("preserved-audio.mkv") == 32);
check(VideoIO.height("preserved-audio.mkv") == 24);
check(VideoIO.transcode("multi-audio.mkv", "multi-preserved.mkv", 32, 24, "ffv1", 0));
check(!VideoIO.transcode("multi-audio.mkv", "multi-preserved.mkv", 32, 24, "ffv1", 0));
check(!VideoIO.transcode("multi-audio.mkv", "incompatible.webm", 32, 24, "vp9", 32));
""", 5, p)
    import json
    def audio_tracks(name):
        info = subprocess.check_output(["ffprobe", "-v", "error", "-select_streams", "a",
                                        "-show_streams", "-of", "json", str(p / name)], timeout=30)
        return json.loads(info)["streams"]
    def audio_samples(name, index):
        return subprocess.check_output(["ffmpeg", "-nostdin", "-v", "error", "-i", str(p / name),
                                        "-map", f"0:a:{index}", "-c:a", "pcm_s16le",
                                        "-f", "s16le", "-"], timeout=30)
    assert len(audio_tracks("preserved-audio.mkv")) == 1
    assert audio_samples("preserved-audio.mkv", 0) == pcm
    tracks = audio_tracks("multi-preserved.mkv")
    assert len(tracks) == 2
    assert [x["tags"]["language"] for x in tracks] == ["ita", "eng"]
    assert audio_samples("multi-preserved.mkv", 0) == pcm
    assert audio_samples("multi-preserved.mkv", 1) == pcm2
    assert decoded("multi-preserved.mkv") == decoded("resized.mkv")
    assert not (p / "incompatible.webm").exists()

    # Audio extraction preserves the selected stream rather than mixing tracks.
    (p / "existing.wav").write_bytes(b"sentinel")
    run("video", """
check(VideoIO.audioTracks("multi-audio.mkv") == 2);
check(VideoIO.audioTracks("generated.mkv") == 0);
check(VideoIO.audioTracks("missing.mkv") == -1);
check(VideoIO.extractAudio("multi-audio.mkv", "italian.wav", 0));
check(VideoIO.extractAudio("multi-audio.mkv", "english.mka", 1));
check(!VideoIO.extractAudio("multi-audio.mkv", "existing.wav", 0));
check(!VideoIO.extractAudio("multi-audio.mkv", "invalid-track.wav", 2));
check(!VideoIO.extractAudio("multi-audio.mkv", "negative-track.wav", -1));
check(!VideoIO.extractAudio("generated.mkv", "silent.wav", 0));
check(!VideoIO.extractAudio("multi-audio.mkv", "wrong-codec.mp3", 0));
check(!VideoIO.extractAudio("missing.mkv", "missing-audio-output.wav", 0));
check(!VideoIO.extractAudio("multi-audio.mkv", "wrong-extension.bin", 0));
""", 12, p)
    assert audio_samples("italian.wav", 0) == pcm
    assert audio_samples("english.mka", 0) == pcm2
    assert len(audio_tracks("english.mka")) == 1
    assert audio_tracks("english.mka")[0]["tags"]["language"] == "eng"
    assert (p / "existing.wav").read_bytes() == b"sentinel"
    for name in ("invalid-track.wav", "negative-track.wav", "silent.wav", "wrong-codec.mp3",
                 "missing-audio-output.wav", "wrong-extension.bin"):
        assert not (p / name).exists(), name

    # AAC fixture exercises planar floating-point decoding, independent of PCM copy.
    subprocess.run(["ffmpeg", "-nostdin", "-v", "error", "-i", str(p / "generated.mkv"),
                    "-i", str(p / "sound.wav"), "-map", "0:v:0", "-map", "1:a:0",
                    "-c:v", "copy", "-c:a", "aac", str(p / "aac-video.mkv")], check=True, timeout=30)
    run("video", """
check(VideoIO.convertAudio("multi-audio.mkv", "decoded.wav", 0));
check(VideoIO.convertAudio("multi-audio.mkv", "decoded.flac", 1));
check(VideoIO.convertAudio("aac-video.mkv", "decoded-aac.wav", 0));
check(!VideoIO.convertAudio("multi-audio.mkv", "decoded.wav", 0));
check(!VideoIO.convertAudio("multi-audio.mkv", "bad-track.wav", 2));
check(!VideoIO.convertAudio("multi-audio.mkv", "bad-format.bin", 0));
check(!VideoIO.convertAudio("generated.mkv", "no-track.wav", 0));
check(!VideoIO.convertAudio("missing.mkv", "missing-source.wav", 0));
""", 8, p)
    assert audio_samples("decoded.wav", 0) == pcm
    assert audio_samples("decoded.flac", 0) == pcm2
    # Compare AAC decoded samples with independent FFmpeg decode, allowing PCM rounding.
    import struct
    ref = audio_samples("aac-video.mkv", 0)
    got = audio_samples("decoded-aac.wav", 0)
    assert len(ref) == len(got) and len(got) > 0
    assert max(abs(a[0] - b[0]) for a, b in zip(struct.iter_unpack("<h", ref), struct.iter_unpack("<h", got))) <= 2
    for name in ("bad-track.wav", "bad-format.bin", "no-track.wav", "missing-source.wav"):
        assert not (p / name).exists(), name

    # Simultaneous conversion: verify independent video/audio codecs, sample rate,
    # track count and approximate duration (codec priming/padding may differ).
    for mode, ext, quality, acodec in (("h264", "mp4", 23, "aac"),
                                       ("vp9", "webm", 32, "opus")):
        target = "av-converted." + ext
        # These audio encoders are required for this specific test environment.
        available = subprocess.check_output(["ffmpeg", "-hide_banner", "-encoders"], stderr=subprocess.STDOUT, timeout=30).decode()
        audio_encoder = "aac" if mode == "h264" else "libopus"
        if audio_encoder not in available:
            print("SKIP audio encoder", audio_encoder)
            continue
        run("video", f"""
if(VideoIO.encoderAvailable("{mode}")) {{
    check(VideoIO.transcodeAV("multi-audio.mkv", "{target}", 32, 24, "{mode}", {quality}, 64));
    check(VideoIO.width("{target}") == 32);
    check(VideoIO.height("{target}") == 24);
    check(!VideoIO.transcodeAV("multi-audio.mkv", "{target}", 32, 24, "{mode}", {quality}, 64));
}} else {{
    <<< "SKIP video encoder {mode}" >>>;
    check(!VideoIO.transcodeAV("multi-audio.mkv", "{target}", 32, 24, "{mode}", {quality}, 64));
    check(!VideoIO.encoderAvailable("{mode}"));
    check(!VideoIO.transcodeAV("multi-audio.mkv", "bad.bin", 32, 24, "{mode}", {quality}, 64));
    check(!VideoIO.transcodeAV("multi-audio.mkv", "{target}", 32, 24, "{mode}", {quality}, 0));
}}
""", 4, p)
        if (p / target).exists():
            tracks = audio_tracks(target)
            assert len(tracks) == 2
            assert all(t["codec_name"] == acodec and t["sample_rate"] == "48000" and t["channels"] == 1 for t in tracks)
            assert len(decoded(target)) == 32 * 24 * 3 * 2
            for i in range(2):
                assert abs(len(audio_samples(target, i)) / 96000 - 1) < 0.08
            # Compare stream start times where reported (encoder delay allowed).
            probe = json.loads(subprocess.check_output(["ffprobe", "-v", "error", "-show_streams", "-of", "json", str(p / target)], timeout=30))
            starts = [float(t["start_time"]) for t in probe["streams"] if "start_time" in t]
            assert len(starts) == 3 and max(starts) - min(starts) < 0.08
    run("video", """
check(!VideoIO.transcodeAV("multi-audio.mkv", "bad-rate.mp4", 32, 24, "h264", 23, 0));
check(!VideoIO.transcodeAV("multi-audio.mkv", "bad-mode.mkv", 32, 24, "ffv1", 0, 64));
check(!VideoIO.transcodeAV("missing.mkv", "missing-av.mp4", 32, 24, "h264", 23, 64));
""", 3, p)
    for name in ("bad-rate.mp4", "bad-mode.mkv", "missing-av.mp4"):
        assert not (p / name).exists(), name

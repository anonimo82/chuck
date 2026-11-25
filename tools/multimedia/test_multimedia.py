"""Run with Python 3 and FFmpeg CLI after building ChucK and all Chugins."""
import os
import pathlib
import subprocess
import tempfile
HERE = pathlib.Path(__file__).resolve().parent
CHUCK = HERE.parents[1] / "src/chuck"

def run(module, script, expected, directory):
    path = directory / "test.ck"
    name = {"audio": "AudioIO", "vector": "VectorIO", "model": "ModelIO", "midi": "MidiFileIO", "video": "VideoIO", "subtitle": "SubtitleIO", "keyframe": "KeyframeIO", "raster": "RasterIO"}[module]
    path.write_text(f'@import "{name}";\n' + 'fun void check(int ok) { if(ok) <<< "PASS" >>>; else <<< "FAIL" >>>; }\n' + script, encoding="utf-8")
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

    # Independent all-intra fixture: four half-second frames, with two PCM tracks.
    colors = (red, blue, red, blue)
    for i, pixels in enumerate(colors):
        (p / f"trimsrc{i:06d}.ppm").write_bytes(b"P6\n16 16\n255\n" + pixels)
    subprocess.run(["ffmpeg", "-nostdin", "-v", "error", "-framerate", "2",
                    "-i", str(p / "trimsrc%06d.ppm"), "-c:v", "ffv1", "-g", "1",
                    str(p / "trim-source.mkv")], check=True, timeout=30)
    # PCM fixture is two seconds; both tracks share the same timeline.
    with wave.open(str(p / "trim-audio.wav"), "wb") as wav:
        wav.setparams((1, 2, 8000, 0, "NONE", "not compressed"))
        wav.writeframes(pcm + pcm2)
    subprocess.run(["ffmpeg", "-nostdin", "-v", "error", "-i", str(p / "trim-source.mkv"),
                    "-i", str(p / "trim-audio.wav"), "-map", "0:v:0", "-map", "1:a:0",
                    "-c", "copy", str(p / "trim-av.mkv")], check=True, timeout=30)
    run("video", """
check(VideoIO.trim("trim-source.mkv", "trimmed.mkv", 0.5, 1.5));
check(VideoIO.trim("trim-source.mkv", "rounded.mkv", 0.1, 1.5));
check(VideoIO.trim("trim-av.mkv", "trimmed-av.mkv", 0.5, 1.5));
check(!VideoIO.trim("trim-source.mkv", "trimmed.mkv", 0.5, 1.5));
check(!VideoIO.trim("trim-source.mkv", "negative.mkv", -1.0, 1.0));
check(!VideoIO.trim("trim-source.mkv", "reversed.mkv", 1.5, 0.5));
check(!VideoIO.trim("trim-source.mkv", "beyond-end.mkv", 3.0, 4.0));
check(!VideoIO.trim("trim-source.mkv", "no-keyframe.mkv", 0.1, 0.4));
check(!VideoIO.trim("missing.mkv", "missing-trim.mkv", 0.0, 1.0));
check(!VideoIO.trim("trim-source.mkv", "invalid-trim.bin", 0.0, 1.0));
""", 10, p)
    assert decoded("trimmed.mkv") == blue + red
    assert decoded("rounded.mkv") == blue + red
    assert decoded("trimmed-av.mkv") == blue + red
    assert len(audio_tracks("trimmed-av.mkv")) == 1
    # Complete audio packets only: a little shorter than the requested interval.
    cut_pcm = audio_samples("trimmed-av.mkv", 0)
    assert 0 < len(cut_pcm) <= 16000
    assert len(cut_pcm) >= 10000
    for name in ("negative.mkv", "reversed.mkv", "beyond-end.mkv",
                 "no-keyframe.mkv", "missing-trim.mkv", "invalid-trim.bin"):
        assert not (p / name).exists(), name

    # Concatenate identical stream configurations, checking actual decoded content.
    run("video", """
check(VideoIO.concat("trim-source.mkv", "trim-source.mkv", "joined.mkv"));
check(VideoIO.concat("trim-av.mkv", "trim-av.mkv", "joined-av.mkv"));
check(!VideoIO.concat("trim-source.mkv", "trim-source.mkv", "joined.mkv"));
check(!VideoIO.concat("trim-source.mkv", "trim-av.mkv", "bad-streams.mkv"));
check(!VideoIO.concat("trim-source.mkv", "resized.mkv", "bad-size.mkv"));
check(!VideoIO.concat("missing.mkv", "trim-source.mkv", "bad-source.mkv"));
check(!VideoIO.concat("trim-source.mkv", "missing.mkv", "bad-second.mkv"));
check(!VideoIO.concat("trim-source.mkv", "trim-source.mkv", "bad-container.bin"));
""", 8, p)
    assert decoded("joined.mkv") == (red + blue + red + blue) * 2
    assert decoded("joined-av.mkv") == decoded("joined.mkv")
    assert len(audio_tracks("joined-av.mkv")) == 1
    assert audio_samples("joined-av.mkv", 0) == (pcm + pcm2) * 2
    for name in ("bad-streams.mkv", "bad-size.mkv", "bad-source.mkv",
                 "bad-second.mkv", "bad-container.bin"):
        assert not (p / name).exists(), name

    # Exact decoded-frame selection, including a non-keyframe in an inter-frame clip.
    subprocess.run(["ffmpeg", "-nostdin", "-v", "error", "-framerate", "2",
                    "-i", str(p / "trimsrc%06d.ppm"), "-c:v", "ffv1", "-g", "12",
                    str(p / "inter-source.mkv")], check=True, timeout=30)
    packets = json.loads(subprocess.check_output(["ffprobe", "-v", "error", "-select_streams", "v:0",
                   "-show_packets", "-of", "json", str(p / "inter-source.mkv")], timeout=30))["packets"]
    assert len(packets) == 4 and "K" not in packets[1]["flags"]
    run("video", """
check(VideoIO.trimFrames("inter-source.mkv", "exact.mkv", 1, 2));
check(VideoIO.trimFrames("inter-source.mkv", "last-frame.mkv", 3, 1));
check(VideoIO.width("exact.mkv") == 16);
check(VideoIO.height("exact.mkv") == 16);
check(!VideoIO.trimFrames("inter-source.mkv", "exact.mkv", 0, 1));
check(!VideoIO.trimFrames("inter-source.mkv", "short-range.mkv", 3, 2));
check(!VideoIO.trimFrames("inter-source.mkv", "past-end.mkv", 4, 1));
check(!VideoIO.trimFrames("inter-source.mkv", "negative-index.mkv", -1, 1));
check(!VideoIO.trimFrames("inter-source.mkv", "zero-count.mkv", 0, 0));
check(!VideoIO.trimFrames("trim-av.mkv", "audio-rejected.mkv", 0, 1));
check(!VideoIO.trimFrames("inter-source.mkv", "wrong-trim.mp4", 0, 1));
check(!VideoIO.trimFrames("missing.mkv", "missing-exact.mkv", 0, 1));
""", 12, p)
    assert decoded("exact.mkv") == blue + red
    assert decoded("last-frame.mkv") == blue
    exact_packets = json.loads(subprocess.check_output(["ffprobe", "-v", "error", "-select_streams", "v:0",
                   "-show_packets", "-of", "json", str(p / "exact.mkv")], timeout=30))["packets"]
    assert [float(x["pts_time"]) for x in exact_packets] == [0.0, 0.5]
    for name in ("short-range.mkv", "past-end.mkv", "negative-index.mkv", "zero-count.mkv",
                 "audio-rejected.mkv", "wrong-trim.mp4", "missing-exact.mkv"):
        assert not (p / name).exists(), name
    if "libx264" in subprocess.check_output(["ffmpeg", "-hide_banner", "-encoders"],
                                             stderr=subprocess.STDOUT, timeout=30).decode():
        subprocess.run(["ffmpeg", "-nostdin", "-v", "error", "-framerate", "2",
                        "-i", str(p / "trimsrc%06d.ppm"), "-c:v", "libx264", "-g", "12",
                        "-bf", "2", "-x264-params", "b-adapt=0:scenecut=0", "-pix_fmt", "yuv420p",
                        str(p / "reordered.mp4")], check=True, timeout=30)
        run("video", """
check(VideoIO.trimFrames("reordered.mp4", "reordered-cut.mkv", 1, 2));
""", 1, p)
        original = decoded("reordered.mp4")
        stride = 16 * 16 * 3
        result = decoded("reordered-cut.mkv")
        assert len(result) == 2 * stride
        assert max(abs(x-y) for x,y in zip(result, original[stride:3*stride])) <= 3

    # Exact audiovisual cut across PCM packet boundaries.
    run("video", """
check(VideoIO.trimAV("trim-av.mkv", "exact-av.mkv", 1, 2));
check(VideoIO.trimAV("multi-audio.mkv", "exact-multi.mkv", 0, 1));
check(VideoIO.trimAV("trim-av.mkv", "exact-tail.mkv", 3, 1));
check(VideoIO.trimAV("trim-source.mkv", "exact-silent.mkv", 1, 2));
check(!VideoIO.trimAV("trim-av.mkv", "exact-av.mkv", 0, 1));
check(!VideoIO.trimAV("trim-av.mkv", "bad-av-range.mkv", 3, 2));
check(!VideoIO.trimAV("trim-av.mkv", "bad-av-codec.mkv", 0, 100));
check(!VideoIO.trimAV("trim-av.mkv", "bad-av-count.mkv", 0, 0));
check(!VideoIO.trimAV("missing.mkv", "bad-av-source.mkv", 0, 1));
""", 9, p)
    assert decoded("exact-av.mkv") == blue + red
    assert audio_samples("exact-av.mkv", 0) == (pcm + pcm2)[8000:24000]
    assert decoded("exact-tail.mkv") == blue
    assert audio_samples("exact-tail.mkv", 0) == pcm2[8000:16000]
    assert decoded("exact-silent.mkv") == blue + red
    assert len(audio_tracks("exact-multi.mkv")) == 2
    assert audio_samples("exact-multi.mkv", 0) == pcm[:8000]
    assert audio_samples("exact-multi.mkv", 1) == pcm2[:8000]
    assert [x["tags"]["language"] for x in audio_tracks("exact-multi.mkv")] == ["ita", "eng"]
    for name in ("bad-av-range.mkv", "bad-av-codec.mkv", "bad-av-count.mkv", "bad-av-source.mkv"):
        assert not (p / name).exists(), name

    # Compressed audio: encode a two-second fixture with video and continuous
    # audio. Compare a middle interval to independently decoded S16 samples.
    for codec, encoder, rate in (("aac", "aac", 8000),
                                 ("mp3", "libmp3lame", 8000),
                                 ("opus", "libopus", 48000)):
        encoders = subprocess.check_output(["ffmpeg", "-hide_banner", "-encoders"],
                                           stderr=subprocess.STDOUT, timeout=30).decode()
        if encoder not in encoders:
            print("SKIP compressed trim encoder", encoder)
            continue
        source = "trim-" + codec + ".mkv"
        target = "cut-" + codec + ".mkv"
        subprocess.run(["ffmpeg", "-nostdin", "-v", "error", "-i", str(p / "trim-source.mkv"),
                        "-i", str(p / "trim-audio.wav"), "-map", "0:v:0", "-map", "1:a:0",
                        "-c:v", "copy", "-c:a", encoder, "-ar", str(rate), str(p / source)],
                       check=True, timeout=30)
        run("video", f"""
check(VideoIO.trimAV("{source}", "{target}", 1, 2));
check(!VideoIO.trimAV("{source}", "{target}", 1, 2));
""", 2, p)
        assert decoded(target) == blue + red
        track = audio_tracks(target)[0]
        assert track["codec_name"] == "pcm_s16le" and int(track["sample_rate"]) == rate
        def first_pts(selector):
            frames = json.loads(subprocess.check_output(["ffprobe", "-v", "error",
                "-select_streams", selector, "-show_frames", "-of", "json", str(p / source)],
                timeout=30))["frames"]
            return float(frames[0]["best_effort_timestamp_time"])
        import math
        start_sample = math.ceil((first_pts("v:0") + 0.5)*rate - 1e-7) - round(first_pts("a:0")*rate)
        reference = audio_samples(source, 0)
        actual = audio_samples(target, 0)
        assert start_sample >= 0 and len(actual) == rate * 2
        assert actual == reference[start_sample*2:(start_sample+rate)*2]

    # Different dimensions, codecs and frame rates; test both duplication and dropping.
    subprocess.run(["ffmpeg", "-nostdin", "-v", "error", "-framerate", "4",
                    "-i", str(p / "trimsrc%06d.ppm"), "-vf", "scale=32:24",
                    "-c:v", "mpeg4", "-pix_fmt", "yuv420p", str(p / "different.mp4")],
                   check=True, timeout=30)
    run("video", """
check(VideoIO.concatNormalized("trim-source.mkv", "different.mp4", "normalized.mkv", 16, 16, 4));
check(VideoIO.concatNormalized("trim-source.mkv", "different.mp4", "normalized-low.mkv", 16, 16, 2));
check(VideoIO.width("normalized.mkv") == 16);
check(VideoIO.height("normalized.mkv") == 16);
check(Math.fabs(VideoIO.fps("normalized.mkv") - 4.0) < 0.01);
check(!VideoIO.concatNormalized("trim-source.mkv", "different.mp4", "normalized.mkv", 16, 16, 4));
check(!VideoIO.concatNormalized("trim-source.mkv", "different.mp4", "bad-norm-size.mkv", 0, 16, 4));
check(!VideoIO.concatNormalized("trim-source.mkv", "different.mp4", "bad-norm-fps.mkv", 16, 16, 0));
check(!VideoIO.concatNormalized("trim-av.mkv", "different.mp4", "bad-norm-audio.mkv", 16, 16, 4));
check(!VideoIO.concatNormalized("missing.mkv", "different.mp4", "bad-norm-source.mkv", 16, 16, 4));
check(!VideoIO.concatNormalized("trim-source.mkv", "different.mp4", "bad-norm.mp4", 16, 16, 4));
""", 11, p)
    stride = 16 * 16 * 3
    high = decoded("normalized.mkv")
    low = decoded("normalized-low.mkv")
    assert len(high) == 12 * stride and len(low) == 6 * stride
    assert high[:8*stride] == red*2 + blue*2 + red*2 + blue*2
    assert low[:4*stride] == red + blue + red + blue
    assert max(abs(x-y) for x,y in zip(high[8*stride:], red+blue+red+blue)) <= 5
    assert max(abs(x-y) for x,y in zip(low[4*stride:], red+red)) <= 5
    for name, expected in (("normalized.mkv", 12), ("normalized-low.mkv", 6)):
        packets = json.loads(subprocess.check_output(["ffprobe", "-v", "error",
            "-select_streams", "v:0", "-show_packets", "-of", "json", str(p / name)],
            timeout=30))["packets"]
        rate = 4 if expected == 12 else 2
        assert [float(x["pts_time"]) for x in packets] == [i/rate for i in range(expected)]
    for name in ("bad-norm-size.mkv", "bad-norm-fps.mkv", "bad-norm-audio.mkv",
                 "bad-norm-source.mkv", "bad-norm.mp4"):
        assert not (p / name).exists(), name

    # Heterogeneous AV: normalize different rates and channel counts; preserve
    # the clip boundary and insert silence when the second clip has no audio.
    subprocess.run(["ffmpeg", "-nostdin", "-v", "error", "-i", str(p / "different.mp4"),
                    "-i", str(p / "trim-audio.wav"), "-map", "0:v:0", "-map", "1:a:0",
                    "-c:v", "copy", "-c:a", "pcm_s16le", "-ar", "16000", "-ac", "2",
                    "-t", "1", str(p / "different-av.mkv")], check=True, timeout=30)
    run("video", """
check(VideoIO.concatAV("trim-av.mkv", "different-av.mkv", "normalized-av.mkv", 16, 16, 4, 16000, 2));
check(VideoIO.concatAV("trim-av.mkv", "different.mp4", "normalized-silent.mkv", 16, 16, 4, 8000, 1));
check(VideoIO.concatAV("different.mp4", "trim-av.mkv", "normalized-leading.mkv", 16, 16, 4, 8000, 1));
check(!VideoIO.concatAV("trim-av.mkv", "different-av.mkv", "normalized-av.mkv", 16, 16, 4, 16000, 2));
check(!VideoIO.concatAV("multi-audio.mkv", "different.mp4", "bad-av-multi.mkv", 16, 16, 4, 16000, 2));
check(!VideoIO.concatAV("trim-av.mkv", "different.mp4", "bad-av-rate.mkv", 16, 16, 4, 0, 1));
check(!VideoIO.concatAV("trim-av.mkv", "different.mp4", "bad-av-channels.mkv", 16, 16, 4, 8000, 3));
""", 7, p)
    assert decoded("normalized-av.mkv") == high
    tracks = audio_tracks("normalized-av.mkv")
    assert len(tracks) == 1 and tracks[0]["codec_name"] == "pcm_s16le"
    assert tracks[0]["sample_rate"] == "16000" and tracks[0]["channels"] == 2
    audio = audio_samples("normalized-av.mkv", 0)
    assert len(audio) == 3 * 16000 * 4
    assert any(audio[:2*16000*4]) and any(audio[2*16000*4:])
    assert audio_samples("normalized-silent.mkv", 0) == pcm + pcm2 + bytes(16000)
    assert audio_samples("normalized-leading.mkv", 0) == bytes(16000) + pcm + pcm2
    for name in ("bad-av-multi.mkv", "bad-av-rate.mkv", "bad-av-channels.mkv"):
        assert not (p / name).exists(), name

    # Linear crossfade: 2 s + 2 s - 1 s = 3 s at 4 fps.
    run("video", """
check(VideoIO.crossfade("trim-av.mkv", "trim-av.mkv", "fade.mkv", 16, 16, 4, 8000, 1, 1.0));
check(VideoIO.crossfade("trim-av.mkv", "trim-source.mkv", "fade-silent.mkv", 16, 16, 4, 8000, 1, 1.0));
check(!VideoIO.crossfade("trim-av.mkv", "trim-av.mkv", "fade.mkv", 16, 16, 4, 8000, 1, 1.0));
check(!VideoIO.crossfade("trim-av.mkv", "trim-av.mkv", "fade-zero.mkv", 16, 16, 4, 8000, 1, 0.0));
check(!VideoIO.crossfade("trim-av.mkv", "trim-av.mkv", "fade-long.mkv", 16, 16, 4, 8000, 1, 3.0));
check(!VideoIO.crossfade("trim-av.mkv", "trim-av.mkv", "fade-grid.mkv", 16, 16, 3, 8000, 1, 1.0));
check(!VideoIO.crossfade("missing.mkv", "trim-av.mkv", "fade-missing.mkv", 16, 16, 4, 8000, 1, 1.0));
""", 7, p)
    # Constant matching frames in the overlap must remain bit-exact.
    expected = (red*2 + blue*2)*3
    assert decoded("fade.mkv") == expected
    assert decoded("fade-silent.mkv") == expected
    import struct
    def shorts(data):
        return struct.unpack("<" + "h"*(len(data)//2), data)
    x,y = shorts(pcm2),shorts(pcm)
    blended = [int((a*(7999-i)+b*i)/7999) for i,(a,b) in enumerate(zip(x,y))]
    faded = [int(a*(7999-i)/7999) for i,a in enumerate(x)]
    assert shorts(audio_samples("fade.mkv",0)) == shorts(pcm)+tuple(blended)+shorts(pcm2)
    assert shorts(audio_samples("fade-silent.mkv",0)) == shorts(pcm)+tuple(faded)+(0,)*8000
    # Different colors across the overlap exercise actual pixel blending.
    run("video", """
check(VideoIO.crossfade("trim-av.mkv", "generated.mkv", "fade-colors.mkv", 16, 16, 4, 8000, 1, 0.5));
""", 1, p)
    expected_colors = red*2+blue*2+red*2+blue+red+blue*2
    assert decoded("fade-colors.mkv") == expected_colors
    for name in ("fade-zero.mkv","fade-long.mkv","fade-grid.mkv","fade-missing.mkv"):
        assert not (p/name).exists(), name

    # Three-frame transition: A, intermediate, B.
    run("video", """
check(VideoIO.transition("trim-av.mkv", "trim-av.mkv", "wipe-x.mkv", 16, 16, 4, 8000, 1, 0.75, "wipeLeft", "linear"));
check(VideoIO.transition("trim-av.mkv", "trim-av.mkv", "wipe-y.mkv", 16, 16, 4, 8000, 1, 0.75, "wipeDown", "linear"));
check(VideoIO.transition("trim-av.mkv", "trim-av.mkv", "dip-black.mkv", 16, 16, 4, 8000, 1, 0.75, "black", "linear"));
check(VideoIO.transition("trim-av.mkv", "trim-av.mkv", "power.mkv", 16, 16, 4, 8000, 1, 1.0, "linear", "equalPower"));
check(VideoIO.transition("trim-av.mkv", "trim-av.mkv", "linear-mode.mkv", 16, 16, 4, 8000, 1, 1.0, "linear", "linear"));
check(!VideoIO.transition("trim-av.mkv", "trim-av.mkv", "bad-visual.mkv", 16, 16, 4, 8000, 1, 1.0, "unknown", "linear"));
check(!VideoIO.transition("trim-av.mkv", "trim-av.mkv", "bad-curve.mkv", 16, 16, 4, 8000, 1, 1.0, "linear", "unknown"));
check(!VideoIO.transition("trim-av.mkv", "trim-av.mkv", "power.mkv", 16, 16, 4, 8000, 1, 1.0, "linear", "equalPower"));
""", 8, p)
    # At overlap index 1, the old frame is blue and the new frame red.
    row_red = red[:16*3]
    row_blue = blue[:16*3]
    mid_x = (row_red[:8*3]+row_blue[8*3:])*16
    mid_y = red[:8*16*3]+blue[8*16*3:]
    for name, middle in (("wipe-x.mkv",mid_x),("wipe-y.mkv",mid_y),("dip-black.mkv",bytes(stride))):
        data=decoded(name)
        assert len(data)==13*stride
        assert data[6*stride:7*stride]==middle
        assert data[5*stride:6*stride]==red
        assert data[7*stride:8*stride]==blue
    assert decoded("linear-mode.mkv")==decoded("fade.mkv")
    assert audio_samples("linear-mode.mkv",0)==audio_samples("fade.mkv",0)
    import math
    def rounded(v):
        return math.floor(v+0.5) if v>=0 else math.ceil(v-0.5)
    expected_power=tuple(max(-32768,min(32767,rounded(a*math.cos(i/7999*math.pi/2)+b*math.sin(i/7999*math.pi/2)))) for i,(a,b) in enumerate(zip(x,y)))
    assert shorts(audio_samples("power.mkv",0))==shorts(pcm)+expected_power+shorts(pcm2)
    assert not (p/"bad-visual.mkv").exists()
    assert not (p/"bad-curve.mkv").exists()

    # Overlay begins at the first presentation frame and disappears at EOF.
    # Generate a short solid-blue layer independently of the base.
    subprocess.run(["ffmpeg","-nostdin","-v","error","-f","lavfi","-i",
                    "color=c=blue:s=16x16:r=2:d=1","-c:v","ffv1",
                    "-pix_fmt","bgr0",str(p/"overlay-blue.mkv")],check=True,timeout=30)
    layer_rgb=decoded("overlay-blue.mkv")[:stride]
    run("video", """
check(VideoIO.overlay("trim-av.mkv", "overlay-blue.mkv", "pip.mkv", 16, 16, 4, 4, 4, 8, 8, 1.0, 8000, 1));
check(VideoIO.overlay("trim-av.mkv", "overlay-blue.mkv", "pip-half.mkv", 16, 16, 4, 4, 4, 8, 8, 0.5, 8000, 1));
check(VideoIO.overlay("trim-av.mkv", "overlay-blue.mkv", "pip-zero.mkv", 16, 16, 4, 4, 4, 8, 8, 0.0, 8000, 1));
check(VideoIO.overlay("trim-source.mkv", "trim-av.mkv", "pip-silent.mkv", 16, 16, 4, 4, 4, 8, 8, 1.0, 8000, 1));
check(!VideoIO.overlay("trim-av.mkv", "overlay-blue.mkv", "pip.mkv", 16, 16, 4, 4, 4, 8, 8, 1.0, 8000, 1));
check(!VideoIO.overlay("trim-av.mkv", "overlay-blue.mkv", "pip-bounds.mkv", 16, 16, 4, 10, 4, 8, 8, 1.0, 8000, 1));
check(!VideoIO.overlay("trim-av.mkv", "overlay-blue.mkv", "pip-alpha.mkv", 16, 16, 4, 4, 4, 8, 8, 1.1, 8000, 1));
check(!VideoIO.overlay("trim-av.mkv", "missing.mkv", "pip-missing.mkv", 16, 16, 4, 4, 4, 8, 8, 1.0, 8000, 1));
check(!VideoIO.overlay("multi-audio.mkv", "overlay-blue.mkv", "pip-multi.mkv", 16, 16, 4, 4, 4, 8, 8, 1.0, 8000, 1));
""",9,p)
    original=red*2+blue*2+red*2+blue*2
    assert decoded("pip-zero.mkv")==original
    for name,alpha in (("pip.mkv",65535),("pip-half.mkv",32768)):
        expected=bytearray(original)
        for frame in range(4):
            for yy in range(4,12):
                for xx in range(4,12):
                    for ch in range(3):
                        j=frame*stride+(yy*16+xx)*3+ch
                        expected[j]=(expected[j]*(65535-alpha)+layer_rgb[ch]*alpha+32767)//65535
        assert decoded(name)==expected
        assert audio_samples(name,0)==pcm+pcm2
    assert audio_samples("pip-silent.mkv",0)==bytes(32000)
    for name in ("pip-bounds.mkv","pip-alpha.mkv","pip-missing.mkv","pip-multi.mkv"):
        assert not (p/name).exists(),name

    # Key out the blue layer (including small YUV-to-RGB rounding errors).
    run("video", """
check(VideoIO.composite("trim-av.mkv", "overlay-blue.mkv", "keyed.mkv", 16, 16, 4, 4, 4, 8, 8, 1.0, 8000, 1, 255, 0.02, 0.0, 1.0, 0.0));
check(VideoIO.composite("trim-av.mkv", "overlay-blue.mkv", "key-other.mkv", 16, 16, 4, 4, 4, 8, 8, 1.0, 8000, 1, 65280, 0.02, 0.0, 1.0, 0.0));
check(VideoIO.composite("trim-av.mkv", "trim-av.mkv", "mixed-audio.mkv", 16, 16, 4, 0, 0, 16, 16, 0.0, 8000, 1, -1, 0.0, 0.0, 0.25, 0.5));
check(VideoIO.composite("trim-source.mkv", "trim-av.mkv", "layer-audio.mkv", 16, 16, 4, 0, 0, 16, 16, 0.0, 8000, 1, -1, 0.0, 0.0, 0.0, 1.0));
check(VideoIO.composite("trim-av.mkv", "trim-av.mkv", "mixed-clipped.mkv", 16, 16, 4, 0, 0, 16, 16, 0.0, 8000, 1, -1, 0.0, 0.0, 4.0, 4.0));
check(!VideoIO.composite("trim-av.mkv", "overlay-blue.mkv", "bad-key.mkv", 16, 16, 4, 4, 4, 8, 8, 1.0, 8000, 1, 16777216, 0.02, 0.0, 1.0, 0.0));
check(!VideoIO.composite("trim-av.mkv", "overlay-blue.mkv", "bad-softness.mkv", 16, 16, 4, 4, 4, 8, 8, 1.0, 8000, 1, 255, 0.8, 0.5, 1.0, 0.0));
check(!VideoIO.composite("trim-av.mkv", "overlay-blue.mkv", "bad-gain.mkv", 16, 16, 4, 4, 4, 8, 8, 1.0, 8000, 1, 255, 0.02, 0.0, 1.0, 5.0));
check(!VideoIO.composite("trim-av.mkv", "overlay-blue.mkv", "keyed.mkv", 16, 16, 4, 4, 4, 8, 8, 1.0, 8000, 1, 255, 0.02, 0.0, 1.0, 0.0));
""",9,p)
    assert decoded("keyed.mkv")==original
    assert decoded("key-other.mkv")==decoded("pip.mkv")
    assert audio_samples("keyed.mkv",0)==pcm+pcm2
    assert decoded("mixed-audio.mkv")==original
    assert shorts(audio_samples("mixed-audio.mkv",0))==tuple(rounded(v*0.75) for v in shorts(pcm+pcm2))
    assert audio_samples("layer-audio.mkv",0)==pcm+pcm2
    assert shorts(audio_samples("mixed-clipped.mkv",0))==tuple(max(-32768,min(32767,v*8)) for v in shorts(pcm+pcm2))
    for name in ("bad-key.mkv","bad-softness.mkv","bad-gain.mkv"):
        assert not (p/name).exists(),name

    # Construct an exact RGBA PNG using the Python standard library.
    import zlib
    def png_chunk(kind,data):
        return struct.pack(">I",len(data))+kind+data+struct.pack(">I",zlib.crc32(kind+data)&0xffffffff)
    rgba_row=bytes((0,255,0,0))*4+bytes((0,255,0,128))*4+bytes((0,255,0,255))*8
    (p/"mark.png").write_bytes(b"\x89PNG\r\n\x1a\n"+
        png_chunk(b"IHDR",struct.pack(">IIBBBBB",16,16,8,6,0,0,0))+
        png_chunk(b"IDAT",zlib.compress((b"\0"+rgba_row)*16))+
        png_chunk(b"IEND",b""))
    run("video", """
check(VideoIO.watermark("trim-av.mkv", "mark.png", "marked.mkv", 16, 16, 4, 0, 0, 16, 16, 1.0, 8000, 1));
check(VideoIO.watermark("trim-av.mkv", "mark.png", "marked-half.mkv", 16, 16, 4, 0, 0, 16, 16, 0.5, 8000, 1));
check(VideoIO.watermark("trim-av.mkv", "mark.png", "marked-zero.mkv", 16, 16, 4, 0, 0, 16, 16, 0.0, 8000, 1));
check(VideoIO.watermark("trim-source.mkv", "mark.png", "marked-small.mkv", 16, 16, 4, 4, 4, 8, 8, 1.0, 8000, 1));
check(!VideoIO.watermark("trim-av.mkv", "mark.png", "marked.mkv", 16, 16, 4, 0, 0, 16, 16, 1.0, 8000, 1));
check(!VideoIO.watermark("trim-av.mkv", "missing.png", "mark-missing.mkv", 16, 16, 4, 0, 0, 16, 16, 1.0, 8000, 1));
check(!VideoIO.watermark("trim-av.mkv", "trim-source.mkv", "mark-video.mkv", 16, 16, 4, 0, 0, 16, 16, 1.0, 8000, 1));
check(!VideoIO.watermark("trim-av.mkv", "mark.png", "mark-bounds.mkv", 16, 16, 4, 4, 4, 16, 16, 1.0, 8000, 1));
check(!VideoIO.watermark("trim-av.mkv", "mark.png", "mark-alpha.mkv", 16, 16, 4, 0, 0, 16, 16, -0.1, 8000, 1));
""",9,p)
    for name,global_alpha in (("marked.mkv",65535),("marked-half.mkv",32768),("marked-zero.mkv",0)):
        expected=bytearray(original)
        for frame in range(8):
            for yy in range(16):
                for xx in range(16):
                    a=(global_alpha*(0 if xx<4 else 128 if xx<8 else 255)+127)//255
                    for ch in range(3):
                        j=frame*stride+(yy*16+xx)*3+ch
                        expected[j]=(expected[j]*(65535-a)+(255 if ch==1 else 0)*a+32767)//65535
        assert decoded(name)==expected,name
        assert audio_samples(name,0)==pcm+pcm2
    small=decoded("marked-small.mkv")
    assert len(small)==len(original)
    for frame in range(8):
        for yy in range(16):
            for xx in range(16):
                j=frame*stride+(yy*16+xx)*3
                if not (4<=yy<12 and 4<=xx<12):
                    assert small[j:j+3]==original[j:j+3]
    assert audio_samples("marked-small.mkv",0)==bytes(32000)
    for name in ("mark-missing.mkv","mark-video.mkv","mark-bounds.mkv","mark-alpha.mkv"):
        assert not (p/name).exists(),name

    # Ordered layers, with an image over a video and vice versa.
    (p/"layers.tsv").write_text("video\toverlay-blue.mkv\t0\t0\t16\t16\t1\nimage\tmark.png\t0\t0\t16\t16\t1\n",encoding="utf-8")
    (p/"reverse.tsv").write_text("image\tmark.png\t0\t0\t16\t16\t1\nvideo\toverlay-blue.mkv\t0\t0\t16\t16\t1\n",encoding="utf-8")
    (p/"one.tsv").write_text("image\tmark.png\t0\t0\t16\t16\t0.5\n",encoding="utf-8")
    (p/"empty.tsv").write_text("# no layers\n",encoding="utf-8")
    (p/"bad.tsv").write_text("image\tmark.png\t0\t0\t16\t16\t1\tjunk\n",encoding="utf-8")
    (p/"too-many.tsv").write_text("image\tmark.png\t0\t0\t16\t16\t1\n"*9,encoding="utf-8")
    (p/"missing-layer.tsv").write_text("image\tmissing.png\t0\t0\t16\t16\t1\n",encoding="utf-8")
    run("video", """
check(VideoIO.multilayer("trim-av.mkv", "layers.tsv", "layers.mkv", 16, 16, 4, 8000, 1));
check(VideoIO.multilayer("trim-av.mkv", "reverse.tsv", "reverse.mkv", 16, 16, 4, 8000, 1));
check(VideoIO.multilayer("trim-av.mkv", "one.tsv", "one.mkv", 16, 16, 4, 8000, 1));
check(!VideoIO.multilayer("trim-av.mkv", "layers.tsv", "layers.mkv", 16, 16, 4, 8000, 1));
check(!VideoIO.multilayer("trim-av.mkv", "empty.tsv", "empty.mkv", 16, 16, 4, 8000, 1));
check(!VideoIO.multilayer("trim-av.mkv", "bad.tsv", "bad-layers.mkv", 16, 16, 4, 8000, 1));
check(!VideoIO.multilayer("trim-av.mkv", "too-many.tsv", "too-many.mkv", 16, 16, 4, 8000, 1));
check(!VideoIO.multilayer("trim-av.mkv", "missing-layer.tsv", "missing-layer.mkv", 16, 16, 4, 8000, 1));
""",8,p)
    expected=bytearray(decoded("marked.mkv"))
    for frame in range(4):
        for yy in range(16):
            for xx in range(16):
                a=(0 if xx<4 else 128 if xx<8 else 255)*257
                for ch in range(3):
                    j=frame*stride+(yy*16+xx)*3+ch
                    expected[j]=(layer_rgb[ch]*(65535-a)+(255 if ch==1 else 0)*a+32767)//65535
    assert decoded("layers.mkv")==expected
    assert decoded("reverse.mkv")==layer_rgb*4+decoded("marked.mkv")[4*stride:]
    assert decoded("one.mkv")==decoded("marked-half.mkv")
    for name in ("layers.mkv","reverse.mkv","one.mkv"):
        assert audio_samples(name,0)==pcm+pcm2
    for name in ("empty.mkv","bad-layers.mkv","too-many.mkv","missing-layer.mkv"):
        assert not (p/name).exists(),name

    # Text uses system fonts, so test visibility and timing rather than font-specific pixels.
    run("video", """
check(VideoIO.title("trim-av.mkv", "title.mkv", "A", "Sans Bold", 12, 16777215, 0, 0, 0.5, 1.5));
check(VideoIO.title("trim-av.mkv", "title-color.mkv", "A", "Sans Bold", 12, 65280, 0, 0, 0.5, 1.5));
check(VideoIO.title("trim-av.mkv", "title-outside.mkv", "A", "Sans", 12, 16777215, 0, 0, 3.0, 4.0));
check(VideoIO.title("trim-source.mkv", "title-utf8.mkv", "Città", "Sans", 10, 16777215, 0, 0, 0.0, 2.0));
check(!VideoIO.title("trim-av.mkv", "title.mkv", "A", "Sans", 12, 16777215, 0, 0, 0.0, 1.0));
check(!VideoIO.title("trim-av.mkv", "title-empty.mkv", "", "Sans", 12, 16777215, 0, 0, 0.0, 1.0));
check(!VideoIO.title("trim-av.mkv", "title-time.mkv", "A", "Sans", 12, 16777215, 0, 0, 1.0, 1.0));
check(!VideoIO.title("trim-av.mkv", "title-position.mkv", "A", "Sans", 12, 16777215, 16, 0, 0.0, 1.0));
check(!VideoIO.title("trim-av.mkv", "title-size.mkv", "A", "Sans", 0, 16777215, 0, 0, 0.0, 1.0));
""",9,p)
    # title preserves the native 2 FPS fixture, unlike earlier 4 FPS overlays.
    title_original=red+blue+red+blue
    assert decoded("trim-av.mkv")==title_original
    for name in ("title.mkv","title-color.mkv"):
        data=decoded(name)
        assert len(data)==len(title_original), (name,len(data)//stride,4)
        for frame in range(4):
            lo,hi=frame*stride,(frame+1)*stride
            assert (data[lo:hi]!=title_original[lo:hi])==(1<=frame<3), (name,frame)
        assert audio_samples(name,0)==pcm+pcm2
    assert decoded("title.mkv")!=decoded("title-color.mkv")
    assert decoded("title-outside.mkv")==title_original
    utf8=decoded("title-utf8.mkv")
    assert len(utf8)==len(title_original)
    assert utf8!=title_original
    for name in ("title-empty.mkv","title-time.mkv","title-position.mkv","title-size.mkv"):
        assert not (p/name).exists(),name

    # Plain-text UTF-8 subtitle round trip; compare exact canonical bytes.
    srt="1\n00:00:00,125 --> 00:00:01,250\nHello\nWorld\n\n2\n00:00:01,000 --> 00:00:02,500\nCittà\n\n"
    vtt="WEBVTT\n\n00:00:00.125 --> 00:00:01.250\nHello\nWorld\n\n00:00:01.000 --> 00:00:02.500\nCittà\n\n"
    (p/"sub.srt").write_bytes(b"\xef\xbb\xbf"+srt.replace("\n","\r\n").encode("utf-8"))
    (p/"short.vtt").write_bytes(b"WEBVTT\n\n00:01.000 --> 00:02.000\nShort\n\n")
    (p/"invalid.srt").write_bytes(b"1\n00:00:02,000 --> 00:00:01,000\nBad\n")
    (p/"styled.vtt").write_bytes(b"WEBVTT\n\n00:01.000 --> 00:02.000 align:start\nBad\n")
    (p/"bad-utf8.srt").write_bytes(b"1\n00:00:01,000 --> 00:00:02,000\n\xff\n")
    run("subtitle", r"""
check(SubtitleIO.count("sub.srt")==2);
check(SubtitleIO.startTime("sub.srt",0)==0.125);
check(SubtitleIO.endTime("sub.srt",0)==1.25);
check(SubtitleIO.text("sub.srt",0)=="Hello\nWorld");
check(SubtitleIO.count("short.vtt")==1);
check(SubtitleIO.startTime("short.vtt",0)==1.0);
check(SubtitleIO.convert("sub.srt","sub.vtt"));
check(SubtitleIO.convert("sub.vtt","roundtrip.srt"));
check(SubtitleIO.writeCue("single.srt","New cue",250,1750));
check(SubtitleIO.writeCue("single.vtt","New cue",250,1750));
check(!SubtitleIO.convert("sub.srt","sub.vtt"));
check(!SubtitleIO.writeCue("single.srt","Replace",0,1000));
check(SubtitleIO.count("invalid.srt")==-1);
check(SubtitleIO.count("styled.vtt")==-1);
check(SubtitleIO.count("bad-utf8.srt")==-1);
check(SubtitleIO.count("missing.srt")==-1);
check(SubtitleIO.startTime("sub.srt",2)==-1.0);
check(SubtitleIO.text("sub.srt",-1)=="");
check(!SubtitleIO.writeCue("bad-cue.vtt","Bad",1000,500));
check(!SubtitleIO.convert("sub.srt","bad-cue.txt"));
""",20,p)
    assert (p/"sub.vtt").read_bytes()==vtt.encode("utf-8")
    assert (p/"roundtrip.srt").read_bytes()==srt.encode("utf-8")
    assert (p/"single.srt").read_bytes()==b"1\n00:00:00,250 --> 00:00:01,750\nNew cue\n\n"
    assert (p/"single.vtt").read_bytes()==b"WEBVTT\n\n00:00:00.250 --> 00:00:01.750\nNew cue\n\n"
    assert not (p/"bad-cue.vtt").exists()
    assert not (p/"bad-cue.txt").exists()

    # At native 2 FPS, only frames 1 and 2 have captions.
    captions="1\n00:00:00,500 --> 00:00:01,500\nA\n\n"
    overlap=captions+"2\n00:00:01,000 --> 00:00:01,500\nB\n\n"
    (p/"burn.srt").write_bytes(captions.encode("utf-8"))
    (p/"burn-overlap.srt").write_bytes(overlap.encode("utf-8"))
    (p/"burn.vtt").write_bytes(b"WEBVTT\n\n00:00:00.500 --> 00:00:01.500\nA\n\n")
    (p/"burn-outside.srt").write_bytes(b"1\n00:00:03,000 --> 00:00:04,000\nA\n\n")
    run("video", """
check(VideoIO.burnSubtitles("trim-av.mkv","burn.srt","burned.mkv","Sans Bold",6,16777215,1));
check(VideoIO.burnSubtitles("trim-av.mkv","burn.vtt","burned-vtt.mkv","Sans Bold",6,16777215,1));
check(VideoIO.burnSubtitles("trim-av.mkv","burn-overlap.srt","burned-overlap.mkv","Sans Bold",6,16777215,1));
check(VideoIO.burnSubtitles("trim-av.mkv","burn-outside.srt","burned-outside.mkv","Sans Bold",6,16777215,1));
check(!VideoIO.burnSubtitles("trim-av.mkv","burn.srt","burned.mkv","Sans Bold",6,16777215,1));
check(!VideoIO.burnSubtitles("trim-av.mkv","missing.srt","burn-missing.mkv","Sans Bold",6,16777215,1));
check(!VideoIO.burnSubtitles("trim-av.mkv","invalid.srt","burn-invalid.mkv","Sans Bold",6,16777215,1));
check(!VideoIO.burnSubtitles("trim-av.mkv","burn.srt","burn-margin.mkv","Sans Bold",6,16777215,16));
check(!VideoIO.burnSubtitles("trim-av.mkv","burn.srt","burn-size.mkv","Sans Bold",0,16777215,1));
""",9,p)
    burned=decoded("burned.mkv")
    stacked=decoded("burned-overlap.mkv")
    assert len(burned)==len(stacked)==len(title_original)
    for frame in range(4):
        lo,hi=frame*stride,(frame+1)*stride
        assert (burned[lo:hi]!=title_original[lo:hi])==(1<=frame<3)
        assert (stacked[lo:hi]!=burned[lo:hi])==(frame==2)
        # Last row lies within the reserved bottom margin.
        assert burned[hi-48:hi]==title_original[hi-48:hi]
    assert decoded("burned-vtt.mkv")==burned
    assert decoded("burned-outside.mkv")==title_original
    for name in ("burned.mkv","burned-vtt.mkv","burned-overlap.mkv","burned-outside.mkv"):
        assert audio_samples(name,0)==pcm+pcm2
    for name in ("burn-missing.mkv","burn-invalid.mkv","burn-margin.mkv","burn-size.mkv"):
        assert not (p/name).exists(),name

    # Shared scalar automation: mixed outgoing interpolation and strict input validation.
    (p/"curve.tsv").write_bytes(b"# seconds value outgoing\n0\t10\tlinear\n2\t30\tsmooth\n4\t50\tstep\n6\t70\tlinear\n")
    (p/"duplicate.tsv").write_bytes(b"0 1 linear\n0 2 step\n")
    (p/"bad-mode.tsv").write_bytes(b"0 1 unknown\n")
    (p/"bad-number.tsv").write_bytes(b"0 nan linear\n")
    (p/"descending.tsv").write_bytes(b"2 1 linear\n1 2 linear\n")
    (p/"single-key.tsv").write_bytes(b"1 -5 linear\r\n")
    run("keyframe", """
check(KeyframeIO.valid("curve.tsv"));
check(KeyframeIO.count("curve.tsv")==4);
check(KeyframeIO.valueAt("curve.tsv",-1.0)==10.0);
check(KeyframeIO.valueAt("curve.tsv",1.0)==20.0);
check(KeyframeIO.valueAt("curve.tsv",2.0)==30.0);
check(KeyframeIO.valueAt("curve.tsv",2.5)==33.125);
check(KeyframeIO.valueAt("curve.tsv",3.5)==46.875);
check(KeyframeIO.valueAt("curve.tsv",5.0)==50.0);
check(KeyframeIO.valueAt("curve.tsv",6.0)==70.0);
check(KeyframeIO.valueAt("curve.tsv",9.0)==70.0);
check(KeyframeIO.valueAt("single-key.tsv",10.0)==-5.0);
check(KeyframeIO.interpolate(0.0,100.0,0.25,"linear")==25.0);
check(KeyframeIO.interpolate(0.0,100.0,0.25,"smooth")==15.625);
check(KeyframeIO.interpolate(0.0,100.0,0.25,"easeIn")==6.25);
check(KeyframeIO.interpolate(0.0,100.0,0.25,"easeOut")==43.75);
check(KeyframeIO.interpolate(0.0,100.0,0.25,"step")==0.0);
check(KeyframeIO.interpolate(0.0,100.0,1.0,"step")==100.0);
check(KeyframeIO.interpolate(0.0,100.0,-1.0,"linear")==0.0);
check(KeyframeIO.interpolate(0.0,100.0,2.0,"linear")==100.0);
check(!KeyframeIO.valid("duplicate.tsv"));
check(!KeyframeIO.valid("bad-mode.tsv"));
check(!KeyframeIO.valid("bad-number.tsv"));
check(!KeyframeIO.valid("descending.tsv"));
check(KeyframeIO.count("missing-curve.tsv")==-1);
""",24,p)

    # A native 16x16 RGBA watermark has transparent, half-alpha and opaque stripes.
    # Create a separate asymmetric 2x1 opaque image to test exact nearest-neighbor rotation.
    (p/"anim.png").write_bytes(b"\x89PNG\r\n\x1a\n"+
        png_chunk(b"IHDR",struct.pack(">IIBBBBB",2,1,8,6,0,0,0))+
        png_chunk(b"IDAT",zlib.compress(b"\0\x00\xff\x00\xff\xff\xff\x00\xff"))+
        png_chunk(b"IEND",b""))
    static_curve="x 0 2 linear\ny 0 2 linear\nwidth 0 4 linear\nheight 0 2 linear\nrotation 0 0 linear\nopacity 0 1 linear\n"
    moving_curve=static_curve.replace("x 0 2 linear\n","x 0 2 linear\nx 1 6 linear\n").replace("opacity 0 1 linear\n","opacity 0 0 linear\nopacity 1 1 linear\n")
    rotate_curve=static_curve.replace("rotation 0 0 linear","rotation 0 90 linear")
    scale_curve=static_curve.replace("width 0 4 linear","width 0 4 linear\nwidth 1 8 linear")
    for name,curve in (("anim-static",static_curve),("anim-moving",moving_curve),
        ("anim-rotate",rotate_curve),("anim-scale",scale_curve),
        ("anim-invalid",static_curve.replace("opacity 0 1","opacity 0 2")),
        ("anim-incomplete",static_curve.replace("rotation 0 0 linear\n","")),
        ("anim-duplicate",static_curve+"x 0 4 linear\n"),
        ("anim-clipped",static_curve.replace("x 0 2","x 0 -2"))):
        (p/(name+".tsv")).write_text(curve,encoding="utf-8")
    run("video", """
check(VideoIO.animateOverlay("trim-av.mkv","anim.png","anim-static.tsv","animated-static.mkv",16,16,4));
check(VideoIO.animateOverlay("trim-av.mkv","anim.png","anim-moving.tsv","animated-moving.mkv",16,16,4));
check(VideoIO.animateOverlay("trim-av.mkv","anim.png","anim-rotate.tsv","animated-rotate.mkv",16,16,4));
check(VideoIO.animateOverlay("trim-av.mkv","anim.png","anim-scale.tsv","animated-scale.mkv",16,16,4));
check(VideoIO.animateOverlay("trim-av.mkv","anim.png","anim-clipped.tsv","animated-clipped.mkv",16,16,4));
check(!VideoIO.animateOverlay("trim-av.mkv","anim.png","anim-static.tsv","animated-static.mkv",16,16,4));
check(!VideoIO.animateOverlay("trim-av.mkv","anim.png","anim-invalid.tsv","anim-bad.mkv",16,16,4));
check(!VideoIO.animateOverlay("trim-av.mkv","anim.png","anim-incomplete.tsv","anim-bad.mkv",16,16,4));
check(!VideoIO.animateOverlay("trim-av.mkv","anim.png","anim-duplicate.tsv","anim-bad.mkv",16,16,4));
check(!VideoIO.animateOverlay("trim-av.mkv","missing.png","anim-static.tsv","anim-bad.mkv",16,16,4));
check(!VideoIO.animateOverlay("trim-av.mkv","anim.png","anim-static.tsv","anim-bad.mkv",16,16,0));
""",11,p)
    def anim_pixel(data,frame,x,y):
        i=frame*stride+(y*16+x)*3
        return data[i:i+3]
    green=bytes((0,255,0))
    yellow=bytes((255,255,0))
    # Exact expected full-frame RGB for unrotated integer rectangles.
    for name in ("animated-static","animated-moving","animated-scale","animated-clipped","animated-rotate"):
        data=decoded(name+".mkv")
        assert len(data)==len(original),name
        assert audio_samples(name+".mkv",0)==pcm+pcm2,name
        for frame in range(8):
            for yy in range(16):
                for xx in range(16):
                    base=anim_pixel(original,frame,xx,yy)
                    x=2+min(frame,4) if name=="animated-moving" else -2 if name=="animated-clipped" else 2
                    width=4+min(frame,4) if name=="animated-scale" else 4
                    alpha=int(min(frame,4)*65535/4+0.5) if name=="animated-moving" else 65535
                    color=None
                    if name=="animated-rotate":
                        if 3<=xx<5 and 1<=yy<5:
                            color=green if yy<3 else yellow
                    elif x<=xx<x+width and 2<=yy<4:
                        color=green if (xx-x+0.5)<width/2 else yellow
                    expected=base if color is None else bytes((b*(65535-alpha)+c*alpha+32767)//65535 for b,c in zip(base,color))
                    assert anim_pixel(data,frame,xx,yy)==expected,(name,frame,xx,yy)
    assert not (p/"anim-bad.mkv").exists()

    # Independent RGBA fixture, including a partially transparent pixel.
    (p/"raster-anim.png").write_bytes(b"\x89PNG\r\n\x1a\n"+
        png_chunk(b"IHDR",struct.pack(">IIBBBBB",2,1,8,6,0,0,0))+
        png_chunk(b"IDAT",zlib.compress(b"\0\x64\x32\x00\xff\x14\x28\x3c\x80"))+
        png_chunk(b"IEND",b""))
    (p/"raster-curve.tsv").write_text("red 0 1 linear\nred 1 2 linear\nopacity 0 1 linear\nopacity 1 0.5 linear\nbrightness 0 0 linear\nbrightness 1 0.1 linear\n",encoding="utf-8")
    (p/"raster-invalid.tsv").write_text("opacity 0 2 linear\n",encoding="utf-8")
    (p/"raster-duplicate.tsv").write_text("red 0 1 linear\nred 0 2 linear\n",encoding="utf-8")
    (p/"raster-identity.tsv").write_text("red 0 1 linear\n",encoding="utf-8")
    (p/"collision000001.png").write_bytes(b"keep")
    run("raster", """
check(RasterIO.animate("raster-anim.png","raster-curve.tsv","raster-frame",3,2));
check(RasterIO.animate("raster-anim.png","raster-identity.tsv","raster-id",1,2));
check(!RasterIO.animate("raster-anim.png","raster-curve.tsv","raster-frame",3,2));
check(!RasterIO.animate("raster-anim.png","raster-curve.tsv","collision",3,2));
check(!RasterIO.animate("raster-anim.png","raster-invalid.tsv","raster-bad",3,2));
check(!RasterIO.animate("raster-anim.png","raster-duplicate.tsv","raster-bad",3,2));
check(!RasterIO.animate("missing.png","raster-curve.tsv","raster-bad",3,2));
check(!RasterIO.animate("raster-anim.png","raster-curve.tsv","raster-bad",0,2));
check(!RasterIO.animate("raster-anim.png","raster-curve.tsv","raster-bad",3,0));
""",9,p)
    def rgba_png(name):
        return subprocess.check_output(["ffmpeg","-v","error","-i",str(p/name),
            "-frames:v","1","-f","rawvideo","-pix_fmt","rgba","-"],timeout=30)
    source_rgba=bytes((100,50,0,255,20,40,60,128))
    assert rgba_png("raster-id000000.png")==source_rgba
    for i in range(3):
        t=i/2
        expected=bytearray()
        for j in (0,4):
            for c in range(3):
                v=source_rgba[j+c]*(1+t if c==0 else 1)+255*0.1*t
                expected.append(int(min(255,v)+0.5))
            expected.append(int(source_rgba[j+3]*(1-0.5*t)+0.5))
        assert rgba_png(f"raster-frame{i:06d}.png")==expected,i
    assert not (p/"collision000000.png").exists()
    assert (p/"collision000001.png").read_bytes()==b"keep"
    assert not list(p.glob("raster-bad*.png"))

    run("keyframe", """
KeyframeCurve a;
KeyframeCurve b;
check(a.count()==0);
check(a.add(0.0,10.0,"linear"));
check(a.add(2.0,30.0,"smooth"));
check(a.valueAt(1.0)==20.0);
check(b.count()==0);
check(!a.add(2.0,40.0,"linear"));
check(a.count()==2);
check(a.set(1,2.0,50.0,"linear"));
check(a.valueAt(1.0)==30.0);
check(!a.set(1,0.0,99.0,"linear"));
check(a.valueAt(1.0)==30.0);
check(b.load("curve.tsv"));
check(b.count()==4);
check(b.valueAt(2.5)==33.125);
check(!b.load("duplicate.tsv"));
check(b.count()==4);
check(b.valueAt(2.5)==33.125);
check(b.remove(1));
check(b.count()==3);
check(b.valueAt(2.0)==30.0);
check(!b.remove(99));
check(a.count()==2);
a.clear();
check(a.count()==0);
check(b.count()==3);
""",24,p)
    # Exercise a second resident curve and reuse it after clearing.
    (p/"resident.tsv").write_bytes(b"0 10 linear\n2 30 linear\n")
    run("keyframe", """
KeyframeCurve resident;
check(resident.load("resident.tsv"));
check(resident.valueAt(1.0)==20.0);
resident.clear();
check(resident.add(0.0,1.0,"step"));
check(resident.add(1.0,2.0,"linear"));
check(resident.valueAt(0.5)==1.0);
""",5,p)

    # Persistent images receive ordinary numerical parameters, independent of KeyframeIO.
    run("raster", """
RasterImage a;
RasterImage b;
check(!a.loaded());
check(a.width()==0 && a.height()==0);
check(!a.save("unloaded.png"));
check(a.load("raster-anim.png"));
check(a.loaded() && a.width()==2 && a.height()==1);
check(!b.loaded());
check(a.red()==1.0 && a.green()==1.0 && a.blue()==1.0);
check(a.opacity()==1.0 && a.brightness()==0.0);
check(a.red(2.0)==2.0);
check(a.green(0.5)==0.5);
check(a.blue(0.5)==0.5);
check(a.opacity(0.5)==0.5);
check(a.brightness(0.1)==0.1);
check(a.save("object-adjusted.png"));
check(!a.save("object-adjusted.png"));
check(!a.load("missing.png"));
check(a.loaded() && a.red()==2.0 && a.opacity()==0.5);
a.opacity(2.0);
check(a.opacity()==0.5);
a.reset();
check(a.red()==1.0 && a.green()==1.0 && a.blue()==1.0);
check(a.opacity()==1.0 && a.brightness()==0.0);
check(a.save("object-reset.png"));
check(b.load("raster-anim.png"));
0.5 => b.red;
check(b.red()==0.5 && a.red()==1.0);
check(b.save("object-second.png"));
check(b.load("raster-anim.png") && b.red()==1.0);
a.clear();
check(!a.loaded() && a.width()==0 && a.height()==0);
check(b.loaded());
check(!a.save("cleared.png"));
""",28,p)
    assert rgba_png("object-adjusted.png")==bytes((226,51,26,128,66,46,56,64))
    assert rgba_png("object-reset.png")==source_rgba
    assert rgba_png("object-second.png")==bytes((50,50,0,255,10,40,60,128))
    assert not (p/"unloaded.png").exists() and not (p/"cleared.png").exists()
    # Explicit composition test: no manifest, no native coupling between the classes.
    run("raster", """
@import "KeyframeIO";
RasterImage image;
KeyframeCurve gain;
check(image.load("raster-anim.png"));
check(gain.add(0.0,1.0,"linear"));
check(gain.add(2.0,2.0,"linear"));
gain.valueAt(1.0) => image.red;
check(image.red()==1.5);
check(image.save("object-curve.png"));
""",5,p)
    assert rgba_png("object-curve.png")==bytes((150,50,0,255,30,40,60,128))

    # VectorImage: independent geometry, opacity and persistent document.
    svg='<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16"><rect x="2" y="4" width="4" height="2" fill="#ff0000"/></svg>'
    (p/"vector-object.svg").write_text(svg,encoding="utf-8")
    run("vector", """
VectorImage a;
VectorImage b;
check(!a.loaded());
check(!a.render("vector-empty.png",16,16));
check(a.load("vector-object.svg"));
check(a.loaded() && !b.loaded());
check(a.x()==0.0 && a.y()==0.0 && a.scaleX()==1.0 && a.scaleY()==1.0);
check(a.rotation()==0.0 && a.opacity()==1.0);
check(a.render("vector-original.png",16,16));
check(!a.render("vector-original.png",16,16));
4.0 => a.x;
2.0 => a.y;
check(a.x()==4.0 && a.y()==2.0);
check(a.render("vector-moved.png",16,16));
check(!a.load("missing.svg"));
check(a.loaded() && a.x()==4.0);
check(!a.setSvg("not svg"));
check(a.x()==4.0);
a.reset();
check(a.x()==0.0 && a.y()==0.0);
2.0 => a.scaleX;
2.0 => a.scaleY;
check(a.render("vector-scaled.png",16,16));
a.reset();
90.0 => a.rotation;
check(a.render("vector-rotated.png",16,16));
a.reset();
0.5 => a.opacity;
check(a.render("vector-alpha.png",16,16));
a.opacity(2.0);
check(a.opacity()==0.5);
check(!a.render("vector-invalid-size.png",0,16));
check(b.setSvg("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16'><rect width='16' height='16' fill='blue'/></svg>"));
check(b.loaded() && b.opacity()==1.0 && a.opacity()==0.5);
check(b.render("vector-blue.png",16,16));
a.clear();
check(!a.loaded() && b.loaded());
check(!a.render("vector-cleared.png",16,16));
""",25,p)
    def vector_rect(x0,y0,x1,y1,alpha=255,color=(255,0,0)):
        return bytes(c for y in range(16) for x in range(16)
                     for c in ((*color,alpha) if x0<=x<x1 and y0<=y<y1 else (0,0,0,0)))
    assert rgba_png("vector-original.png")==vector_rect(2,4,6,6)
    assert rgba_png("vector-moved.png")==vector_rect(6,6,10,8)
    assert rgba_png("vector-scaled.png")==vector_rect(0,0,4,4)
    assert rgba_png("vector-rotated.png")==vector_rect(10,2,12,6)
    assert rgba_png("vector-alpha.png")==vector_rect(2,4,6,6,128)
    assert rgba_png("vector-blue.png")==vector_rect(0,0,16,16,color=(0,0,255))
    for name in ("vector-empty.png","vector-invalid-size.png","vector-cleared.png"):
        assert not (p/name).exists()
    run("vector", """
@import "KeyframeIO";
VectorImage image;
KeyframeCurve move;
check(image.load("vector-object.svg"));
check(move.add(0.0,0.0,"linear"));
check(move.add(2.0,8.0,"linear"));
move.valueAt(1.0) => image.x;
check(image.x()==4.0);
check(image.render("vector-curve.png",16,16));
""",5,p)
    assert rgba_png("vector-curve.png")==vector_rect(6,4,10,6)

    # Export transformed scene and independently inspect OBJ world-space vertices.
    run("model", """
Model3D a;
Model3D b;
check(!a.loaded());
check(a.meshes()==-1 && a.vertices()==-1);
check(!a.save("model-empty/out.obj"));
check(a.load("triangle.obj"));
check(a.loaded() && !b.loaded());
check(a.meshes()==1 && a.vertices()==3);
check(a.x()==0.0 && a.y()==0.0 && a.z()==0.0);
check(a.rotationX()==0.0 && a.rotationY()==0.0 && a.rotationZ()==0.0);
check(a.scaleX()==1.0 && a.scaleY()==1.0 && a.scaleZ()==1.0);
check(a.save("model-original/out.obj"));
check(!a.save("model-original/out.obj"));
2.0 => a.x;
3.0 => a.y;
4.0 => a.z;
check(a.x()==2.0 && a.y()==3.0 && a.z()==4.0);
check(a.save("model-translated/out.obj"));
check(!a.load("missing.obj"));
check(a.loaded() && a.x()==2.0);
a.reset();
2.0 => a.scaleX;
3.0 => a.scaleY;
4.0 => a.scaleZ;
check(a.save("model-scaled/out.obj"));
a.scaleX(0.0);
check(a.scaleX()==2.0);
a.reset();
90.0 => a.rotationZ;
check(a.save("model-rotated/out.obj"));
a.reset();
90.0 => a.rotationX;
check(a.save("model-rotated-x/out.obj"));
a.reset();
90.0 => a.rotationY;
check(a.save("model-rotated-y/out.obj"));
check(b.load("triangle.obj"));
check(b.rotationY()==0.0 && a.rotationY()==90.0);
check(b.save("model-independent/out.obj"));
a.clear();
check(!a.loaded() && b.loaded());
check(!a.save("model-cleared/out.obj"));
""",25,p)
    def obj_vertices(path):
        return sorted(tuple(round(float(v),5)+0.0 for v in line.split()[1:4])
                      for line in (p/path).read_text(encoding="utf-8").splitlines()
                      if line.startswith("v "))
    def check_model(directory,expected):
        assert obj_vertices(directory+"/out.obj")==sorted(expected),directory
    triangle=[(0,0,0),(1,0,0),(0,1,0)]
    check_model("model-original",triangle)
    check_model("model-translated",[(2,3,4),(3,3,4),(2,4,4)])
    check_model("model-scaled",[(0,0,0),(2,0,0),(0,3,0)])
    check_model("model-rotated",[(0,0,0),(0,1,0),(-1,0,0)])
    check_model("model-rotated-x",[(0,0,0),(1,0,0),(0,0,1)])
    check_model("model-rotated-y",[(0,0,0),(0,0,-1),(0,1,0)])
    check_model("model-independent",triangle)
    assert not (p/"model-empty").exists() and not (p/"model-cleared").exists()
    run("model", """
@import "KeyframeIO";
Model3D model;
KeyframeCurve motion;
check(model.load("triangle.obj"));
check(motion.add(0.0,0.0,"linear"));
check(motion.add(2.0,6.0,"linear"));
motion.valueAt(1.0) => model.z;
check(model.z()==3.0);
check(model.save("model-curve/out.obj"));
""",5,p)
    check_model("model-curve",[(0,0,3),(1,0,3),(0,1,3)])

    import wave
    with wave.open(str(p/"buffer-input.wav"),"wb") as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(8000)
        wav.writeframes(struct.pack("<8h",8192,-8192,16384,-16384,4096,8192,0,0))
    run("audio", """
AudioBuffer a;
AudioBuffer b;
check(!a.loaded());
check(a.frames()==0 && a.channels()==0 && a.sampleRate()==0);
check(!a.save("buffer-empty.wav"));
check(a.load("buffer-input.wav"));
check(a.loaded() && !b.loaded());
check(a.frames()==4 && a.channels()==2 && a.sampleRate()==8000);
check(a.seconds()==0.0005);
check(a.sampleAt(0,0)==0.25 && a.sampleAt(0,1)==-0.25);
check(a.gain()==1.0 && a.balance()==0.0);
2.0 => a.gain;
0.5 => a.balance;
check(a.valueAt(0,0)==0.25 && a.valueAt(0,1)==-0.5);
check(a.sampleAt(0,0)==0.25);
check(a.save("buffer-adjusted.wav"));
check(!a.save("buffer-adjusted.wav"));
check(!a.load("missing.wav"));
check(a.loaded() && a.gain()==2.0);
a.gain(-1.0);
a.balance(2.0);
check(a.gain()==2.0 && a.balance()==0.5);
a.reset();
check(a.gain()==1.0 && a.balance()==0.0);
check(a.save("buffer-reset.wav"));
check(a.setSample(0,0,0.75));
check(a.sampleAt(0,0)==0.75);
check(!a.setSample(4,0,0.5));
check(!a.setSample(0,2,0.5));
check(!a.setSample(0,0,2.0));
2.0 => a.gain;
check(a.valueAt(0,0)==1.0);
check(b.load("buffer-input.wav"));
check(b.sampleAt(0,0)==0.25 && b.gain()==1.0);
a.clear();
check(!a.loaded() && a.frames()==0 && b.loaded());
check(!a.save("buffer-cleared.wav"));
""",28,p)
    def buffer_pcm(name):
        data=subprocess.check_output(["ffmpeg","-v","error","-i",str(p/name),
            "-f","s32le","-acodec","pcm_s32le","-"],timeout=30)
        return struct.unpack("<"+"i"*(len(data)//4),data)
    # PCM24 rounding permits one output least significant bit (256 in s32).
    def check_buffer(name,expected):
        actual=buffer_pcm(name)
        assert len(actual)==len(expected),name
        assert all(abs(a-b*65536)<=256 for a,b in zip(actual,expected)),(name,actual)
    check_buffer("buffer-adjusted.wav",[8192,-16384,16384,-32768,4096,16384,0,0])
    check_buffer("buffer-reset.wav",[8192,-8192,16384,-16384,4096,8192,0,0])
    assert not (p/"buffer-empty.wav").exists() and not (p/"buffer-cleared.wav").exists()
    run("audio", """
@import "KeyframeIO";
AudioBuffer audio;
KeyframeCurve volume;
check(audio.load("buffer-input.wav"));
check(volume.add(0.0,0.0,"linear"));
check(volume.add(2.0,2.0,"linear"));
volume.valueAt(0.5) => audio.gain;
check(audio.gain()==0.5);
check(audio.save("buffer-curve.wav"));
""",5,p)
    check_buffer("buffer-curve.wav",[4096,-4096,8192,-8192,2048,4096,0,0])

    # Independent SMF fixture: tempo, note-on, note-off, controller, end-of-track.
    midi_track=bytes.fromhex("00 ff 51 03 07 a1 20 00 90 3c 64 83 60 80 3c 20 00 b0 07 50 00 ff 2f 00")
    (p/"sequence-input.mid").write_bytes(b"MThd"+struct.pack(">IHHH",6,0,1,480)+
        b"MTrk"+struct.pack(">I",len(midi_track))+midi_track)
    run("midi", """
MidiSequence a;
MidiSequence b;
check(!a.loaded());
check(a.tracks()==-1 && a.ticksPerQuarter()==-1);
check(!a.save("sequence-empty.mid"));
check(a.load("sequence-input.mid"));
check(a.loaded() && !b.loaded());
check(a.tracks()==1 && a.events(0)==5);
check(a.ticksPerQuarter()==480);
check(a.tickAt(0,1)==0 && a.tickAt(0,2)==480);
check(a.sizeAt(0,1)==3);
check(a.byteAt(0,1,0)==144 && a.byteAt(0,1,1)==60 && a.byteAt(0,1,2)==100);
check(a.byteAt(0,99,0)==-1 && a.byteAt(0,1,3)==-1);
check(a.transpose()==0 && a.velocityGain()==1.0);
12 => a.transpose;
0.5 => a.velocityGain;
check(a.transpose()==12 && a.velocityGain()==0.5);
check(a.save("sequence-adjusted.mid"));
check(!a.save("sequence-adjusted.mid"));
check(a.byteAt(0,1,1)==60 && a.byteAt(0,1,2)==100);
check(!a.load("missing.mid"));
check(a.loaded() && a.transpose()==12);
a.transpose(200);
a.velocityGain(0.0);
check(a.transpose()==12 && a.velocityGain()==0.5);
127 => a.transpose;
check(!a.save("sequence-overflow.mid"));
a.reset();
check(a.transpose()==0 && a.velocityGain()==1.0);
check(a.setVelocity(0,1,80));
check(a.byteAt(0,1,2)==80);
check(!a.setVelocity(0,2,80));
check(!a.setVelocity(0,1,0));
check(a.save("sequence-edited.mid"));
check(b.load("sequence-input.mid"));
check(b.byteAt(0,1,2)==100 && a.byteAt(0,1,2)==80);
a.clear();
check(!a.loaded() && b.loaded());
check(!a.save("sequence-cleared.mid"));
""",30,p)
    # Parse the actual exported SMF independently (including running status).
    def smf_events(path):
        data=(p/path).read_bytes()
        assert data[:4]==b"MThd" and struct.unpack_from(">H",data,12)[0]==480
        pos=8+struct.unpack_from(">I",data,4)[0]
        assert data[pos:pos+4]==b"MTrk"
        end=pos+8+struct.unpack_from(">I",data,pos+4)[0]
        pos+=8
        tick=0
        running=None
        events=[]
        def vlq():
            nonlocal pos
            v=0
            for _ in range(4):
                b=data[pos]
                pos+=1
                v=(v<<7)|(b&127)
                if b<128:
                    return v
            raise AssertionError("invalid VLQ")
        while pos<end:
            tick+=vlq()
            status=data[pos]
            if status>=128:
                pos+=1
                running=status if status<240 else None
            else:
                status=running
            assert status is not None
            if status==255:
                kind=data[pos]
                pos+=1
                size=vlq()
                msg=(status,kind,*data[pos:pos+size])
            else:
                size=1 if status&240 in (192,208) else 2
                msg=(status,*data[pos:pos+size])
            pos+=size
            events.append((tick,msg))
        assert pos==end
        return events
    baseline=smf_events("sequence-input.mid")
    expected=list(baseline)
    expected[1]=(0,(144,72,50))
    expected[2]=(480,(128,72,32))
    assert smf_events("sequence-adjusted.mid")==expected
    expected=list(baseline)
    expected[1]=(0,(144,60,80))
    assert smf_events("sequence-edited.mid")==expected
    assert not any((p/name).exists() for name in ("sequence-empty.mid","sequence-overflow.mid","sequence-cleared.mid"))
    run("midi", """
@import "KeyframeIO";
MidiSequence sequence;
KeyframeCurve pitch;
check(sequence.load("sequence-input.mid"));
check(pitch.add(0.0,0.0,"linear"));
check(pitch.add(2.0,12.0,"linear"));
(pitch.valueAt(1.0) $ int) => sequence.transpose;
check(sequence.transpose()==6);
check(sequence.save("sequence-curve.mid"));
""",5,p)
    expected=list(baseline)
    expected[1]=(0,(144,66,100))
    expected[2]=(480,(128,66,32))
    assert smf_events("sequence-curve.mid")==expected

    # Persistent sequential decoder: two independent readers, original and processed frames.
    run("video", """
VideoClip a;
VideoClip b;
check(!a.loaded());
check(a.frameIndex()==-1 && a.timeSeconds()==-1.0);
check(a.next()==-1);
check(!a.saveFrame("clip-empty.ppm"));
check(a.load("generated.mkv"));
check(a.loaded() && !b.loaded());
check(a.width()==16 && a.height()==16);
check(a.frameIndex()==0 && a.timeSeconds()==0.0 && !a.eof());
check(a.red()==1.0 && a.green()==1.0 && a.blue()==1.0 && a.brightness()==0.0);
check(a.saveFrame("clip-object-first.ppm"));
check(!a.saveFrame("clip-object-first.ppm"));
check(a.saveFrame("clip-object-scaled.ppm",8,8));
check(!a.saveFrame("clip-invalid-size.ppm",0,8));
0.5 => a.red;
0.1 => a.brightness;
check(a.saveFrame("clip-object-adjusted.ppm"));
a.red(-1.0);
check(a.red()==0.5 && a.brightness()==0.1);
check(!a.load("missing.mkv"));
check(a.loaded() && a.frameIndex()==0 && a.red()==0.5);
check(a.next()==1);
check(a.frameIndex()==1 && a.timeSeconds()==0.5 && a.eof());
a.reset();
check(a.red()==1.0 && a.brightness()==0.0);
check(a.saveFrame("clip-object-last.ppm"));
check(a.next()==0);
check(a.frameIndex()==1 && a.loaded());
check(b.load("generated.mkv"));
check(b.frameIndex()==0 && a.frameIndex()==1);
check(b.saveFrame("clip-object-independent.ppm"));
check(a.load("generated.mkv"));
check(a.frameIndex()==0 && !a.eof());
a.clear();
check(!a.loaded() && b.loaded());
check(!a.saveFrame("clip-cleared.ppm"));
""",30,p)
    def clip_ppm(name,w=16,h=16):
        header=f"P6\n{w} {h}\n255\n".encode()
        data=(p/name).read_bytes()
        assert data.startswith(header),name
        return data[len(header):]
    assert clip_ppm("clip-object-first.ppm")==red
    assert clip_ppm("clip-object-scaled.ppm",8,8)==bytes((255,0,0))*64
    assert clip_ppm("clip-object-adjusted.ppm")==bytes((153,26,26))*256
    assert clip_ppm("clip-object-last.ppm")==blue
    assert clip_ppm("clip-object-independent.ppm")==red
    assert not any((p/name).exists() for name in ("clip-empty.ppm","clip-invalid-size.ppm","clip-cleared.ppm"))
    run("video", """
@import "KeyframeIO";
VideoClip clip;
KeyframeCurve gain;
check(clip.load("generated.mkv"));
check(gain.add(0.0,1.0,"linear"));
check(gain.add(1.0,0.0,"linear"));
check(clip.next()==1);
gain.valueAt(clip.timeSeconds()) => clip.blue;
check(clip.blue()==0.5);
check(clip.saveFrame("clip-object-curve.ppm"));
""",6,p)
    assert clip_ppm("clip-object-curve.ppm")==bytes((0,0,128))*256

    (p/"track-input.srt").write_text("1\n00:00:01,000 --> 00:00:02,000\nFirst\n\n2\n00:00:01,500 --> 00:00:03,000\nSecond\n\n",encoding="utf-8")
    run("subtitle", """
SubtitleTrack a;
SubtitleTrack b;
check(a.count()==0);
check(!a.save("track-empty.srt"));
check(a.load("track-input.srt"));
check(a.count()==2 && b.count()==0);
check(a.startTime(0)==1.0 && a.endTime(0)==2.0);
check(a.text(0)=="First");
check(a.startTime(99)==-1.0 && a.text(99)=="");
check(a.textAt(0.5)=="");
check(a.textAt(1.0)=="First");
check(a.textAt(1.75)=="First\\nSecond");
check(a.textAt(2.0)=="Second");
check(a.textAt(3.0)=="");
check(a.offset()==0.0 && a.timeScale()==1.0);
0.5 => a.offset;
2.0 => a.timeScale;
check(a.offset()==0.5 && a.timeScale()==2.0);
check(a.textAt(2.5)=="First");
check(a.textAt(4.0)=="First\\nSecond");
check(a.save("track-shifted.vtt"));
check(!a.save("track-shifted.vtt"));
check(a.startTime(0)==1.0);
check(!a.load("missing.srt"));
check(a.count()==2 && a.offset()==0.5);
a.timeScale(0.0);
check(a.timeScale()==2.0);
-100.0 => a.offset;
check(!a.save("track-negative.srt"));
a.reset();
check(a.offset()==0.0 && a.timeScale()==1.0);
check(a.set(0,1000,2000,"Edited"));
check(a.add(3000,4000,"Third"));
check(a.count()==3 && a.text(2)=="Third");
check(!a.add(2000,3000,"Out of order"));
check(!a.set(0,2000,3000,"Out of order"));
check(!a.add(4000,5000,"<markup>"));
check(!a.add(5000,4000,"Invalid"));
check(a.remove(1));
check(a.count()==2 && a.text(1)=="Third");
check(!a.remove(99));
check(a.save("track-edited.srt"));
check(b.load("track-input.srt"));
check(b.text(0)=="First" && a.text(0)=="Edited");
a.clear();
check(a.count()==0 && b.count()==2);
""",38,p)
    assert (p/"track-shifted.vtt").read_bytes()==b"WEBVTT\n\n00:00:02.500 --> 00:00:04.500\nFirst\n\n00:00:03.500 --> 00:00:06.500\nSecond\n\n"
    assert (p/"track-edited.srt").read_bytes()==b"1\n00:00:01,000 --> 00:00:02,000\nEdited\n\n2\n00:00:03,000 --> 00:00:04,000\nThird\n\n"
    assert not (p/"track-empty.srt").exists() and not (p/"track-negative.srt").exists()
    run("subtitle", """
@import "KeyframeIO";
SubtitleTrack captions;
KeyframeCurve delay;
check(captions.load("track-input.srt"));
check(delay.add(0.0,0.0,"linear"));
check(delay.add(2.0,2.0,"linear"));
delay.valueAt(1.0) => captions.offset;
check(captions.offset()==1.0);
check(captions.textAt(2.0)=="First");
check(captions.save("track-curve.srt"));
""",6,p)
    assert (p/"track-curve.srt").read_bytes()==b"1\n00:00:02,000 --> 00:00:03,000\nFirst\n\n2\n00:00:02,500 --> 00:00:04,000\nSecond\n\n"

    run("raster", """
RasterImage image;
int pixels[8];
check(image.create(2,1));
check(image.width()==2 && image.height()==1);
check(image.pixelAt(0,0,3)==0);
check(image.setPixel(0,0,200,100,50,255));
check(image.pixelAt(0,0,0)==200);
check(!image.setPixel(2,0,1,2,3,4));
check(!image.setPixel(0,0,256,0,0,255));
0.5 => image.red;
check(image.exportPixels(pixels));
check(pixels[0]==100 && pixels[1]==100 && pixels[2]==50 && pixels[3]==255);
check(image.pixelAt(0,0,0)==200);
RasterImage copy;
check(copy.importPixels(2,1,pixels));
check(copy.red()==1.0 && copy.pixelAt(0,0,0)==100);
-1 => pixels[0];
check(!copy.importPixels(2,1,pixels));
check(copy.pixelAt(0,0,0)==100);
check(!copy.create(0,1));
check(copy.save("memory-raster.png"));
""",16,p)
    assert decoded("memory-raster.png")==bytes((100,100,50,0,0,0))
    run("audio", """
AudioBuffer audio;
float samples[4];
check(audio.create(2,8000,2));
check(audio.frames()==2 && audio.channels()==2);
check(audio.sampleAt(0,0)==0.0);
check(audio.setSample(0,0,0.25));
check(audio.setSample(0,1,-0.5));
2.0 => audio.gain;
check(audio.exportSamples(samples));
check(samples[0]==0.5 && samples[1]==-1.0);
AudioBuffer copy;
check(copy.importSamples(8000,2,samples));
check(copy.gain()==1.0 && copy.sampleAt(0,0)==0.5);
2.0 => samples[0];
check(!copy.importSamples(8000,2,samples));
check(copy.sampleAt(0,0)==0.5);
check(!copy.create(2,0,2));
check(copy.save("memory-audio.wav"));
""",13,p)
    check_buffer("memory-audio.wav",[16384,-32768,0,0])
    run("video", """
@import "RasterIO";
VideoClip clip;
RasterImage image;
int pixels[1024];
check(clip.load("generated.mkv"));
check(clip.exportPixels(pixels));
check(pixels[0]==255 && pixels[1]==0 && pixels[2]==0 && pixels[3]==255);
check(image.importPixels(16,16,pixels));
check(image.setPixel(0,0,0,255,0,255));
check(image.save("memory-video-frame.png"));
check(clip.next()==1);
0.5 => clip.blue;
check(clip.exportPixels(pixels));
check(pixels[0]==0 && pixels[1]==0 && pixels[2]==128 && pixels[3]==255);
check(image.importPixels(16,16,pixels));
check(image.save("memory-video-blue.png"));
""",11,p)
    assert decoded("memory-video-frame.png")==bytes((0,255,0))+red[3:]
    assert decoded("memory-video-blue.png")==bytes((0,0,128))*256

    # Incremental writer with raster synthesis and decoder -> raster -> encoder exchange.
    run("video", """
@import "RasterIO";
VideoWriter writer;
RasterImage image;
VideoClip clip;
int pixels[1024];
int bad[4];
check(!writer.active());
check(writer.frames()==0);
check(!writer.appendFrame(pixels));
check(!writer.finish());
check(!writer.open("writer-invalid.mp4",16,16,2));
check(!writer.open("writer-invalid.mkv",0,16,2));
check(writer.open("writer-created.mkv",16,16,2));
check(writer.active());
check(!writer.open("writer-other.mkv",16,16,2));
check(!writer.appendFrame(bad));
check(!writer.appendFrame(pixels));
check(writer.frames()==0);
check(image.create(16,16));
for(0 => int y; y<16; y++)
    for(0 => int x; x<16; x++) image.setPixel(x,y,255,0,0,255);
check(image.exportPixels(pixels));
check(writer.appendFrame(pixels));
check(writer.frames()==1);
for(0 => int y; y<16; y++)
    for(0 => int x; x<16; x++) image.setPixel(x,y,0,0,255,255);
check(image.exportPixels(pixels));
256 => pixels[0];
check(!writer.appendFrame(pixels));
check(writer.frames()==1);
0 => pixels[0];
check(writer.appendFrame(pixels));
check(writer.frames()==2);
check(writer.finish());
check(!writer.active() && writer.frames()==0);
check(!writer.open("writer-created.mkv",16,16,2));
check(writer.open("writer-empty.mkv",16,16,2));
check(!writer.finish());
check(writer.open("writer-abort.mkv",16,16,2));
check(writer.appendFrame(pixels));
writer.abort();
check(!writer.active());
check(clip.load("generated.mkv"));
check(writer.open("writer-processed.mkv",16,16,2));
0.5 => clip.red;
check(clip.exportPixels(pixels));
check(image.importPixels(16,16,pixels));
check(image.setPixel(0,0,0,255,0,255));
check(image.exportPixels(pixels));
check(writer.appendFrame(pixels));
check(clip.next()==1);
check(clip.exportPixels(pixels));
check(writer.appendFrame(pixels));
check(writer.finish());
""",40,p)
    assert decoded("writer-created.mkv")==red+blue
    assert decoded("writer-processed.mkv")==bytes((0,255,0))+bytes((128,0,0))*255+blue
    assert not any((p/name).exists() for name in ("writer-invalid.mp4","writer-invalid.mkv",
        "writer-other.mkv","writer-empty.mkv","writer-abort.mkv"))
    run("video", """
VideoWriter a;
VideoWriter b;
int pixels[1024];
for(0 => int i; i<256; i++) 255 => pixels[4*i+3];
check(a.open("writer-a.mkv",16,16,4));
check(b.open("writer-b.mkv",16,16,4));
check(a.appendFrame(pixels));
check(b.appendFrame(pixels));
check(a.frames()==1 && b.frames()==1);
check(a.appendFrame(pixels));
check(a.finish());
check(b.active() && b.frames()==1);
check(b.finish());
""",9,p)
    assert decoded("writer-a.mkv")==bytes(16*16*3*2)
    assert decoded("writer-b.mkv")==bytes(16*16*3)

    # Synchronized raster + AudioBuffer blocks; no temporary media files.
    run("video", """
@import "RasterIO";
@import "AudioIO";
VideoWriter writer;
RasterImage image;
AudioBuffer audio;
int pixels[1024];
float block[8000];
float bad[1];
check(writer.nextAudioFrames()==0);
check(writer.audioFrames()==0);
check(!writer.openAV("avwriter-bad.mkv",16,16,2,7999,2));
check(!writer.openAV("avwriter-bad.mkv",16,16,2,8000,3));
check(writer.openAV("avwriter.mkv",16,16,2,8000,2));
check(writer.nextAudioFrames()==4000);
check(image.create(16,16));
for(0 => int y; y<16; y++)
    for(0 => int x; x<16; x++) image.setPixel(x,y,255,0,0,255);
check(image.exportPixels(pixels));
check(audio.create(4000,8000,2));
for(0 => int i; i<4000; i++) {
    audio.setSample(i,0,0.25);
    audio.setSample(i,1,-0.5);
}
check(audio.exportSamples(block));
check(!writer.appendFrame(pixels));
check(!writer.appendAV(pixels,bad));
2.0 => block[0];
check(!writer.appendAV(pixels,block));
check(writer.frames()==0 && writer.audioFrames()==0);
check(audio.exportSamples(block));
0 => pixels[3];
check(!writer.appendAV(pixels,block));
check(writer.frames()==0 && writer.audioFrames()==0);
255 => pixels[3];
check(writer.appendAV(pixels,block));
check(writer.frames()==1 && writer.audioFrames()==4000);
check(writer.nextAudioFrames()==4000);
for(0 => int y; y<16; y++)
    for(0 => int x; x<16; x++) image.setPixel(x,y,0,0,255,255);
check(image.exportPixels(pixels));
0.5 => audio.gain;
check(audio.exportSamples(block));
check(writer.appendAV(pixels,block));
check(writer.frames()==2 && writer.audioFrames()==8000);
check(writer.finish());
check(!writer.active() && writer.audioFrames()==0);
check(!writer.openAV("avwriter.mkv",16,16,2,8000,2));
check(writer.openAV("avwriter-abort.mkv",16,16,2,8000,2));
check(writer.appendAV(pixels,block));
writer.abort();
check(!writer.active());
check(writer.open("avwriter-video.mkv",16,16,2));
check(!writer.appendAV(pixels,block));
check(writer.appendFrame(pixels));
check(writer.finish());
""",34,p)
    assert decoded("avwriter.mkv")==red+blue
    def writer_pcm(name):
        return subprocess.check_output(["ffmpeg","-nostdin","-v","error","-i",str(p/name),
            "-map","0:a:0","-f","s16le","-acodec","pcm_s16le","-"],timeout=30)
    assert writer_pcm("avwriter.mkv")==struct.pack("<hh",8192,-16384)*4000+struct.pack("<hh",4096,-8192)*4000
    assert not (p/"avwriter-bad.mkv").exists() and not (p/"avwriter-abort.mkv").exists()
    # 8000 / 3 is nonintegral: use rounded cumulative boundaries, not fixed blocks.
    run("video", """
VideoWriter writer;
int pixels[1024];
float longBlock[2667];
float shortBlock[2666];
for(0 => int i; i<256; i++) 255 => pixels[i*4+3];
check(writer.openAV("avwriter-fractional.mkv",16,16,3,8000,1));
check(writer.nextAudioFrames()==2667);
check(writer.appendAV(pixels,longBlock));
check(writer.nextAudioFrames()==2666);
check(!writer.appendAV(pixels,longBlock));
check(writer.appendAV(pixels,shortBlock));
check(writer.nextAudioFrames()==2667);
check(writer.appendAV(pixels,longBlock));
check(writer.audioFrames()==8000 && writer.frames()==3);
check(writer.finish());
""",10,p)
    assert decoded("avwriter-fractional.mkv")==bytes(16*16*3*3)
    assert writer_pcm("avwriter-fractional.mkv")==bytes(16000)

    run("video", """
@import "AudioIO";
VideoAudio source;
VideoAudio other;
AudioBuffer audio;
float samples[16000];
float block[8000];
float small[3];
check(!source.loaded());
check(source.frames()==0 && source.channels()==0 && source.sampleRate()==0);
check(!source.exportSamples(samples));
check(!source.load("generated.mkv",0));
check(!source.load("avwriter.mkv",-1));
check(!source.load("avwriter.mkv",1));
check(source.load("avwriter.mkv",0));
check(source.loaded() && !other.loaded());
check(source.frames()==8000 && source.channels()==2 && source.sampleRate()==8000);
check(!source.exportSamples(block));
check(source.exportSamples(samples));
check(samples[0]==0.25 && samples[1]==-0.5);
check(samples[8000]==0.125 && samples[8001]==-0.25);
check(source.readSamples(4000,block));
check(block[0]==0.125 && block[1]==-0.25);
check(!source.readSamples(4001,block));
check(!source.readSamples(-1,block));
check(!source.readSamples(0,small));
check(audio.importSamples(source.sampleRate(),source.channels(),samples));
check(audio.frames()==8000 && audio.sampleAt(4000,0)==0.125);
0.5 => audio.gain;
check(audio.save("videoaudio-memory.wav"));
check(!source.load("missing.mkv",0));
check(source.frames()==8000);
check(other.load("avwriter-fractional.mkv",0));
check(other.frames()==8000 && other.channels()==1);
source.clear();
check(!source.loaded() && source.frames()==0 && other.loaded());
""",26,p)
    assert writer_pcm("videoaudio-memory.wav")==struct.pack("<hh",4096,-8192)*4000+struct.pack("<hh",2048,-4096)*4000

    run("raster", """
RasterImage image;
int overlay[8];
check(!image.fill(0,0,0,255));
check(image.create(4,4));
check(image.fill(0,0,0,255));
check(image.fillRect(1,1,2,2,255,0,0,255));
check(image.pixelAt(1,1,0)==255 && image.pixelAt(0,0,0)==0);
check(!image.fillRect(3,3,2,2,0,0,0,255));
check(image.line(0,0,3,3,0,255,0,255));
check(image.pixelAt(2,2,1)==255 && image.pixelAt(1,2,0)==255);
check(!image.line(-1,0,3,3,0,0,0,255));
check(image.fillCircle(0,0,1,0,0,255,255));
check(image.pixelAt(0,1,2)==255 && image.pixelAt(1,0,2)==255);
check(image.pixelAt(1,1,1)==255);
check(!image.fillCircle(0,0,-1,0,0,0,255));
check(!image.fill(256,0,0,255));
check(image.save("draw-primitives.png"));
0.5 => image.red;
check(image.crop(1,1,2,2));
check(image.width()==2 && image.height()==2 && image.red()==0.5);
check(!image.crop(1,1,2,2));
check(image.resize(4,4));
check(image.pixelAt(0,0,1)==255 && image.pixelAt(3,0,0)==255);
check(image.pixelAt(0,3,0)==255 && image.pixelAt(3,3,1)==255);
check(!image.resize(0,4));
check(image.save("draw-resize.png"));
image.reset();
check(image.fill(0,0,255,255));
255 => overlay[0]; 128 => overlay[3];
255 => overlay[5]; 255 => overlay[7];
check(image.composite(0,0,2,1,overlay,1.0));
check(image.pixelAt(0,0,0)==128 && image.pixelAt(0,0,2)==127);
check(image.pixelAt(1,0,1)==255);
check(image.composite(-1,1,2,1,overlay,0.5));
check(image.pixelAt(0,1,1)==128 && image.pixelAt(0,1,2)==128);
check(!image.composite(0,0,2,1,overlay,2.0));
256 => overlay[0];
check(!image.composite(0,0,2,1,overlay,1.0));
check(image.pixelAt(0,0,0)==128);
check(image.save("draw-composite.png"));
check(image.fill(0,0,0,0));
255 => overlay[0];
check(image.composite(0,0,2,1,overlay,1.0));
check(image.pixelAt(0,0,0)==255 && image.pixelAt(0,0,3)==128);
check(image.pixelAt(1,0,1)==255 && image.pixelAt(1,0,3)==255);
""",37,p)
    black=bytes((0,0,0))
    green=bytes((0,255,0))
    assert decoded("draw-primitives.png")==b"".join((blue[:3],blue[:3],black,black,
        blue[:3],green,red[:3],black,black,red[:3],green,black,black,black,black,green))
    halfred=bytes((128,0,0))
    assert decoded("draw-resize.png")==((green*2+halfred*2)*2+(halfred*2+green*2)*2)
    assert decoded("draw-composite.png")==bytes((128,0,127))+green+blue[:3]*2+bytes((0,128,128))+blue[:3]*11

    run("vector", """
@import "RasterIO";
VectorImage image;
VectorImage copy;
RasterImage raster;
int pixels[1024];
float points[6];
check(image.shapes()==-1);
check(!image.addRect(0.0,0.0,4.0,4.0,16711680,1.0));
check(image.create(16,16));
check(image.loaded() && image.shapes()==0);
check(!image.create(0,16));
check(image.addRect(2.0,4.0,4.0,2.0,16711680,1.0));
check(image.addCircle(12.0,12.0,2.0,255,1.0));
check(image.addLine(0.0,8.0,8.0,8.0,1.0,65280,1.0));
0.0 => points[0]; 0.0 => points[1];
4.0 => points[2]; 0.0 => points[3];
0.0 => points[4]; 4.0 => points[5];
check(image.addPolygon(points,16777215,1.0));
check(image.shapes()==4);
check(!image.addRect(0.0,0.0,-1.0,1.0,0,1.0));
check(!image.addCircle(2.0,2.0,1.0,16777216,1.0));
check(!image.addLine(0.0,0.0,1.0,1.0,0.0,0,1.0));
check(!image.addPolygon(points,0,2.0));
check(image.shapes()==4);
check(image.save("vector-shapes.svg"));
check(!image.save("vector-shapes.svg"));
check(copy.load("vector-shapes.svg"));
check(copy.shapes()==-1);
check(!copy.removeShape(0));
check(image.removeShape(3));
check(image.removeShape(2));
check(image.removeShape(1));
check(!image.removeShape(1));
check(image.shapes()==1);
check(image.exportPixels(16,16,pixels));
check(pixels[(4*16+2)*4]==255 && pixels[(4*16+2)*4+3]==255);
check(pixels[3]==0);
check(raster.importPixels(16,16,pixels));
check(raster.save("vector-memory.png"));
2.0 => image.x;
check(image.exportPixels(16,16,pixels));
check(pixels[(4*16+2)*4+3]==0 && pixels[(4*16+4)*4]==255);
check(image.save("vector-raw.svg"));
check(image.render("vector-moved-memory.png",16,16));
image.clear();
check(!image.loaded() && image.shapes()==-1);
check(!image.exportPixels(16,16,pixels));
check(copy.loaded());
""",37,p)
    import xml.etree.ElementTree as ET
    ns="{http://www.w3.org/2000/svg}"
    assert [e.tag for e in ET.parse(p/"vector-shapes.svg").getroot()]==[ns+"rect",ns+"circle",ns+"line",ns+"polygon"]
    assert len(ET.parse(p/"vector-raw.svg").getroot())==1
    def expected_vector_rect(left):
        return b"".join(bytes((255,0,0)) if left<=x<left+4 and 4<=y<6 else bytes(3)
                        for y in range(16) for x in range(16))
    assert decoded("vector-memory.png")==expected_vector_rect(2)
    assert decoded("vector-moved-memory.png")==expected_vector_rect(4)

    run("model", """
Model3D model;
Model3D copy;
float xyz[9];
int triangles[3];
float outXYZ[9];
int outFaces[3];
check(!model.loaded());
check(model.faces(0)==-1);
check(!model.createMesh(xyz,triangles));
1.0 => xyz[3]; 1.0 => xyz[7];
1 => triangles[1]; 2 => triangles[2];
check(model.createMesh(xyz,triangles));
check(model.loaded() && model.meshes()==1 && model.vertices()==3);
check(model.meshVertices(0)==3 && model.faces(0)==1);
check(model.faceSize(0,0)==3 && model.faceIndex(0,0,2)==2);
check(model.faceIndex(0,0,3)==-1);
check(model.vertexAt(0,1,0)==1.0);
check(model.exportVertices(0,outXYZ));
check(model.exportTriangles(0,outFaces));
check(outXYZ[3]==1.0 && outXYZ[7]==1.0 && outFaces[2]==2);
check(model.setVertex(1,2.0,0.0,0.0));
check(model.vertexAt(0,1,0)==2.0);
check(!model.setVertex(3,0.0,0.0,0.0));
3 => triangles[2];
check(!model.createMesh(xyz,triangles));
check(model.vertexAt(0,1,0)==2.0);
2.0 => model.x;
check(model.save("procedural-triangle/mesh.obj"));
check(!model.save("procedural-triangle/mesh.obj"));
check(copy.load("procedural-triangle/mesh.obj"));
check(copy.faces(0)==1);
check(!copy.setVertex(0,0.0,0.0,0.0));
check(model.createBox(2.0,4.0,6.0));
check(model.vertices()==8 && model.faces(0)==12);
check(model.vertexAt(0,0,0)==-1.0 && model.vertexAt(0,0,1)==-2.0 && model.vertexAt(0,0,2)==-3.0);
check(model.x()==0.0);
check(!model.createBox(0.0,4.0,6.0));
check(model.vertices()==8);
check(!model.exportVertices(0,outXYZ));
check(model.save("procedural-box/mesh.obj"));
model.clear();
check(!model.loaded() && copy.loaded());
""",31,p)
    def procedural_obj(name):
        lines=(p/name).read_text().splitlines()
        return ([tuple(map(float,l.split()[1:4])) for l in lines if l.startswith("v ")],
                [l.split()[1:] for l in lines if l.startswith("f ")])
    vertices,faces=procedural_obj("procedural-triangle/mesh.obj")
    assert set(vertices)=={(2.,0.,0.),(4.,0.,0.),(2.,1.,0.)} and len(faces)==1
    vertices,faces=procedural_obj("procedural-box/mesh.obj")
    assert set(vertices)=={(x,y,z) for x in (-1.,1.) for y in (-2.,2.) for z in (-3.,3.)}
    assert len(faces)==12 and all(len(face)==3 for face in faces)

    run("midi", """
MidiSequence seq;
MidiSequence copy;
int pitches[3];
check(!seq.loaded());
check(seq.addTrack()==-1);
check(!seq.create(0,120.0));
check(!seq.create(480,0.0));
check(seq.create(480,120.0));
check(seq.loaded() && seq.tracks()==1 && seq.ticksPerQuarter()==480);
check(seq.addTrack()==1);
check(seq.tracks()==2);
check(seq.addProgram(1,0,0,10));
check(seq.addCC(1,0,0,7,100));
check(seq.addNote(1,480,240,0,72,100));
check(seq.addNote(1,0,240,0,60,80));
60 => pitches[0]; 64 => pitches[1]; 67 => pitches[2];
check(seq.addChord(1,960,480,0,pitches,90));
check(seq.addTempo(960,60.0));
check(!seq.addTempo(960,90.0));
check(!seq.addNote(2,0,1,0,60,80));
check(!seq.addNote(1,0,0,0,60,80));
check(!seq.addNote(1,0,1,16,60,80));
check(!seq.addNote(1,0,1,0,128,80));
check(!seq.addCC(1,0,0,128,100));
check(!seq.addProgram(1,0,0,128));
60 => pitches[1];
check(!seq.addChord(1,960,480,0,pitches,90));
check(seq.events(1)==12);
check(seq.events(0)==2);
check(seq.tickAt(1,0)==0 && seq.tickAt(1,11)==1440);
check(seq.save("procedural.mid"));
check(!seq.save("procedural.mid"));
check(copy.load("procedural.mid"));
check(copy.tracks()==2 && copy.ticksPerQuarter()==480);
check(copy.addTrack()==-1);
2 => seq.transpose;
0.5 => seq.velocityGain;
check(seq.save("procedural-adjusted.mid"));
check(seq.create(96,90.0));
check(seq.tracks()==1 && seq.transpose()==0 && seq.velocityGain()==1.0);
seq.clear();
check(!seq.loaded() && copy.loaded());
""",34,p)
    # Independent SMF parsing: no dependency on midifile for verification.
    def procedural_midi(name):
        data=(p/name).read_bytes()
        assert data[:4]==b"MThd" and int.from_bytes(data[4:8],"big")==6
        assert data[8:14]==b"\x00\x01\x00\x02\x01\xe0"
        tracks=[]
        pos=14
        def vlq():
            nonlocal pos
            value=0
            for _ in range(4):
                b=data[pos]; pos+=1
                value=(value<<7)|(b&127)
                if b<128: return value
            raise AssertionError("invalid VLQ")
        for _ in range(2):
            assert data[pos:pos+4]==b"MTrk"
            end=pos+8+int.from_bytes(data[pos+4:pos+8],"big")
            pos+=8
            tick=0; running=None; events=[]
            while pos<end:
                tick+=vlq()
                status=data[pos]
                if status>=128: pos+=1
                else:
                    assert running is not None
                    status=running
                if status==255:
                    kind=data[pos]; pos+=1
                    size=vlq()
                    payload=data[pos:pos+size]; pos+=size
                    events.append((tick,status,kind,payload))
                    running=None
                else:
                    assert 128<=status<240
                    running=status
                    size=1 if status&240 in (192,208) else 2
                    payload=data[pos:pos+size]; pos+=size
                    events.append((tick,status,payload))
            assert pos==end
            tracks.append(events)
        assert pos==len(data)
        return tracks
    original=procedural_midi("procedural.mid")
    adjusted=procedural_midi("procedural-adjusted.mid")
    assert [(e[0],e[3]) for e in original[0] if e[1:3]==(255,81)]==[(0,b"\x07\xa1\x20"),(960,b"\x0f\x42\x40")]
    assert (0,192,bytes((10,))) in original[1] and (0,176,bytes((7,100))) in original[1]
    def note_ons(events):
        return sorted((e[0],e[2][0],e[2][1]) for e in events if e[1]==144 and e[2][1]>0)
    assert note_ons(original[1])==[(0,60,80),(480,72,100),(960,60,90),(960,64,90),(960,67,90)]
    assert note_ons(adjusted[1])==[(0,62,40),(480,74,50),(960,62,45),(960,66,45),(960,69,45)]

    run("audio", """
AudioBuffer audio;
float input[8];
float tail[4];
float block[4];
float bad[3];
check(!audio.reverse());
check(!audio.normalize(1.0));
0.25 => input[0]; -0.5 => input[1];
0.5 => input[2]; -0.25 => input[3];
0.75 => input[4]; -1.0 => input[5];
0.125 => input[6]; -0.125 => input[7];
check(audio.importSamples(8000,2,input));
check(audio.peak()==1.0);
0.5 => audio.gain;
check(audio.crop(1,2));
check(audio.frames()==2 && audio.gain()==0.5);
check(audio.sampleAt(0,0)==0.5 && audio.sampleAt(1,1)==-1.0);
check(!audio.crop(1,2));
check(audio.reverse());
check(audio.sampleAt(0,0)==0.75 && audio.sampleAt(0,1)==-1.0);
check(audio.sampleAt(1,0)==0.5 && audio.sampleAt(1,1)==-0.25);
check(audio.normalize(0.5));
check(audio.peak()==0.5 && audio.sampleAt(0,0)==0.375);
check(!audio.normalize(2.0));
check(audio.fade(0,2,0.0,1.0));
check(audio.sampleAt(0,0)==0.0 && audio.sampleAt(1,0)==0.25);
check(!audio.fade(0,1,0.0,1.0));
check(!audio.fade(0,2,-1.0,1.0));
0.5 => tail[0]; -0.5 => tail[1];
0.25 => tail[2]; -0.25 => tail[3];
check(audio.appendSamples(tail));
check(audio.frames()==4);
check(!audio.appendSamples(bad));
2.0 => tail[0];
check(!audio.appendSamples(tail));
check(audio.frames()==4);
0.5 => tail[0];
check(audio.mixSamples(1,tail,2.0));
check(audio.sampleAt(1,0)==1.0 && audio.sampleAt(1,1)==-1.0);
check(audio.sampleAt(2,0)==1.0 && audio.sampleAt(2,1)==-1.0);
check(!audio.mixSamples(3,tail,1.0));
check(!audio.mixSamples(0,tail,-1.0));
check(audio.readSamples(1,block));
check(block[0]==0.5 && block[1]==-0.5 && block[2]==0.5 && block[3]==-0.5);
check(!audio.readSamples(3,block));
check(block[0]==0.5);
check(audio.remove(1,1));
check(audio.frames()==3 && audio.sampleAt(1,0)==1.0);
check(!audio.remove(0,3));
check(audio.save("audio-edited.wav"));
check(audio.create(2,8000,1));
check(audio.normalize(1.0) && audio.peak()==0.0);
check(audio.fade(0,2,1.0,0.0));
check(audio.reverse());
check(audio.peak()==0.0);
""",41,p)
    check_buffer("audio-edited.wav",[0,0,16384,-16384,4096,-4096])

    run("audio", """
AudioBuffer audio;
float stereo[8];
float mono[4];
check(!audio.resample(16000));
check(!audio.changeSpeed(2.0));
check(!audio.convertChannels(1));
check(!audio.lowPass(1000.0));
0.5 => stereo[0]; 0.0 => stereo[1];
0.25 => stereo[2]; 0.75 => stereo[3];
-0.5 => stereo[4]; 0.5 => stereo[5];
-0.25 => stereo[6]; -0.75 => stereo[7];
check(audio.importSamples(8000,2,stereo));
0.5 => audio.gain;
check(audio.convertChannels(1));
check(audio.channels()==1 && audio.frames()==4 && audio.gain()==0.5);
check(audio.sampleAt(0,0)==0.25 && audio.sampleAt(1,0)==0.5);
check(audio.sampleAt(2,0)==0.0 && audio.sampleAt(3,0)==-0.5);
check(!audio.convertChannels(3));
check(audio.convertChannels(2));
check(audio.sampleAt(1,0)==0.5 && audio.sampleAt(1,1)==0.5);
check(audio.resample(8000));
check(audio.frames()==4 && audio.sampleAt(1,0)==0.5);
check(!audio.resample(0));
check(!audio.changeSpeed(0.0));
for(0 => int i; i<4; i++) 0.25 => mono[i];
check(audio.importSamples(8000,1,mono));
check(audio.resample(16000));
check(audio.frames()==8 && audio.sampleRate()==16000);
check(Math.fabs(audio.sampleAt(3,0)-0.25)<0.000001);
check(audio.changeSpeed(2.0));
check(audio.frames()==4 && audio.sampleRate()==16000);
check(Math.fabs(audio.sampleAt(3,0)-0.25)<0.000001);
check(audio.save("dsp-constant.wav"));
check(audio.resample(8000));
check(audio.frames()==2 && audio.sampleRate()==8000);
check(audio.changeSpeed(0.5));
check(audio.frames()==4 && audio.sampleRate()==8000);
check(!audio.lowPass(4000.0));
check(!audio.highPass(0.0));
check(audio.importSamples(8000,1,mono));
check(audio.lowPass(1000.0));
1.0-Math.exp(-2.0*pi*1000.0/8000.0) => float coeff;
check(Math.fabs(audio.sampleAt(0,0)-0.25*coeff)<0.000001);
check(Math.fabs(audio.sampleAt(1,0)-0.25*(1.0-Math.pow(1.0-coeff,2.0)))<0.000001);
check(audio.importSamples(8000,1,mono));
check(audio.highPass(1000.0));
check(Math.fabs(audio.sampleAt(0,0)-0.25*(1.0-coeff))<0.000001);
check(Math.fabs(audio.sampleAt(3,0)-0.25*Math.pow(1.0-coeff,4.0))<0.000001);
check(audio.create(8,8000,2));
check(audio.setSample(0,0,1.0));
check(audio.lowPass(1000.0));
check(audio.sampleAt(0,1)==0.0 && audio.sampleAt(7,1)==0.0);
check(audio.create(1024,16000,1));
for(0 => int i; i<1024; i++) audio.setSample(i,0,0.8*Math.sin(2.0*pi*6000.0*i/16000.0));
check(audio.resample(8000));
check(audio.frames()==512);
check(audio.crop(64,384));
check(audio.peak()<0.01);
check(audio.create(4,8000,3));
check(!audio.convertChannels(2));
check(audio.channels()==3);
""",50,p)
    check_buffer("dsp-constant.wav",[8192]*4)

    run("midi", """
MidiSequence seq;
MidiSequence copy;
check(seq.notes(0)==-1);
check(seq.create(480,120.0));
check(seq.addTrack()==1);
check(seq.addNote(1,113,60,0,60,80));
check(seq.addNote(1,251,90,0,64,90));
check(seq.addCC(1,100,0,7,100));
check(seq.notes(1)==2);
check(seq.noteLength(1,0)==60 && seq.noteLength(1,1)==90);
check(seq.noteLength(1,2)==-1);
check(seq.quantize(1,120,1.0));
check(seq.noteLength(1,0)==60 && seq.noteLength(1,1)==90);
check(seq.tickAt(1,0)==100 && seq.tickAt(1,1)==120);
check(seq.tickAt(1,3)==240);
check(!seq.quantize(1,0,1.0));
check(!seq.shiftNotes(1,-121));
check(seq.tickAt(1,1)==120);
check(seq.shiftNotes(1,10));
check(seq.scaleLengths(1,0.5));
check(seq.noteLength(1,0)==30 && seq.noteLength(1,1)==45);
check(seq.setNote(1,0,130,40,62,70));
check(seq.noteLength(1,0)==40);
check(!seq.setNote(1,0,250,100,64,70));
check(seq.noteLength(1,0)==40);
check(seq.save("midi-edited.mid"));
check(copy.load("midi-edited.mid"));
check(copy.notes(1)==2 && copy.noteLength(1,0)==40);
check(copy.removeNote(1,0));
check(copy.notes(1)==1 && copy.noteLength(1,0)==45);
check(!copy.removeNote(1,1));
check(copy.save("midi-removed.mid"));
check(seq.notes(1)==2);
check(seq.removeNote(1,1));
check(seq.notes(1)==1);
check(seq.removeNote(1,0));
check(seq.notes(1)==0);
check(seq.create(480,120.0));
check(seq.addNote(0,0,100,0,60,80));
check(seq.addNote(0,50,100,0,60,80));
check(seq.notes(0)==-1);
check(!seq.quantize(0,120,1.0));
""",40,p)
    edited=procedural_midi("midi-edited.mid")
    removed=procedural_midi("midi-removed.mid")
    assert note_ons(edited[1])==[(130,62,70),(250,64,90)]
    assert note_ons(removed[1])==[(250,64,90)]
    assert (100,176,bytes((7,100))) in edited[1] and (100,176,bytes((7,100))) in removed[1]
    def note_ends(events):
        return sorted((e[0],e[2][0]) for e in events if e[1]==128 or (e[1]==144 and e[2][1]==0))
    assert note_ends(edited[1])==[(170,62),(295,64)]
    assert note_ends(removed[1])==[(295,64)]

    run("model", """
Model3D model;
[0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,0.0] @=> float xyz[];
[0,1,2] @=> int tris[];
[0.0,0.0,1.0,0.0,0.0,1.0] @=> float uv[];
float normals[9];
float outUV[6];
float shortUV[2];
[1.0,0.0,0.0,2.0,0.0,1.0,0.0,3.0,0.0,0.0,1.0,4.0] @=> float matrix[];
check(model.addMesh(xyz,tris)==-1);
check(!model.computeNormals(0));
check(model.createMesh(xyz,tris));
check(model.meshes()==1);
check(model.addMesh(xyz,tris)==1);
check(model.meshes()==2 && model.vertices()==6);
check(model.meshVertices(1)==3 && model.faces(1)==1);
check(!model.hasNormals(1));
check(model.computeNormals(1));
check(model.hasNormals(1));
check(model.exportNormals(1,normals));
check(normals[0]==0.0 && normals[1]==0.0 && normals[2]==1.0);
check(normals[5]==1.0 && normals[8]==1.0);
check(!model.exportUV(1,outUV));
check(model.setUV(1,uv));
check(model.exportUV(1,outUV));
check(outUV[2]==1.0 && outUV[5]==1.0);
check(!model.setUV(1,shortUV));
check(!model.transformMesh(1,shortUV));
check(model.transformMesh(1,matrix));
check(model.vertexAt(1,0,0)==2.0 && model.vertexAt(1,0,1)==3.0 && model.vertexAt(1,0,2)==4.0);
check(model.vertexAt(0,0,0)==0.0);
check(!model.hasNormals(1));
check(model.exportUV(1,outUV));
check(model.computeNormals(1));
check(model.setVertex(1,2,2.0,4.0,5.0));
check(!model.hasNormals(1));
check(model.computeNormals(1));
check(model.exportNormals(1,normals));
check(Math.fabs(normals[1]+0.70710678)<0.00001 && Math.fabs(normals[2]-0.70710678)<0.00001);
check(!model.setVertex(2,0,0.0,0.0,0.0));
check(model.computeNormals(0));
check(model.setVertex(0,0.0,0.0,0.0));
check(!model.hasNormals(0));
check(model.computeNormals(0));
check(model.setUV(0,uv));
check(model.save("multi-mesh/model.obj"));
check(!model.save("multi-mesh/model.obj"));
Model3D imported;
check(imported.load("multi-mesh/model.obj"));
check(imported.meshes()>=1);
check(imported.hasNormals(0));
check(!imported.setVertex(0,0,0.0,0.0,0.0));
check(imported.addMesh(xyz,tris)==-1);
""",43,p)
    obj=(p/"multi-mesh/model.obj").read_text(encoding="utf-8")
    rows=[line.split() for line in obj.splitlines()]
    assert len([row for row in rows if row and row[0]=="f"])==2
    assert any(row and row[0]=="vn" for row in rows)
    assert any(row and row[0]=="vt" for row in rows)
    assert any(row and row[0]=="v" and tuple(map(float,row[1:4]))==(2.0,4.0,5.0) for row in rows)

    run("model", """
Model3D model;
[0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,0.0] @=> float xyz[];
[0,1,2] @=> int tris[];
[1.0,0.0,0.0,1.0,0.0,1.0,0.0,1.0,0.0,0.0,1.0,0.5] @=> float rgba[];
[1.0,0.0,0.5,0.5,0.25,0.75] @=> float weights[];
float colorsOut[12];
float weightsOut[6];
float small[2];
check(model.materials()==-1);
check(model.addMaterial("red",1.0,0.0,0.0,1.0)==-1);
check(model.createMesh(xyz,tris));
check(model.materials()==1);
check(model.addMaterial("red",1.0,0.0,0.0,1.0)==1);
check(model.materials()==2);
check(model.addMaterial("bad",2.0,0.0,0.0,1.0)==-1);
check(model.assignMaterial(0,1));
check(model.materialAt(0)==1);
check(!model.assignMaterial(0,2));
check(model.materialAt(0)==1);
check(!model.exportColors(0,colorsOut));
check(model.setColors(0,rgba));
check(model.exportColors(0,colorsOut));
check(colorsOut[0]==1.0 && colorsOut[5]==1.0 && colorsOut[10]==1.0 && colorsOut[11]==0.5);
check(!model.setColors(0,small));
2.0 => rgba[0];
check(!model.setColors(0,rgba));
check(model.exportColors(0,colorsOut) && colorsOut[0]==1.0);
check(model.bones(0)==0);
check(!model.setSkin(0,2,small));
0.0 => weights[0];
check(!model.setSkin(0,2,weights));
check(model.bones(0)==0);
0.5 => weights[0];
check(model.setSkin(0,2,weights));
check(model.bones(0)==2);
check(model.exportWeights(0,weightsOut));
check(weightsOut[0]==1.0 && weightsOut[1]==0.0);
check(weightsOut[2]==0.5 && weightsOut[3]==0.5);
check(weightsOut[4]==0.25 && weightsOut[5]==0.75);
check(!model.setSkin(0,2,weights));
check(!model.exportWeights(0,small));
check(model.computeNormals(0));
check(model.save("weighted/model.gltf"));
check(!model.save("weighted/model.gltf"));
Model3D imported;
check(imported.load("weighted/model.gltf"));
check(imported.bones(0)==2);
check(imported.exportWeights(0,weightsOut));
check(Math.fabs(weightsOut[2]+weightsOut[3]-1.0)<0.00001);
check(imported.exportColors(0,colorsOut));
check(Math.fabs(colorsOut[11]-0.5)<0.00001);
""",39,p)
    import json
    skin_doc=json.loads((p/"weighted/model.gltf").read_text(encoding="utf-8"))
    assert len(skin_doc["skins"])==1
    assert len(skin_doc["skins"][0]["joints"])==2
    attrs=skin_doc["meshes"][0]["primitives"][0]["attributes"]
    assert all(k in attrs for k in ("POSITION","NORMAL","COLOR_0","JOINTS_0","WEIGHTS_0"))

    run("model", """
Model3D model;
[0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,0.0] @=> float xyz[];
[0,1,2] @=> int tris[];
[1.0,0.0,0.5,0.5,0.0,1.0] @=> float weights[];
float posed[9];
float small[3];
check(model.boneParent(0,0)==-2);
check(model.createMesh(xyz,tris));
check(!model.resetPose(0));
check(model.setSkin(0,2,weights));
check(model.boneParent(0,0)==-1 && model.boneParent(0,1)==-1);
check(model.exportPosedVertices(0,posed));
check(posed[3]==1.0 && posed[7]==1.0);
check(!model.exportPosedVertices(0,small));
check(model.setBoneParent(0,1,0));
check(model.boneParent(0,1)==0);
check(!model.setBoneParent(0,0,1));
check(!model.setBoneParent(0,1,1));
check(model.boneParent(0,0)==-1 && model.boneParent(0,1)==0);
check(model.setBonePose(0,0,2.0,0.0,0.0,0.0,0.0,0.0));
check(model.setBonePose(0,1,0.0,2.0,0.0,0.0,0.0,0.0));
check(model.exportPosedVertices(0,posed));
check(posed[0]==2.0 && posed[1]==0.0);
check(posed[3]==3.0 && posed[4]==1.0);
check(posed[6]==2.0 && posed[7]==3.0);
check(model.vertexAt(0,1,0)==1.0 && model.vertexAt(0,2,1)==1.0);
check(model.exportPosedVertices(0,posed) && posed[7]==3.0);
check(!model.setBonePose(0,2,0.0,0.0,0.0,0.0,0.0,0.0));
check(model.resetPose(0));
check(model.exportPosedVertices(0,posed));
check(posed[0]==0.0 && posed[3]==1.0 && posed[7]==1.0);
check(model.boneParent(0,1)==0);
check(model.setBonePose(0,0,0.0,0.0,0.0,0.0,0.0,90.0));
check(model.exportPosedVertices(0,posed));
check(Math.fabs(posed[3])<0.00001 && Math.fabs(posed[4]-1.0)<0.00001);
check(Math.fabs(posed[6]+1.0)<0.00001 && Math.fabs(posed[7])<0.00001);
check(model.setBoneParent(0,1,-1));
check(model.exportPosedVertices(0,posed));
check(Math.fabs(posed[3]-0.5)<0.00001 && Math.fabs(posed[4]-0.5)<0.00001);
check(model.setBoneParent(0,1,0));
check(model.resetPose(0));
check(model.save("hierarchical/model.gltf"));
""",36,p)
    hierarchy=json.loads((p/"hierarchical/model.gltf").read_text(encoding="utf-8"))
    nodes=hierarchy["nodes"]
    parent=next(i for i,n in enumerate(nodes) if n.get("name")=="Skin_0_0")
    child=next(i for i,n in enumerate(nodes) if n.get("name")=="Skin_0_1")
    assert child in nodes[parent]["children"]
    assert {parent,child}==set(hierarchy["skins"][0]["joints"])


    run("model", """
Model3D model;
[0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,0.0] @=> float xyz[];
[0,1,2] @=> int tris[];
[1.0,0.0,0.5,0.5,0.0,1.0] @=> float weights[];
[0.0,2.0] @=> float times[];
[0.0,0.0] @=> float badTimes[];
[0.0,0.0,0.0,0.0,0.0,0.0, 0.0,0.0,0.0,0.0,0.0,0.0,
 2.0,0.0,0.0,0.0,0.0,90.0, 0.0,0.0,0.0,0.0,0.0,0.0] @=> float poses[];
float out[9];
check(model.animations()==-1);
check(model.createMesh(xyz,tris));
check(model.animations()==0);
check(model.addAnimation(0,"walk",times,poses)==-1);
check(model.setSkin(0,2,weights));
check(model.setBoneParent(0,1,0));
check(model.addAnimation(0,"walk",badTimes,poses)==-1);
check(model.addAnimation(0,"walk",times,xyz)==-1);
check(model.addAnimation(0,"walk",times,poses)==0);
check(model.addAnimation(0,"walk",times,poses)==-1);
check(model.animations()==1);
check(model.animationDuration(0)==2.0);
check(model.animationDuration(1)==-1.0);
check(!model.sampleAnimation(1,1.0,0));
check(!model.sampleAnimation(0,1.0,2));
check(model.sampleAnimation(0,1.0,0));
check(model.exportPosedVertices(0,out));
check(Math.fabs(out[0]-1.0)<0.00001);
check(Math.fabs(out[3]-1.70710678)<0.00001 && Math.fabs(out[4]-0.70710678)<0.00001);
check(model.sampleAnimation(0,3.0,0));
check(model.exportPosedVertices(0,out) && Math.fabs(out[3]-2.0)<0.00001 && Math.fabs(out[4]-1.0)<0.00001);
check(model.sampleAnimation(0,-1.0,1));
check(model.exportPosedVertices(0,out) && Math.fabs(out[0]-1.0)<0.00001);
check(model.sampleAnimation(0,2.0,1));
check(model.exportPosedVertices(0,out) && out[0]==0.0 && out[3]==1.0);
check(model.sampleAnimation(0,-1.0,0));
check(model.exportPosedVertices(0,out) && out[3]==1.0);
check(model.vertexAt(0,1,0)==1.0);
check(model.resetPose(0));
check(model.save("animated/model.gltf"));
check(model.createMesh(xyz,tris));
check(model.animations()==0);
""",32,p)
    animated=json.loads((p/"animated/model.gltf").read_text(encoding="utf-8"))
    assert len(animated["animations"])==1
    clip=animated["animations"][0]
    assert clip["name"]=="walk"
    assert {c["target"]["path"] for c in clip["channels"]}>={"translation","rotation"}
    assert len({c["target"]["node"] for c in clip["channels"]})==2


    run("model", """
Model3D model;
float posed[9];
check(model.load("animated/model.gltf"));
check(model.animations()==1);
check(Math.fabs(model.animationDuration(0)-2.0)<0.00001);
check(model.exportPosedVertices(0,posed));
check(Math.fabs(posed[3]-1.0)<0.00001);
check(model.sampleAnimation(0,1.0,0));
check(model.exportPosedVertices(0,posed));
check(Math.fabs(posed[0]-1.0)<0.00001);
check(Math.fabs(posed[3]-1.70710678)<0.00001 && Math.fabs(posed[4]-0.70710678)<0.00001);
check(!model.sampleAnimation(1,0.0,0));
check(model.exportPosedVertices(0,posed) && Math.fabs(posed[0]-1.0)<0.00001);
check(model.sampleAnimation(0,-1.0,1));
check(model.exportPosedVertices(0,posed) && Math.fabs(posed[0]-1.0)<0.00001);
check(model.resetAnimation());
check(model.exportPosedVertices(0,posed) && Math.fabs(posed[3]-1.0)<0.00001);
check(model.load("triangle.obj"));
check(model.animations()==0);
check(!model.sampleAnimation(0,0.0,0));
""",18,p)


    run("model", """
Model3D model;
[0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,0.0] @=> float xyz[];
[0,1,2] @=> int tris[];
[1.0,1.0,1.0] @=> float weights[];
[0.0,2.0] @=> float times[];
[0.0,0.0,0.0,0.0,0.0,0.0, 0.0,0.0,0.0,0.0,0.0,0.0] @=> float a[];
[0.0,0.0,0.0,0.0,0.0,0.0, 4.0,0.0,0.0,0.0,0.0,90.0] @=> float b[];
float out[9];
check(model.createMesh(xyz,tris));
check(model.setSkin(0,1,weights));
check(model.addAnimation(0,"idle",times,a)==0);
check(model.addAnimation(0,"move",times,b)==1);
check(model.blendAnimations(0,0.0,1,2.0,0.0,0));
check(model.exportPosedVertices(0,out) && Math.fabs(out[3]-1.0)<0.00001);
check(model.blendAnimations(0,0.0,1,2.0,0.5,0));
check(model.exportPosedVertices(0,out));
check(Math.fabs(out[0]-2.0)<0.00001);
check(Math.fabs(out[3]-2.70710678)<0.00001 && Math.fabs(out[4]-0.70710678)<0.00001);
check(!model.blendAnimations(0,0.0,1,2.0,1.1,0));
check(!model.blendAnimations(0,0.0,2,2.0,0.5,0));
check(!model.blendAnimations(0,0.0,1,2.0,0.5,2));
check(model.exportPosedVertices(0,out) && Math.fabs(out[0]-2.0)<0.00001);
check(model.blendAnimations(0,0.0,1,2.0,1.0,0));
check(model.exportPosedVertices(0,out) && Math.fabs(out[0]-4.0)<0.00001 && Math.fabs(out[4]-1.0)<0.00001);
check(model.blendAnimations(0,0.0,1,-1.0,0.5,1));
check(model.exportPosedVertices(0,out) && Math.fabs(out[0]-1.0)<0.00001);
check(model.resetPose(0));
check(model.save("blended/model.gltf"));
Model3D imported;
check(imported.load("blended/model.gltf"));
check(imported.animations()==2);
check(imported.blendAnimations(0,0.0,1,2.0,0.5,0));
check(imported.exportPosedVertices(0,out));
check(Math.fabs(out[0]-2.0)<0.00001 && Math.fabs(out[3]-2.70710678)<0.00001);
check(imported.resetAnimation());
check(imported.exportPosedVertices(0,out) && Math.fabs(out[0])<0.00001);
""",27,p)


    run("model", """
Model3D model;
[0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,0.0] @=> float xyz[];
[0,1,2] @=> int tris[];
[1.0,0.0,0.5,0.5,0.0,1.0] @=> float weights[];
[0.0,2.0] @=> float times[];
[0.0,0.0,0.0,0.0,0.0,0.0, 0.0,0.0,0.0,0.0,0.0,0.0,
 3.0,0.0,0.0,90.0,0.0,0.0, 0.0,0.0,0.0,0.0,0.0,0.0] @=> float poses[];
float out[9];
float rest[9];
float small[3];
check(!model.exportPosedNormals(0,out));
check(model.createMesh(xyz,tris));
check(!model.exportPosedNormals(0,out));
check(model.setSkin(0,2,weights));
check(!model.exportPosedNormals(0,out));
check(model.computeNormals(0));
check(model.exportPosedNormals(0,out));
check(Math.fabs(out[2]-1.0)<0.00001 && Math.fabs(out[5]-1.0)<0.00001);
check(!model.exportPosedNormals(0,small));
check(model.setBonePose(0,0,3.0,0.0,0.0,90.0,0.0,0.0));
check(model.exportPosedNormals(0,out));
check(Math.fabs(out[1]+1.0)<0.00001 && Math.fabs(out[2])<0.00001);
check(Math.fabs(out[4]+0.70710678)<0.00001 && Math.fabs(out[5]-0.70710678)<0.00001);
check(Math.fabs(out[8]-1.0)<0.00001);
check(model.exportNormals(0,rest));
check(Math.fabs(rest[2]-1.0)<0.00001);
check(model.resetPose(0));
check(model.addAnimation(0,"tilt",times,poses)==0);
check(model.sampleAnimation(0,2.0,0));
check(model.exportPosedNormals(0,out) && Math.fabs(out[1]+1.0)<0.00001);
check(model.resetPose(0));
check(model.save("posednormals/model.gltf"));
Model3D imported;
check(imported.load("posednormals/model.gltf"));
check(imported.sampleAnimation(0,2.0,0));
check(imported.exportPosedNormals(0,out));
check(Math.fabs(out[1]+1.0)<0.00001 && Math.fabs(out[5]-0.70710678)<0.00001);
check(imported.resetAnimation());
check(imported.exportPosedNormals(0,out) && Math.fabs(out[2]-1.0)<0.00001);
""",28,p)


    run("model", """
Model3D model;
float xyz[9];
float nrm[9];
float separateXYZ[9];
float separateNrm[9];
float small[3];
check(!model.exportPosedGeometry(0,xyz,nrm));
check(model.createMesh([0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,0.0],[0,1,2]));
check(model.setSkin(0,2,[1.0,0.0,0.5,0.5,0.0,1.0]));
check(!model.exportPosedGeometry(0,xyz,nrm));
check(model.computeNormals(0));
check(model.setBonePose(0,0,2.0,0.0,0.0,90.0,0.0,0.0));
check(model.exportPosedGeometry(0,xyz,nrm));
check(model.exportPosedVertices(0,separateXYZ));
check(model.exportPosedNormals(0,separateNrm));
1 => int equal;
for(0 => int i; i < 9; i++) {
    if(Math.fabs(xyz[i]-separateXYZ[i])>0.00001 ||
       Math.fabs(nrm[i]-separateNrm[i])>0.00001) 0 => equal;
}
check(equal);
check(!model.exportPosedGeometry(0,xyz,xyz));
check(!model.exportPosedGeometry(0,xyz,small));
check(!model.exportPosedGeometry(0,small,nrm));
check(Math.fabs(xyz[0]-2.0)<0.00001 && Math.fabs(nrm[1]+1.0)<0.00001);
check(model.resetPose(0));
check(model.exportPosedGeometry(0,xyz,nrm));
check(Math.fabs(xyz[0])<0.00001 && Math.fabs(nrm[2]-1.0)<0.00001);
Model3D imported;
check(imported.load("posednormals/model.gltf"));
check(imported.sampleAnimation(0,2.0,0));
check(imported.exportPosedGeometry(0,xyz,nrm));
check(Math.fabs(xyz[0]-3.0)<0.00001 && Math.fabs(nrm[1]+1.0)<0.00001);
""",21,p)


    run("model", """
Model3D model;
float localMat[16];
float worldMat[16];
float shortMat[3];
check(model.nodes()==-1);
check(model.nodeParent(0)==-2);
check(model.nodeMeshes(0)==-1);
check(!model.exportNodeWorldMatrix(0,worldMat));
check(model.createMesh([0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,0.0],[0,1,2]));
check(model.nodes()==1);
check(model.nodeParent(0)==-1);
check(model.nodeMeshes(0)==1);
check(model.nodeMeshAt(0,0)==0);
check(model.nodeMeshAt(0,1)==-1);
check(model.exportNodeLocalMatrix(0,localMat));
check(localMat[0]==1.0 && localMat[5]==1.0 && localMat[10]==1.0 && localMat[15]==1.0);
check(!model.exportNodeWorldMatrix(0,shortMat));
check(model.setSkin(0,2,[1.0,0.0,0.5,0.5,0.0,1.0]));
check(model.nodes()==3);
check(model.setBoneParent(0,1,0));
check(model.nodeParent(1)==0 && model.nodeParent(2)==1);
check(model.nodeMeshes(2)==0);
check(model.setBonePose(0,0,2.0,0.0,0.0,0.0,0.0,0.0));
check(model.setBonePose(0,1,0.0,3.0,0.0,0.0,0.0,0.0));
check(model.exportNodeLocalMatrix(2,localMat));
check(Math.fabs(localMat[3])<0.00001 && Math.fabs(localMat[7]-3.0)<0.00001);
check(model.exportNodeWorldMatrix(2,worldMat));
check(Math.fabs(worldMat[3]-2.0)<0.00001 && Math.fabs(worldMat[7]-3.0)<0.00001);
check(!model.exportNodeWorldMatrix(3,worldMat));
check(Math.fabs(worldMat[3]-2.0)<0.00001);
check(model.resetPose(0));
check(model.save("scenehierarchy/model.gltf"));
Model3D imported;
check(imported.load("scenehierarchy/model.gltf"));
check(imported.nodes()>=3);
0 => int attached;
for(0 => int i; i<imported.nodes(); i++) {
    for(0 => int j; j<imported.nodeMeshes(i); j++) {
        if(imported.nodeMeshAt(i,j)==0) attached++;
    }
}
check(attached==1);
check(imported.exportNodeWorldMatrix(0,worldMat));
imported.clear();
check(imported.nodes()==-1);
""",33,p)


    run("model", """
Model3D model;
float props[6];
float small[3];
check(!model.exportMaterial(0,props));
check(model.baseColorTextures(0)==-1);
check(model.createMesh([0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,0.0],[0,1,2]));
check(model.addMaterial("surface",0.2,0.4,0.6,0.75)==1);
check(model.assignMaterial(0,1));
check(model.exportMaterial(model.materialAt(0),props));
check(Math.fabs(props[0]-0.2)<0.00001 && Math.fabs(props[1]-0.4)<0.00001 && Math.fabs(props[2]-0.6)<0.00001);
check(Math.fabs(props[3]-0.75)<0.00001);
check(props[4]==0.0 && props[5]==1.0);
check(model.baseColorTextures(1)==0);
check(!model.exportMaterial(1,small));
check(!model.exportMaterial(999,props));
check(Math.fabs(props[0]-0.2)<0.00001);
check(model.baseColorTextures(-1)==-1);
check(model.save("materialprops/model.gltf"));
Model3D imported;
check(imported.load("materialprops/model.gltf"));
check(imported.exportMaterial(imported.materialAt(0),props));
check(Math.fabs(props[0]-0.2)<0.00001 && Math.fabs(props[2]-0.6)<0.00001);
check(imported.baseColorTextures(imported.materialAt(0))==0);
""",19,p)


    (p/"texmodel").mkdir()
    (p/"texmodel"/"images").mkdir()
    (p/"texmodel"/"images"/"albedo.ppm").write_bytes(b"P6\n1 1\n255\n\xff\x00\x00")
    (p/"texmodel"/"surface.mtl").write_text("newmtl surface\nKd 1 1 1\nmap_Kd images/albedo.ppm\n")
    (p/"texmodel"/"mesh.obj").write_text("mtllib surface.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nusemtl surface\nf 1/1 2/2 3/3\n")
    (p/"texmodel"/"missing.mtl").write_text("newmtl surface\nmap_Kd missing.png\n")
    (p/"texmodel"/"missing.obj").write_text((p/"texmodel"/"mesh.obj").read_text().replace("surface.mtl","missing.mtl"))
    (p/"texmodel"/"escape.mtl").write_text("newmtl surface\nmap_Kd ../triangle.obj\n")
    (p/"texmodel"/"escape.obj").write_text((p/"texmodel"/"mesh.obj").read_text().replace("surface.mtl","escape.mtl"))
    run("model", """
Model3D model;
check(model.baseColorTexturePath(0,0)=="");
check(model.load("texmodel/mesh.obj"));
model.materialAt(0) => int mat;
check(model.baseColorTextures(mat)==1);
model.baseColorTexturePath(mat,0) => string path;
check(path!="");
check(path.find("albedo.ppm")>=0);
check(model.baseColorTexturePath(mat,-1)=="");
check(model.baseColorTexturePath(mat,1)=="");
check(model.baseColorTexturePath(-1,0)=="");
check(!model.load("missing.obj"));
check(model.baseColorTexturePath(mat,0)==path);
check(model.load("texmodel/missing.obj"));
check(model.baseColorTextures(model.materialAt(0))==1);
check(model.baseColorTexturePath(model.materialAt(0),0)=="");
check(model.load("texmodel/escape.obj"));
check(model.baseColorTexturePath(model.materialAt(0),0)=="");
check(model.createMesh([0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,0.0],[0,1,2]));
check(model.baseColorTexturePath(0,0)=="");
model.clear();
check(model.baseColorTexturePath(0,0)=="");
""",18,p)


    import base64, struct
    png=base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jR9sAAAAASUVORK5CYII=")
    geom=struct.pack("<9f6f3H",0,0,0,1,0,0,0,1,0,0,0,1,0,0,1,0,1,2)
    embedded={"asset":{"version":"2.0"},"buffers":[{"byteLength":len(geom),"uri":"data:application/octet-stream;base64,"+base64.b64encode(geom).decode()}],
      "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36,"target":34962},{"buffer":0,"byteOffset":36,"byteLength":24,"target":34962},{"buffer":0,"byteOffset":60,"byteLength":6,"target":34963}],
      "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":2,"componentType":5123,"count":3,"type":"SCALAR"}],
      "images":[{"uri":"data:image/png;base64,"+base64.b64encode(png).decode()}],"textures":[{"source":0}],
      "materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],
      "meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1},"indices":2,"material":0}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0}
    (p/"embedded.gltf").write_text(json.dumps(embedded),encoding="utf-8")
    run("model", """
Model3D model;
check(model.embeddedBaseColorFormat(0,0)=="");
check(!model.exportEmbeddedBaseColor(0,0,"embedded.png"));
check(model.load("embedded.gltf"));
model.materialAt(0) => int mat;
check(model.baseColorTextures(mat)==1);
check(model.baseColorTexturePath(mat,0)=="");
check(model.embeddedBaseColorFormat(mat,0)=="png");
check(model.embeddedBaseColorFormat(mat,1)=="");
check(model.embeddedBaseColorFormat(-1,0)=="");
check(!model.exportEmbeddedBaseColor(mat,0,"embedded.jpg"));
check(!model.exportEmbeddedBaseColor(mat,1,"wrong.png"));
check(model.exportEmbeddedBaseColor(mat,0,"embedded.png"));
check(!model.exportEmbeddedBaseColor(mat,0,"embedded.png"));
check(!model.exportEmbeddedBaseColor(mat,0,"missing/embedded.png"));
check(model.load("triangle.obj"));
check(model.embeddedBaseColorFormat(model.materialAt(0),0)=="");
""",15,p)
    assert (p/"embedded.png").read_bytes()==png
    assert not (p/"embedded.jpg").exists()
    assert not (p/"wrong.png").exists()

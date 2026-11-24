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

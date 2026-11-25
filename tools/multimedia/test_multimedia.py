"""Run with Python 3 and FFmpeg CLI after building ChucK and all Chugins."""
import os
import pathlib
import subprocess
import tempfile
HERE = pathlib.Path(__file__).resolve().parent
CHUCK = HERE.parents[1] / "src/chuck"

def run(module, script, expected, directory):
    path = directory / "test.ck"
    name = {"audio": "AudioIO", "vector": "VectorIO", "model": "ModelIO", "midi": "MidiFileIO", "video": "VideoIO", "subtitle": "SubtitleIO", "keyframe": "KeyframeIO", "raster": "RasterIO", "text": "TextIO", "transport": "MediaTransport"}[module]
    path.write_text(f'@import "{name}";\n' + '0 => int smokeCheckIndex;\nfun void check(int ok) { smokeCheckIndex++; if(ok) <<< "PASS", smokeCheckIndex >>>; else <<< "FAIL", smokeCheckIndex >>>; }\n' + script, encoding="utf-8")
    print(f"SMOKE_EXPECTED {expected}", flush=True)
    r = subprocess.run([os.environ.get("CHUCK_EXE", str(CHUCK)), "--silent", "--chugin-path:" + os.environ.get("CHUGIN_DIR", str(HERE / module)), path.name], cwd=directory, capture_output=True, text=True, timeout=60)
    log = r.stdout + r.stderr
    print(module, log)
    print(f"SMOKE_COUNTS {expected} {log.count('PASS')} {log.count('FAIL')}", flush=True)
    if r.returncode or "FAIL" in log or log.count("PASS") != expected:
        raise RuntimeError(module + " smoke test failed")

def legacy():
    directory = tempfile.TemporaryDirectory()
    p = pathlib.Path(directory.name)
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


    (p/"lighting.mtl").write_text("newmtl surface\nKd 0.2 0.4 0.6\nKs 0.1 0.3 0.5\nKe 0.1 0.2 0.3\nNs 32\nd 0.5\n")
    (p/"lighting.obj").write_text("mtllib lighting.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl surface\nf 1 2 3\n")
    run("model", """
Model3D model;
float lighting[8];
float props[6];
float small[3];
check(!model.exportMaterialLighting(0,lighting));
check(model.createMesh([0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,0.0],[0,1,2]));
check(model.addMaterial("surface",0.2,0.4,0.6,0.5)==1);
check(model.exportMaterialLighting(1,lighting));
check(lighting[0]==0.0 && lighting[1]==0.0 && lighting[2]==0.0);
check(Math.fabs(lighting[3]-0.2)<0.00001 && lighting[6]==64.0 && lighting[7]==0.0);
check(!model.exportMaterialLighting(1,small));
check(!model.exportMaterialLighting(-1,lighting));
check(lighting[6]==64.0);
check(model.load("lighting.obj"));
model.materialAt(0) => int mat;
check(model.exportMaterialLighting(mat,lighting));
check(Math.fabs(lighting[0]-0.1)<0.00001 && Math.fabs(lighting[2]-0.3)<0.00001);
check(Math.fabs(lighting[3]-0.1)<0.00001 && Math.fabs(lighting[5]-0.5)<0.00001);
check(Math.fabs(lighting[6]-32.0)<0.00001);
check(model.exportMaterial(mat,props));
check(Math.fabs(props[3]-0.5)<0.00001);
""",16,p)


    run("model", """
Model3D model;
float values[10];
float small[3];
check(!model.exportNodeTRS(0,values));
check(model.createMesh([0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,0.0],[0,1,2]));
check(model.exportNodeTRS(0,values));
check(values[0]==0.0 && values[6]==1.0 && values[7]==1.0 && values[8]==1.0 && values[9]==1.0);
check(!model.exportNodeTRS(0,small));
check(model.setSkin(0,1,[1.0,1.0,1.0]));
check(model.setBonePose(0,0,2.0,3.0,4.0,0.0,0.0,90.0));
check(model.exportNodeTRS(1,values));
check(Math.fabs(values[0]-2.0)<0.00001 && Math.fabs(values[1]-3.0)<0.00001 && Math.fabs(values[2]-4.0)<0.00001);
check(Math.fabs(Math.fabs(values[5])-0.70710678)<0.00001 && Math.fabs(Math.fabs(values[6])-0.70710678)<0.00001);
check(!model.exportNodeTRS(2,values));
check(Math.fabs(values[0]-2.0)<0.00001);
check(model.resetPose(0));
check(model.exportNodeTRS(1,values));
check(Math.fabs(values[0])<0.00001 && Math.fabs(values[6]-1.0)<0.00001);
check(model.load("scenehierarchy/model.gltf"));
check(model.exportNodeTRS(0,values));
""",17,p)


    run("model", """
Model3D model;
[0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,0.0] @=> float xyz[];
[0,1,2] @=> int tris[];
[0.0,2.0] @=> float times[];
[0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0] @=> float a[];
[0.0,0.0,0.0,0.0,0.0,0.0,4.0,0.0,0.0,0.0,0.0,0.0] @=> float b[];
float out[9];
check(model.playbackClip()==-1);
check(!model.advanceAnimation(1.0));
check(model.createMesh(xyz,tris));
check(model.setSkin(0,1,[1.0,1.0,1.0]));
check(model.addAnimation(0,"idle",times,a)==0);
check(model.addAnimation(0,"move",times,b)==1);
check(!model.playAnimation(2,0));
check(model.playAnimation(1,0));
check(model.playbackClip()==1 && model.playbackTime()==0.0);
check(model.playbackSpeed(2.0));
check(!model.playbackSpeed(0.0));
check(model.advanceAnimation(0.25));
check(Math.fabs(model.playbackTime()-0.5)<0.00001);
check(model.exportPosedVertices(0,out) && Math.fabs(out[0]-1.0)<0.00001);
check(model.pauseAnimation(1));
check(model.advanceAnimation(1.0) && Math.fabs(model.playbackTime()-0.5)<0.00001);
check(model.pauseAnimation(0));
check(!model.advanceAnimation(-1.0));
check(model.advanceAnimation(2.0) && model.playbackEnded());
check(model.exportPosedVertices(0,out) && Math.fabs(out[0]-4.0)<0.00001);
check(model.seekAnimation(0.5) && !model.playbackEnded());
check(model.playAnimation(1,1));
check(model.advanceAnimation(2.5) && Math.fabs(model.playbackTime()-0.5)<0.00001);
check(model.playAnimation(0,0));
check(model.fadeAnimation(1,1.0));
check(!model.fadeAnimation(1,1.0));
check(!model.seekAnimation(0.1));
check(model.advanceAnimation(0.5));
check(model.exportPosedVertices(0,out) && Math.fabs(out[0]-0.5)<0.00001);
check(model.advanceAnimation(0.5) && model.playbackClip()==1);
check(model.exportPosedVertices(0,out) && Math.fabs(out[0]-2.0)<0.00001);
model.stopAnimation();
check(model.playbackClip()==-1 && model.playbackTime()==-1.0);
check(model.exportPosedVertices(0,out) && Math.fabs(out[0]-2.0)<0.00001);
check(model.playAnimation(1,0));
check(model.load("triangle.obj") && model.playbackClip()==-1);
""",35,p)

    run("midi", """
MidiSequence seq;
float small[2];
check(seq.renderFrames(8000)==-1);
check(!seq.renderSamples(8000,small));
check(seq.create(480,120.0));
check(seq.addNote(0,0,480,0,69,127));
check(seq.renderFrames(8000)==4000);
float pcm[4000];
check(seq.renderSamples(8000,pcm));
check(pcm[0]==0.0 && pcm[3999]==0.0);
check(Math.fabs(pcm[100])<0.00001);
check(Math.fabs(pcm[105]-0.25*Math.sin(2.0*Math.pi*440.0*105.0/8000.0))<0.00001);
check(!seq.renderSamples(8000,small));
check(seq.renderFrames(7999)==-1);
check(seq.addTempo(240,60.0));
check(seq.renderFrames(8000)==6000);
float slow[6000];
check(seq.renderSamples(8000,slow));
check(seq.addNote(0,480,480,0,69,64));
check(seq.renderFrames(8000)==14000);
float combined[14000];
check(seq.renderSamples(8000,combined));
check(Math.fabs(combined[6105]-0.25*64.0/127.0*Math.sin(2.0*Math.pi*440.0*105.0/8000.0))<0.00001);
check(seq.addNote(0,960,480,9,36,100));
check(seq.renderFrames(8000)==-1);
check(!seq.renderSamples(8000,combined));
""",21,p)

    run("midi", """
MidiSequence seq;
check(seq.create(480,120.0));
check(seq.addNote(0,0,480,0,60,100));
check(seq.addNote(0,480,480,0,60,90));
check(seq.notes(0)==2);
check(seq.noteLength(0,0)==480 && seq.noteLength(0,1)==480);
check(seq.renderFrames(8000)==8000);
float pcm[8000];
check(seq.renderSamples(8000,pcm));
check(pcm[3999]==0.0 && pcm[4000]==0.0);
check(seq.addNote(0,240,480,0,60,80));
check(seq.notes(0)==-1);
check(seq.renderFrames(8000)==-1);
check(!seq.renderSamples(8000,pcm));
""",12,p)

    run("audio", """
AudioBuffer audio;
int pixels[4*5*4];
check(!audio.exportWaveform(0,4,5,pixels));
check(audio.importSamples(8000,1,[-1.0,1.0,0.0,0.0]));
check(audio.exportWaveform(0,4,5,pixels));
check(pixels[(4*4)*4]==255 && pixels[4]==255);
check(pixels[(2*4+2)*4]==255 && pixels[(2*4+3)*4]==255);
check(pixels[0]==0 && pixels[3]==255);
check(!audio.exportWaveform(1,4,5,pixels));
check(!audio.exportWaveform(0,3,5,pixels));
check(pixels[0]==0 && pixels[3]==255);
check(audio.create(64,8000,1));
int spec[17*4];
check(audio.exportSpectrogram(0,1,32,-80.0,spec));
check(spec[0]==0 && spec[3]==255 && spec[64]==0);
for(0=>int i;i<64;i++) audio.setSample(i,0,Math.sin(2.0*Math.pi*4.0*i/32.0));
check(audio.exportSpectrogram(0,1,32,-80.0,spec));
check(spec[(16-4)*4]>250);
check(spec[(16-4)*4]>spec[(16-8)*4]);
check(!audio.exportSpectrogram(0,1,31,-80.0,spec));
check(!audio.exportSpectrogram(0,1,32,0.0,spec));
check(!audio.exportSpectrogram(1,1,32,-80.0,spec));
check(spec[(16-4)*4]>250);
0.0=>audio.gain;
check(audio.exportSpectrogram(0,1,32,-80.0,spec));
check(spec[(16-4)*4]==0);
""",21,p)

    run("audio", """
AudioBuffer audio;
float bounds[4];
check(!audio.exportEnvelope(0,2,bounds));
check(audio.importSamples(8000,1,[-1.0,0.5,0.25,1.0]));
check(audio.exportEnvelope(0,2,bounds));
check(bounds[0]==-1.0 && bounds[1]==0.5);
check(bounds[2]==0.25 && bounds[3]==1.0);
check(!audio.exportEnvelope(1,2,bounds));
check(!audio.exportEnvelope(0,3,bounds));
check(!audio.exportEnvelope(0,0,bounds));
check(bounds[0]==-1.0 && bounds[3]==1.0);
0.5=>audio.gain;
check(audio.exportEnvelope(0,2,bounds));
check(bounds[0]==-0.5 && bounds[3]==0.5);
float repeated[16];
check(audio.exportEnvelope(0,8,repeated));
check(repeated[0]==-0.5 && repeated[3]==-0.5 && repeated[15]==0.5);
""",13,p)

    run("vector", """
VectorImage image;
[-1.0,1.0,0.0,0.0] @=> float bounds[];
check(image.importWaveform(4,5,bounds,0xff0000,1.0));
check(image.loaded() && image.shapes()==1);
int pixels[80];
check(image.exportPixels(4,5,pixels));
check(pixels[0]==255 && pixels[3]==255 && pixels[11]==0);
check(pixels[(2*4+2)*4]==255 && pixels[(2*4+2)*4+3]==255);
2.0=>image.x;
check(!image.importWaveform(0,5,bounds,0xff0000,1.0));
check(!image.importWaveform(4,5,[1.0,-1.0],0xff0000,1.0));
check(!image.importWaveform(4,5,[-2.0,1.0],0xff0000,1.0));
check(!image.importWaveform(4,5,[0.0],0xff0000,1.0));
check(!image.importWaveform(4,5,bounds,-1,1.0));
check(!image.importWaveform(4,5,bounds,0xff0000,2.0));
check(image.x()==2.0 && image.shapes()==1);
check(image.importWaveform(4,5,bounds,0xff0000,1.0));
check(image.x()==0.0 && image.shapes()==1);
check(image.removeShape(0) && image.shapes()==0);
""",15,p)

    run("raster", """
RasterImage image;
float mono[4];
float stereo[8];
check(!image.exportAudioSamples(1,mono));
check(image.importPixels(2,2,[0,0,0,255,255,255,255,255,255,0,0,255,0,255,0,0]));
check(image.exportAudioSamples(1,mono));
check(Math.fabs(mono[0]+1.0)<0.00001 && Math.fabs(mono[1]-1.0)<0.00001);
check(Math.fabs(mono[2]+0.5748)<0.00001 && mono[3]==0.0);
check(image.exportAudioSamples(2,stereo));
check(stereo[0]==-1.0 && stereo[1]==-1.0 && stereo[2]==1.0 && stereo[3]==1.0);
check(stereo[4]==0.0 && stereo[5]==-1.0 && stereo[6]==0.0 && stereo[7]==0.0);
check(!image.exportAudioSamples(0,mono));
check(!image.exportAudioSamples(3,mono));
check(!image.exportAudioSamples(2,mono));
check(mono[0]==-1.0 && mono[1]==1.0);
0.0=>image.opacity;
check(image.exportAudioSamples(1,mono));
check(mono[0]==0.0 && mono[1]==0.0 && mono[2]==0.0 && mono[3]==0.0);
image.reset();
0.0=>image.red;
check(image.exportAudioSamples(1,mono));
check(Math.fabs(mono[2]+1.0)<0.00001);
check(image.pixelAt(0,1,0)==255);
image.clear();
check(!image.exportAudioSamples(1,mono));
check(Math.fabs(mono[2]+1.0)<0.00001);
""",19,p)

    run("model", """
Model3D model;
check(!model.createSilhouette(0,1,[255],128,2.0));
check(!model.createSilhouette(1,1,[0],128,2.0));
check(!model.createSilhouette(1,1,[256],128,2.0));
check(!model.createSilhouette(1,1,[255],0,2.0));
check(!model.createSilhouette(1,1,[255],256,2.0));
check(!model.createSilhouette(1,1,[255],128,0.0));
check(!model.createSilhouette(2,1,[255],128,2.0));
check(model.createSilhouette(1,1,[255],128,2.0));
check(model.loaded() && model.meshVertices(0)==24 && model.faces(0)==12);
check(model.hasNormals(0));
check(model.vertexAt(0,0,0)==-0.5 && model.vertexAt(0,0,1)==-0.5);
check(model.vertexAt(0,0,2)==1.0);
float normals[72];
check(model.exportNormals(0,normals));
check(normals[0]==0.0 && normals[1]==0.0 && normals[2]==1.0);
check(normals[12]==0.0 && normals[13]==0.0 && normals[14]==-1.0);
check(!model.createSilhouette(1,1,[127],128,1.0));
check(model.meshVertices(0)==24 && model.faces(0)==12);
check(model.createSilhouette(2,1,[128,255],128,1.0));
check(model.meshVertices(0)==40 && model.faces(0)==20);
check(model.createSilhouette(3,3,[255,255,255,255,0,255,255,255,255],128,1.0));
check(model.faces(0)==64 && model.meshVertices(0)==128);
check(model.createSilhouette(2,2,[255,0,0,255],128,1.0));
check(model.faces(0)==24);
""",23,p)

    run("vector", """
VectorImage image;
int alpha[4];
check(!image.exportAlpha(2,2,alpha));
check(image.create(2,2));
check(image.addRect(0.0,0.0,1.0,2.0,0xffffff,1.0));
check(image.exportAlpha(2,2,alpha));
check(alpha[0]==255 && alpha[1]==0 && alpha[2]==255 && alpha[3]==0);
check(!image.exportAlpha(1,2,alpha));
check(alpha[0]==255 && alpha[1]==0);
0.0=>image.opacity;
check(image.exportAlpha(2,2,alpha));
check(alpha[0]==0 && alpha[2]==0);
""",9,p)

    run("vector", """
VectorImage image;
check(!image.importPixels(0,2,[0,0,0,255]));
check(image.importPixels(3,2,[255,0,0,255,255,0,0,255,0,0,255,255,0,0,0,0,0,255,0,255,0,255,0,255]));
check(image.shapes()==3);
int rgba[24];
check(image.exportPixels(3,2,rgba));
check(rgba[0]==255 && rgba[4]==255 && rgba[8]==0 && rgba[10]==255);
check(rgba[15]==0 && rgba[17]==255 && rgba[21]==255);
2.0=>image.x;
check(!image.importPixels(3,2,[0,0,0,255]));
check(!image.importPixels(1,1,[256,0,0,255]));
check(!image.importPixels(1,1,[0,0,0,-1]));
check(image.x()==2.0 && image.shapes()==3);
check(image.importPixels(1,1,[255,0,0,128]));
check(image.x()==0.0 && image.shapes()==1);
int one[4];
check(image.exportPixels(1,1,one));
check(one[0]==255 && one[3]==128);
check(image.importPixels(1,1,[255,0,0,0]));
check(image.loaded() && image.shapes()==0);
check(image.exportPixels(1,1,one));
check(one[0]==0 && one[3]==0);
""",18,p)

    run("raster", """
RasterImage image;
float heights[4];
check(!image.exportHeightmap(heights));
check(image.importPixels(2,2,[0,0,0,255,255,255,255,255,255,0,0,255,255,255,255,0]));
check(image.exportHeightmap(heights));
check(heights[0]==0.0 && Math.fabs(heights[1]-1.0)<0.00001);
check(Math.fabs(heights[2]-0.2126)<0.00001 && heights[3]==0.0);
float small[3];
check(!image.exportHeightmap(small));
check(Math.fabs(heights[2]-0.2126)<0.00001);
0.0=>image.opacity;
check(image.exportHeightmap(heights));
check(heights[0]==0.0 && heights[1]==0.0 && heights[2]==0.0 && heights[3]==0.0);
check(image.pixelAt(1,0,0)==255);
""",10,p)

    run("model", """
Model3D model;
check(!model.createHeightmap(1,2,[0.0,1.0],1.0,1.0));
check(!model.createHeightmap(2,2,[0.0,1.0],1.0,1.0));
check(!model.createHeightmap(2,2,[0.0,0.0,0.0,1.1],1.0,1.0));
check(!model.createHeightmap(2,2,[0.0,0.0,0.0,1.0],0.0,1.0));
check(!model.createHeightmap(2,2,[0.0,0.0,0.0,1.0],1.0,-1.0));
check(model.createHeightmap(2,2,[0.0,0.0,0.0,0.0],2.0,4.0));
check(model.meshVertices(0)==4 && model.faces(0)==2);
check(model.vertexAt(0,0,0)==-1.0 && model.vertexAt(0,0,1)==1.0);
check(model.vertexAt(0,3,0)==1.0 && model.vertexAt(0,3,1)==-1.0);
check(model.hasNormals(0));
float normals[12];
check(model.exportNormals(0,normals));
check(normals[0]==0.0 && normals[1]==0.0 && normals[2]==1.0);
float uv[8];
check(model.exportUV(0,uv));
check(uv[0]==0.0 && uv[1]==1.0 && uv[6]==1.0 && uv[7]==0.0);
check(model.createHeightmap(2,2,[0.0,1.0,0.0,1.0],2.0,4.0));
check(model.vertexAt(0,1,2)==4.0 && model.vertexAt(0,2,2)==0.0);
check(model.exportNormals(0,normals));
check(Math.fabs(normals[0]+0.89442719)<0.00001 && Math.fabs(normals[2]-0.44721359)<0.00001);
check(!model.createHeightmap(513,2,[0.0],1.0,1.0));
check(model.vertexAt(0,1,2)==4.0);
check(model.createHeightmap(3,2,[0.0,0.0,0.0,0.0,0.0,0.0],1.0,0.0));
check(model.meshVertices(0)==6 && model.faces(0)==4);
""",22,p)

    run("model", """
Model3D model;
int pixels[64];
check(!model.renderOrthographic(0,4,4,2.0,0xffffff,pixels));
check(model.createMesh([-1.0,-1.0,0.0,1.0,-1.0,0.0,1.0,1.0,0.0,-1.0,1.0,0.0],[0,1,2,0,2,3]));
check(model.renderOrthographic(0,4,4,2.0,0xff8040,pixels));
check(pixels[0]==255 && pixels[1]==128 && pixels[2]==64 && pixels[3]==255);
check(pixels[60]==255 && pixels[63]==255);
check(!model.renderOrthographic(-1,4,4,2.0,0xffffff,pixels));
check(!model.renderOrthographic(0,0,4,2.0,0xffffff,pixels));
check(!model.renderOrthographic(0,4,4,0.0,0xffffff,pixels));
check(!model.renderOrthographic(0,4,4,2.0,-1,pixels));
check(!model.renderOrthographic(0,2,2,2.0,0xffffff,pixels));
check(pixels[0]==255 && pixels[1]==128);
check(model.renderOrthographic(0,4,4,4.0,0xffffff,pixels));
check(pixels[3]==0 && pixels[23]==255 && pixels[63]==0);
4.0=>model.x;
check(model.renderOrthographic(0,4,4,2.0,0xffffff,pixels));
check(pixels[3]==0 && pixels[23]==0 && pixels[63]==0);
model.reset();
check(model.createMesh([-1.0,-1.0,1.0,1.0,-1.0,1.0,1.0,1.0,1.0,-1.0,1.0,1.0,-1.0,-1.0,0.0,1.0,-1.0,0.0,1.0,1.0,0.0,-1.0,1.0,0.0],[0,1,2,0,2,3,4,6,5,4,7,6]));
check(model.renderOrthographic(0,4,4,2.0,0xffffff,pixels));
check(pixels[20]==255 && pixels[23]==255);
check(model.createMesh([-1.0,-1.0,0.0,1.0,-1.0,0.0,1.0,1.0,0.0,-1.0,1.0,0.0],[0,2,1,0,3,2]));
check(model.renderOrthographic(0,4,4,2.0,0xffffff,pixels));
check(pixels[20]==64 && pixels[23]==255);
check(model.load("triangle.obj"));
check(model.renderOrthographic(0,4,4,2.0,0xffffff,pixels));
check(pixels[23]==0 && pixels[27]==255);
check(pixels[59]==0);
""",25,p)

    run("model", """
Model3D model;
float projected[14];
check(!model.projectTriangles(0,8,8,2.0,0xffffff,projected));
check(model.createMesh([-1.0,-1.0,1.0,1.0,-1.0,1.0,-1.0,1.0,1.0,-1.0,-1.0,0.0,-1.0,1.0,0.0,1.0,-1.0,0.0],[0,1,2,3,4,5]));
check(model.projectTriangles(0,8,8,2.0,0xffffff,projected));
check(projected[0]==0.0 && projected[1]==8.0 && projected[2]==0.0 && projected[3]==0.0);
check(projected[6]==4210752.0 && projected[13]==16777215.0);
check(projected[7]==0.0 && projected[8]==8.0 && projected[9]==8.0 && projected[10]==8.0);
check(!model.projectTriangles(-1,8,8,2.0,0xffffff,projected));
check(!model.projectTriangles(0,0,8,2.0,0xffffff,projected));
check(!model.projectTriangles(0,8,8,0.0,0xffffff,projected));
check(!model.projectTriangles(0,8,8,2.0,-1,projected));
float small[7];
check(!model.projectTriangles(0,8,8,2.0,0xffffff,small));
check(projected[6]==4210752.0 && projected[13]==16777215.0);
0.5=>model.x;
check(model.projectTriangles(0,8,8,2.0,0xff0000,projected));
check(projected[0]==2.0 && projected[7]==2.0 && projected[13]==16711680.0);
check(model.load("triangle.obj"));
check(model.projectTriangles(0,8,8,2.0,0xffffff,small));
check(small[0]==4.0 && small[1]==4.0 && small[2]==8.0 && small[3]==4.0);
""",17,p)

    run("vector", """
VectorImage image;
[0.0,0.0,8.0,0.0,0.0,8.0,16711680.0,0.0,0.0,8.0,0.0,0.0,8.0,255.0] @=> float projected[];
check(image.importTriangles(8,8,projected));
check(image.shapes()==2);
int pixels[256];
check(image.exportPixels(8,8,pixels));
check(pixels[0]==0 && pixels[2]==255 && pixels[3]==255);
check(pixels[255]==0);
check(image.removeShape(1));
check(image.exportPixels(8,8,pixels));
check(pixels[0]==255 && pixels[2]==0 && pixels[3]==255);
2.0=>image.x;
check(!image.importTriangles(0,8,projected));
check(!image.importTriangles(8,8,[0.0,0.0]));
check(!image.importTriangles(8,8,[0.0,0.0,8.0,0.0,0.0,8.0,-1.0]));
check(!image.importTriangles(8,8,[0.0,0.0,8.0,0.0,0.0,8.0,1.5]));
check(image.x()==2.0 && image.shapes()==1);
check(image.importTriangles(8,8,[0.0,0.0,1.0,1.0,2.0,2.0,255.0]));
check(image.shapes()==0 && image.x()==0.0);
check(image.exportPixels(8,8,pixels));
check(pixels[3]==0);
""",17,p)

    run("model", """
Model3D model;
float pcm[5];
check(!model.exportAudioSamples(0,0,pcm));
check(model.createMesh([-2.0,0.0,5.0,0.0,2.0,5.0,2.0,0.0,5.0],[0,1,2]));
check(model.exportAudioSamples(0,0,pcm));
check(pcm[0]==-1.0 && pcm[2]==0.0 && pcm[4]==1.0);
check(pcm[1]==-0.5 && pcm[3]==0.5);
check(model.exportAudioSamples(0,1,pcm));
check(pcm[0]==-1.0 && pcm[1]==0.0 && pcm[2]==1.0 && pcm[3]==0.0 && pcm[4]==-1.0);
check(model.exportAudioSamples(0,2,pcm));
check(pcm[0]==0.0 && pcm[2]==0.0 && pcm[4]==0.0);
check(!model.exportAudioSamples(-1,0,pcm));
check(!model.exportAudioSamples(0,-1,pcm));
check(!model.exportAudioSamples(0,3,pcm));
check(pcm[0]==0.0 && pcm[4]==0.0);
float exact[3];
check(model.exportAudioSamples(0,0,exact));
check(exact[0]==-1.0 && exact[1]==0.0 && exact[2]==1.0);
float shortPCM[2];
check(model.exportAudioSamples(0,0,shortPCM));
check(shortPCM[0]==-1.0 && shortPCM[1]==1.0);
float one[1];
check(model.exportAudioSamples(0,0,one));
check(one[0]==-1.0);
float empty[0];
check(!model.exportAudioSamples(0,0,empty));
30.0=>model.rotationY;
check(model.exportAudioSamples(0,0,exact));
check(exact[0]==-1.0 && exact[2]==1.0);
check(model.vertexAt(0,0,0)==-2.0);
check(model.load("triangle.obj"));
check(model.exportAudioSamples(0,0,exact));
check(exact[0]==-1.0 && exact[1]==1.0 && exact[2]==-1.0);
model.clear();
check(!model.exportAudioSamples(0,0,exact));
check(exact[0]==-1.0 && exact[1]==1.0);
""",28,p)

    run("audio", """
AudioBuffer audio;
int pitches[4];
check(!audio.detectPitches(0,1024,0.02,0.9,pitches));
float pcm[4096];
for(0=>int i;i<2048;i++) 0.5*Math.sin(2.0*Math.pi*440.0*i/8000.0)=>pcm[i];
for(3072=>int i;i<4096;i++) 0.5*Math.sin(2.0*Math.pi*523.251*i/8000.0)=>pcm[i];
check(audio.importSamples(8000,1,pcm));
check(audio.detectPitches(0,1024,0.02,0.9,pitches));
check(pitches[0]==69 && pitches[1]==69 && pitches[2]==-1 && pitches[3]==72);
check(!audio.detectPitches(1,1024,0.02,0.9,pitches));
check(!audio.detectPitches(0,128,0.02,0.9,pitches));
check(!audio.detectPitches(0,1024,0.0,0.9,pitches));
check(!audio.detectPitches(0,1024,0.02,0.4,pitches));
int wrong[3];
check(!audio.detectPitches(0,1024,0.02,0.9,wrong));
check(pitches[0]==69 && pitches[3]==72);
0.0=>audio.gain;
check(audio.detectPitches(0,1024,0.02,0.9,pitches));
check(pitches[0]==-1 && pitches[1]==-1 && pitches[2]==-1 && pitches[3]==-1);
audio.reset();
check(audio.detectPitches(0,1024,0.02,0.9,pitches));
check(pitches[0]==69 && pitches[3]==72);
""",14,p)

    run("midi", """
MidiSequence seq;
check(seq.importPitches(480,120.0,0.125,0,100,[69,69,-1,72]));
check(seq.loaded() && seq.ticksPerQuarter()==480);
check(seq.notes(0)==2);
check(seq.noteLength(0,0)==240 && seq.noteLength(0,1)==120);
check(seq.renderFrames(8000)==4000);
check(!seq.importPitches(480,120.0,0.125,0,100,[69,128]));
check(!seq.importPitches(480,120.0,0.125,0,100,[-2]));
check(!seq.importPitches(0,120.0,0.125,0,100,[69]));
check(!seq.importPitches(480,0.0,0.125,0,100,[69]));
check(!seq.importPitches(480,120.0,0.0,0,100,[69]));
check(!seq.importPitches(480,120.0,0.125,16,100,[69]));
check(!seq.importPitches(480,120.0,0.125,0,0,[69]));
check(seq.notes(0)==2 && seq.noteLength(0,0)==240);
check(seq.importPitches(480,120.0,0.125,0,100,[-1,-1]));
check(seq.notes(0)==0);
check(seq.importPitches(480,120.0,0.125,0,100,[60,62,60]));
check(seq.notes(0)==3 && seq.noteLength(0,2)==120);
""",17,p)

    run("raster", """
RasterImage image;
int pitches[6];
check(!image.exportPitches(48,60,128,0,pitches));
check(image.importPixels(3,2,[0,0,0,255,255,255,255,255,255,0,0,255,0,255,0,255,0,0,255,127,255,255,255,128]));
check(image.exportPitches(48,60,128,0,pitches));
check(pitches[0]==48 && pitches[1]==60 && pitches[2]==51);
check(pitches[3]==57 && pitches[4]==-1 && pitches[5]==60);
check(image.exportPitches(48,60,128,1,pitches));
check(pitches[0]==48 && pitches[1]==57 && pitches[2]==60 && pitches[3]==-1 && pitches[4]==51 && pitches[5]==60);
check(image.exportPitches(48,60,128,2,pitches));
check(pitches[0]==48 && pitches[2]==51 && pitches[3]==60 && pitches[4]==-1 && pitches[5]==57);
check(!image.exportPitches(-1,60,128,0,pitches));
check(!image.exportPitches(48,128,128,0,pitches));
check(!image.exportPitches(60,48,128,0,pitches));
check(!image.exportPitches(48,60,0,0,pitches));
check(!image.exportPitches(48,60,256,0,pitches));
check(!image.exportPitches(48,60,128,3,pitches));
int small[5];
check(!image.exportPitches(48,60,128,0,small));
check(pitches[3]==60 && pitches[4]==-1 && pitches[5]==57);
check(image.exportPitches(64,64,128,0,pitches));
check(pitches[0]==64 && pitches[4]==-1 && pitches[5]==64);
0.0=>image.opacity;
check(image.exportPitches(48,60,128,0,pitches));
check(pitches[0]==-1 && pitches[5]==-1);
image.reset();
0.0=>image.red;
check(image.exportPitches(48,60,128,0,pitches));
check(pitches[2]==48);
check(image.pixelAt(2,0,0)==255);
""",24,p)

    run("vector", """
VectorImage image;
int pitches[6];
check(!image.exportPitches(3,2,48,60,128,0,pitches));
check(image.create(3,2));
check(image.addRect(0.0,0.0,1.0,1.0,0x000000,1.0));
check(image.addRect(1.0,0.0,1.0,1.0,0xffffff,1.0));
check(image.addRect(2.0,0.0,1.0,1.0,0xff0000,1.0));
check(image.addRect(0.0,1.0,1.0,1.0,0x00ff00,1.0));
check(image.addRect(2.0,1.0,1.0,1.0,0xffffff,0.5));
check(image.exportPitches(3,2,48,60,100,0,pitches));
check(pitches[0]==48 && pitches[1]==60 && pitches[2]==51);
check(pitches[3]==57 && pitches[4]==-1 && pitches[5]==60);
check(image.exportPitches(3,2,48,60,200,1,pitches));
check(pitches[0]==48 && pitches[1]==57 && pitches[2]==60 && pitches[3]==-1 && pitches[4]==51 && pitches[5]==-1);
check(image.exportPitches(3,2,48,60,100,2,pitches));
check(pitches[3]==60 && pitches[4]==-1 && pitches[5]==57);
check(!image.exportPitches(0,2,48,60,100,0,pitches));
check(!image.exportPitches(4096,4096,48,60,100,0,pitches));
check(!image.exportPitches(3,2,-1,60,100,0,pitches));
check(!image.exportPitches(3,2,48,128,100,0,pitches));
check(!image.exportPitches(3,2,60,48,100,0,pitches));
check(!image.exportPitches(3,2,48,60,0,0,pitches));
check(!image.exportPitches(3,2,48,60,256,0,pitches));
check(!image.exportPitches(3,2,48,60,100,3,pitches));
check(!image.exportPitches(2,2,48,60,100,0,pitches));
check(pitches[3]==60 && pitches[5]==57);
0.0=>image.opacity;
check(image.exportPitches(3,2,48,60,100,0,pitches));
check(pitches[0]==-1 && pitches[5]==-1);
image.reset();
check(image.exportPitches(3,2,64,64,100,0,pitches));
check(pitches[0]==64 && pitches[4]==-1 && pitches[5]==64);
check(image.shapes()==5);
""",29,p)

    run("model", """
Model3D model;
int pitches[5];
check(!model.exportPitches(0,0,48,60,pitches));
check(model.createMesh([-2.0,0.0,5.0,0.0,2.0,5.0,2.0,0.0,5.0],[0,1,2]));
check(model.exportPitches(0,0,48,60,pitches));
check(pitches[0]==48 && pitches[1]==51 && pitches[2]==54 && pitches[3]==57 && pitches[4]==60);
check(model.exportPitches(0,1,48,60,pitches));
check(pitches[0]==48 && pitches[1]==54 && pitches[2]==60 && pitches[3]==54 && pitches[4]==48);
check(model.exportPitches(0,2,48,61,pitches));
check(pitches[0]==55 && pitches[2]==55 && pitches[4]==55);
check(!model.exportPitches(-1,0,48,60,pitches));
check(!model.exportPitches(0,-1,48,60,pitches));
check(!model.exportPitches(0,3,48,60,pitches));
check(!model.exportPitches(0,0,-1,60,pitches));
check(!model.exportPitches(0,0,48,128,pitches));
check(!model.exportPitches(0,0,60,48,pitches));
check(pitches[0]==55 && pitches[4]==55);
int empty[0];
check(!model.exportPitches(0,0,48,60,empty));
int one[1];
check(model.exportPitches(0,0,48,60,one));
check(one[0]==48);
int two[2];
check(model.exportPitches(0,0,48,60,two));
check(two[0]==48 && two[1]==60);
check(model.exportPitches(0,0,64,64,pitches));
check(pitches[0]==64 && pitches[4]==64);
30.0=>model.rotationY;
check(model.exportPitches(0,0,48,60,pitches));
check(pitches[0]==48 && pitches[4]==60);
check(model.load("triangle.obj"));
int exact[3];
check(model.exportPitches(0,0,48,60,exact));
check(exact[0]==48 && exact[1]==60 && exact[2]==48);
model.clear();
check(!model.exportPitches(0,0,48,60,exact));
check(exact[0]==48 && exact[1]==60 && exact[2]==48);
""",29,p)

    run("video", """
@import "RasterIO";
VideoClip clip;
RasterImage image;
int pixels[64];
check(!clip.exportPixels(4,4,pixels));
check(clip.load("generated.mkv"));
check(clip.exportPixels(4,4,pixels));
check(pixels[0]==255 && pixels[1]==0 && pixels[2]==0 && pixels[3]==255);
check(pixels[60]==255 && pixels[63]==255);
check(clip.frameIndex()==0 && clip.width()==16 && clip.height()==16);
check(!clip.exportPixels(0,4,pixels));
check(!clip.exportPixels(8193,4,pixels));
check(!clip.exportPixels(8192,8192,pixels));
check(!clip.exportPixels(2,2,pixels));
check(pixels[0]==255 && pixels[63]==255);
check(image.importPixels(4,4,pixels));
check(image.width()==4 && image.height()==4);
check(image.pixelAt(3,3,0)==255);
check(clip.next()==1);
check(clip.exportPixels(4,4,pixels));
check(pixels[0]==0 && pixels[2]==255 && pixels[3]==255);
0.5=>clip.blue;
check(clip.exportPixels(4,4,pixels));
check(pixels[2]==128 && pixels[62]==128);
int nativePixels[1024];
check(clip.exportPixels(nativePixels));
check(nativePixels[2]==pixels[2]);
check(clip.exportPixels(16,16,nativePixels));
check(nativePixels[2]==128 && nativePixels[1023]==255);
check(clip.frameIndex()==1);
clip.clear();
check(!clip.exportPixels(4,4,pixels));
check(pixels[2]==128);
""",26,p)

    run("audio", """
AudioBuffer audio;
int pixels[24];
check(!audio.exportWaveform(0,0,2,2,3,pixels));
check(audio.importSamples(8000,1,[-1.0,1.0,0.0,0.0]));
check(audio.exportWaveform(0,0,2,2,3,pixels));
check(pixels[0]==0 && pixels[4]==255 && pixels[16]==255 && pixels[20]==0);
check(pixels[3]==255 && pixels[23]==255);
check(audio.exportWaveform(0,2,2,2,3,pixels));
check(pixels[0]==0 && pixels[4]==0 && pixels[8]==255 && pixels[12]==255 && pixels[16]==0);
check(!audio.exportWaveform(0,-1,2,2,3,pixels));
check(!audio.exportWaveform(0,3,2,2,3,pixels));
check(!audio.exportWaveform(0,0,0,2,3,pixels));
check(!audio.exportWaveform(1,0,2,2,3,pixels));
check(!audio.exportWaveform(0,0,2,3,3,pixels));
check(pixels[8]==255 && pixels[0]==0);
check(audio.exportWaveform(0,1,1,2,3,pixels));
check(pixels[0]==255 && pixels[4]==255 && pixels[8]==0);
0.0=>audio.gain;
check(audio.exportWaveform(0,0,2,2,3,pixels));
check(pixels[8]==255 && pixels[12]==255 && pixels[0]==0);
audio.reset();
check(audio.exportWaveform(0,0,4,2,3,pixels));
int whole[24];
check(audio.exportWaveform(0,2,3,whole));
check(whole[0]==pixels[0] && whole[8]==pixels[8] && whole[20]==pixels[20]);
check(audio.frames()==4 && audio.sampleAt(0,0)==-1.0);
""",21,p)

    run("video", """
@import "AudioIO";
AudioBuffer audio;
VideoWriter writer;
VideoClip clip;
VideoAudio soundtrack;
float source[8000];
for(0=>int i;i<4000;i++) 0.5=>source[i];
for(4000=>int i;i<8000;i++) -0.5=>source[i];
check(audio.importSamples(8000,1,source));
check(writer.openAV("waveform-animation.mkv",16,16,2,8000,1));
int pixels[1024];
float samples[4000];
for(0=>int i;i<2;i++) {
    check(audio.exportWaveform(0,i*4000,4000,16,16,pixels));
    check(audio.readSamples(i*4000,samples));
    check(writer.appendAV(pixels,samples));
}
check(writer.audioFrames()==8000 && writer.frames()==2);
check(writer.finish());
check(clip.load("waveform-animation.mkv"));
check(clip.exportPixels(pixels));
check(pixels[4*16*4]==255 && pixels[11*16*4]==0);
check(clip.next()==1);
check(clip.exportPixels(pixels));
check(pixels[4*16*4]==0 && pixels[11*16*4]==255);
check(soundtrack.load("waveform-animation.mkv",0));
check(soundtrack.frames()==8000 && soundtrack.sampleRate()==8000);
check(soundtrack.exportSamples(source));
check(source[0]==0.5 && source[7999]==-0.5);
""",20,p)

    run("midi", """
MidiSequence seq;
int pixels[4096];
check(!seq.exportPianoRoll(8000,0,8,128,pixels));
check(seq.create(480,120.0));
check(seq.addNote(0,0,480,0,60,100));
check(seq.addNote(0,480,480,0,64,100));
check(seq.renderFrames(8000)==8000);
check(seq.exportPianoRoll(8000,0,8,128,pixels));
check(pixels[(67*8+1)*4]==255 && pixels[(67*8+1)*4+1]==192);
check(pixels[(63*8+5)*4]==48 && pixels[(63*8+5)*4+2]==192);
check(pixels[0]==255 && pixels[3]==255 && pixels[7]==255);
check(seq.exportPianoRoll(8000,4000,8,128,pixels));
check(pixels[(67*8+1)*4]==48 && pixels[(63*8+5)*4]==255);
check(pixels[4*4]==255 && pixels[4*4+1]==255);
check(seq.exportPianoRoll(8000,8000,8,128,pixels));
check(pixels[(63*8+5)*4]==48 && pixels[7*4]==255);
check(!seq.exportPianoRoll(7999,0,8,128,pixels));
check(!seq.exportPianoRoll(8000,-1,8,128,pixels));
check(!seq.exportPianoRoll(8000,8001,8,128,pixels));
check(!seq.exportPianoRoll(8000,0,8,127,pixels));
check(!seq.exportPianoRoll(8000,0,9,128,pixels));
check(pixels[(63*8+5)*4]==48 && pixels[7*4]==255);
12=>seq.transpose;
check(seq.exportPianoRoll(8000,0,8,128,pixels));
check(pixels[(55*8+1)*4]==255 && pixels[(67*8+1)*4]==0);
seq.reset();
check(seq.addTempo(480,60.0));
check(seq.renderFrames(8000)==12000);
check(seq.exportPianoRoll(8000,4000,8,128,pixels));
check(pixels[(63*8+5)*4]==255 && pixels[(67*8+1)*4]==48);
""",26,p)

    run("video", """
@import "MidiFileIO";
MidiSequence seq;
VideoWriter writer;
VideoClip clip;
VideoAudio soundtrack;
check(seq.create(480,120.0));
check(seq.addNote(0,0,480,0,60,100));
check(seq.addNote(0,480,480,0,64,100));
check(seq.renderFrames(8000)==8000);
float pcm[8000];
check(seq.renderSamples(8000,pcm));
check(writer.openAV("piano-roll-animation.mkv",8,128,2,8000,1));
int pixels[4096];
float samples[4000];
for(0=>int i;i<2;i++) {
    check(seq.exportPianoRoll(8000,i*4000,8,128,pixels));
    for(0=>int j;j<4000;j++) pcm[i*4000+j]=>samples[j];
    check(writer.appendAV(pixels,samples));
}
check(writer.frames()==2 && writer.audioFrames()==8000);
check(writer.finish());
check(clip.load("piano-roll-animation.mkv"));
check(clip.exportPixels(pixels));
check(pixels[(67*8+1)*4]==255 && pixels[(63*8+5)*4]==48);
check(clip.next()==1);
check(clip.exportPixels(pixels));
check(pixels[(67*8+1)*4]==48 && pixels[(63*8+5)*4]==255);
check(soundtrack.load("piano-roll-animation.mkv",0));
check(soundtrack.frames()==8000 && soundtrack.sampleRate()==8000);
check(soundtrack.exportSamples(pcm));
check(Math.fabs(pcm[100])>0.001);
""",22,p)

    run("raster", """
RasterImage image;
int other[8];
int pixels[8];
check(!image.exportBlend(other,0.0,0,pixels));
check(image.importPixels(2,1,[255,0,0,255,255,0,0,0]));
[0,0,255,255,0,255,0,255] @=> int target[];
check(image.exportBlend(target,0.0,0,pixels));
check(pixels[0]==255 && pixels[2]==0 && pixels[3]==255);
check(pixels[4]==0 && pixels[5]==0 && pixels[7]==255);
check(image.exportBlend(target,0.5,0,pixels));
check(pixels[0]==128 && pixels[2]==128 && pixels[5]==128);
check(image.exportBlend(target,1.0,0,pixels));
check(pixels[2]==255 && pixels[5]==255);
check(image.exportBlend(target,0.0,0xffffff,pixels));
check(pixels[4]==255 && pixels[5]==255 && pixels[6]==255);
check(!image.exportBlend(target,-0.1,0,pixels));
check(!image.exportBlend(target,1.1,0,pixels));
check(!image.exportBlend(target,0.5,-1,pixels));
check(!image.exportBlend(target,0.5,0x1000000,pixels));
check(!image.exportBlend([1,2,3,4],0.5,0,pixels));
256=>target[7];
check(!image.exportBlend(target,0.5,0,pixels));
check(pixels[4]==255 && pixels[6]==255);
255=>target[7];
0.5=>image.red;
check(image.exportBlend(target,0.0,0,pixels));
check(pixels[0]==128 && image.pixelAt(0,0,0)==255);
check(image.exportBlend(target,0.5,0,target));
check(target[0]==64 && target[2]==128 && target[5]==128 && target[7]==255);
""",22,p)

    run("video", """
@import "RasterIO";
RasterImage image;
VideoWriter writer;
VideoClip clip;
check(image.create(16,16));
check(image.fill(255,0,0,255));
int target[1024];
int pixels[1024];
for(0=>int i;i<256;i++) { 255=>target[i*4+2]; 255=>target[i*4+3]; }
check(writer.open("raster-dissolve.mkv",16,16,3));
for(0=>int i;i<3;i++) {
    check(image.exportBlend(target,i/2.0,0,pixels));
    check(writer.appendFrame(pixels));
}
check(writer.frames()==3);
check(writer.finish());
check(clip.load("raster-dissolve.mkv"));
check(clip.exportPixels(pixels));
check(pixels[0]==255 && pixels[2]==0);
check(clip.next()==1);
check(clip.exportPixels(pixels));
check(pixels[0]==128 && pixels[2]==128);
check(clip.next()==1);
check(clip.exportPixels(pixels));
check(pixels[0]==0 && pixels[2]==255);
""",20,p)

    run("vector", """
VectorImage image;
int pixels[64];
check(!image.exportFrame(4,4,0,pixels));
check(image.create(4,4));
check(image.addRect(0.0,0.0,2.0,2.0,0xff0000,1.0));
check(image.exportFrame(4,4,0x0000ff,pixels));
check(pixels[0]==255 && pixels[2]==0 && pixels[3]==255);
check(pixels[60]==0 && pixels[62]==255 && pixels[63]==255);
0.5=>image.opacity;
check(image.exportFrame(4,4,0x0000ff,pixels));
check(pixels[0]>=127 && pixels[0]<=128 && pixels[2]>=127 && pixels[2]<=128 && pixels[3]==255);
0.0=>image.opacity;
check(image.exportFrame(4,4,0x123456,pixels));
check(pixels[0]==18 && pixels[1]==52 && pixels[2]==86 && pixels[3]==255);
image.reset();
2.0=>image.x;
check(image.exportFrame(4,4,0,pixels));
check(pixels[0]==0 && pixels[8]==255);
check(image.x()==2.0 && image.shapes()==1);
check(!image.exportFrame(0,4,0,pixels));
check(!image.exportFrame(4,4097,0,pixels));
check(!image.exportFrame(4096,4096,0,pixels));
check(!image.exportFrame(4,4,-1,pixels));
check(!image.exportFrame(4,4,0x1000000,pixels));
check(!image.exportFrame(2,2,0,pixels));
check(pixels[8]==255 && pixels[3]==255);
image.reset();
check(image.exportPixels(4,4,pixels));
check(pixels[63]==0);
check(image.exportFrame(4,4,0,pixels));
check(pixels[63]==255);
""",24,p)

    run("video", """
@import "VectorIO";
VectorImage image;
VideoWriter writer;
VideoClip clip;
check(image.create(16,16));
check(image.addRect(0.0,0.0,4.0,4.0,0xff0000,1.0));
check(writer.open("svg-animation.mkv",16,16,3));
int pixels[1024];
for(0=>int i;i<3;i++) {
    i*4.0=>image.x;
    check(image.exportFrame(16,16,0x0000ff,pixels));
    check(writer.appendFrame(pixels));
}
check(writer.frames()==3);
check(writer.finish());
check(clip.load("svg-animation.mkv"));
check(clip.exportPixels(pixels));
check(pixels[0]==255 && pixels[18]==255);
check(clip.next()==1);
check(clip.exportPixels(pixels));
check(pixels[2]==255 && pixels[16]==255);
check(clip.next()==1);
check(clip.exportPixels(pixels));
check(pixels[18]==255 && pixels[32]==255);
""",20,p)

    run("model", """
Model3D model;
int pixels[256];
check(!model.renderOrthographic(0,8,8,2.0,0xffffff,0x123456,pixels));
check(model.load("triangle.obj"));
check(model.renderOrthographic(0,8,8,2.0,0xffffff,0x123456,pixels));
check(pixels[0]==18 && pixels[1]==52 && pixels[2]==86 && pixels[3]==255);
check(pixels[115]==255 && pixels[112]==255);
check(pixels[59]==255);
int transparent[256];
check(model.renderOrthographic(0,8,8,2.0,0xffffff,transparent));
check(transparent[3]==0 && transparent[115]==255);
check(!model.renderOrthographic(-1,8,8,2.0,0xffffff,0,pixels));
check(!model.renderOrthographic(0,0,8,2.0,0xffffff,0,pixels));
check(!model.renderOrthographic(0,8,2049,2.0,0xffffff,0,pixels));
check(!model.renderOrthographic(0,8,8,0.0,0xffffff,0,pixels));
check(!model.renderOrthographic(0,8,8,2.0,-1,0,pixels));
check(!model.renderOrthographic(0,8,8,2.0,0xffffff,-1,pixels));
check(!model.renderOrthographic(0,8,8,2.0,0xffffff,0x1000000,pixels));
check(!model.renderOrthographic(0,4,4,2.0,0xffffff,0,pixels));
check(pixels[0]==18 && pixels[112]==255);
10.0=>model.x;
check(model.renderOrthographic(0,8,8,2.0,0xffffff,0x123456,pixels));
check(pixels[112]==18 && pixels[113]==52 && pixels[114]==86 && pixels[115]==255);
check(model.x()==10.0);
model.reset();
check(model.renderOrthographic(0,8,8,2.0,0xffffff,0,pixels));
check(pixels[112]==255 && pixels[0]==0 && pixels[3]==255);
check(model.vertexAt(0,1,0)==1.0);
""",23,p)

    run("video", """
@import "ModelIO";
Model3D model;
VideoWriter writer;
VideoClip clip;
check(model.createMesh([0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,0.0],[0,1,2]));
check(writer.open("model-animation.mkv",16,16,2));
int pixels[1024];
check(model.renderOrthographic(0,16,16,2.0,0xffffff,0x123456,pixels));
check(writer.appendFrame(pixels));
10.0=>model.x;
check(model.renderOrthographic(0,16,16,2.0,0xffffff,0x123456,pixels));
check(writer.appendFrame(pixels));
check(writer.frames()==2);
check(writer.finish());
check(clip.load("model-animation.mkv"));
check(clip.exportPixels(pixels));
check(pixels[0]==18 && pixels[1]==52 && pixels[2]==86);
check(pixels[(7*16+8)*4]==255);
check(clip.next()==1);
check(clip.exportPixels(pixels));
check(pixels[(7*16+8)*4]==18 && pixels[(7*16+8)*4+3]==255);
""",15,p)

    run("video", """
@import "AudioIO";
VideoAudio source;
AudioBuffer audio;
float block[4000];
float stats[4];
float bad[3];
check(!source.readChannel(0,0,block));
check(!source.analyze(0,0,1,stats));
check(source.load("avwriter.mkv",0));
check(source.readChannel(0,0,block));
check(block[0]==0.25 && block[3999]==0.25);
check(source.readChannel(1,4000,block));
check(block[0]==-0.25 && block[3999]==-0.25);
check(source.readChannel(-1,0,block));
check(block[0]==-0.125 && block[3999]==-0.125);
check(!source.readChannel(-2,0,block));
check(!source.readChannel(2,0,block));
check(!source.readChannel(0,-1,block));
check(!source.readChannel(0,4001,block));
check(block[0]==-0.125);
check(source.analyze(0,0,8000,stats));
check(stats[0]==0.125 && stats[1]==0.25 && stats[2]==0.1875);
check(Math.fabs(stats[3]-Math.sqrt(0.0390625))<0.000001);
check(source.analyze(1,0,4000,stats));
check(stats[0]==-0.5 && stats[1]==-0.5 && stats[2]==-0.5 && stats[3]==0.5);
check(source.analyze(-1,4000,4000,stats));
check(stats[0]==-0.0625 && stats[1]==-0.0625 && stats[3]==0.0625);
check(!source.analyze(2,0,1,stats));
check(!source.analyze(0,-1,1,stats));
check(!source.analyze(0,0,0,stats));
check(!source.analyze(0,7999,2,stats));
check(!source.analyze(0,0,1,bad));
check(stats[0]==-0.0625 && stats[3]==0.0625);
check(audio.importSamples(source.sampleRate(),1,block));
check(audio.channels()==1 && audio.frames()==4000 && audio.sampleAt(0,0)==-0.125);
check(audio.save("videoaudio-mono-analysis.wav"));
check(source.frames()==8000 && source.channels()==2);
source.clear();
check(!source.analyze(0,0,1,stats));
""",32,p)
    assert writer_pcm("videoaudio-mono-analysis.wav")==struct.pack("<h",-4096)*4000

    run("video", """
@import "MidiFileIO";
@import "AudioIO";
VideoClip clip;
MidiSequence seq;
check(clip.framePitch(48,60,0.0)==-1);
check(clip.load("generated.mkv"));
check(clip.framePitch(48,60,0.0)==51);
check(clip.framePitch(48,60,0.3)==-2);
check(clip.framePitch(-1,60,0.0)==-1);
check(clip.framePitch(60,48,0.0)==-1);
check(clip.framePitch(48,128,0.0)==-1);
check(clip.framePitch(48,60,-0.1)==-1);
check(clip.framePitch(48,60,1.1)==-1);
check(clip.framePitch(64,64,0.0)==64);
check(clip.frameIndex()==0);
int pitches[2];
clip.framePitch(48,60,0.0)=>pitches[0];
check(clip.next()==1);
clip.framePitch(48,60,0.0)=>pitches[1];
check(pitches[0]==51 && pitches[1]==49);
check(seq.importPitches(480,120.0,0.5,0,100,pitches));
check(seq.notes(0)==2 && seq.noteLength(0,0)==480);
check(seq.save("video-visual.mid"));
0.0=>clip.blue;
check(clip.framePitch(48,60,0.0)==48);
check(clip.framePitch(48,60,0.01)==-2);
1.0=>clip.brightness;
check(clip.framePitch(48,60,0.0)==60);
check(clip.frameIndex()==1);
clip.clear();
check(clip.framePitch(48,60,0.0)==-1);
VideoWriter writer;
VideoAudio source;
AudioBuffer audio;
int pixels[1024];
for(0=>int i;i<256;i++) 255=>pixels[i*4+3];
float samples[4096];
for(0=>int i;i<4096;i++) 0.5*Math.sin(2.0*Math.pi*440.0*i/8000.0)=>samples[i];
check(writer.openAV("video-pitch-audio.mkv",16,16,2,8000,1));
float block[4000];
for(0=>int i;i<4000;i++) samples[i]=>block[i];
check(writer.appendAV(pixels,block));
check(writer.finish());
check(source.load("video-pitch-audio.mkv",0));
check(source.readChannel(0,0,block));
check(audio.importSamples(8000,1,block));
int detected[3];
check(audio.detectPitches(0,1024,0.02,0.9,detected));
check(detected[0]==69 && detected[1]==69 && detected[2]==69);
check(seq.importPitches(480,120.0,0.128,0,100,detected));
check(seq.notes(0)==1);
check(seq.save("video-audio.mid"));
""",32,p)

    run("video", """
@import "VectorIO";
VideoClip clip;
VectorImage image;
int pixels[64];
int rendered[64];
check(!clip.exportQuantized(4,4,2,pixels));
check(clip.load("generated.mkv"));
check(clip.exportQuantized(4,4,2,pixels));
check(pixels[0]==255 && pixels[1]==0 && pixels[2]==0 && pixels[3]==255);
check(pixels[60]==255 && pixels[63]==255);
check(image.importPixels(4,4,pixels));
check(image.shapes()==4);
check(image.exportPixels(4,4,rendered));
check(rendered[0]==255 && rendered[62]==0 && rendered[63]==255);
check(image.save("video-red.svg"));
0.4=>clip.red;
check(clip.exportQuantized(4,4,2,pixels));
check(pixels[0]==0 && pixels[3]==255);
check(clip.exportQuantized(4,4,3,pixels));
check(pixels[0]==128 && pixels[60]==128);
check(clip.exportQuantized(4,4,256,pixels));
check(pixels[0]==102);
check(!clip.exportQuantized(4,4,1,pixels));
check(!clip.exportQuantized(4,4,257,pixels));
check(!clip.exportQuantized(0,4,2,pixels));
check(!clip.exportQuantized(65,65,2,pixels));
check(!clip.exportQuantized(2,2,2,pixels));
check(pixels[0]==102 && pixels[63]==255);
check(clip.frameIndex()==0);
check(clip.next()==1);
check(clip.exportQuantized(4,4,4,pixels));
check(pixels[0]==0 && pixels[2]==255 && pixels[63]==255);
check(image.importPixels(4,4,pixels));
check(image.shapes()==4);
check(image.exportPixels(4,4,rendered));
check(rendered[2]==255 && rendered[60]==0 && rendered[63]==255);
check(image.save("video-blue.svg"));
check(clip.frameIndex()==1 && clip.width()==16 && clip.height()==16);
clip.clear();
check(!clip.exportQuantized(4,4,2,pixels));
check(pixels[2]==255);
""",34,p)

    run("video", """
@import "ModelIO";
VideoClip clip;
Model3D model;
float heights[6];
check(!clip.exportHeightmap(3,2,heights));
check(clip.load("generated.mkv"));
check(clip.exportHeightmap(3,2,heights));
check(Math.fabs(heights[0]-0.2126)<0.000001 && Math.fabs(heights[5]-0.2126)<0.000001);
check(model.createHeightmap(3,2,heights,2.0,10.0));
check(model.meshVertices(0)==6 && model.faces(0)==4);
check(Math.fabs(model.vertexAt(0,0,2)-2.126)<0.00001);
check(model.vertexAt(0,0,0)==-2.0 && model.vertexAt(0,0,1)==1.0);
check(model.hasNormals(0));
float normals[18];
check(model.exportNormals(0,normals));
check(Math.fabs(normals[2]-1.0)<0.000001);
check(!clip.exportHeightmap(1,6,heights));
check(!clip.exportHeightmap(513,2,heights));
check(!clip.exportHeightmap(2,2,heights));
check(Math.fabs(heights[0]-0.2126)<0.000001);
check(clip.frameIndex()==0 && clip.width()==16 && clip.height()==16);
0.0=>clip.red;
check(clip.exportHeightmap(3,2,heights));
check(heights[0]==0.0 && heights[5]==0.0);
check(Math.fabs(model.vertexAt(0,0,2)-2.126)<0.00001);
check(clip.next()==1);
check(clip.exportHeightmap(3,2,heights));
check(Math.fabs(heights[0]-0.0722)<0.000001 && Math.fabs(heights[5]-0.0722)<0.000001);
check(model.createHeightmap(3,2,heights,2.0,10.0));
check(Math.fabs(model.vertexAt(0,5,2)-0.722)<0.00001);
check(model.meshVertices(0)==6 && model.faces(0)==4);
1.0=>clip.brightness;
check(clip.exportHeightmap(3,2,heights));
check(Math.fabs(heights[0]-1.0)<0.000001 && Math.fabs(heights[5]-1.0)<0.000001);
check(model.createHeightmap(3,2,heights,2.0,10.0));
check(Math.fabs(model.vertexAt(0,5,2)-10.0)<0.00001);
check(clip.frameIndex()==1);
clip.clear();
check(!clip.exportHeightmap(3,2,heights));
check(Math.fabs(heights[0]-1.0)<0.000001);
""",32,p)

    run("video", """
VideoClip clip;
VideoClip result;
VideoWriter writer;
int pixels[1024];
int original[1024];
[1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0,0.0] @=> float identity[];
[-1.0,0.0,0.0,1.0,0.0,-1.0,0.0,1.0,0.0,0.0,-1.0,1.0] @=> float negative[];
[0.2126,0.7152,0.0722,0.0,0.2126,0.7152,0.0722,0.0,0.2126,0.7152,0.0722,0.0] @=> float gray[];
check(!clip.exportColorMatrix(16,16,identity,pixels));
check(clip.load("generated.mkv"));
check(clip.exportPixels(original));
check(clip.exportColorMatrix(16,16,identity,pixels));
check(pixels[0]==original[0] && pixels[2]==original[2] && pixels[1023]==255);
check(clip.exportColorMatrix(16,16,negative,pixels));
check(pixels[0]==0 && pixels[1]==255 && pixels[2]==255 && pixels[3]==255);
check(clip.exportColorMatrix(16,16,gray,pixels));
check(pixels[0]==54 && pixels[1]==54 && pixels[2]==54 && pixels[1023]==255);
check(!clip.exportColorMatrix(16,16,[1.0],pixels));
check(!clip.exportColorMatrix(0,16,identity,pixels));
check(!clip.exportColorMatrix(4096,4096,identity,pixels));
check(!clip.exportColorMatrix(8,8,identity,pixels));
9.0=>identity[0];
check(!clip.exportColorMatrix(16,16,identity,pixels));
check(pixels[0]==54 && pixels[3]==255);
1.0=>identity[0];
0.5=>clip.red;
check(clip.exportColorMatrix(16,16,identity,pixels));
check(pixels[0]==128 && pixels[2]==0);
clip.reset();
check(clip.frameIndex()==0);
check(writer.open("matrix-video.mkv",16,16,2));
check(clip.exportColorMatrix(16,16,negative,pixels));
check(writer.appendFrame(pixels));
check(clip.next()==1);
check(clip.exportColorMatrix(16,16,gray,pixels));
check(pixels[0]==18 && pixels[1]==18 && pixels[2]==18);
check(writer.appendFrame(pixels));
check(writer.frames()==2);
check(writer.finish());
check(result.load("matrix-video.mkv"));
check(result.exportPixels(pixels));
check(pixels[0]==0 && pixels[1]==255 && pixels[2]==255);
check(result.next()==1);
check(result.exportPixels(pixels));
check(pixels[0]==18 && pixels[1]==18 && pixels[2]==18 && pixels[3]==255);
check(clip.frameIndex()==1);
clip.clear();
check(!clip.exportColorMatrix(16,16,identity,pixels));
check(pixels[0]==18);
""",36,p)

    run("audio", """
@import "ModelIO";
AudioBuffer audio;
Model3D model;
float heights[34];
float wave[4];
check(!audio.exportSpectralHeightmap(0,2,32,-60.0,heights));
check(!audio.exportWaveformHeightmap(0,2,wave));
float pcm[128];
check(audio.importSamples(8000,1,pcm));
check(audio.exportSpectralHeightmap(0,2,32,-60.0,heights));
check(heights[0]==0.0 && heights[33]==0.0);
check(audio.exportWaveformHeightmap(0,2,wave));
check(wave[0]==0.5 && wave[3]==0.5);
for(0=>int i;i<128;i++) 0.5*Math.sin(2.0*Math.pi*4*i/32.0)=>pcm[i];
check(audio.importSamples(8000,1,pcm));
check(audio.exportSpectralHeightmap(0,2,32,-60.0,heights));
check(heights[24]>0.89 && heights[24]<0.91);
check(heights[24]>heights[0] && heights[24]>heights[32]);
int rgba[136];
check(audio.exportSpectrogram(0,2,32,-60.0,rgba));
check(Math.fabs(heights[24]-rgba[96]/255.0)<0.002);
check(model.createHeightmap(2,17,heights,1.0,10.0));
check(model.meshVertices(0)==34 && model.faces(0)==32);
check(Math.fabs(model.vertexAt(0,24,2)-10.0*heights[24])<0.00001);
check(model.hasNormals(0));
check(!audio.exportSpectralHeightmap(1,2,32,-60.0,heights));
check(!audio.exportSpectralHeightmap(0,1,32,-60.0,heights));
check(!audio.exportSpectralHeightmap(0,2,64,-60.0,heights));
check(!audio.exportSpectralHeightmap(0,2,48,-60.0,heights));
check(!audio.exportSpectralHeightmap(0,2,1024,-60.0,heights));
check(!audio.exportSpectralHeightmap(0,2,32,-5.0,heights));
check(heights[24]>0.89);
0.0=>audio.gain;
check(audio.exportSpectralHeightmap(0,2,32,-60.0,heights));
check(heights[24]==0.0);
audio.reset();
check(audio.exportWaveformHeightmap(0,2,wave));
check(Math.fabs(wave[0]-0.75)<0.000001 && Math.fabs(wave[2]-0.25)<0.000001);
check(model.createHeightmap(2,2,wave,1.0,4.0));
check(model.vertexAt(0,0,2)==3.0 && model.vertexAt(0,2,2)==1.0);
check(!audio.exportWaveformHeightmap(1,2,wave));
check(!audio.exportWaveformHeightmap(0,1,wave));
check(!audio.exportWaveformHeightmap(0,3,wave));
check(wave[0]==0.75 && wave[2]==0.25);
check(audio.frames()==128 && Math.fabs(audio.sampleAt(2,0)-0.5)<0.000001);
""",35,p)

    run("midi", """
@import "RasterIO";
MidiSequence seq;
RasterImage image;
int pixels[2048];
check(!seq.exportStaticPianoRoll(4,128,0xffffff,0,pixels));
check(seq.create(480,120.0));
check(seq.addNote(0,0,480,0,60,127));
check(seq.addNote(0,480,480,9,64,64));
check(seq.exportStaticPianoRoll(4,128,0xffffff,0,pixels));
(127-60)*16=>int a;
(127-64)*16=>int b;
check(pixels[a]==255 && pixels[a+4]==255 && pixels[a+8]==0);
check(pixels[b]==0 && pixels[b+8]==129 && pixels[b+12]==129);
check(pixels[0]==0 && pixels[3]==255 && pixels[2047]==255);
check(image.importPixels(4,128,pixels));
check(image.pixelAt(0,67,0)==255 && image.pixelAt(3,63,0)==129);
check(image.save("static-piano-roll.png"));
check(seq.exportStaticPianoRoll(4,128,0xff0000,0x0000ff,pixels));
check(pixels[a]==255 && pixels[a+2]==0 && pixels[a+8+2]==255);
check(pixels[b+8]==129 && pixels[b+10]==126);
check(!seq.exportStaticPianoRoll(0,128,0xffffff,0,pixels));
check(!seq.exportStaticPianoRoll(4,127,0xffffff,0,pixels));
check(!seq.exportStaticPianoRoll(4,128,-1,0,pixels));
check(!seq.exportStaticPianoRoll(4,128,0xffffff,0x1000000,pixels));
check(!seq.exportStaticPianoRoll(2,128,0xffffff,0,pixels));
check(pixels[b+8]==129 && pixels[b+10]==126);
0.5=>seq.velocityGain;
check(seq.exportStaticPianoRoll(4,128,0xffffff,0,pixels));
check(pixels[a]==129 && pixels[b+8]==64);
1=>seq.transpose;
check(seq.exportStaticPianoRoll(4,128,0xffffff,0,pixels));
check(pixels[a]==0 && pixels[a-16]==129);
check(seq.noteLength(0,0)==480 && seq.notes(0)==2);
seq.reset();
check(seq.exportStaticPianoRoll(4,128,0xffffff,0,pixels));
check(pixels[a]==255 && pixels[b+8]==129);
seq.clear();
check(!seq.exportStaticPianoRoll(4,128,0xffffff,0,pixels));
""",28,p)

    run("midi", """
@import "VectorIO";
MidiSequence seq;
VectorImage image;
float rects[12];
check(seq.pianoRollRectCount()==-1);
check(!seq.exportPianoRollRects(8,128,0xff0000,rects));
check(seq.create(480,120.0));
check(seq.addNote(0,0,480,0,60,127));
check(seq.addNote(0,480,480,9,64,64));
check(seq.pianoRollRectCount()==2);
check(seq.exportPianoRollRects(8,128,0xff0000,rects));
check(rects[0]==0.0 && rects[1]==67.0 && rects[2]==4.0 && rects[3]==1.0);
check(rects[4]==16711680.0 && rects[5]==1.0);
check(rects[6]==4.0 && rects[7]==63.0 && rects[8]==4.0 && rects[9]==1.0);
check(Math.fabs(rects[11]-64.0/127.0)<0.000001);
check(!seq.exportPianoRollRects(0,128,0xff0000,rects));
check(!seq.exportPianoRollRects(8,127,0xff0000,rects));
check(!seq.exportPianoRollRects(8,128,-1,rects));
float bad[6];
check(!seq.exportPianoRollRects(8,128,0xff0000,bad));
check(rects[2]==4.0 && rects[4]==16711680.0);
check(image.importRects(8,128,rects));
check(image.shapes()==2);
int pixels[4096];
check(image.exportPixels(8,128,pixels));
check(pixels[67*32]==255 && pixels[67*32+3]==255);
check(pixels[63*32+16]==255 && pixels[63*32+19]>=128 && pixels[63*32+19]<=129);
check(pixels[3]==0);
check(image.save("vector-piano-roll.svg"));
1=>seq.transpose;
0.5=>seq.velocityGain;
check(seq.exportPianoRollRects(8,128,0x00ff00,rects));
check(rects[1]==66.0 && rects[4]==65280.0 && Math.fabs(rects[5]-64.0/127.0)<0.000001);
check(image.importRects(8,128,rects));
check(image.shapes()==2);
check(image.removeShape(0));
check(image.shapes()==1);
check(seq.notes(0)==2 && seq.noteLength(0,0)==480);
""",30,p)

    run("vector", """
VectorImage image;
float rects[6];
check(!image.importRects(8,8,rects));
check(image.importRects(8,8,[1.0,2.0,3.0,4.0,255.0,1.0]));
check(image.shapes()==1);
int pixels[256];
check(image.exportPixels(8,8,pixels));
check(pixels[(2*8+1)*4+2]==255 && pixels[(2*8+1)*4+3]==255);
check(pixels[3]==0);
2.0=>image.x;
check(!image.importRects(8,8,[1.0,2.0,0.0,4.0,255.0,1.0]));
check(!image.importRects(8,8,[1.0,2.0,3.0,4.0,255.5,1.0]));
check(!image.importRects(8,8,[1.0,2.0,3.0,4.0,255.0,1.1]));
check(!image.importRects(8,8,[1.0,2.0,3.0]));
check(!image.importRects(0,8,rects));
check(image.shapes()==1 && image.x()==2.0);
check(image.importRects(8,8,[0.0,0.0,2.0,2.0,16711680.0,0.5]));
check(image.x()==0.0 && image.shapes()==1);
check(image.exportPixels(8,8,pixels));
check(pixels[0]==255 && pixels[3]>=127 && pixels[3]<=128);
""",16,p)

    run("midi", """
@import "ModelIO";
MidiSequence seq;
Model3D model;
float boxes[12];
check(!seq.exportNoteBoxes(8.0,2.0,4.0,boxes));
check(seq.create(480,120.0));
check(seq.addNote(0,0,480,0,60,127));
check(seq.addNote(0,480,480,9,64,64));
check(seq.pianoRollRectCount()==2);
check(seq.exportNoteBoxes(8.0,2.0,4.0,boxes));
check(boxes[0]==-4.0 && boxes[3]==0.0 && boxes[2]==0.0 && boxes[5]==4.0);
check(Math.fabs(boxes[1]+7.8)<0.000001 && Math.fabs(boxes[4]+6.2)<0.000001);
check(boxes[6]==0.0 && boxes[9]==4.0 && Math.fabs(boxes[11]-256.0/127.0)<0.000001);
check(!seq.exportNoteBoxes(0.0,2.0,4.0,boxes));
check(!seq.exportNoteBoxes(8.0,-1.0,4.0,boxes));
check(!seq.exportNoteBoxes(8.0,2.0,0.0,boxes));
float wrong[6];
check(!seq.exportNoteBoxes(8.0,2.0,4.0,wrong));
check(boxes[0]==-4.0 && boxes[5]==4.0);
check(model.createBoxes(boxes));
check(model.meshVertices(0)==48 && model.faces(0)==24);
check(model.hasNormals(0));
check(model.vertexAt(0,0,0)==-4.0 && model.vertexAt(0,4,2)==4.0);
check(Math.fabs(model.vertexAt(0,28,2)-256.0/127.0)<0.00001);
1=>seq.transpose;
0.5=>seq.velocityGain;
check(seq.exportNoteBoxes(8.0,2.0,4.0,boxes));
check(Math.fabs(boxes[1]+5.8)<0.000001 && Math.fabs(boxes[5]-256.0/127.0)<0.000001);
check(model.createBoxes(boxes));
check(Math.fabs(model.vertexAt(0,4,2)-256.0/127.0)<0.00001);
check(seq.notes(0)==2 && seq.noteLength(0,0)==480);
seq.clear();
check(!seq.exportNoteBoxes(8.0,2.0,4.0,boxes));
""",25,p)

    run("model", """
Model3D model;
float boxes[6];
check(!model.createBoxes(boxes));
check(model.createBoxes([-1.0,-1.0,0.0,1.0,1.0,2.0]));
check(model.meshVertices(0)==24 && model.faces(0)==12);
check(model.hasNormals(0));
float normals[72];
check(model.exportNormals(0,normals));
check(normals[2]==-1.0 && normals[14]==1.0 && normals[25]==-1.0);
float uv[48];
check(model.exportUV(0,uv));
check(uv[0]==0.0 && uv[1]==0.0 && uv[2]==1.0 && uv[3]==0.0);
check(model.faceIndex(0,0,0)==0 && model.faceIndex(0,0,1)==1 && model.faceIndex(0,0,2)==2);
int pixels[64];
check(model.renderOrthographic(0,4,4,2.0,0xffffff,pixels));
check(pixels[0]==255 && pixels[3]==255 && pixels[63]==255);
check(!model.createBoxes([0.0,0.0,0.0,1.0,1.0,0.0]));
check(!model.createBoxes([2.0,0.0,0.0,1.0,1.0,1.0]));
check(!model.createBoxes([0.0,0.0,0.0,1.0,1.0,1000000001.0]));
check(!model.createBoxes([0.0,0.0,0.0]));
check(model.meshVertices(0)==24 && model.vertexAt(0,4,2)==2.0);
check(model.createBoxes([0.0,0.0,0.0,1.0,1.0,1.0,2.0,2.0,0.0,3.0,3.0,2.0]));
check(model.meshVertices(0)==48 && model.faces(0)==24);
check(model.vertexAt(0,28,2)==2.0);
""",19,p)

    run("vector", """
@import "AudioIO";
@import "RasterIO";
VectorImage image;
AudioBuffer audio;
RasterImage raster;
float mono[4];
float stereo[8];
float reference[8];
int pixels[16];
check(!image.exportAudioSamples(2,2,1,mono));
check(image.create(2,2));
check(image.addRect(0.0,0.0,1.0,1.0,0x000000,1.0));
check(image.addRect(1.0,0.0,1.0,1.0,0xffffff,1.0));
check(image.addRect(0.0,1.0,1.0,1.0,0xff0000,1.0));
check(image.exportAudioSamples(2,2,1,mono));
check(Math.fabs(mono[0]+1.0)<0.000001 && Math.fabs(mono[1]-1.0)<0.000001);
check(Math.fabs(mono[2]+0.5748)<0.000001 && mono[3]==0.0);
check(image.exportAudioSamples(2,2,2,stereo));
check(stereo[0]==-1.0 && stereo[1]==-1.0 && stereo[2]==1.0 && stereo[3]==1.0);
check(stereo[4]==0.0 && stereo[5]==-1.0 && stereo[6]==0.0 && stereo[7]==0.0);
check(image.exportPixels(2,2,pixels));
check(raster.importPixels(2,2,pixels));
check(raster.exportAudioSamples(2,reference));
check(reference[0]==stereo[0] && reference[5]==stereo[5] && reference[7]==stereo[7]);
check(!image.exportAudioSamples(0,2,1,mono));
check(!image.exportAudioSamples(2,4097,1,mono));
check(!image.exportAudioSamples(4096,4096,1,mono));
check(!image.exportAudioSamples(2,2,0,mono));
check(!image.exportAudioSamples(2,2,3,mono));
check(!image.exportAudioSamples(2,2,2,mono));
check(mono[0]==-1.0 && mono[1]==1.0);
0.5=>image.opacity;
check(image.exportAudioSamples(2,2,1,mono));
check(mono[0]< -0.49 && mono[0]> -0.51 && mono[1]>0.49 && mono[1]<0.51);
check(mono[3]==0.0);
0.0=>image.opacity;
check(image.exportAudioSamples(2,2,1,mono));
check(mono[0]==0.0 && mono[1]==0.0 && mono[2]==0.0 && mono[3]==0.0);
image.reset();
1.0=>image.x;
check(image.exportAudioSamples(2,2,1,mono));
check(mono[0]==0.0 && mono[1]==-1.0 && mono[2]==0.0 && Math.fabs(mono[3]+0.5748)<0.000001);
check(image.x()==1.0 && image.shapes()==3);
image.reset();
check(image.exportAudioSamples(2,2,2,stereo));
check(audio.importSamples(8000,2,stereo));
check(audio.frames()==4 && audio.channels()==2 && audio.sampleRate()==8000);
check(audio.sampleAt(2,1)==-1.0);
0.5=>audio.gain;
check(audio.save("svg-sonification.wav"));
image.clear();
check(!image.exportAudioSamples(2,2,1,mono));
check(mono[1]==-1.0);
""",37,p)

def text_core():
    directory = tempfile.TemporaryDirectory()
    p = pathlib.Path(directory.name)
    run("text", """
TextStyle base;
TextStyle title;
TextDocument doc;
check(base.font()=="Sans" && base.fontSize()==16.0);
check(base.bold()==0 && base.italic()==0 && base.underline()==0 && base.color()==0);
"Serif"=>base.font;
20.0=>base.fontSize;
0x123456=>base.color;
check(title.inherit(base));
check(title.font()=="Serif" && title.fontSize()==20.0 && title.color()==0x123456);
40.0=>title.fontSize;
1=>title.bold;
1=>title.italic;
1=>title.underline;
check(title.bold()==1 && title.italic()==1 && title.underline()==1);
check(base.fontSize()==20.0 && base.bold()==0);
check(title.fontSize(0.0)==-1.0 && title.fontSize()==40.0);
check(title.color(-1)==-1 && title.color()==0x123456);
check(title.bold(2)==-1 && title.bold()==1);
check(doc.paragraphCount()==0);
check(doc.addParagraph("Ciao <b>mondo</b> & testo",base));
check(doc.addParagraph("Titolo",title));
check(doc.paragraphCount()==2);
check(doc.paragraphText(0)=="Ciao <b>mondo</b> & testo");
check(doc.paragraphText(1)=="Titolo");
int dims[2];
check(!doc.measureParagraph(-1,500,dims));
check(doc.measureParagraph(1,500,dims));
check(dims[0]>0 && dims[1]>0);
dims[1]=>int tall;
10.0=>title.fontSize;
check(doc.measureParagraph(1,500,dims));
check(dims[1]==tall);
check(doc.setParagraphStyle(1,title));
check(doc.measureParagraph(1,500,dims));
check(dims[1]<tall);
check(doc.setParagraphText(0,"Testo lungo con molte parole da distribuire su righe"));
check(doc.measureParagraph(0,500,dims));
dims[1]=>int wide;
check(doc.measureParagraph(0,60,dims));
check(dims[1]>wide);
check(!doc.measureParagraph(0,0,dims));
check(!doc.setParagraphText(8,"invalid"));
check(!doc.setParagraphStyle(8,title));
check(!doc.removeParagraph(-1));
check(doc.removeParagraph(0));
check(doc.paragraphCount()==1 && doc.paragraphText(0)=="Titolo");
doc.clear();
check(doc.paragraphCount()==0);
check(!doc.measureParagraph(0,100,dims));
""",35,p)

    run("text", """
TextStyle style;
TextStyle copy;
TextDocument doc;
check(style.weight()==400 && style.bold()==0);
check(style.strikethrough()==0 && style.backgroundColor()==-1);
check(style.letterSpacing()==0.0 && style.baselineShift()==0.0 && style.script()==0 && style.opacity()==1.0);
check(style.weight(550)==550 && style.bold()==0);
check(style.weight(800)==800 && style.bold()==1);
check(style.bold(0)==0 && style.weight()==400);
check(style.bold(1)==1 && style.weight()==700);
check(style.weight(99)==-1 && style.weight()==700);
check(style.weight(1001)==-1 && style.weight()==700);
check(style.strikethrough(1)==1 && style.strikethrough()==1);
check(style.strikethrough(2)==-1 && style.strikethrough()==1);
check(style.backgroundColor(0xffcc00)==0xffcc00);
check(style.backgroundColor(-2)==-2 && style.backgroundColor()==0xffcc00);
check(style.backgroundColor(-1)==-1 && style.backgroundColor()==-1);
check(style.backgroundColor(0x102030)==0x102030);
check(style.letterSpacing(3.0)==3.0);
style.letterSpacing(129.0);
check(style.letterSpacing()==3.0);
check(style.baselineShift(-4.0)==-4.0);
style.baselineShift(-513.0);
check(style.baselineShift()==-4.0);
check(style.script(1)==1);
check(style.script(3)==-1 && style.script()==1);
check(style.opacity(0.5)==0.5);
style.opacity(1.1);
check(style.opacity()==0.5);
check(copy.inherit(style));
check(copy.weight()==700 && copy.strikethrough()==1 && copy.backgroundColor()==0x102030);
check(copy.letterSpacing()==3.0 && copy.baselineShift()==-4.0 && copy.script()==1 && copy.opacity()==0.5);
check(style.script(2)==2 && copy.script()==1);
TextStyle plain;
32.0=>plain.fontSize;
check(doc.addParagraph("MMMMMMMM",plain));
int dims[2];
check(doc.measureParagraph(0,1000,dims));
dims[0]=>int originalWidth;
8.0=>plain.letterSpacing;
check(doc.setParagraphStyle(0,plain));
check(doc.measureParagraph(0,1000,dims));
check(dims[0]>originalWidth);
0.0=>plain.letterSpacing;
1=>plain.script;
check(doc.setParagraphStyle(0,plain));
check(doc.measureParagraph(0,1000,dims));
check(dims[0]<originalWidth);
2=>plain.script;
check(doc.setParagraphStyle(0,plain));
check(doc.measureParagraph(0,1000,dims));
check(dims[0]<originalWidth);
check(doc.paragraphText(0)=="MMMMMMMM");
""",39,p)

    run("text", """
@import "KeyframeIO";
TextStyle base;
TextStyle accent;
TextRun first;
TextRun secondRun;
TextDocument doc;
check(first.text()=="");
check(first.set(" bold",accent));
check(first.text()==" bold");
check(secondRun.set(" italic",accent));
check(doc.addParagraph("Base",base));
check(doc.runCount(0)==0 && doc.runCount(1)==-1);
check(doc.appendRun(0,first));
check(doc.appendRun(0,secondRun));
check(doc.runCount(0)==2 && doc.paragraphText(0)=="Base bold italic");
check(doc.runText(0,0)==" bold" && doc.runText(0,1)==" italic");
check(first.setText(" changed"));
check(doc.runText(0,0)==" bold");
check(doc.setRun(0,0,first));
check(doc.paragraphText(0)=="Base changed italic");
check(!doc.setRun(0,2,first));
check(!doc.appendRun(1,first));
check(!doc.removeRun(0,-1));
check(!doc.setRunStyle(0,4,base));
check(doc.removeRun(0,0));
check(doc.paragraphText(0)=="Base italic" && doc.runCount(0)==1);
check(doc.setParagraphText(0,""));
check(doc.runCount(0)==0 && doc.paragraphText(0)=="");
check(first.set("MMMMMMMM",base));
check(doc.appendRun(0,first));
int dims[2];
check(doc.measureParagraph(0,1000,dims));
dims[0]=>int original;
KeyframeCurve curve;
check(curve.add(0.0,16.0,"linear"));
check(curve.add(2.0,48.0,"linear"));
curve.valueAt(1.0)=>accent.fontSize;
check(accent.fontSize()==32.0);
check(doc.setRunStyle(0,0,accent));
check(doc.measureParagraph(0,1000,dims));
check(dims[0]>original);
check(first.setStyle(accent));
check(doc.setRun(0,0,first));
check(doc.measureParagraph(0,1000,dims));
check(dims[0]>original);
check(doc.setParagraphStyle(0,base));
check(doc.measureParagraph(0,1000,dims));
check(dims[0]>original);
check(doc.removeParagraph(0));
check(doc.paragraphCount()==0);
check(doc.addParagraph("Unicode",base));
check(first.set(" café",accent));
check(doc.appendRun(0,first));
check(doc.paragraphText(0)=="Unicode café");
doc.clear();
check(doc.paragraphCount()==0 && doc.runCount(0)==-1);
""",45,p)

    run("text", """
@import "KeyframeIO";
TextStyle style;
TextParagraphStyle layout;
TextParagraphStyle saved;
TextDocument doc;
check(layout.alignment()==0 && layout.wrapMode()==0 && layout.direction()==0);
check(layout.lineSpacing()==0.0 && layout.indent()==0.0);
check(layout.marginLeft()==0.0 && layout.marginRight()==0.0 && layout.marginTop()==0.0 && layout.marginBottom()==0.0);
check(layout.alignment(3)==3);
check(layout.alignment(4)==-1 && layout.alignment()==3);
check(layout.wrapMode(1)==1 && layout.wrapMode(3)==-1);
check(layout.direction(2)==2 && layout.direction(3)==-1);
check(layout.indent(-10.0)==-10.0);
layout.indent(-1025.0);
check(layout.indent()==-10.0);
check(layout.lineSpacing(8.0)==8.0);
layout.lineSpacing(-1.0);
check(layout.lineSpacing()==8.0);
check(layout.marginLeft(12.0)==12.0 && layout.marginRight(14.0)==14.0);
check(layout.marginTop(5.0)==5.0 && layout.marginBottom(7.0)==7.0);
layout.marginLeft(2049.0);
check(layout.marginLeft()==12.0);
check(saved.inherit(layout));
check(saved.alignment()==3 && saved.wrapMode()==1 && saved.direction()==2);
check(saved.indent()==-10.0 && saved.lineSpacing()==8.0 && saved.marginRight()==14.0);
check(!doc.setParagraphLayout(0,layout));
check(doc.addParagraph("A long paragraph with multiple words for reliable line wrapping.",style));
int dims[2];
check(doc.measureParagraph(0,180,dims));
dims[1]=>int originalHeight;
TextParagraphStyle flow;
20.0=>flow.marginLeft;
20.0=>flow.marginRight;
10.0=>flow.marginTop;
15.0=>flow.marginBottom;
check(doc.setParagraphLayout(0,flow));
check(doc.measureParagraph(0,180,dims));
check(dims[1]>=originalHeight+25);
dims[1]=>int marginHeight;
check(doc.getParagraphLayout(0,saved));
check(saved.marginLeft()==20.0 && saved.marginBottom()==15.0);
KeyframeCurve curve;
check(curve.add(0.0,0.0,"linear"));
check(curve.add(2.0,20.0,"linear"));
curve.valueAt(1.0)=>flow.lineSpacing;
check(flow.lineSpacing()==10.0);
check(doc.measureParagraph(0,180,dims));
check(dims[1]==marginHeight);
check(doc.setParagraphLayout(0,flow));
check(doc.measureParagraph(0,180,dims));
check(dims[1]>marginHeight);
dims[1]=>int firstHeight;
check(doc.addParagraph("Second",style));
check(doc.measureParagraph(1,180,dims));
dims[1]=>int secondHeight;
check(doc.measureDocument(180,dims));
check(dims[1]==firstHeight+secondHeight);
check(!doc.measureDocument(30,dims));
check(dims[1]==firstHeight+secondHeight);
check(!doc.getParagraphLayout(3,saved));
check(doc.setParagraphText(0,"Changed"));
check(doc.getParagraphLayout(0,saved));
check(saved.lineSpacing()==10.0 && saved.marginLeft()==20.0);
doc.clear();
check(doc.measureDocument(180,dims));
check(dims[0]==0 && dims[1]==0);
""",45,p)

    run("text", """
@import "RasterIO";
@import "KeyframeIO";
TextDocument doc;
TextStyle style;
TextStyle accent;
TextRun run;
TextParagraphStyle flow;
RasterImage raster;
int rgba[128*64*4];
int tiny[4];
check(doc.exportPixels(1,1,-1,tiny));
check(tiny[0]==0 && tiny[1]==0 && tiny[2]==0 && tiny[3]==0);
check(doc.exportPixels(1,1,0x123456,tiny));
check(tiny[0]==18 && tiny[1]==52 && tiny[2]==86 && tiny[3]==255);
check(!doc.exportPixels(0,1,-1,tiny));
check(!doc.exportPixels(1,4097,-1,tiny));
check(!doc.exportPixels(4096,4096,-1,tiny));
check(!doc.exportPixels(1,1,-2,tiny));
check(!doc.exportPixels(1,1,0x1000000,tiny));
check(!doc.exportPixels(2,1,-1,tiny));
check(tiny[0]==18 && tiny[3]==255);
24.0=>style.fontSize;
0xff0000=>style.color;
check(doc.addParagraph("M",style));
check(accent.inherit(style));
0x0000ff=>accent.color;
check(run.set("M",accent));
check(doc.appendRun(0,run));
8.0=>flow.marginLeft;
8.0=>flow.marginTop;
check(doc.setParagraphLayout(0,flow));
check(doc.exportPixels(128,64,-1,rgba));
check(rgba[3]==0);
0=>int redCount; 0=>int blueCount; 0=>int alphaCount; 0=>int outside;
for(0=>int i;i<128*64;i++) {
    if(rgba[4*i+3]>0) {
        alphaCount++;
        if(rgba[4*i]>200 && rgba[4*i+2]<30) redCount++;
        if(rgba[4*i+2]>200 && rgba[4*i]<30) blueCount++;
        if(i%128<8 || i/128<8) outside++;
    }
}
check(redCount>0 && blueCount>0 && alphaCount>0);
check(outside==0);
check(raster.importPixels(128,64,rgba));
check(raster.pixelAt(0,0,3)==0);
check(doc.exportPixels(128,64,0xffffff,rgba));
0=>int nonOpaque;
for(0=>int i;i<128*64;i++) if(rgba[4*i+3]!=255) nonOpaque++;
check(nonOpaque==0 && rgba[0]==255 && rgba[1]==255 && rgba[2]==255);
check(doc.savePNG("text-render.png",128,64,-1));
check(!doc.savePNG("text-render.png",128,64,-1));
check(!doc.savePNG("text-render.txt",128,64,-1));
check(raster.load("text-render.png"));
check(raster.width()==128 && raster.height()==64);
check(raster.pixelAt(0,0,3)==0);
KeyframeCurve curve;
check(curve.add(0.0,8.0,"linear"));
check(curve.add(2.0,48.0,"linear"));
curve.valueAt(2.0)=>flow.marginLeft;
check(doc.setParagraphLayout(0,flow));
check(doc.exportPixels(128,64,-1,rgba));
0=>int leftInk; 0=>int rightInk;
for(0=>int i;i<128*64;i++) if(rgba[4*i+3]>0) {
    if(i%128<48) leftInk++; else rightInk++;
}
check(leftInk==0 && rightInk>0);
check(doc.paragraphText(0)=="MM" && doc.runCount(0)==1);
128.0=>flow.marginLeft;
check(doc.setParagraphLayout(0,flow));
123=>rgba[0];
check(!doc.exportPixels(128,64,-1,rgba));
check(rgba[0]==123);
check(!doc.savePNG("invalid-layout.png",128,64,-1));
doc.clear();
check(doc.exportPixels(128,64,-1,rgba));
0=>int nonzero;
for(0=>int i;i<rgba.size();i++) if(rgba[i]!=0) nonzero++;
check(nonzero==0);
""",42,p)
    assert (p / "text-render.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
    assert not (p / "invalid-layout.png").exists()

    run("text", """
@import "KeyframeIO";
TextDocument doc;
TextStyle style;
TextParagraphStyle flow;
TextContainer box;
TextContainer copy;
check(box.paddingLeft()==0.0 && box.paddingRight()==0.0 && box.paddingTop()==0.0 && box.paddingBottom()==0.0);
check(box.borderWidth()==0.0 && box.borderColor()==0 && box.backgroundColor()==-1 && box.opacity()==1.0);
check(box.paddingLeft(8.0)==8.0 && box.paddingRight(10.0)==10.0);
check(box.paddingTop(6.0)==6.0 && box.paddingBottom(7.0)==7.0);
check(box.borderWidth(2.0)==2.0 && box.borderColor(0xff0000)==0xff0000);
check(box.backgroundColor(0x00ff00)==0x00ff00);
box.paddingLeft(-1.0);
check(box.paddingLeft()==8.0);
box.borderWidth(129.0);
check(box.borderWidth()==2.0);
check(box.backgroundColor(-2)==-2 && box.backgroundColor()==0x00ff00);
check(box.borderColor(-1)==-1 && box.borderColor()==0xff0000);
check(copy.inherit(box));
check(copy.paddingLeft()==8.0 && copy.borderWidth()==2.0 && copy.backgroundColor()==0x00ff00);
check(!doc.setParagraphContainer(0,box));
check(doc.addParagraph("M",style));
int dims[2];
check(doc.measureParagraph(0,96,dims));
dims[0]=>int plainW; dims[1]=>int plainH;
check(doc.setParagraphContainer(0,box));
check(doc.measureParagraph(0,96,dims));
check(dims[0]==plainW+22 && dims[1]==plainH+17);
check(doc.measureDocument(96,dims));
check(dims[0]==plainW+22 && dims[1]==plainH+17);
check(doc.getParagraphContainer(0,copy));
check(copy.paddingBottom()==7.0 && copy.borderColor()==0xff0000);
check(!doc.getParagraphContainer(3,copy));
4.0=>flow.marginLeft; 5.0=>flow.marginRight; 3.0=>flow.marginTop;
check(doc.setParagraphLayout(0,flow));
int rgba[96*64*4];
check(doc.exportPixels(96,64,-1,rgba));
check(rgba[3]==0);
(3*96+4)*4=>int edge;
check(rgba[edge]==255 && rgba[edge+1]==0 && rgba[edge+3]==255);
(7*96+8)*4=>int fill;
check(rgba[fill]==0 && rgba[fill+1]==255 && rgba[fill+3]==255);
0.0=>box.opacity;
check(doc.exportPixels(96,64,-1,rgba));
check(rgba[edge+3]==255);
check(doc.setParagraphContainer(0,box));
check(doc.exportPixels(96,64,-1,rgba));
0=>int ink;
for(0=>int i;i<96*64;i++) if(rgba[4*i+3]>0) ink++;
check(ink==0);
KeyframeCurve curve;
check(curve.add(0.0,0.0,"linear"));
check(curve.add(2.0,1.0,"linear"));
curve.valueAt(1.0)=>box.opacity;
check(doc.setParagraphContainer(0,box));
check(doc.exportPixels(96,64,-1,rgba));
check(rgba[edge+3]>=126 && rgba[edge+3]<=129);
check(rgba[fill+3]>=126 && rgba[fill+3]<=129);
check(doc.paragraphText(0)=="M");
96.0=>box.paddingLeft;
check(doc.setParagraphContainer(0,box));
123=>rgba[0];
check(!doc.exportPixels(96,64,-1,rgba));
check(rgba[0]==123);
check(!doc.measureParagraph(0,96,dims));
check(doc.setParagraphText(0,"New"));
check(doc.getParagraphContainer(0,copy));
check(copy.paddingLeft()==96.0);
doc.clear();
check(doc.paragraphCount()==0);
""",48,p)

    run("text", """
@import "KeyframeIO";
TextDocument doc;
TextStyle style;
TextList items;
TextList saved;
check(items.mode()==0 && items.number()==1 && items.level()==0);
check(items.levelIndent()==20.0 && items.markerWidth()==32.0);
check(items.mode(4)==-1 && items.mode()==0);
check(items.number(0)==-1 && items.number()==1);
check(items.level(17)==-1 && items.level()==0);
items.markerWidth(0.0);
check(items.markerWidth()==32.0);
check(!doc.setParagraphList(0,items));
check(doc.addParagraph("M",style));
check(doc.paragraphListLabel(0)=="");
int dims[2];
check(doc.measureParagraph(0,160,dims));
dims[0]=>int plainWidth;
1=>items.mode;
check(doc.setParagraphList(0,items));
check(doc.paragraphListLabel(0)=="•");
check(doc.paragraphText(0)=="M");
check(doc.measureParagraph(0,160,dims));
check(dims[0]==plainWidth+32);
2=>items.mode;
7=>items.number;
check(doc.paragraphListLabel(0)=="•");
check(doc.setParagraphList(0,items));
check(doc.paragraphListLabel(0)=="7.");
check(doc.getParagraphList(0,saved));
check(saved.number()==7 && saved.mode()==2);
check(!doc.getParagraphList(3,saved));
3=>items.mode;
"++"=>items.marker;
check(items.marker()=="++");
items.marker("");
check(items.marker()=="++");
check(doc.setParagraphList(0,items));
check(doc.paragraphListLabel(0)=="++");
int pixels[160*64*4];
check(doc.exportPixels(160,64,-1,pixels));
0=>int markerInk; 0=>int bodyInk;
for(0=>int i;i<160*64;i++) if(pixels[i*4+3]>0) {
    if(i%160<32) markerInk++; else bodyInk++;
}
check(markerInk>0 && bodyInk>0);
1=>items.level;
check(doc.setParagraphList(0,items));
check(doc.measureParagraph(0,160,dims));
check(dims[0]==plainWidth+52);
check(doc.exportPixels(160,64,-1,pixels));
0=>int leftInk;
for(0=>int i;i<160*64;i++) if(i%160<20 && pixels[i*4+3]>0) leftInk++;
check(leftInk==0);
KeyframeCurve curve;
check(curve.add(0.0,20.0,"linear"));
check(curve.add(2.0,40.0,"linear"));
curve.valueAt(1.0)=>items.levelIndent;
check(doc.setParagraphList(0,items));
check(doc.measureParagraph(0,160,dims));
check(dims[0]==plainWidth+62);
check(doc.setParagraphText(0,"New"));
check(doc.paragraphListLabel(0)=="++");
check(!doc.measureParagraph(0,30,dims));
0=>items.mode;
check(doc.setParagraphList(0,items));
check(doc.paragraphListLabel(0)=="");
check(doc.measureParagraph(0,30,dims));
doc.clear();
check(doc.paragraphCount()==0);
""",44,p)

    run("text", """
@import "KeyframeIO";
TextTable tbl;
TextDocument doc;
TextStyle style;
TextContainer box;
TextParagraphStyle flow;
check(tbl.rows()==0 && tbl.columns()==0);
check(!tbl.create(0,2));
check(!tbl.create(256,32));
check(!doc.appendTable(tbl));
check(tbl.create(2,2));
check(tbl.rows()==2 && tbl.columns()==2);
check(tbl.columnWeight(0)==1.0 && tbl.columnWeight(1)==1.0);
check(tbl.columnWeight(2,1.0)==-1.0);
check(tbl.columnWeight(0,0.0)==-1.0);
check(tbl.setCell(0,0,"A",style));
check(tbl.setCell(0,1,"B",style));
check(tbl.cellText(0,0)=="A" && tbl.cellText(0,1)=="B");
check(!tbl.setCell(2,0,"X",style));
check(tbl.cellText(-1,0)=="");
0xff0000=>box.backgroundColor;
check(tbl.setCellContainer(0,0,box));
0x00ff00=>box.backgroundColor;
check(tbl.setCellContainer(0,1,box));
check(!tbl.setCellContainer(2,0,box));
1=>flow.alignment;
check(tbl.setCellLayout(0,1,flow));
check(!tbl.setCellLayout(0,2,flow));
check(doc.appendTable(tbl));
check(doc.tableCount()==1 && doc.paragraphCount()==0);
int dims[2];
check(doc.measureDocument(120,dims));
check(dims[0]==120 && dims[1]>0);
int pixels[120*96*4];
check(doc.exportPixels(120,96,-1,pixels));
check(pixels[0]==255 && pixels[1]==0 && pixels[3]==255);
check(pixels[4*60]==0 && pixels[4*60+1]==255 && pixels[4*60+3]==255);
check(tbl.columnWeight(0,3.0)==3.0);
check(doc.exportPixels(120,96,-1,pixels));
check(pixels[4*60+1]==255);
check(doc.setTable(0,tbl));
check(doc.exportPixels(120,96,-1,pixels));
check(pixels[4*60]==255 && pixels[4*60+1]==0);
check(pixels[4*90+1]==255);
KeyframeCurve curve;
check(curve.add(0.0,1.0,"linear"));
check(curve.add(2.0,3.0,"linear"));
check(tbl.columnWeight(0,curve.valueAt(1.0))==2.0);
check(doc.setTable(0,tbl));
check(doc.exportPixels(120,96,-1,pixels));
check(pixels[4*79]==255 && pixels[4*80+1]==255);
check(!doc.setTable(1,tbl));
check(!doc.measureDocument(1,dims));
check(doc.removeTable(0));
check(doc.tableCount()==0);
check(!doc.removeTable(0));
check(doc.addParagraph("Before",style));
check(doc.appendTable(tbl));
check(doc.addParagraph("After",style));
check(doc.measureDocument(120,dims));
check(dims[0]==120);
check(doc.removeParagraph(0));
check(doc.exportPixels(120,96,-1,pixels));
check(pixels[0]==255);
doc.clear();
check(doc.tableCount()==0 && doc.paragraphCount()==0);
""",53,p)

    run("text", """
@import "KeyframeIO";
TextGrid panels;
TextTable tbl;
TextDocument doc;
TextStyle style;
TextContainer box;
TextParagraphStyle flow;
check(panels.rows()==0 && panels.columns()==0);
check(!panels.create(0,2));
check(!doc.appendGrid(panels));
check(panels.create(2,2));
check(panels.rows()==2 && panels.columns()==2);
check(panels.columnGap()==0 && panels.rowGap()==0);
check(panels.columnGap(10)==10 && panels.rowGap(8)==8);
check(panels.columnGap(-1)==-1 && panels.columnGap()==10);
check(panels.rowGap(513)==-1 && panels.rowGap()==8);
check(panels.setPanel(0,0,"A",style));
check(panels.setPanel(0,1,"B",style));
check(panels.panelText(0,0)=="A");
check(!panels.setPanel(2,0,"X",style));
check(panels.panelText(-1,0)=="");
0xff0000=>box.backgroundColor;
check(panels.setPanelContainer(0,0,box));
0x00ff00=>box.backgroundColor;
check(panels.setPanelContainer(0,1,box));
check(!panels.setPanelContainer(2,0,box));
1=>flow.alignment;
check(panels.setPanelLayout(0,1,flow));
check(doc.appendGrid(panels));
check(doc.gridCount()==1 && doc.tableCount()==0);
int dims[2];
check(doc.measureDocument(110,dims));
check(dims[0]==110 && dims[1]>8);
dims[1]=>int oldHeight;
int pixels[110*96*4];
check(doc.exportPixels(110,96,-1,pixels));
check(pixels[0]==255 && pixels[3]==255);
check(pixels[4*49]==255 && pixels[4*49+3]==255);
check(pixels[4*50+3]==0 && pixels[4*59+3]==0);
check(pixels[4*60+1]==255 && pixels[4*60+3]==255);
check(panels.rowGap(18)==18);
check(doc.measureDocument(110,dims));
check(dims[1]==oldHeight);
check(doc.setGrid(0,panels));
check(doc.measureDocument(110,dims));
check(dims[1]==oldHeight+10);
KeyframeCurve curve;
check(curve.add(0.0,1.0,"linear"));
check(curve.add(2.0,3.0,"linear"));
check(panels.columnWeight(0,curve.valueAt(1.0))==2.0);
check(doc.setGrid(0,panels));
check(doc.exportPixels(110,96,-1,pixels));
check(pixels[4*66]==255 && pixels[4*67+3]==0 && pixels[4*77+1]==255);
check(tbl.create(1,1));
check(doc.appendTable(tbl));
check(doc.gridCount()==1 && doc.tableCount()==1);
check(!doc.setGrid(1,panels));
check(!doc.removeTable(1));
check(doc.removeTable(0));
check(doc.gridCount()==1 && doc.tableCount()==0);
check(!doc.measureDocument(10,dims));
check(doc.removeGrid(0));
check(!doc.removeGrid(0));
check(doc.gridCount()==0);
check(doc.appendGrid(panels));
doc.clear();
check(doc.gridCount()==0 && doc.tableCount()==0);
""",52,p)

    run("text", """
@import "KeyframeIO";
TextDocument doc;
TextPage sheet;
TextStyle style;
TextContainer box;
TextParagraphStyle flow;
check(sheet.width()==640 && sheet.height()==480 && sheet.backgroundColor()==0xffffff);
check(sheet.marginLeft()==0 && sheet.marginBottom()==0);
check(sheet.width(0)==-1 && sheet.width()==640);
check(sheet.backgroundColor(-2)==-2 && sheet.backgroundColor()==0xffffff);
check(sheet.width(100)==100 && sheet.height(80)==80);
check(sheet.marginLeft(10)==10 && sheet.marginRight(10)==10);
check(sheet.marginTop(10)==10 && sheet.marginBottom(10)==10);
check(sheet.backgroundColor(-1)==-1);
check(doc.pageCount(sheet)==1);
int pixels[100*80*4];
check(doc.exportPage(sheet,0,pixels));
check(pixels[3]==0);
check(!doc.exportPage(sheet,1,pixels));
check(!doc.exportPage(sheet,-1,pixels));
check(doc.addParagraph("M",style));
check(doc.addParagraph("M",style));
check(doc.addParagraph("M",style));
int dims[2];
check(doc.measureParagraph(0,80,dims));
dims[1]=>int glyphHeight;
check(glyphHeight>0 && glyphHeight<40);
(60-glyphHeight)$float=>flow.marginBottom;
check(doc.setParagraphLayout(0,flow));
check(doc.setParagraphLayout(1,flow));
check(doc.setParagraphLayout(2,flow));
0xff0000=>box.backgroundColor;
check(doc.setParagraphContainer(0,box));
0x00ff00=>box.backgroundColor;
check(doc.setParagraphContainer(1,box));
0x0000ff=>box.backgroundColor;
check(doc.setParagraphContainer(2,box));
check(doc.pageCount(sheet)==3);
check(doc.exportPage(sheet,0,pixels));
(10*100+10)*4=>int corner;
check(pixels[corner]==255 && pixels[corner+3]==255 && pixels[3]==0);
check(doc.exportPage(sheet,1,pixels));
check(pixels[corner+1]==255 && pixels[corner]==0);
check(doc.exportPage(sheet,2,pixels));
check(pixels[corner+2]==255 && pixels[corner+1]==0);
check(doc.savePagePNG("text-page.png",sheet,2));
check(!doc.savePagePNG("text-page.png",sheet,2));
check(!doc.savePagePNG("text-invalid-page.png",sheet,3));
KeyframeCurve curve;
check(curve.add(0.0,80.0,"linear"));
check(curve.add(2.0,140.0,"linear"));
curve.valueAt(2.0)$int=>sheet.height;
check(doc.pageCount(sheet)==2);
50=>sheet.height;
check(doc.pageCount(sheet)==-1);
80=>sheet.height;
100=>sheet.marginLeft;
check(doc.pageCount(sheet)==-1);
123=>pixels[0];
check(!doc.exportPage(sheet,0,pixels));
check(pixels[0]==123);
10=>sheet.marginLeft;
doc.clear();
TextTable tbl;
TextGrid panels;
check(tbl.create(1,1));
check(panels.create(1,1));
check(doc.appendTable(tbl));
check(doc.appendGrid(panels));
check(doc.pageCount(sheet)>=1);
check(doc.exportPage(sheet,0,pixels));
check(doc.tableCount()==1 && doc.gridCount()==1);
doc.clear();
check(doc.pageCount(sheet)==1);
""",49,p)
    assert (p / "text-page.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
    assert not (p / "text-invalid-page.png").exists()

    run("text", """
TextNode root;
TextNode leaf;
TextNode saved;
TextNode group;
TextStyle style;
check(root.kind()==0 && root.childCount()==0);
check(root.depth()==1 && root.nodeCount()==1 && root.textBytes()==0);
check(root.text()=="");
check(leaf.setParagraph("Hello",style));
check(leaf.kind()==1 && leaf.text()=="Hello" && leaf.textBytes()==5);
check(!leaf.appendChild(root));
check(!root.appendChild(root));
check(root.appendChild(leaf));
check(root.childCount()==1 && root.depth()==2 && root.nodeCount()==2);
check(root.textBytes()==5);
check(leaf.setParagraph("World!",style));
check(root.getChild(0,saved));
check(saved.text()=="Hello");
check(!root.getChild(0,root));
check(!root.getChild(1,saved));
check(saved.text()=="Hello");
check(root.insertChild(0,leaf));
check(root.childCount()==2 && root.textBytes()==11);
check(root.getChild(0,saved));
check(saved.text()=="World!");
check(root.getChild(1,saved));
check(saved.text()=="Hello");
check(!root.insertChild(-1,leaf));
check(!root.insertChild(3,leaf));
check(!root.setChild(2,leaf));
check(root.setChild(1,leaf));
check(root.textBytes()==12);
check(group.appendChild(root));
check(group.depth()==3 && group.nodeCount()==4);
check(group.copyFrom(group));
check(saved.copyFrom(group));
check(saved.depth()==3 && saved.textBytes()==12);
check(root.removeChild(0));
check(root.childCount()==1 && root.textBytes()==6);
check(group.textBytes()==12 && saved.textBytes()==12);
check(!root.removeChild(2));
check(saved.clear());
check(saved.kind()==0 && saved.nodeCount()==1);
check(root.setParagraph("Reset",style));
check(root.kind()==1 && root.childCount()==0 && root.text()=="Reset");
check(root.clear());
check(root.kind()==0 && root.depth()==1);
TextNode chain;
TextNode wrapper;
0=>int successCount;
for(0=>int i;i<31;i++) {
    wrapper.clear();
    if(wrapper.appendChild(chain) && chain.copyFrom(wrapper)) successCount++;
}
check(successCount==31 && chain.depth()==32 && chain.nodeCount()==32);
wrapper.clear();
check(!wrapper.appendChild(chain));
check(wrapper.depth()==1 && wrapper.childCount()==0);
TextNode wide;
0=>int added;
for(0=>int i;i<4095;i++) if(wide.appendChild(leaf)) added++;
check(added==4095 && wide.nodeCount()==4096);
check(!wide.appendChild(leaf));
check(wide.nodeCount()==4096);
check(wide.setChild(0,leaf));
check(wide.nodeCount()==4096);
check(wide.removeChild(0));
check(wide.appendChild(leaf));
check(wide.nodeCount()==4096);
""",53,p)

    run("text", """
@import "KeyframeIO";
TextNode root;
TextNode child;
TextNode leaf;
TextNode saved;
TextContainer box;
TextContainer outBox;
TextParagraphStyle flow;
TextParagraphStyle outFlow;
TextStyle style;
TextStyle outStyle;
check(root.gap()==0.0);
check(root.gap(12.0)==12.0);
check(root.gap(-1.0)==-1.0 && root.gap()==12.0);
check(root.gap(513.0)==-1.0);
check(root.getContainer(outBox));
check(outBox.borderWidth()==0.0 && outBox.backgroundColor()==-1);
check(leaf.setParagraph("First",style));
check(root.appendChild(leaf));
8.0=>box.paddingLeft;
2.0=>box.borderWidth;
0xff0000=>box.backgroundColor;
check(root.setContainer(box));
check(root.kind()==2 && root.childCount()==1 && root.gap()==12.0);
check(root.getContainer(outBox));
check(outBox.paddingLeft()==8.0 && outBox.borderWidth()==2.0 && outBox.backgroundColor()==0xff0000);
0x00ff00=>box.backgroundColor;
check(root.getContainer(outBox));
check(outBox.backgroundColor()==0xff0000);
check(child.setContainer(box));
check(child.kind()==2);
check(child.appendChild(leaf));
check(root.appendChild(child));
check(root.depth()==3 && root.nodeCount()==4);
check(root.getChild(1,saved));
check(saved.kind()==2 && saved.childCount()==1);
check(saved.getContainer(outBox));
check(outBox.backgroundColor()==0x00ff00);
check(!root.appendChild(root));
check(!leaf.appendChild(child));
check(leaf.gap(4.0)==-1.0);
check(!root.setStyle(style));
check(!root.getStyle(outStyle));
check(leaf.setContainer(box));
check(leaf.kind()==1 && leaf.childCount()==0);
check(leaf.getContainer(outBox));
check(outBox.backgroundColor()==0x00ff00);
20.0=>style.fontSize;
check(leaf.setStyle(style));
check(leaf.getStyle(outStyle));
check(outStyle.fontSize()==20.0);
1=>flow.alignment;
5.0=>flow.marginTop;
check(leaf.setLayout(flow));
check(leaf.getLayout(outFlow));
check(outFlow.alignment()==1 && outFlow.marginTop()==5.0);
check(leaf.setText("Updated"));
check(leaf.text()=="Updated");
check(leaf.getContainer(outBox));
check(outBox.backgroundColor()==0x00ff00);
check(leaf.getLayout(outFlow));
check(outFlow.marginTop()==5.0);
check(!root.setText("Invalid"));
check(root.setChild(0,leaf));
check(root.textBytes()==12);
check(root.getChild(0,saved));
check(saved.text()=="Updated");
check(saved.getStyle(outStyle));
check(outStyle.fontSize()==20.0);
KeyframeCurve curve;
check(curve.add(0.0,0.0,"linear"));
check(curve.add(2.0,24.0,"linear"));
check(root.gap(curve.valueAt(1.0))==12.0);
check(saved.copyFrom(root));
check(root.gap(24.0)==24.0 && saved.gap()==12.0);
check(root.clear());
check(root.kind()==0 && root.gap()==0.0 && root.childCount()==0);
""",58,p)

    run("text", """
@import "KeyframeIO";
TextNode outer;
TextNode inner;
TextNode cell;
TextNode leaf;
TextNode saved;
TextNode root;
TextStyle style;
TextContainer box;
check(outer.rows()==0 && outer.columns()==0);
check(!outer.createTable(0,2));
check(!outer.createTable(256,32));
check(outer.createTable(2,2));
check(outer.kind()==3 && outer.rows()==2 && outer.columns()==2);
check(outer.childCount()==4 && outer.nodeCount()==5 && outer.depth()==2);
check(outer.textBytes()==0);
check(outer.columnWeight(0)==1.0 && outer.columnWeight(1)==1.0);
check(outer.columnWeight(2,1.0)==-1.0);
check(outer.columnWeight(0,0.0)==-1.0);
check(outer.getCellNode(0,0,cell));
check(cell.kind()==2 && cell.childCount()==0);
check(cell.getContainer(box));
check(box.borderWidth()==1.0 && box.paddingLeft()==4.0);
check(!outer.getCellNode(2,0,cell));
check(!outer.getCellNode(0,0,outer));
check(!outer.setCellNode(0,0,outer));
check(!outer.appendChild(cell));
check(!outer.insertChild(0,cell));
check(!outer.setChild(0,cell));
check(!outer.removeChild(0));
check(outer.gap(2.0)==-1.0);
check(leaf.setParagraph("A",style));
check(cell.appendChild(leaf));
check(leaf.setText("BB"));
check(cell.appendChild(leaf));
check(outer.setCellNode(0,0,cell));
check(outer.textBytes()==3 && outer.nodeCount()==7);
check(inner.createTable(1,2));
check(inner.setCellNode(0,0,leaf));
check(outer.setCellNode(1,1,inner));
check(outer.depth()==3 && outer.nodeCount()==9 && outer.textBytes()==5);
check(outer.getCellNode(1,1,saved));
check(saved.kind()==3 && saved.columns()==2 && saved.textBytes()==2);
check(saved.getCellNode(0,0,cell));
check(cell.text()=="BB");
check(inner.clear());
check(outer.getCellNode(1,1,saved));
check(saved.kind()==3);
check(root.appendChild(outer));
check(root.depth()==4 && root.nodeCount()==10);
KeyframeCurve curve;
check(curve.add(0.0,1.0,"linear"));
check(curve.add(2.0,3.0,"linear"));
check(outer.columnWeight(0,curve.valueAt(1.0))==2.0);
check(root.getChild(0,saved));
check(saved.columnWeight(0)==1.0);
check(root.setChild(0,outer));
check(root.getChild(0,saved));
check(saved.columnWeight(0)==2.0);
check(!outer.createTable(1024,1));
check(outer.kind()==3 && outer.columns()==2);
TextNode chain;
TextNode wrapNode;
for(0=>int i;i<31;i++) {
    wrapNode.clear();
    wrapNode.appendChild(chain);
    chain.copyFrom(wrapNode);
}
check(chain.depth()==32);
check(!outer.setCellNode(0,1,chain));
check(outer.nodeCount()==9);
check(outer.clear());
check(outer.kind()==0 && outer.rows()==0 && outer.childCount()==0);
""",56,p)

    run("text", """
@import "KeyframeIO";
TextNode root;
TextNode panels;
TextNode items;
TextNode nested;
TextNode leaf;
TextNode saved;
TextNode tbl;
TextStyle style;
TextList listStyle;
TextList outList;
check(panels.createGrid(2,2));
check(panels.kind()==4 && panels.rows()==2 && panels.columns()==2);
check(panels.nodeCount()==5 && panels.depth()==2);
check(panels.columnGap()==0 && panels.rowGap()==0);
check(panels.columnGap(12)==12 && panels.rowGap(8)==8);
check(panels.columnGap(-1)==-1 && panels.columnGap()==12);
check(panels.rowGap(513)==-1 && panels.rowGap()==8);
check(!panels.createGrid(256,32));
check(panels.kind()==4);
check(!panels.appendChild(leaf));
check(!panels.removeChild(0));
check(!panels.setChild(0,leaf));
check(!panels.setCellNode(0,0,panels));
check(panels.columnWeight(0,2.0)==2.0);
check(!items.createList(listStyle));
2=>listStyle.mode;
7=>listStyle.number;
check(items.createList(listStyle));
check(items.kind()==5 && items.childCount()==0);
check(items.itemLabel(0)=="");
check(leaf.setParagraph("One",style));
check(items.appendChild(leaf));
check(leaf.setText("Two"));
check(items.appendChild(leaf));
check(items.itemLabel(0)=="7." && items.itemLabel(1)=="8.");
check(items.itemLabel(-1)=="");
check(items.gap(4.0)==4.0);
check(items.getList(outList));
check(outList.mode()==2 && outList.number()==7);
1=>listStyle.mode;
check(nested.createList(listStyle));
check(nested.appendChild(leaf));
check(nested.itemLabel(0)=="•");
check(items.appendChild(nested));
check(items.depth()==3 && items.nodeCount()==5);
check(items.itemLabel(2)=="9.");
check(panels.setCellNode(0,0,items));
check(panels.nodeCount()==9 && panels.depth()==4);
check(panels.getCellNode(0,0,saved));
check(saved.kind()==5 && saved.itemLabel(2)=="9.");
check(items.removeChild(0));
check(items.itemLabel(0)=="7." && items.childCount()==2);
check(panels.getCellNode(0,0,saved));
check(saved.childCount()==3);
check(tbl.createTable(1,1));
check(tbl.setCellNode(0,0,panels));
check(tbl.depth()==5);
check(root.appendChild(tbl));
check(root.depth()==6);
check(tbl.columnGap(4)==-1);
check(tbl.columnGap()==-1);
check(leaf.setList(listStyle));
check(leaf.getList(outList));
check(outList.mode()==1);
0=>listStyle.mode;
check(!items.setList(listStyle));
check(leaf.setList(listStyle));
check(!panels.setList(listStyle));
KeyframeCurve curve;
check(curve.add(0.0,8.0,"linear"));
check(curve.add(2.0,24.0,"linear"));
check(panels.columnGap(curve.valueAt(1.0)$int)==16);
check(panels.clear());
check(panels.kind()==0 && panels.rows()==0 && panels.columnGap()==-1);
""",59,p)

    run("text", """
@import "KeyframeIO";
TextDocument doc;
TextNode root;
TextNode leaf;
TextNode saved;
TextNode panels;
TextNode items;
TextNode tbl;
TextStyle style;
TextContainer box;
TextList numbering;
TextPage sheet;
check(doc.nodeCount()==0);
check(!doc.getNode(0,saved));
check(leaf.setParagraph("M",style));
check(doc.addParagraph("M",style));
int dims[2];
check(doc.measureParagraph(0,120,dims));
dims[1]=>int glyphHeight;
doc.clear();
0xff0000=>box.backgroundColor;
check(root.setContainer(box));
check(root.appendChild(leaf));
check(root.appendChild(leaf));
check(root.gap(6.0)==6.0);
check(doc.appendNode(root));
check(doc.nodeCount()==1 && doc.paragraphCount()==0);
check(doc.measureDocument(120,dims));
check(dims[0]==120 && dims[1]==2*glyphHeight+6);
int pixels[120*120*4];
check(doc.exportPixels(120,120,-1,pixels));
check(pixels[0]==255 && pixels[3]==255);
check(pixels[(glyphHeight*120+119)*4]==255);
check(root.gap(12.0)==12.0);
check(doc.measureDocument(120,dims));
check(dims[1]==2*glyphHeight+6);
check(doc.setNode(0,root));
check(doc.measureDocument(120,dims));
check(dims[1]==2*glyphHeight+12);
check(doc.getNode(0,saved));
check(saved.gap()==12.0 && saved.childCount()==2);
0.0=>box.opacity;
check(root.setContainer(box));
check(doc.setNode(0,root));
check(doc.exportPixels(120,120,-1,pixels));
0=>int ink;
for(0=>int i;i<120*120;i++) if(pixels[4*i+3]>0) ink++;
check(ink==0);
check(doc.removeNode(0));
check(!doc.removeNode(0));
check(panels.createGrid(1,2));
check(panels.columnGap(10)==10);
1.0=>box.opacity;
0xff0000=>box.backgroundColor;
check(leaf.setContainer(box));
check(panels.setCellNode(0,0,leaf));
0x00ff00=>box.backgroundColor;
check(leaf.setContainer(box));
check(panels.setCellNode(0,1,leaf));
check(doc.appendNode(panels));
check(doc.exportPixels(120,120,-1,pixels));
check(pixels[4*54]==255 && pixels[4*55+3]==0 && pixels[4*64+3]==0 && pixels[4*65+1]==255);
check(tbl.createTable(1,1));
check(tbl.setCellNode(0,0,panels));
check(doc.setNode(0,tbl));
check(doc.exportPixels(120,120,-1,pixels));
check(pixels[4*65+1]==255);
2=>numbering.mode;
check(items.createList(numbering));
check(items.appendChild(leaf));
check(doc.setNode(0,items));
check(doc.exportPixels(120,120,-1,pixels));
0=>int markerInk;
for(0=>int i;i<120*120;i++) if(i%120<32 && pixels[i*4+3]>0) markerInk++;
check(markerInk>0);
check(pixels[4*119+1]==255);
120=>sheet.width; 120=>sheet.height;
-1=>sheet.backgroundColor;
check(doc.pageCount(sheet)==1);
check(doc.exportPage(sheet,0,pixels));
check(pixels[4*119+1]==255);
check(doc.savePagePNG("recursive-page.png",sheet,0));
check(!doc.measureDocument(20,dims));
123=>pixels[0];
check(!doc.exportPixels(20,120,-1,pixels));
check(pixels[0]==123);
doc.clear();
check(doc.nodeCount()==0);
check(doc.appendNode(panels));
TextTable legacy;
check(legacy.create(1,1));
check(doc.appendTable(legacy));
check(doc.appendNode(items));
check(doc.exportPixels(120,120,-1,pixels));
check(pixels[4*65+1]==255);
check(doc.pageCount(sheet)>=1);
check(doc.exportPage(sheet,0,pixels));
check(pixels[4*65+1]==255);
KeyframeCurve curve;
check(curve.add(0.0,10.0,"linear"));
check(curve.add(2.0,30.0,"linear"));
check(panels.columnGap(curve.valueAt(1.0)$int)==20);
check(doc.setNode(0,panels));
check(doc.exportPixels(120,120,-1,pixels));
check(pixels[4*50+3]==0 && pixels[4*69+3]==0 && pixels[4*70+1]==255);
doc.clear();
check(doc.nodeCount()==0 && doc.tableCount()==0);
""",74,p)
    assert (p / "recursive-page.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")

def text_recent():
    directory = tempfile.TemporaryDirectory()
    p = pathlib.Path(directory.name)
    run("text", """
TextDocument doc;
TextStyle style;
TextRun runItem;
TextParagraphStyle flow;
TextContainer box;
TextList numbering;
TextNode root;
TextNode leaf;
TextTable tbl;
TextGrid panels;
check(doc.saveNative("native-empty.cktxt"));
check(!doc.saveNative("native-empty.cktxt"));
check(!doc.saveNative("native-wrong.txt"));
check(!doc.saveNative(""));
check(doc.addParagraph("Native café",style));
check(runItem.set(" mixed",style));
check(doc.appendRun(0,runItem));
2=>flow.alignment;
check(doc.setParagraphLayout(0,flow));
0xff0000=>box.backgroundColor;
check(doc.setParagraphContainer(0,box));
2=>numbering.mode;
check(doc.setParagraphList(0,numbering));
check(root.createList(numbering));
check(leaf.setParagraph("Nested UTF8",style));
check(root.appendChild(leaf));
check(doc.appendNode(root));
check(tbl.create(1,1));
check(tbl.setCell(0,0,"Table cell",style));
check(doc.appendTable(tbl));
check(panels.create(1,1));
check(panels.setPanel(0,0,"Grid cell",style));
check(doc.appendGrid(panels));
check(doc.saveNative("native-full.cktxt"));
check(doc.saveNative("native-copy.cktxt"));
check(!doc.saveNative("native-full.cktxt"));
check(doc.paragraphCount()==1 && doc.nodeCount()==1 && doc.tableCount()==1 && doc.gridCount()==1);
check(doc.paragraphText(0)=="Native café mixed");
check(doc.removeNode(0));
check(doc.saveNative("native-changed.cktxt"));
doc.clear();
check(doc.saveNative("native-cleared.cktxt"));
""",28,p)
    import struct
    native_empty = (p / "native-empty.cktxt").read_bytes()
    native_full = (p / "native-full.cktxt").read_bytes()
    assert native_empty == b"CKTXTDOC" + struct.pack("<IIII", 1, 0, 0, 0)
    assert native_full[:24] == b"CKTXTDOC" + struct.pack("<IIII", 1, 1, 2, 1)
    assert native_full == (p / "native-copy.cktxt").read_bytes()
    assert native_empty == (p / "native-cleared.cktxt").read_bytes()
    assert native_full != (p / "native-changed.cktxt").read_bytes()
    assert "Native café mixed".encode("utf-8") in native_full
    assert b"Nested UTF8" in native_full and b"Table cell" in native_full and b"Grid cell" in native_full
    assert not (p / "native-wrong.txt").exists()

    # Invalid native fixtures use the writer's previous test output.
    native_good = (p / "native-full.cktxt").read_bytes()
    (p / "native-bad-magic.cktxt").write_bytes(b"BADMAGIC" + native_good[8:])
    (p / "native-bad-version.cktxt").write_bytes(native_good[:8] + struct.pack("<I", 2) + native_good[12:])
    (p / "native-truncated.cktxt").write_bytes(native_good[:-1])
    (p / "native-trailing.cktxt").write_bytes(native_good + b"x")
    (p / "native-bad-count.cktxt").write_bytes(native_good[:12] + struct.pack("<I", 0xffffffff) + native_good[16:])
    (p / "native-bad-string.cktxt").write_bytes(native_good[:24] + struct.pack("<I", 0xffffffff) + native_good[28:])
    run("text", """
TextDocument doc;
TextStyle style;
TextNode root;
TextContainer box;
TextParagraphStyle flow;
TextList numbering;
check(doc.addParagraph("Original",style));
check(!doc.loadNative("native-missing.cktxt"));
check(!doc.loadNative("native-wrong.txt"));
check(!doc.loadNative("native-bad-magic.cktxt"));
check(!doc.loadNative("native-bad-version.cktxt"));
check(!doc.loadNative("native-truncated.cktxt"));
check(!doc.loadNative("native-trailing.cktxt"));
check(!doc.loadNative("native-bad-count.cktxt"));
check(!doc.loadNative("native-bad-string.cktxt"));
check(doc.paragraphCount()==1 && doc.paragraphText(0)=="Original");
check(doc.loadNative("native-full.cktxt"));
check(doc.paragraphCount()==1 && doc.tableCount()==1 && doc.gridCount()==1 && doc.nodeCount()==1);
check(doc.paragraphText(0)=="Native café mixed");
check(doc.runCount(0)==1 && doc.runText(0,0)==" mixed");
check(doc.getParagraphLayout(0,flow));
check(flow.alignment()==2);
check(doc.getParagraphContainer(0,box));
check(box.backgroundColor()==0xff0000);
check(doc.getParagraphList(0,numbering));
check(numbering.mode()==2);
check(doc.getNode(0,root));
check(root.kind()==5 && root.childCount()==1);
check(root.itemLabel(0)=="1.");
check(doc.saveNative("native-roundtrip.cktxt"));
int dims[2];
check(doc.measureDocument(320,dims));
check(dims[0]==320 && dims[1]>0);
int pixels[320*240*4];
check(doc.exportPixels(320,240,-1,pixels));
check(doc.savePNG("native-restored.png",320,240,-1));
check(!doc.loadNative("native-truncated.cktxt"));
check(doc.nodeCount()==1 && doc.runCount(0)==1);
check(doc.saveNative("native-after-error.cktxt"));
check(doc.loadNative("native-empty.cktxt"));
check(doc.paragraphCount()==0 && doc.nodeCount()==0 && doc.tableCount()==0 && doc.gridCount()==0);
check(doc.saveNative("native-empty-roundtrip.cktxt"));
""",34,p)
    assert (p / "native-roundtrip.cktxt").read_bytes() == native_good
    assert (p / "native-after-error.cktxt").read_bytes() == native_good
    assert (p / "native-empty-roundtrip.cktxt").read_bytes() == (p / "native-empty.cktxt").read_bytes()

    # TXT import/export: exact UTF-8 bytes and transactional failures.
    (p / "plain-input.txt").write_bytes(b"\xef\xbb\xbf" + "café\r\n\rsecond\n".encode("utf-8"))
    (p / "plain-empty.txt").write_bytes(b"")
    (p / "plain-invalid.txt").write_bytes(b"\xff")
    (p / "plain-nul.txt").write_bytes(b"a\x00b")
    (p / "plain-long.txt").write_bytes(b"a" * 65537)
    (p / "plain-lines.txt").write_bytes(b"\n" * 4096)
    (p / "plain-budget.txt").write_bytes((b"a" * 65536 + b"\n") * 17)
    run("text", """
TextDocument doc;
TextStyle style;
TextTable table;
TextGrid grid;
TextNode root;
TextNode leaf;
TextList marker;
TextRun mixed;
check(doc.addParagraph("original",style));
check(!doc.loadTXT("plain-missing.txt"));
check(!doc.loadTXT("native-full.cktxt"));
check(!doc.loadTXT("plain-invalid.txt"));
check(!doc.loadTXT("plain-nul.txt"));
check(!doc.loadTXT("plain-long.txt"));
check(!doc.loadTXT("plain-lines.txt"));
check(!doc.loadTXT("plain-budget.txt"));
check(doc.paragraphCount()==1 && doc.paragraphText(0)=="original");
check(doc.loadTXT("plain-input.txt"));
check(doc.paragraphCount()==4);
check(doc.paragraphText(0)=="café");
check(doc.paragraphText(1)=="");
check(doc.paragraphText(2)=="second");
check(doc.paragraphText(3)=="");
check(doc.saveTXT("plain-output.txt"));
check(!doc.saveTXT("plain-output.txt"));
check(!doc.saveTXT("plain-wrong.cktxt"));
check(doc.loadTXT("plain-empty.txt"));
check(doc.paragraphCount()==0);
check(doc.saveTXT("plain-empty-out.txt"));
check(doc.addParagraph("first",style));
check(mixed.set(" mixed",style));
check(doc.appendRun(0,mixed));
check(table.create(1,2));
check(table.setCell(0,0,"left",style));
check(table.setCell(0,1,"right",style));
check(doc.appendTable(table));
check(grid.create(1,2));
check(grid.setPanel(0,0,"A",style));
check(grid.setPanel(0,1,"B",style));
check(doc.appendGrid(grid));
check(marker.mode(2)==2);
check(root.createList(marker));
check(leaf.setParagraph("nested",style));
check(root.appendChild(leaf));
check(doc.appendNode(root));
check(doc.addParagraph("last",style));
check(doc.saveTXT("plain-complex.txt"));
check(doc.loadTXT("plain-complex.txt"));
check(doc.tableCount()==0 && doc.gridCount()==0 && doc.nodeCount()==0);
check(doc.paragraphCount()==5);
check(doc.paragraphText(0)=="first mixed");
check(doc.paragraphText(3)=="1. nested");
check(doc.paragraphText(4)=="last");
check(doc.runCount(0)==0);
""",46,p)
    assert (p / "plain-output.txt").read_bytes() == "café\n\nsecond\n".encode("utf-8")
    assert (p / "plain-empty-out.txt").read_bytes() == b""
    assert (p / "plain-complex.txt").read_bytes() == b"first mixed\nleft\tright\nA\tB\n1. nested\nlast"
    assert not (p / "plain-wrong.cktxt").exists()

    # SVG uses the shared text renderer rather than embedding a PNG.
    run("text", """
TextDocument doc;
TextStyle style;
TextContainer box;
TextParagraphStyle flow;
TextNode root;
TextNode leaf;
TextTable table;
check(doc.saveSVG("text-svg-empty.svg",120,80,-1));
check(doc.saveSVG("text-svg-white.svg",120,80,0xffffff));
check(!doc.saveSVG("text-svg-empty.svg",120,80,0));
check(!doc.saveSVG("text-svg-wrong.png",120,80,0));
check(!doc.saveSVG("text-svg-zero.svg",0,80,0));
check(!doc.saveSVG("text-svg-large.svg",4096,4096,0));
check(!doc.saveSVG("text-svg-bg.svg",120,80,-2));
check(!doc.saveSVG("text-svg-color.svg",120,80,0x1000000));
check(style.color(0xff0000)==0xff0000);
check(doc.addParagraph("Vector café",style));
check(box.backgroundColor(0x00ff00)==0x00ff00);
check(doc.setParagraphContainer(0,box));
check(table.create(1,2));
check(table.setCell(0,0,"Left",style));
check(table.setCell(0,1,"Right",style));
check(doc.appendTable(table));
check(leaf.setParagraph("Nested",style));
check(root.appendChild(leaf));
check(doc.appendNode(root));
check(doc.saveSVG("text-svg-rich.svg",240,160,-1));
check(doc.savePNG("text-svg-reference.png",240,160,-1));
check(doc.paragraphCount()==1 && doc.tableCount()==1 && doc.nodeCount()==1);
check(flow.marginLeft(200)==200);
check(doc.setParagraphLayout(0,flow));
check(!doc.saveSVG("text-svg-invalid.svg",120,80,-1));
check(flow.marginLeft(0)==0);
check(doc.setParagraphLayout(0,flow));
check(doc.saveSVG("text-svg-invalid.svg",240,160,-1));
""",28,p)
    import xml.etree.ElementTree as ET
    svg_ns = "{http://www.w3.org/2000/svg}"
    for name, w, h in (("text-svg-empty.svg",120,80), ("text-svg-white.svg",120,80), ("text-svg-rich.svg",240,160), ("text-svg-invalid.svg",240,160)):
        svg = ET.parse(p / name).getroot()
        assert svg.tag == svg_ns + "svg"
        assert tuple(map(float, svg.attrib["viewBox"].split())) == (0,0,w,h)
        assert not list(svg.iter(svg_ns + "image")), name
    rich = ET.parse(p / "text-svg-rich.svg").getroot()
    assert list(rich.iter(svg_ns + "path"))
    assert not list(ET.parse(p / "text-svg-empty.svg").getroot().iter(svg_ns + "path"))
    for name in ("text-svg-wrong.png","text-svg-zero.svg","text-svg-large.svg","text-svg-bg.svg","text-svg-color.svg"):
        assert not (p / name).exists(), name

    # PDF pages are emitted by Cairo, not assembled from PNG snapshots.
    run("text", """
TextDocument doc;
TextPage page;
TextStyle style;
TextParagraphStyle flow;
TextNode root;
TextNode leaf;
TextTable table;
check(page.width(240)==240);
check(page.height(120)==120);
check(page.marginLeft(10)==10);
check(page.marginRight(10)==10);
check(page.marginTop(10)==10);
check(page.marginBottom(10)==10);
check(doc.pageCount(page)==1);
check(doc.savePDF("text-pdf-empty.pdf",page));
check(!doc.savePDF("text-pdf-empty.pdf",page));
check(!doc.savePDF("text-pdf-wrong.svg",page));
check(page.backgroundColor(-1)==-1);
check(doc.savePDF("text-pdf-transparent.pdf",page));
check(style.fontSize(16)==16);
check(doc.addParagraph("First café",style));
check(flow.marginBottom(75)==75);
check(doc.setParagraphLayout(0,flow));
check(doc.addParagraph("Second page",style));
check(doc.setParagraphLayout(1,flow));
check(doc.addParagraph("Third page",style));
check(doc.setParagraphLayout(2,flow));
check(doc.pageCount(page)==3);
check(doc.savePDF("text-pdf-three.pdf",page));
check(doc.paragraphCount()==3);
check(page.marginTop(110)==110);
check(doc.pageCount(page)==-1);
check(!doc.savePDF("text-pdf-invalid.pdf",page));
check(page.marginTop(10)==10);
check(page.height(40)==40);
check(doc.pageCount(page)==-1);
check(!doc.savePDF("text-pdf-oversize.pdf",page));
check(page.height(120)==120);
check(doc.savePDF("text-pdf-invalid.pdf",page));
doc.clear();
check(table.create(1,2));
check(table.setCell(0,0,"Left",style));
check(table.setCell(0,1,"Right",style));
check(doc.appendTable(table));
check(leaf.setParagraph("Nested",style));
check(root.appendChild(leaf));
check(doc.appendNode(root));
check(doc.pageCount(page)==1);
check(doc.savePDF("text-pdf-rich.pdf",page));
""",41,p)
    import re
    for name, pages in (("text-pdf-empty.pdf",1), ("text-pdf-transparent.pdf",1), ("text-pdf-three.pdf",3), ("text-pdf-invalid.pdf",3), ("text-pdf-rich.pdf",1)):
        pdf = (p / name).read_bytes()
        assert pdf.startswith(b"%PDF-") and pdf.rstrip().endswith(b"%%EOF"), name
        # Cairo 1.18+ may store page dictionaries in compressed PDF object streams.
        import zlib
        chunks = [pdf]
        for stream in re.finditer(rb"(?m)^\d+\s+\d+\s+obj\s*\r?\n<<\s*/Type\s*/ObjStm\b([^>]*?)>>\s*\r?\nstream\r?\n", pdf):
            assert re.search(rb"/Filter\s*/FlateDecode\b", stream.group(1)), name
            chunks.append(zlib.decompress(pdf[stream.end():]))
        objects = b"\n".join(chunks)
        assert len(re.findall(rb"/Type\s*/Page\b",objects)) == pages, name
        assert len(re.findall(rb"/MediaBox\s*\[\s*0\s+0\s+180\s+90\s*\]",objects)) == pages, name
    assert not (p / "text-pdf-wrong.svg").exists()
    assert not (p / "text-pdf-oversize.pdf").exists()


def text_page_breaks():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("text", """
TextDocument doc;
TextPage page;
TextStyle style;
TextTable table;
TextNode leaf;
check(page.width(240)==240);
check(page.height(240)==240);
check(page.breakCount()==0);
check(page.breakAt(0)==-1);
check(!page.addBreak(0));
check(!page.addBreak(-1));
check(!page.addBreak(4608));
check(doc.addParagraph("First",style));
check(table.create(1,1));
check(table.setCell(0,0,"Table",style));
check(doc.appendTable(table));
check(leaf.setParagraph("Node",style));
check(doc.appendNode(leaf));
check(doc.addParagraph("Last",style));
check(doc.pageCount(page)==1);
check(page.addBreak(3));
check(page.addBreak(1));
check(!page.addBreak(1));
check(page.breakCount()==2);
check(page.breakAt(0)==1 && page.breakAt(1)==3);
check(page.breakAt(-1)==-1 && page.breakAt(2)==-1);
check(doc.pageCount(page)==3);
check(doc.savePDF("explicit.pdf",page));
check(doc.savePagePNG("explicit-page.png",page,2));
check(!doc.savePagePNG("explicit-bad.png",page,3));
check(!page.removeBreak(2));
check(page.removeBreak(1));
check(doc.pageCount(page)==2);
check(page.addBreak(4));
check(doc.pageCount(page)==-1);
check(!doc.savePDF("invalid-break.pdf",page));
check(page.removeBreak(4));
check(doc.pageCount(page)==2);
page.clearBreaks();
check(page.breakCount()==0);
check(doc.pageCount(page)==1);
check(doc.paragraphCount()==2 && doc.tableCount()==1 && doc.nodeCount()==1);
doc.clear();
check(page.addBreak(1));
check(doc.pageCount(page)==-1);
page.clearBreaks();
check(doc.pageCount(page)==1);
""",39,p)
        assert (p / "explicit.pdf").read_bytes().startswith(b"%PDF-")
        assert (p / "explicit-page.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
        assert not (p / "explicit-bad.png").exists()
        assert not (p / "invalid-break.pdf").exists()



def text_page_furniture():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("text", """
TextDocument doc;
TextPage page;
TextStyle style;
TextParagraphStyle flow;
check(page.width(160)==160);
check(page.height(120)==120);
check(page.headerHeight()==0 && page.footerHeight()==0);
check(page.backgroundColor(-1)==-1);
check(!page.setHeader("Header",style,0));
check(!page.setFooter("Footer",style,2049));
check(style.color(0xff0000)==0xff0000);
check(page.setHeader("Header",style,30));
check(style.color(0x0000ff)==0x0000ff);
check(page.setFooter("Footer",style,30));
check(page.headerHeight()==30 && page.footerHeight()==30);
check(style.color(0)==0);
check(doc.addParagraph("First",style));
check(doc.addParagraph("Second",style));
check(flow.marginBottom(25)==25);
check(doc.setParagraphLayout(0,flow));
check(doc.setParagraphLayout(1,flow));
check(doc.pageCount(page)==2);
int pixels[160*120*4];
for(0=>int j;j<2;j++) {
    check(doc.exportPage(page,j,pixels));
    0=>int redInk; 0=>int blueInk;
    for(0=>int i;i<160*120;i++) {
        if(pixels[4*i+3]>0 && i/160<30 && pixels[4*i]>200 && pixels[4*i+2]<20) redInk++;
        if(pixels[4*i+3]>0 && i/160>=90 && pixels[4*i+2]>200 && pixels[4*i]<20) blueInk++;
    }
    check(redInk>0 && blueInk>0);
}
check(doc.savePDF("bands.pdf",page));
check(doc.savePagePNG("bands.png",page,1));
check(!page.setHeader("Bad",style,-1));
check(page.headerHeight()==30);
check(page.setHeader("Too short",style,1));
check(doc.pageCount(page)==-1);
check(!doc.savePDF("bad-bands.pdf",page));
check(page.setHeader("Header",style,100));
check(doc.pageCount(page)==-1);
page.clearHeader();
page.clearFooter();
check(page.headerHeight()==0 && page.footerHeight()==0);
check(doc.pageCount(page)==1);
doc.clear();
check(page.setFooter("Footer",style,30));
check(doc.pageCount(page)==1);
check(doc.exportPage(page,0,pixels));
""",36,p)
        assert (p / "bands.pdf").read_bytes().startswith(b"%PDF-")
        assert (p / "bands.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
        assert not (p / "bad-bands.pdf").exists()



def text_page_numbers():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("text", """
TextDocument doc;
TextPage page;
TextPage reference;
TextStyle style;
check(page.width(240)==240);
check(page.height(120)==120);
check(reference.width(240)==240);
check(reference.height(120)==120);
check(page.numbering()==0 && page.firstNumber()==1);
check(page.numbering(2)==-1);
check(page.firstNumber(0)==-1);
check(page.firstNumber(1000000)==-1);
check(page.setFooter("Page {page} / {pages}",style,30));
check(reference.setFooter("Page {page} / {pages}",style,30));
check(doc.addParagraph("Same body",style));
check(doc.addParagraph("Same body",style));
check(page.addBreak(1));
check(reference.addBreak(1));
check(doc.pageCount(page)==2);
int a[240*120*4];
int b[240*120*4];
check(doc.exportPage(page,0,a));
check(doc.exportPage(reference,0,b));
0=>int difference;
for(0=>int i;i<a.size();i++) if(a[i]!=b[i]) difference++;
check(difference==0);
check(page.numbering(1)==1);
check(page.firstNumber(7)==7);
check(doc.pageCount(page)==2);
check(reference.setFooter("Page 7 / 2",style,30));
check(doc.exportPage(page,0,a));
check(doc.exportPage(reference,0,b));
0=>difference;
for(0=>int i;i<a.size();i++) if(a[i]!=b[i]) difference++;
check(difference==0);
check(reference.setFooter("Page 8 / 2",style,30));
check(doc.exportPage(page,1,a));
check(doc.exportPage(reference,1,b));
0=>difference;
for(0=>int i;i<a.size();i++) if(a[i]!=b[i]) difference++;
check(difference==0);
check(doc.savePDF("numbered.pdf",page));
check(doc.savePagePNG("numbered.png",page,1));
check(page.setFooter("{{page}} {unknown} }} {page}",style,30));
check(reference.setFooter("{page} {unknown} } 7",style,30));
check(doc.exportPage(page,0,a));
check(doc.exportPage(reference,0,b));
0=>difference;
for(0=>int i;i<a.size();i++) if(a[i]!=b[i]) difference++;
check(difference==0);
check(page.setHeader("{pages}",style,1));
check(doc.pageCount(page)==-1);
check(!doc.savePDF("invalid-numbered.pdf",page));
page.clearHeader();
// The unexpanded escape template can be wider than its rendered text.
// Restore a short template before checking the disabled numbering mode.
check(page.setFooter("Page {page}",style,30));
check(page.numbering(0)==0);
check(doc.pageCount(page)==2);
""",42,p)
        assert (p / "numbered.pdf").read_bytes().startswith(b"%PDF-")
        assert (p / "numbered.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
        assert not (p / "invalid-numbered.pdf").exists()



def text_split_paragraphs():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("text", """
TextDocument doc;
TextPage page;
TextStyle style;
TextRun mixed;
check(page.width(160)==160);
check(page.height(48)==48);
check(page.splitParagraphs()==0);
check(page.splitParagraphs(2)==-1);
check(style.fontSize(16)==16);
check(doc.addParagraph("One\\nTwo\\nThree\\nFour\\nFive\\nSix",style));
check(style.color(0xff0000)==0xff0000);
check(mixed.set("\\nSeven\\nEight",style));
check(doc.appendRun(0,mixed));
check(doc.pageCount(page)==-1);
check(page.splitParagraphs(1)==1);
doc.pageCount(page)=>int count;
check(count>=3 && count<=8);
int pixels[160*48*4];
check(doc.exportPage(page,0,pixels));
check(doc.exportPage(page,count-1,pixels));
check(!doc.exportPage(page,count,pixels));
0=>int redInk;
for(0=>int i;i<160*48;i++) {
    if(pixels[i*4]>200 && pixels[i*4+1]<100 && pixels[i*4+2]<100) redInk++;
}
check(redInk>0);
check(doc.savePDF("split.pdf",page));
check(doc.savePagePNG("split-last.png",page,count-1));
check(doc.paragraphCount()==1);
check(doc.paragraphText(0)=="One\\nTwo\\nThree\\nFour\\nFive\\nSix\\nSeven\\nEight");
check(doc.addParagraph("Next",style));
check(page.addBreak(1));
check(doc.pageCount(page)==count+1);
check(page.removeBreak(1));
check(doc.pageCount(page)>=count);
check(page.height(1)==1);
check(doc.pageCount(page)==-1);
check(!doc.savePDF("bad-split.pdf",page));
check(page.height(48)==48);
check(page.splitParagraphs(0)==0);
check(doc.pageCount(page)==-1);
""",31,p)
        assert (p / "split.pdf").read_bytes().startswith(b"%PDF-")
        assert (p / "split-last.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
        assert not (p / "bad-split.pdf").exists()



def text_split_tables():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("text", """
TextDocument doc;
TextPage page;
TextStyle style;
TextTable table;
TextContainer box;
check(page.width(160)==160);
check(page.height(48)==48);
check(page.splitTables()==0);
check(page.splitTables(2)==-1);
check(style.fontSize(16)==16);
check(table.create(6,1));
for(0=>int i;i<6;i++) {
    check(table.setCell(i,0,"Row "+Std.itoa(i),style));
    if(i<3) 0xff0000=>box.backgroundColor;
    else 0x0000ff=>box.backgroundColor;
    check(table.setCellContainer(i,0,box));
}
check(doc.appendTable(table));
check(doc.pageCount(page)==-1);
check(page.splitTables(1)==1);
doc.pageCount(page)=>int count;
check(count>=3 && count<=6);
int pixels[160*48*4];
check(doc.exportPage(page,0,pixels));
check(pixels[0]==255 && pixels[2]==0);
check(doc.exportPage(page,count-1,pixels));
check(pixels[0]==0 && pixels[2]==255);
check(!doc.exportPage(page,count,pixels));
check(doc.savePDF("table-split.pdf",page));
check(doc.savePagePNG("table-last.png",page,count-1));
check(table.rows()==6 && doc.tableCount()==1);
check(doc.addParagraph("After table",style));
check(page.addBreak(1));
check(doc.pageCount(page)==count+1);
check(page.removeBreak(1));
check(page.height(1)==1);
check(doc.pageCount(page)==-1);
check(!doc.savePDF("bad-table.pdf",page));
check(page.height(48)==48);
check(page.splitTables(0)==0);
check(doc.pageCount(page)==-1);
doc.clear();
TextGrid grid;
check(grid.create(6,1));
for(0=>int i;i<6;i++) check(grid.setPanel(i,0,"Panel",style));
check(doc.appendGrid(grid));
check(page.splitTables(1)==1);
check(doc.pageCount(page)==-1);
""",50,p)
        assert (p / "table-split.pdf").read_bytes().startswith(b"%PDF-")
        assert (p / "table-last.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
        assert not (p / "bad-table.pdf").exists()



def text_frame_foundation():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("text", """
TextFrame frame;
TextFrame other;
TextContainer box;
TextContainer copy;
int dims[2];
int wrong[1];
check(frame.x()==0 && frame.y()==0);
check(frame.width()==320 && frame.height()==240);
check(frame.page()==0);
check(frame.contentSize(dims));
check(dims[0]==320 && dims[1]==240);
check(frame.setGeometry(10,20,100,80));
check(frame.x()==10 && frame.y()==20 && frame.width()==100 && frame.height()==80);
check(!frame.setGeometry(1,2,0,40));
check(frame.x()==10 && frame.y()==20 && frame.width()==100 && frame.height()==80);
check(frame.x(-1)==-1);
check(frame.y(4096)==-1);
check(frame.width(4097)==-1);
check(frame.height(0)==-1);
check(frame.page(-1)==-1);
check(frame.page(4096)==-1);
check(frame.page(3)==3);
check(frame.x(15)==15);
check(frame.y(25)==25);
check(frame.width(120)==120);
check(frame.height(90)==90);
check(other.width()==320 && other.page()==0);
5=>box.paddingLeft;
7=>box.paddingRight;
3=>box.paddingTop;
4=>box.paddingBottom;
2=>box.borderWidth;
0xff0000=>box.backgroundColor;
check(frame.setContainer(box));
check(frame.contentSize(dims));
check(dims[0]==104 && dims[1]==79);
20=>box.paddingLeft;
check(frame.getContainer(copy));
check(copy.paddingLeft()==5 && copy.backgroundColor()==0xff0000);
check(frame.contentSize(dims) && dims[0]==104);
check(!frame.contentSize(wrong));
check(frame.width(1)==1);
check(!frame.contentSize(dims));
check(dims[0]==104 && dims[1]==79);
frame.reset();
check(frame.x()==0 && frame.y()==0 && frame.page()==0);
check(frame.contentSize(dims) && dims[0]==320 && dims[1]==240);
check(frame.getContainer(copy));
check(copy.paddingLeft()==0 && copy.borderWidth()==0);
""",35,p)



def text_frame_flow():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("text", """
TextFlow flow;
TextFrame frame;
TextFrame copy;
TextStyle style;
check(flow.frameCount()==0);
check(flow.ready()==0);
check(flow.frameText(0)=="");
check(style.fontSize(16)==16);
check(frame.setGeometry(0,0,200,25));
check(flow.appendFrame(frame));
check(frame.page(1)==1);
check(flow.appendFrame(frame));
check(flow.frameCount()==2);
check(flow.getFrame(0,copy));
check(copy.page()==0);
check(flow.getFrame(1,copy));
check(copy.page()==1);
check(!flow.getFrame(2,copy));
check(flow.setText("First\\nSecond",style));
check(flow.layout());
check(flow.ready()==1);
check(flow.frameText(0)=="First\\n");
check(flow.frameText(1)=="Second");
check(flow.frameText(0)+flow.frameText(1)=="First\\nSecond");
check(flow.frameText(-1)=="");
check(frame.height(100)==100);
check(flow.setFrame(0,frame));
check(flow.ready()==0);
check(flow.frameText(0)=="");
check(flow.layout());
check(flow.frameText(0)=="First\\nSecond");
check(flow.frameText(1)=="");
check(!flow.setFrame(2,frame));
check(flow.ready()==1);
check(flow.setText("café",style));
check(flow.ready()==0);
check(flow.layout());
check(flow.frameText(0)=="café");
check(flow.removeFrame(1));
check(flow.frameCount()==1);
check(!flow.removeFrame(1));
check(flow.ready()==0);
check(flow.layout());
flow.clearFrames();
check(flow.frameCount()==0);
check(flow.ready()==0);
check(flow.layout());
check(flow.ready()==1);
""",43,p)



def text_frame_mixed():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("text", """
TextFlow flow;
TextFrame frame;
TextStyle base;
TextStyle large;
TextRun run;
check(flow.overflowBytes()==-1);
check(flow.overflowText()=="");
check(base.fontSize(16)==16);
check(large.fontSize(64)==64);
check(flow.setText("small\\n",base));
check(run.set("café",large));
check(flow.appendRun(run));
check(run.setText("changed"));
check(frame.setGeometry(0,0,200,25));
check(flow.appendFrame(frame));
check(flow.layout());
check(flow.ready()==1);
check(flow.frameText(0)=="small\\n");
check(flow.overflowText()=="café");
check(flow.overflowBytes()==5);
check(frame.setGeometry(0,0,320,150));
check(flow.appendFrame(frame));
check(flow.overflowBytes()==-1);
check(flow.overflowText()=="");
check(flow.layout());
check(flow.frameText(1)=="café");
check(flow.overflowBytes()==0);
check(flow.overflowText()=="");
check(flow.setText("",base));
check(flow.overflowBytes()==-1);
check(run.set("MMMMMMMMMMMMMMMMMMMMMMMM",large));
check(flow.appendRun(run));
check(frame.setGeometry(0,0,100,100));
check(flow.setFrame(0,frame));
check(frame.setGeometry(0,0,2000,300));
check(flow.setFrame(1,frame));
check(flow.layout());
check(flow.frameText(0)!="");
check(flow.frameText(1)!="");
check(flow.frameText(0)+flow.frameText(1)+flow.overflowText()=="MMMMMMMMMMMMMMMMMMMMMMMM");
check(flow.overflowBytes()==0);
flow.clearFrames();
check(flow.layout());
check(flow.overflowBytes()==24);
check(flow.overflowText()=="MMMMMMMMMMMMMMMMMMMMMMMM");
check(flow.setText("reset",base));
check(flow.layout());
check(flow.overflowBytes()==5);
check(flow.overflowText()=="reset");
check(frame.setGeometry(0,0,200,1));
check(flow.appendFrame(frame));
check(flow.layout());
check(flow.frameText(0)=="");
check(flow.overflowText()=="reset");
""",48,p)


def text_frame_render():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("text", """
TextFlow flow;
TextFrame frame;
TextContainer box;
TextStyle style;
TextStyle accent;
TextRun run;
int pixels[160*100*4];
int wrong[1];
check(flow.pageCount()==-1);
check(!flow.exportPage(0,160,100,-1,pixels));
check(flow.setText("A\\n",style));
check(accent.fontSize(20)==20);
check(run.set("B",accent));
check(flow.appendRun(run));
check(frame.setGeometry(10,10,60,25));
0xff0000=>box.backgroundColor;
check(frame.setContainer(box));
check(flow.appendFrame(frame));
check(frame.setGeometry(80,20,60,60));
check(frame.page(2)==2);
0x0000ff=>box.backgroundColor;
check(frame.setContainer(box));
check(flow.appendFrame(frame));
check(flow.layout());
check(flow.pageCount()==3);
check(flow.frameText(0)=="A\\n");
check(flow.frameText(1)=="B");
check(flow.exportPage(0,160,100,-1,pixels));
check(pixels[3]==0);
check(pixels[(11*160+11)*4]==255);
check(pixels[(11*160+11)*4+3]==255);
check(pixels[(21*160+81)*4+3]==0);
check(flow.exportPage(1,160,100,0xffffff,pixels));
check(pixels[0]==255 && pixels[1]==255 && pixels[2]==255 && pixels[3]==255);
check(flow.exportPage(2,160,100,-1,pixels));
check(pixels[(21*160+81)*4+2]==255);
check(pixels[(11*160+11)*4+3]==0);
check(!flow.exportPage(3,160,100,-1,pixels));
check(!flow.exportPage(-1,160,100,-1,pixels));
check(!flow.exportPage(0,160,100,-2,pixels));
check(!flow.exportPage(0,160,100,-1,wrong));
check(!flow.savePagePNG("bad.png",0,100,100,-1));
check(flow.savePagePNG("flow.png",2,160,100,-1));
check(!flow.savePagePNG("flow.png",2,160,100,-1));
check(!flow.savePagePNG("flow.jpg",2,160,100,-1));
check(flow.setText("reset",style));
check(flow.pageCount()==-1);
check(!flow.exportPage(0,160,100,-1,pixels));
flow.clearFrames();
check(flow.layout());
check(flow.pageCount()==0);
check(!flow.exportPage(0,160,100,-1,pixels));
""",41,p)
        assert (p / "flow.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
        assert not (p / "bad.png").exists()


def text_frame_native():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("text", """
TextFlow flow;
TextFlow restored;
TextFrame frame;
TextFrame copy;
TextContainer box;
TextContainer decoration;
TextStyle style;
TextStyle large;
TextRun run;
check(style.fontSize(16)==16);
check(large.fontSize(64)==64);
check(flow.setText("small\\n",style));
check(run.set("café",large));
check(flow.appendRun(run));
check(frame.setGeometry(10,20,200,25));
0xff0000=>box.backgroundColor;
2=>box.paddingLeft;
check(frame.setContainer(box));
check(flow.appendFrame(frame));
check(frame.setGeometry(30,40,300,150));
check(frame.page(2)==2);
check(flow.appendFrame(frame));
check(flow.layout());
check(flow.frameText(0)=="small\\n");
check(flow.frameText(1)=="café");
check(flow.saveNative("flow.ckflow"));
check(!flow.saveNative("flow.ckflow"));
check(!flow.saveNative("flow.cktxt"));
check(restored.setText("old",style));
check(restored.loadNative("flow.ckflow"));
check(restored.ready()==0);
check(restored.pageCount()==-1);
check(restored.frameCount()==2);
check(restored.getFrame(0,copy));
check(copy.x()==10 && copy.y()==20 && copy.width()==200 && copy.height()==25);
check(copy.page()==0);
check(copy.getContainer(decoration));
check(decoration.backgroundColor()==0xff0000 && decoration.paddingLeft()==2);
check(restored.getFrame(1,copy));
check(copy.page()==2 && copy.width()==300 && copy.height()==150);
check(restored.layout());
check(restored.frameText(0)==flow.frameText(0));
check(restored.frameText(1)==flow.frameText(1));
check(restored.overflowBytes()==0);
check(restored.pageCount()==3);
check(restored.saveNative("roundtrip.ckflow"));
check(!restored.loadNative("missing.ckflow"));
check(!restored.loadNative("flow.cktxt"));
check(restored.ready()==1);
check(restored.frameText(1)=="café");
TextFlow empty;
check(empty.saveNative("empty.ckflow"));
check(restored.loadNative("empty.ckflow"));
check(restored.frameCount()==0);
check(restored.layout());
check(restored.overflowBytes()==0);
""",44,p)
        data = (p / "flow.ckflow").read_bytes()
        assert data.startswith(b"CKTXFLOW\x01\x00\x00\x00")
        assert data == (p / "roundtrip.ckflow").read_bytes()
        (p / "bad-magic.ckflow").write_bytes(b"X" + data[1:])
        (p / "bad-version.ckflow").write_bytes(data[:8] + b"\x02" + data[9:])
        (p / "truncated.ckflow").write_bytes(data[:-1])
        (p / "trailing.ckflow").write_bytes(data + b"x")
        # Final frame occupies 76 bytes: width begins eight bytes into it.
        offset = len(data) - 76 + 8
        (p / "bad-geometry.ckflow").write_bytes(data[:offset] + b"\x00"*4 + data[offset+4:])
        run("text", """
TextFlow flow;
TextStyle style;
TextFrame frame;
check(flow.setText("preserve",style));
check(flow.appendFrame(frame));
check(flow.layout());
check(!flow.loadNative("bad-magic.ckflow"));
check(!flow.loadNative("bad-version.ckflow"));
check(!flow.loadNative("truncated.ckflow"));
check(!flow.loadNative("trailing.ckflow"));
check(!flow.loadNative("bad-geometry.ckflow"));
check(flow.ready()==1);
check(flow.frameCount()==1);
check(flow.frameText(0)=="preserve");
""",11,p)


def text_accessibility():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("text", """
TextDocument doc;
TextStyle style;
TextRun mixed;
TextTable table;
TextGrid grid;
TextNode root;
TextNode leaf;
TextList marker;
check(doc.plainText()=="");
check(doc.addParagraph("first",style));
check(mixed.set(" café",style));
check(doc.appendRun(0,mixed));
check(table.create(1,2));
check(table.setCell(0,0,"left",style));
check(table.setCell(0,1,"right",style));
check(doc.appendTable(table));
check(grid.create(1,2));
check(grid.setPanel(0,0,"A",style));
check(grid.setPanel(0,1,"B",style));
check(doc.appendGrid(grid));
check(marker.mode(2)==2);
check(root.createList(marker));
check(leaf.setParagraph("nested",style));
check(root.appendChild(leaf));
check(doc.appendNode(root));
check(doc.addParagraph("last",style));
check(doc.plainText()=="first café\\nleft\\tright\\nA\\tB\\n1. nested\\nlast");
check(doc.saveTXT("reading.txt"));
check(doc.removeNode(0));
check(doc.plainText()=="first café\\nleft\\tright\\nA\\tB\\nlast");
doc.clear();
check(doc.plainText()=="");
TextFlow flow;
TextFrame frame;
check(flow.sourceText()=="");
check(flow.frameStartByte(0)==-1);
check(flow.frameEndByte(0)==-1);
check(style.fontSize(16)==16);
check(flow.setText("café\\nsecond",style));
check(flow.sourceText()=="café\\nsecond");
check(frame.setGeometry(0,0,200,25));
check(flow.appendFrame(frame));
check(frame.height(1)==1);
check(flow.appendFrame(frame));
check(frame.height(100)==100);
check(flow.appendFrame(frame));
check(flow.layout());
check(flow.frameText(0)=="café\\n");
check(flow.frameStartByte(0)==0 && flow.frameEndByte(0)==6);
check(flow.frameText(1)=="");
check(flow.frameStartByte(1)==6 && flow.frameEndByte(1)==6);
check(flow.frameText(2)=="second");
check(flow.frameStartByte(2)==6 && flow.frameEndByte(2)==12);
check(flow.frameStartByte(-1)==-1 && flow.frameEndByte(3)==-1);
check(flow.overflowBytes()==0);
check(mixed.set("!",style));
check(flow.appendRun(mixed));
check(flow.sourceText()=="café\\nsecond!");
check(flow.frameStartByte(0)==-1 && flow.frameEndByte(0)==-1);
check(flow.layout());
check(flow.frameEndByte(2)==13);
flow.clearFrames();
check(flow.sourceText()=="café\\nsecond!");
check(flow.frameEndByte(0)==-1);
check(flow.layout());
check(flow.overflowBytes()==13);
""",54,p)
        assert (p / "reading.txt").read_bytes() == "first café\nleft\tright\nA\tB\n1. nested\nlast".encode("utf-8")


def text_flow_txt():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        (p / "input.txt").write_bytes(b"\xef\xbb\xbf" + "café\r\n\rsecond\n".encode("utf-8"))
        (p / "invalid.txt").write_bytes(b"\xff")
        (p / "embedded-nul.txt").write_bytes(b"a\x00b")
        (p / "long.txt").write_bytes(b"a" * 65537)
        (p / "oversize.txt").write_bytes(b"a" * 131076)
        (p / "empty.txt").write_bytes(b"")
        (p / "boundary.txt").write_bytes(b"a" * 65536)
        (p / "crlf-boundary.txt").write_bytes(b"\xef\xbb\xbf" + b"\r\n" * 65536)
        run("text", """
TextFlow flow;
TextStyle style;
TextRun run;
TextFrame frame;
TextFrame copy;
check(style.fontSize(16)==16);
check(flow.setText("original",style));
check(run.set(" mixed",style));
check(flow.appendRun(run));
check(frame.setGeometry(10,20,200,25));
check(flow.appendFrame(frame));
check(flow.layout());
check(!flow.loadTXT("missing.txt",style));
check(!flow.loadTXT("invalid.txt",style));
check(!flow.loadTXT("embedded-nul.txt",style));
check(!flow.loadTXT("long.txt",style));
check(!flow.loadTXT("oversize.txt",style));
check(!flow.loadTXT("wrong.ckflow",style));
check(flow.ready()==1);
check(flow.sourceText()=="original mixed");
check(flow.loadTXT("input.txt",style));
check(flow.sourceText()=="café\\n\\nsecond\\n");
check(flow.ready()==0);
check(flow.frameCount()==1);
check(flow.getFrame(0,copy));
check(copy.x()==10 && copy.y()==20 && copy.width()==200 && copy.height()==25);
check(flow.layout());
check(flow.frameText(0)=="café\\n");
check(flow.overflowText()=="\\nsecond\\n");
check(flow.saveTXT("output.txt"));
check(!flow.saveTXT("output.txt"));
check(!flow.saveTXT("wrong.ckflow"));
check(flow.loadTXT("empty.txt",style));
check(flow.sourceText()=="");
check(flow.frameCount()==1);
check(flow.saveTXT("empty-out.txt"));
check(flow.layout());
check(flow.overflowBytes()==0);
check(flow.setText("A\\r",style));
check(run.set("\\nB\\rC",style));
check(flow.appendRun(run));
check(flow.saveTXT("mixed-out.txt"));
check(flow.sourceText()=="A\\r\\nB\\rC");
check(flow.loadTXT("boundary.txt",style));
check(flow.sourceText().length()==65536);
check(flow.saveTXT("boundary-out.txt"));
check(flow.loadTXT("crlf-boundary.txt",style));
check(flow.sourceText().length()==65536);
check(flow.saveTXT("crlf-out.txt"));
check(flow.frameCount()==1);
""",45,p)
        assert (p / "output.txt").read_bytes() == "café\n\nsecond\n".encode("utf-8")
        assert (p / "empty-out.txt").read_bytes() == b""
        assert (p / "mixed-out.txt").read_bytes() == b"A\nB\nC"
        assert (p / "boundary-out.txt").read_bytes() == b"a" * 65536
        assert (p / "crlf-out.txt").read_bytes() == b"\n" * 65536
        assert not (p / "wrong.ckflow").exists()


def text_flow_interop():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("text", """
TextDocument doc;
TextDocument target;
TextFlow flow;
TextFrame frame;
TextStyle style;
TextStyle large;
TextRun run;
TextParagraphStyle layoutStyle;
TextContainer box;
TextList marker;
int dims[2];
check(style.fontSize(16)==16);
check(large.fontSize(64)==64);
check(doc.addParagraph("small\\n",style));
check(run.set("café",large));
check(doc.appendRun(0,run));
check(layoutStyle.marginLeft(10)==10);
check(doc.setParagraphLayout(0,layoutStyle));
check(box.paddingLeft(5)==5);
check(doc.setParagraphContainer(0,box));
check(marker.mode(2)==2);
check(doc.setParagraphList(0,marker));
check(frame.setGeometry(10,20,200,25));
check(flow.appendFrame(frame));
check(flow.setText("old",style));
check(flow.layout());
check(!doc.exportParagraphToFlow(-1,flow));
check(!doc.exportParagraphToFlow(1,flow));
check(flow.ready()==1 && flow.sourceText()=="old");
check(doc.exportParagraphToFlow(0,flow));
check(flow.ready()==0);
check(flow.sourceText()=="small\\ncafé");
check(flow.frameCount()==1);
check(flow.layout());
check(flow.frameText(0)=="small\\n");
check(flow.overflowText()=="café");
check(target.addParagraph("before",style));
check(target.appendFlow(flow));
check(target.paragraphCount()==2);
check(target.paragraphText(1)=="small\\ncafé");
check(target.runCount(1)==1);
check(target.runText(1,0)=="café");
check(target.getParagraphLayout(1,layoutStyle));
check(layoutStyle.marginLeft()==0);
check(target.getParagraphContainer(1,box));
check(box.paddingLeft()==0);
check(target.getParagraphList(1,marker));
check(marker.mode()==0);
check(target.measureParagraph(1,200,dims));
check(dims[1]>40);
check(doc.setParagraphText(0,"changed"));
check(flow.sourceText()=="small\\ncafé");
check(flow.setText("new",style));
check(target.paragraphText(1)=="small\\ncafé");
check(target.exportParagraphToFlow(1,flow));
check(flow.layout());
check(flow.overflowText()=="café");
check(flow.setText("",style));
check(target.appendFlow(flow));
check(target.paragraphCount()==3);
check(target.paragraphText(2)=="");
check(target.runCount(2)==0);
check(target.saveNative("interop.cktxt"));
check(flow.saveNative("interop.ckflow"));
""",53,p)
        assert (p / "interop.cktxt").read_bytes().startswith(b"CKTXTDOC")
        assert (p / "interop.ckflow").read_bytes().startswith(b"CKTXFLOW")


def text_flow_performance():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("text", """
TextFlow flow;
TextFrame frame;
TextStyle style;
TextRun run;
check(style.fontSize(16)==16);
check(flow.setText("café\\n",style));
check(run.set("second\\nthird",style));
check(flow.appendRun(run));
check(frame.setGeometry(0,0,200,25));
check(flow.appendFrame(frame));
check(frame.height(1)==1);
check(flow.appendFrame(frame));
check(frame.height(25)==25);
check(flow.appendFrame(frame));
check(flow.appendFrame(frame));
check(flow.appendFrame(frame));
check(flow.layout());
check(flow.frameStartByte(0)==0 && flow.frameEndByte(0)==6);
check(flow.frameStartByte(1)==6 && flow.frameEndByte(1)==6);
check(flow.frameStartByte(2)==6 && flow.frameEndByte(2)==13);
check(flow.frameStartByte(3)==13 && flow.frameEndByte(3)==18);
check(flow.frameStartByte(4)==18 && flow.frameEndByte(4)==18);
check(flow.frameText(0)=="café\\n");
check(flow.frameText(1)=="");
check(flow.frameText(2)=="second\\n");
check(flow.frameText(3)=="third");
check(flow.frameText(4)=="");
check(flow.overflowBytes()==0);
check(flow.layout());
check(flow.frameEndByte(3)==18);
check(flow.removeFrame(2));
check(flow.ready()==0);
check(flow.frameStartByte(0)==-1 && flow.frameEndByte(0)==-1);
check(flow.frameText(0)=="");
check(flow.layout());
check(flow.frameText(2)=="second\\n");
check(flow.frameText(3)=="third");
flow.clearFrames();
check(flow.setText("x",style));
check(frame.setGeometry(0,0,200,1));
for(0=>int i;i<255;i++) flow.appendFrame(frame);
check(frame.height(100)==100);
check(flow.appendFrame(frame));
check(flow.frameCount()==256);
check(flow.layout());
check(flow.frameStartByte(254)==0 && flow.frameEndByte(254)==0);
check(flow.frameStartByte(255)==0 && flow.frameEndByte(255)==1);
check(flow.frameText(255)=="x");
check(flow.overflowBytes()==0);
check(flow.frameStartByte(256)==-1);
check(flow.setText("",style));
check(flow.layout());
check(flow.frameStartByte(255)==0 && flow.frameEndByte(255)==0);
check(flow.frameText(255)=="");
check(flow.overflowBytes()==0);
""",49,p)


def text_flow_consolidation():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("text", """
TextDocument doc;
TextDocument copy;
TextStyle style;
TextStyle accent;
TextRun run;
TextFlow flow;
TextFlow restored;
TextFrame frame;
TextContainer box;
check(style.fontSize(16)==16);
check(accent.fontSize(20)==20);
check(doc.addParagraph("café\\n",style));
check(run.set("Second",accent));
check(doc.appendRun(0,run));
check(doc.exportParagraphToFlow(0,flow));
check(frame.setGeometry(10,10,200,25));
check(flow.appendFrame(frame));
check(frame.setGeometry(20,20,220,100));
check(frame.page(1)==1);
check(flow.appendFrame(frame));
check(flow.layout());
check(flow.pageCount()==2);
check(flow.sourceText()=="café\\nSecond");
check(flow.frameText(0)=="café\\n");
check(flow.frameText(1)=="Second");
check(flow.frameStartByte(1)==6 && flow.frameEndByte(1)==12);
check(flow.overflowBytes()==0);
check(flow.saveNative("integrated.ckflow"));
check(flow.saveTXT("integrated.txt"));
check(restored.loadNative("integrated.ckflow"));
check(restored.ready()==0);
check(restored.layout());
check(restored.frameText(0)==flow.frameText(0));
check(restored.frameText(1)==flow.frameText(1));
int pixels[256*144*4];
int other[256*144*4];
check(flow.exportPage(1,256,144,-1,pixels));
check(restored.exportPage(1,256,144,-1,other));
1=>int pixelsMatch;
for(0=>int i;i<pixels.size();i++) if(pixels[i]!=other[i]) 0=>pixelsMatch;
check(pixelsMatch);
check(restored.savePagePNG("integrated.png",1,256,144,-1));
check(copy.appendFlow(restored));
check(copy.paragraphText(0)==doc.paragraphText(0));
check(copy.runCount(0)==1 && copy.runText(0,0)=="Second");
check(copy.plainText()==flow.sourceText());
check(copy.saveNative("integrated.cktxt"));
check(restored.loadTXT("integrated.txt",style));
check(restored.frameCount()==2);
check(restored.sourceText()==flow.sourceText());
check(restored.ready()==0);
check(restored.layout());
check(restored.overflowBytes()==0);
check(restored.frameStartByte(1)==6);
check(restored.saveTXT("integrated-copy.txt"));
check(frame.height(1)==1);
check(restored.setFrame(0,frame));
check(restored.setFrame(1,frame));
check(restored.layout());
check(restored.overflowText()==restored.sourceText());
check(restored.saveTXT("overflow.txt"));
""",48,p)
        assert (p / "integrated.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
        assert (p / "integrated.cktxt").read_bytes().startswith(b"CKTXTDOC")
        assert (p / "integrated.txt").read_bytes() == (p / "integrated-copy.txt").read_bytes() == (p / "overflow.txt").read_bytes()


def text_flow_hit_test():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("text", """
TextFlow flow;
TextFrame frame;
TextStyle style;
TextContainer box;
check(flow.hitTest(0,0.0,0.0)==-1);
check(style.fontSize(16)==16);
check(flow.setText("abc\\ncafé",style));
check(frame.setGeometry(10,20,200,30));
check(box.paddingLeft(5)==5);
check(box.paddingTop(3)==3);
check(frame.setContainer(box));
check(flow.appendFrame(frame));
check(frame.height(80)==80);
check(flow.appendFrame(frame));
check(flow.layout());
check(flow.frameText(0)=="abc\\n");
check(flow.frameText(1)=="café");
check(flow.hitTest(-1,5.0,4.0)==-1);
check(flow.hitTest(2,5.0,4.0)==-1);
check(flow.hitTest(0,-1.0,4.0)==-1);
check(flow.hitTest(0,4.0,4.0)==-1);
check(flow.hitTest(0,5.0,2.0)==-1);
check(flow.hitTest(0,200.0,4.0)==-1);
check(flow.hitTest(0,5.0,30.0)==-1);
check(flow.hitTest(0,5.0,4.0)==0);
check(flow.hitTest(1,5.0,4.0)==4);
check(flow.hitTest(0,190.0,4.0)>=3);
check(flow.hitTest(0,190.0,4.0)<=4);
check(flow.hitTest(1,190.0,4.0)==9);
0=>int invalidOffset;
for(5=>int x;x<195;x++) {
    flow.hitTest(1,x$float,4.0)=>int offset;
    if(offset<4 || offset>9 || offset==8) 1=>invalidOffset;
}
check(!invalidOffset);
check(flow.hitTest(1,5.0,70.0)==-1);
check(flow.setText("changed",style));
check(flow.hitTest(0,5.0,4.0)==-1);
check(flow.layout());
check(flow.hitTest(1,5.0,4.0)==-1);
check(flow.hitTest(0,5.0,4.0)==0);
check(flow.setText("",style));
check(flow.layout());
check(flow.hitTest(0,5.0,4.0)==-1);
""",35,p)


def text_flow_selection():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        (p / "input.txt").write_bytes("é".encode("utf-8"))
        run("text", """
TextFlow flow;
TextFlow restored;
TextFrame frame;
TextStyle style;
TextRun run;
TextDocument doc;
check(flow.selectionAnchor()==0);
check(flow.selectionFocus()==0);
check(flow.selectedText()=="");
check(flow.setText("A café\\nZ",style));
check(flow.setSelection(2,7));
check(flow.selectionAnchor()==2);
check(flow.selectionFocus()==7);
check(flow.selectionStart()==2);
check(flow.selectionEnd()==7);
check(flow.selectedText()=="café");
check(flow.setSelection(7,2));
check(flow.selectionAnchor()==7);
check(flow.selectionFocus()==2);
check(flow.selectionStart()==2);
check(flow.selectionEnd()==7);
check(flow.selectedText()=="café");
check(!flow.setSelection(6,7));
check(!flow.setSelection(-1,2));
check(!flow.setSelection(2,10));
check(flow.selectionAnchor()==7 && flow.selectionFocus()==2);
flow.clearSelection();
check(flow.selectionStart()==2 && flow.selectionEnd()==2);
check(flow.selectedText()=="");
check(flow.setSelection(0,9));
check(flow.selectedText()==flow.sourceText());
check(frame.setGeometry(0,0,200,25));
check(flow.appendFrame(frame));
check(flow.layout());
check(flow.setSelection(7,9));
check(flow.ready()==1);
check(flow.selectedText()=="\\nZ");
check(run.set("!",style));
check(flow.appendRun(run));
check(flow.ready()==0);
check(flow.selectedText()=="\\nZ");
check(flow.setSelection(10,10));
check(flow.selectedText()=="");
check(flow.saveNative("selection.ckflow"));
check(restored.setText("old",style));
check(restored.setSelection(0,3));
check(restored.loadNative("selection.ckflow"));
check(restored.selectionAnchor()==0 && restored.selectionFocus()==0);
check(restored.sourceText()==flow.sourceText());
check(flow.setText("reset",style));
check(flow.selectionAnchor()==0 && flow.selectionFocus()==0);
check(flow.setSelection(0,5));
check(doc.addParagraph("new",style));
check(doc.exportParagraphToFlow(0,flow));
check(flow.selectionAnchor()==0 && flow.selectionFocus()==0);
check(flow.setSelection(0,3));
check(flow.loadTXT("input.txt",style));
check(flow.selectionAnchor()==0 && flow.selectionFocus()==0);
check(flow.sourceText()=="é");
check(!flow.setSelection(1,2));
check(flow.setSelection(0,2));
check(flow.selectedText()=="é");
check(!flow.loadTXT("missing.txt",style));
check(flow.selectedText()=="é");
""",57,p)


def text_flow_edit():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        (p / "limit.txt").write_bytes(b"a" * 65536)
        run("text", """
TextFlow flow;
TextFlow restored;
TextStyle style;
TextRun run;
TextFrame frame;
check(flow.insertText(0,"abc"));
check(flow.sourceText()=="abc");
check(flow.selectionFocus()==3);
check(flow.insertText(0,"é"));
check(flow.sourceText()=="éabc");
check(flow.selectionStart()==2);
check(!flow.insertText(1,"x"));
check(!flow.insertText(-1,"x"));
check(!flow.insertText(6,"x"));
check(flow.sourceText()=="éabc");
check(flow.deleteText(2,3));
check(flow.sourceText()=="ébc");
check(flow.selectionFocus()==2);
check(!flow.deleteText(3,2));
check(!flow.deleteText(1,2));
check(flow.replaceText(0,2,"猫"));
check(flow.sourceText()=="猫bc");
check(flow.selectionFocus()==3);
check(flow.setSelection(5,3));
check(flow.replaceSelection("é"));
check(flow.sourceText()=="猫é");
check(flow.selectionAnchor()==5 && flow.selectionFocus()==5);
check(flow.replaceSelection("!"));
check(flow.sourceText()=="猫é!");
check(flow.setSelection(0,6));
check(flow.replaceSelection(""));
check(flow.sourceText()=="");
check(flow.selectionFocus()==0);
check(flow.setText("base",style));
check(style.fontSize(22)==22);
check(run.set(" café",style));
check(flow.appendRun(run));
check(frame.setGeometry(0,0,200,80));
check(flow.appendFrame(frame));
check(flow.layout());
check(flow.setSelection(4,10));
check(!flow.replaceText(9,10,"x"));
check(flow.ready()==1);
check(flow.selectedText()==" café");
check(flow.insertText(4,"!"));
check(flow.ready()==0);
check(flow.sourceText()=="base! café");
check(flow.frameCount()==1);
check(flow.replaceText(3,6,"X"));
check(flow.sourceText()=="basXcafé");
check(flow.selectionFocus()==4);
check(flow.layout());
check(flow.deleteText(4,4));
check(flow.ready()==1);
check(flow.saveNative("edited.ckflow"));
check(restored.loadNative("edited.ckflow"));
check(restored.sourceText()==flow.sourceText());
check(restored.frameCount()==1);
check(restored.loadTXT("limit.txt",style));
check(!restored.insertText(0,"x"));
check(restored.sourceText().length()==65536);
check(restored.replaceText(0,1,"z"));
check(restored.sourceText().length()==65536);
check(restored.deleteText(0,65536));
check(restored.sourceText()=="");
""",60,p)


def text_flow_styled_edit():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("text", """
TextFlow flow;
TextFlow restored;
TextStyle baseStyle;
TextStyle accent;
TextStyle other;
TextStyle found;
TextFrame frame;
check(baseStyle.fontSize(16)==16);
check(accent.fontSize(24)==24);
check(other.fontSize(30)==30);
check(flow.setText("abcd",baseStyle));
check(flow.insertText(2,"é",accent));
check(flow.sourceText()=="abécd");
check(flow.selectionFocus()==4);
check(flow.styleAt(0,found));
check(found.fontSize()==16);
check(flow.styleAt(2,found));
check(found.fontSize()==24);
check(!flow.styleAt(3,found));
check(found.fontSize()==24);
check(flow.styleAt(4,found));
check(found.fontSize()==16);
check(!flow.styleAt(6,found));
check(!flow.styleAt(-1,found));
check(accent.fontSize(26)==26);
check(flow.styleAt(2,found));
check(found.fontSize()==24);
check(flow.setSelection(5,1));
check(flow.applyStyle(1,5,other));
check(flow.sourceText()=="abécd");
check(flow.selectionAnchor()==5 && flow.selectionFocus()==1);
check(flow.styleAt(0,found));
check(found.fontSize()==16);
check(flow.styleAt(1,found));
check(found.fontSize()==30);
check(flow.styleAt(2,found));
check(found.fontSize()==30);
check(flow.styleAt(4,found));
check(found.fontSize()==30);
check(flow.styleAt(5,found));
check(found.fontSize()==16);
check(frame.setGeometry(0,0,220,100));
check(flow.appendFrame(frame));
check(flow.layout());
check(!flow.applyStyle(3,5,accent));
check(!flow.applyStyle(5,1,accent));
check(flow.ready()==1);
check(flow.applyStyle(2,2,accent));
check(flow.ready()==1);
check(flow.selectionAnchor()==5 && flow.selectionFocus()==1);
check(flow.replaceSelection("猫",accent));
check(flow.sourceText()=="a猫d");
check(flow.selectionFocus()==4);
check(flow.ready()==0);
check(flow.styleAt(1,found));
check(found.fontSize()==26);
check(flow.styleAt(4,found));
check(found.fontSize()==16);
check(flow.replaceText(1,4,"XY",other));
check(flow.sourceText()=="aXYd");
check(flow.styleAt(2,found));
check(found.fontSize()==30);
check(flow.insertText(2,"!"));
check(flow.sourceText()=="aX!Yd");
check(flow.styleAt(2,found));
check(found.fontSize()==16);
check(flow.styleAt(3,found));
check(found.fontSize()==30);
check(flow.saveNative("styled.ckflow"));
check(restored.loadNative("styled.ckflow"));
check(restored.sourceText()==flow.sourceText());
check(restored.styleAt(1,found));
check(found.fontSize()==30);
check(restored.styleAt(2,found));
check(found.fontSize()==16);
check(restored.styleAt(4,found));
check(found.fontSize()==16);
check(restored.applyStyle(0,5,accent));
check(restored.styleAt(4,found));
check(found.fontSize()==26);
check(restored.deleteText(0,5));
check(!restored.styleAt(0,found));
""",75,p)


def text_flow_edit_consolidation():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("text", """
TextFlow flow;
TextFlow restored;
TextDocument doc;
TextStyle normal;
TextStyle accent;
TextStyle found;
TextFrame frame;
check(normal.fontSize(16)==16);
check(accent.fontSize(20)==20);
check(flow.setText("café\\nSecond",normal));
check(frame.setGeometry(10,10,200,25));
check(flow.appendFrame(frame));
check(frame.setGeometry(20,20,220,100));
check(frame.page(1)==1);
check(flow.appendFrame(frame));
check(flow.layout());
check(flow.frameText(0)=="café\\n");
check(flow.frameText(1)=="Second");
flow.hitTest(1,0.0,1.0)=>int leftByte;
flow.hitTest(1,210.0,1.0)=>int rightByte;
check(leftByte==6);
check(rightByte==12);
check(flow.setSelection(rightByte,leftByte));
check(flow.selectedText()=="Second");
check(flow.replaceSelection("World",accent));
check(flow.sourceText()=="café\\nWorld");
check(flow.selectionFocus()==11);
check(flow.ready()==0);
check(flow.hitTest(1,0.0,1.0)==-1);
check(flow.layout());
check(flow.frameText(0)=="café\\n");
check(flow.frameText(1)=="World");
check(flow.styleAt(6,found));
check(found.fontSize()==20);
check(flow.styleAt(0,found));
check(found.fontSize()==16);
check(flow.setSelection(0,5));
check(flow.applyStyle(0,5,normal));
check(flow.selectedText()=="café");
check(flow.insertText(11,"!",accent));
check(flow.sourceText()=="café\\nWorld!");
check(flow.layout());
check(flow.overflowBytes()==0);
check(flow.pageCount()==2);
check(flow.saveNative("edit.ckflow"));
check(flow.saveTXT("edit.txt"));
check(restored.loadNative("edit.ckflow"));
check(restored.selectionFocus()==0);
check(restored.sourceText()==flow.sourceText());
check(restored.layout());
check(restored.styleAt(11,found));
check(found.fontSize()==20);
int pixels[256*144*4];
int other[256*144*4];
check(flow.exportPage(1,256,144,-1,pixels));
check(restored.exportPage(1,256,144,-1,other));
1=>int pixelsMatch;
for(0=>int i;i<pixels.size();i++) if(pixels[i]!=other[i]) 0=>pixelsMatch;
check(pixelsMatch);
check(restored.savePagePNG("edit.png",1,256,144,-1));
check(doc.appendFlow(restored));
check(doc.plainText()==flow.sourceText());
check(doc.saveNative("edit.cktxt"));
check(restored.deleteText(6,12));
check(restored.sourceText()=="café\\n");
check(restored.frameCount()==2);
check(restored.layout());
check(restored.overflowBytes()==0);
check(flow.sourceText()=="café\\nWorld!");
""",56,p)
        assert (p / "edit.txt").read_bytes() == "café\nWorld!".encode("utf-8")
        assert (p / "edit.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
        assert (p / "edit.cktxt").read_bytes().startswith(b"CKTXTDOC")


def vector_shape_edit():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("vector", """
VectorImage image;
check(image.shapeType(0)=="");
check(image.shapeGeometrySize(0)==-1);
check(image.create(16,16));
check(image.addRect(0,0,4,4,16711680,1));
check(image.addCircle(10,10,2,65280,1));
check(image.addLine(0,12,5,12,2,255,1));
[1.0,8.0,4.0,8.0,4.0,10.0] @=> float points[];
check(image.addPolygon(points,16777215,1));
check(image.shapes()==4);
check(image.shapeType(0)=="rect");
check(image.shapeType(1)=="circle");
check(image.shapeType(2)=="line");
check(image.shapeType(3)=="polygon");
check(image.shapeGeometrySize(0)==4);
check(image.shapeGeometrySize(1)==3);
check(image.shapeGeometrySize(2)==5);
check(image.shapeGeometrySize(3)==6);
float rect[4];
float circle[3];
float lineValues[5];
float polygon[6];
check(image.getShapeGeometry(0,rect));
check(rect[0]==0 && rect[1]==0 && rect[2]==4 && rect[3]==4);
check(image.getShapeGeometry(1,circle));
check(circle[0]==10 && circle[1]==10 && circle[2]==2);
check(image.getShapeGeometry(2,lineValues));
check(lineValues[0]==0 && lineValues[1]==12 && lineValues[2]==5 && lineValues[3]==12 && lineValues[4]==2);
check(image.getShapeGeometry(3,polygon));
check(polygon[0]==1 && polygon[5]==10);
check(!image.getShapeGeometry(0,circle));
check(circle[0]==10);
check(!image.setShapeGeometry(0,circle));
-1.0=>rect[2];
check(!image.setShapeGeometry(0,rect));
check(image.getShapeGeometry(0,rect));
check(rect[2]==4);
int pixels[1024];
check(image.exportPixels(16,16,pixels));
check(pixels[0]==255 && pixels[3]==255);
5.0=>rect[0];
check(image.setShapeGeometry(0,rect));
check(image.exportPixels(16,16,pixels));
check(pixels[3]==0);
check(pixels[20]==255 && pixels[23]==255);
3.0=>circle[2];
check(image.setShapeGeometry(1,circle));
check(image.getShapeGeometry(1,circle));
check(circle[2]==3);
3.0=>lineValues[4];
check(image.setShapeGeometry(2,lineValues));
check(image.getShapeGeometry(2,lineValues));
check(lineValues[4]==3);
[1.0,8.0,4.0,8.0,4.0,10.0,1.0,10.0] @=> float quad[];
check(image.setShapeGeometry(3,quad));
check(image.shapeGeometrySize(3)==8);
check(image.removeShape(0));
check(image.shapeType(0)=="circle");
check(image.shapes()==3);
check(image.save("edited.svg"));
check(image.load("edited.svg"));
check(image.shapes()==-1);
check(image.shapeType(0)=="");
check(!image.setShapeGeometry(0,circle));
check(image.exportPixels(16,16,pixels));
[0.0,1.0] @=> float bounds[];
check(image.importWaveform(16,16,bounds,255,1));
check(image.shapeType(0)=="path");
check(image.shapeGeometrySize(0)==-1);
check(!image.setShapeGeometry(0,rect));
""",57,p)
        import xml.etree.ElementTree as ET
        root = ET.parse(p / "edited.svg").getroot()
        assert [e.tag.split("}")[-1] for e in root] == ["circle", "line", "polygon"]
        assert float(root[0].get("r")) == 3
        assert float(root[1].get("stroke-width")) == 3


def vector_shape_transform():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("vector", """
VectorImage image;
float matrix[6];
float geometry[4];
float wrong[5];
check(!image.getShapeTransform(0,matrix));
check(image.create(16,16));
check(image.addRect(0,0,2,2,16711680,1));
check(image.addCircle(12,12,2,65280,1));
check(image.getShapeTransform(0,matrix));
check(matrix[0]==1 && matrix[1]==0 && matrix[2]==0 && matrix[3]==1 && matrix[4]==0 && matrix[5]==0);
check(!image.getShapeTransform(0,wrong));
check(!image.setShapeTransform(0,wrong));
check(!image.translateShape(-1,1,1));
check(image.translateShape(0,4,2));
check(image.getShapeTransform(0,matrix));
check(matrix[4]==4 && matrix[5]==2);
check(image.getShapeGeometry(0,geometry));
check(geometry[0]==0 && geometry[1]==0 && geometry[2]==2 && geometry[3]==2);
int pixels[1024];
check(image.exportPixels(16,16,pixels));
check(pixels[3]==0);
check(pixels[(2*16+4)*4]==255 && pixels[(2*16+4)*4+3]==255);
check(pixels[(12*16+12)*4+1]==255);
check(image.scaleShape(0,2,3,4,2));
check(image.getShapeTransform(0,matrix));
check(matrix[0]==2 && matrix[3]==3 && matrix[4]==4 && matrix[5]==2);
check(image.rotateShape(0,90,4,2));
check(image.getShapeTransform(0,matrix));
check(Math.fabs(matrix[0])<0.000001 && Math.fabs(matrix[1]-2)<0.000001);
check(Math.fabs(matrix[2]+3)<0.000001 && Math.fabs(matrix[3])<0.000001);
check(Math.fabs(matrix[4]-4)<0.000001 && Math.fabs(matrix[5]-2)<0.000001);
check(!image.scaleShape(0,0,1,0,0));
check(!image.translateShape(0,1000001,0));
check(image.getShapeTransform(0,matrix));
check(Math.fabs(matrix[1]-2)<0.000001);
[1.0,0.0,0.0,0.0,0.0,0.0] @=> float singular[];
check(!image.setShapeTransform(0,singular));
check(image.getShapeTransform(0,matrix));
check(Math.fabs(matrix[1]-2)<0.000001);
check(image.resetShapeTransform(0));
check(image.getShapeTransform(0,matrix));
check(matrix[0]==1 && matrix[3]==1 && matrix[4]==0 && matrix[5]==0);
check(image.scaleShape(0,-1,1,4,0));
check(image.getShapeTransform(0,matrix));
check(matrix[0]==-1 && matrix[4]==8);
check(image.resetShapeTransform(0));
[1.0,0.0,0.5,1.0,3.0,4.0] @=> float shear[];
check(image.setShapeTransform(0,shear));
check(image.getShapeTransform(0,matrix));
check(matrix[2]==0.5 && matrix[4]==3 && matrix[5]==4);
3.0=>geometry[2];
check(image.setShapeGeometry(0,geometry));
check(image.getShapeTransform(0,matrix));
check(matrix[2]==0.5 && matrix[4]==3);
check(image.x(2)==2);
check(image.getShapeTransform(0,matrix));
check(matrix[4]==3);
image.reset();
check(image.x()==0);
check(image.getShapeTransform(0,matrix));
check(matrix[4]==3);
check(image.getShapeTransform(1,matrix));
check(matrix[0]==1 && matrix[4]==0);
check(image.save("transformed.svg"));
check(image.load("transformed.svg"));
check(!image.getShapeTransform(0,matrix));
check(image.exportPixels(16,16,pixels));
""",58,p)
        import xml.etree.ElementTree as ET
        root = ET.parse(p / "transformed.svg").getroot()
        assert root[0].get("transform") == "matrix(1 0 0.5 1 3 4)"
        assert float(root[0].get("width")) == 3
        assert root[1].get("transform") is None


def vector_shape_style():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("vector", """
VectorImage image;
float paint[6];
float wrong[5];
check(!image.getShapeStyle(0,paint));
check(image.create(16,16));
check(image.addRect(4,4,8,8,16711680,1));
check(image.addLine(1,1,3,1,2,255,0.5));
check(image.getShapeStyle(0,paint));
check(paint[0]==16711680 && paint[1]==1 && paint[2]==-1);
check(paint[3]==1 && paint[4]==1 && paint[5]==1);
check(image.getShapeStyle(1,paint));
check(paint[0]==-1 && paint[2]==255 && paint[3]==0.5 && paint[4]==2);
check(!image.getShapeStyle(0,wrong));
check(!image.setShapeStyle(0,wrong));
check(!image.setShapeFill(1,0,1));
check(image.setShapeFill(0,65280,1));
check(image.setShapeStroke(0,255,1,2));
check(image.getShapeStyle(0,paint));
check(paint[0]==65280 && paint[2]==255 && paint[4]==2);
int pixels[1024];
check(image.exportPixels(16,16,pixels));
check(pixels[(8*16+8)*4+1]==255 && pixels[(8*16+8)*4+3]==255);
check(pixels[(4*16+8)*4+2]==255 && pixels[(4*16+8)*4+3]==255);
check(image.setShapeOpacity(0,0.5));
check(image.exportPixels(16,16,pixels));
check(pixels[(8*16+8)*4+1]==255);
check(pixels[(8*16+8)*4+3]>=127 && pixels[(8*16+8)*4+3]<=128);
check(image.setShapeFill(0,-1,1));
check(image.exportPixels(16,16,pixels));
check(pixels[(8*16+8)*4+3]==0);
check(pixels[(4*16+8)*4+2]==255);
check(image.setShapeStroke(0,-1,1,2));
check(image.exportPixels(16,16,pixels));
check(pixels[(4*16+8)*4+3]==0);
check(!image.setShapeFill(0,16777216,1));
check(!image.setShapeStroke(0,0,1,0));
check(!image.setShapeOpacity(0,1.1));
check(image.getShapeStyle(0,paint));
check(paint[0]==-1 && paint[2]==-1 && paint[5]==0.5);
[16711680.0,0.75,255.0,0.5,2.0,1.0] @=> float newPaint[];
check(image.setShapeStyle(0,newPaint));
check(image.getShapeStyle(0,paint));
check(paint[0]==16711680 && paint[1]==0.75 && paint[2]==255 && paint[3]==0.5 && paint[4]==2 && paint[5]==1);
0.5=>newPaint[0];
check(!image.setShapeStyle(0,newPaint));
check(image.getShapeStyle(0,paint));
check(paint[0]==16711680);
check(image.translateShape(0,1,0));
check(image.setShapeOpacity(0,0.8));
float matrix[6];
check(image.getShapeTransform(0,matrix));
check(matrix[4]==1);
float geometry[4];
check(image.getShapeGeometry(0,geometry));
check(geometry[0]==4 && geometry[2]==8);
check(image.setShapeStroke(1,65280,1,3));
float lineValues[5];
check(image.getShapeGeometry(1,lineValues));
check(lineValues[4]==3);
4.0=>lineValues[4];
check(image.setShapeGeometry(1,lineValues));
check(image.getShapeStyle(1,paint));
check(paint[4]==4 && paint[2]==65280);
check(image.save("styled.svg"));
check(image.load("styled.svg"));
check(!image.getShapeStyle(0,paint));
check(image.exportPixels(16,16,pixels));
""",57,p)
        import xml.etree.ElementTree as ET
        root = ET.parse(p / "styled.svg").getroot()
        assert root[0].get("fill") == "#ff0000"
        assert root[0].get("stroke") == "#0000ff"
        assert float(root[0].get("opacity")) == 0.8
        assert root[0].get("transform") == "matrix(1 0 0 1 1 0)"
        assert root[1].get("fill") == "none"
        assert float(root[1].get("stroke-width")) == 4


def vector_shape_group():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("vector", """
VectorImage image;
check(image.duplicateShape(0)==-1);
check(image.groupShapes(0,2)==-1);
check(image.create(16,16));
check(image.addRect(0,0,8,8,16711680,1));
check(image.addRect(0,0,8,8,255,1));
check(image.addCircle(12,12,2,65280,1));
int pixels[1024];
check(image.exportPixels(16,16,pixels));
check(pixels[2]==255 && pixels[0]==0);
check(image.moveShape(0,1));
check(image.exportPixels(16,16,pixels));
check(pixels[0]==255 && pixels[2]==0);
check(image.moveShape(1,1));
check(!image.moveShape(0,3));
check(!image.moveShape(-1,0));
check(image.duplicateShape(1)==2);
check(image.shapes()==4);
check(image.shapeType(2)=="rect");
check(image.translateShape(2,4,0));
float matrix[6];
check(image.getShapeTransform(1,matrix));
check(matrix[4]==0);
check(image.getShapeTransform(2,matrix));
check(matrix[4]==4);
check(image.setShapeFill(2,65280,1));
check(image.groupShapes(0,1)==-1);
check(image.groupShapes(2,3)==-1);
check(image.groupShapes(0,2)==0);
check(image.shapes()==3);
check(image.shapeType(0)=="g");
check(image.groupSize(0)==2);
check(image.groupSize(1)==-1);
check(image.shapeGeometrySize(0)==-1);
check(!image.getShapeTransform(0,matrix));
check(!image.translateShape(0,1,1));
float paint[6];
check(!image.getShapeStyle(0,paint));
check(!image.setShapeFill(0,0,1));
check(image.ungroupShape(1)==-1);
check(image.groupShapes(0,2)==0);
check(image.shapes()==2);
check(image.groupSize(0)==2);
check(image.duplicateShape(0)==1);
check(image.shapes()==3);
check(image.groupSize(1)==2);
check(image.removeShape(1));
check(image.shapes()==2);
check(image.save("grouped.svg"));
check(image.ungroupShape(0)==2);
check(image.shapes()==3);
check(image.shapeType(0)=="g");
check(image.ungroupShape(0)==2);
check(image.shapes()==4);
check(image.shapeType(0)=="rect");
check(image.shapeType(3)=="circle");
check(image.getShapeStyle(2,paint));
check(paint[0]==65280);
check(image.getShapeTransform(2,matrix));
check(matrix[4]==4);
check(image.exportPixels(16,16,pixels));
check(pixels[0]==255 && pixels[2]==0);
check(pixels[4*5+1]==255);
check(image.load("grouped.svg"));
check(image.shapes()==-1);
check(image.ungroupShape(0)==-1);
check(image.exportPixels(16,16,pixels));
check(pixels[0]==255 && pixels[2]==0);
check(pixels[4*5+1]==255);
""",65,p)
        import xml.etree.ElementTree as ET
        root = ET.parse(p / "grouped.svg").getroot()
        assert [e.tag.split("}")[-1] for e in root] == ["g", "circle"]
        assert [e.tag.split("}")[-1] for e in root[0]] == ["g", "rect"]
        assert len(root[0][0]) == 2
        assert root[0][1].get("transform") == "matrix(1 0 0 1 4 0)"
        assert root[0][1].get("fill") == "#00ff00"


def vector_editable_svg():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("vector", """
VectorImage image;
VectorImage restored;
check(image.create(16,16));
check(image.addRect(0,0,4,4,16711680,1));
check(image.addCircle(12,12,2,65280,1));
check(image.translateShape(0,4,2));
check(image.setShapeStroke(0,255,1,2));
check(image.setShapeOpacity(0,0.75));
check(image.groupShapes(0,2)==0);
check(image.duplicateShape(0)==1);
check(image.groupShapes(0,2)==0);
check(image.save("editable.svg"));
check(restored.loadEditable("editable.svg"));
check(restored.shapes()==1);
check(restored.shapeType(0)=="g");
check(restored.groupSize(0)==2);
int beforePixels[1024];
int afterPixels[1024];
check(image.exportPixels(16,16,beforePixels));
check(restored.exportPixels(16,16,afterPixels));
1=>int equalPixels;
for(0=>int k;k<1024;k++) if(beforePixels[k]!=afterPixels[k]) 0=>equalPixels;
check(equalPixels);
check(restored.ungroupShape(0)==2);
check(restored.ungroupShape(0)==2);
check(restored.shapeType(0)=="rect");
float matrix[6];
float paint[6];
float geometry[4];
check(restored.getShapeTransform(0,matrix));
check(matrix[4]==4 && matrix[5]==2);
check(restored.getShapeStyle(0,paint));
check(paint[0]==16711680 && paint[2]==255 && paint[4]==2 && paint[5]==0.75);
check(restored.getShapeGeometry(0,geometry));
check(geometry[2]==4);
6.0=>geometry[2];
check(restored.setShapeGeometry(0,geometry));
check(restored.setShapeFill(0,65280,1));
check(restored.save("edited.svg"));
check(!restored.loadEditable("missing.svg"));
check(restored.shapes()==3);
check(restored.getShapeGeometry(0,geometry));
check(geometry[2]==6);
"<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'>" => string prefix;
check(!restored.setEditableSvg(prefix+"<rect x='0' y='0' width='2' height='2' onclick='bad'/></svg>"));
check(!restored.setEditableSvg(prefix+"<image href='external.png'/></svg>"));
check(!restored.setEditableSvg(prefix+"<rect x='0' y='0' width='-2' height='2'/></svg>"));
check(!restored.setEditableSvg(prefix+"<rect x='0' y='0' width='2' height='2' fill='url(#a)'/></svg>"));
check(!restored.setEditableSvg(prefix+"<g opacity='0.5'><circle cx='2' cy='2' r='1'/></g></svg>"));
check(!restored.setEditableSvg(prefix+"<rect x='0' x='1' y='0' width='2' height='2'/></svg>"));
check(!restored.setEditableSvg(prefix+"<rect x='0' y='0' width='2' height='2' transform='matrix(1 0 0 0 0 0)'/></svg>"));
check(restored.shapes()==3);
check(restored.setEditableSvg(prefix+"<rect x='0' y='0' width='2' height='2' fill='#ff0000'/></svg>"));
check(restored.shapes()==1);
check(restored.getShapeGeometry(0,geometry));
check(geometry[2]==2);
check(restored.load("editable.svg"));
check(restored.shapes()==-1);
check(restored.loadEditable("editable.svg"));
check(restored.shapes()==1);
[0.0,1.0] @=> float bounds[];
check(image.importWaveform(16,16,bounds,255,1));
check(image.save("wave.svg"));
check(restored.loadEditable("wave.svg"));
check(restored.shapeType(0)=="path");
check(restored.translateShape(0,1,0));
check(restored.setShapeFill(0,16711680,1));
check(restored.save("wave-edited.svg"));
check(restored.loadEditable("wave-edited.svg"));
check(restored.getShapeTransform(0,matrix));
check(matrix[4]==1);
check(restored.create(16,16));
check(restored.save("empty.svg"));
check(restored.loadEditable("empty.svg"));
check(restored.shapes()==0);
""",63,p)
        import xml.etree.ElementTree as ET
        root = ET.parse(p / "edited.svg").getroot()
        assert float(root[0].get("width")) == 6
        assert root[0].get("fill") == "#00ff00"
        assert root[0].get("transform") == "matrix(1 0 0 1 4 2)"


def midi_capture_events():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("midi", """
MidiCapture capture;
check(capture.count()==0);
check(!capture.append(0,144,60,100));
check(!capture.create(0,120));
check(!capture.create(480,0));
check(capture.create(480,120));
check(capture.append(0,144,60,100));
check(capture.append(0.125,176,64,127));
check(capture.append(0.25,192,5,0));
check(capture.append(0.25,208,70,0));
check(capture.append(0.5,128,60,0));
check(capture.count()==5);
check(capture.tickAt(0)==0);
check(capture.tickAt(1)==120);
check(capture.tickAt(2)==240);
check(capture.tickAt(3)==240);
check(capture.tickAt(4)==480);
check(capture.secondsAt(1)==0.125);
check(capture.secondsAt(4)==0.5);
check(capture.sizeAt(0)==3);
check(capture.sizeAt(2)==2);
check(capture.sizeAt(3)==2);
check(capture.byteAt(0,0)==144);
check(capture.byteAt(0,1)==60);
check(capture.byteAt(0,2)==100);
check(capture.byteAt(2,0)==192);
check(capture.byteAt(2,1)==5);
check(capture.byteAt(2,2)==-1);
check(!capture.append(0.4,144,60,100));
check(!capture.append(0.5,240,0,0));
check(!capture.append(0.5,127,0,0));
check(!capture.append(0.5,144,128,0));
check(!capture.append(0.5,144,60,-1));
check(!capture.append(0.5,192,5,1));
check(!capture.append(1000000000.0,144,60,100));
check(capture.count()==5);
check(capture.tickAt(-1)==-1);
check(capture.secondsAt(5)==-1);
check(capture.sizeAt(5)==-1);
check(capture.byteAt(0,-1)==-1);
check(capture.byteAt(5,0)==-1);
check(!capture.create(32768,120));
check(capture.count()==5);
MidiCapture other;
check(other.create(960,60));
check(other.append(0.5,239,0,127));
check(other.tickAt(0)==480);
check(other.sizeAt(0)==3);
check(other.byteAt(0,0)==239);
check(capture.count()==5);
capture.clear();
check(capture.count()==0);
check(capture.append(0.001,160,60,20));
check(capture.tickAt(0)==1);
check(capture.create(100,60));
check(capture.count()==0);
check(capture.append(0.125,144,64,0));
check(capture.tickAt(0)==13);
check(capture.byteAt(0,2)==0);
check(capture.append(0.125,128,64,12));
check(capture.tickAt(1)==13);
""",58,p)


def midi_capture_save():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("midi", """
MidiCapture capture;
check(!capture.save("unconfigured.mid"));
check(capture.create(480,120));
MidiMsg msg;
144=>msg.data1; 60=>msg.data2; 100=>msg.data3;
check(capture.appendMessage(0,msg.data1,msg.data2,msg.data3));
192=>msg.data1; 7=>msg.data2; 99=>msg.data3;
check(capture.appendMessage(0.25,msg.data1,msg.data2,msg.data3));
check(capture.sizeAt(1)==2);
check(capture.byteAt(1,2)==-1);
check(capture.appendMessage(0.5,128,60,40));
check(capture.appendMessage(0.5,144,60,80));
check(capture.appendMessage(0.75,176,64,127));
check(capture.appendMessage(1,144,60,0));
check(!capture.appendMessage(1,248,0,0));
check(capture.count()==6);
check(capture.save("captured.mid"));
check(!capture.save("captured.mid"));
check(capture.count()==6);
MidiSequence seq;
check(seq.load("captured.mid"));
check(seq.ticksPerQuarter()==480);
check(seq.tracks()==1);
check(seq.events(0)>=7);
check(seq.byteAt(0,0,0)==255);
check(seq.tickAt(0,1)==0);
check(seq.byteAt(0,1,0)==144);
check(seq.byteAt(0,1,1)==60);
check(seq.byteAt(0,1,2)==100);
check(seq.tickAt(0,2)==240);
check(seq.sizeAt(0,2)==2);
check(seq.byteAt(0,2,0)==192);
check(seq.byteAt(0,2,1)==7);
check(seq.tickAt(0,3)==480);
check(seq.tickAt(0,4)==480);
check(seq.byteAt(0,3,0)==128);
check(seq.byteAt(0,3,2)==40);
check(seq.byteAt(0,4,0)==144);
check(seq.byteAt(0,4,2)==80);
check(seq.tickAt(0,5)==720);
check(seq.byteAt(0,5,0)==176);
check(seq.tickAt(0,6)==960);
check(seq.byteAt(0,6,2)==0);
capture.clear();
check(capture.save("empty.mid"));
check(seq.load("empty.mid"));
check(seq.ticksPerQuarter()==480);
check(seq.events(0)>=1);
""",42,p)
        assert not (p / "unconfigured.mid").exists()
        assert (p / "captured.mid").read_bytes().startswith(b"MThd")


def midi_capture_session():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("midi", """
MidiCapture recorder;
check(!recorder.start(0));
check(recorder.create(480,120));
check(recorder.sessionState()==0);
check(recorder.durationAt(0)==-1);
check(!recorder.pause(0));
check(recorder.start(10));
check(recorder.sessionState()==1);
check(!recorder.start(10));
check(!recorder.append(0,144,60,90));
check(!recorder.appendMessage(0,144,60,90));
check(recorder.recordMessage(10,144,60,90));
check(recorder.recordMessage(10.25,176,64,127));
check(recorder.tickAt(1)==240);
check(recorder.durationAt(10.5)==0.5);
check(!recorder.pause(10.125));
check(recorder.pause(10.5));
check(recorder.sessionState()==2);
check(!recorder.recordMessage(11,128,60,0));
check(recorder.durationAt(12)==0.5);
check(!recorder.resume(10.25));
check(recorder.resume(15));
check(recorder.sessionState()==1);
check(recorder.recordMessage(15.25,128,60,0));
check(recorder.secondsAt(2)==0.75);
check(recorder.tickAt(2)==720);
check(!recorder.recordMessage(15.5,248,0,0));
check(recorder.recordMessage(15.375,192,7,99));
check(recorder.sizeAt(3)==2);
check(recorder.secondsAt(3)==0.875);
check(recorder.stop(15.5));
check(recorder.sessionState()==3);
check(recorder.durationAt(20)==1);
check(!recorder.recordMessage(20,144,60,90));
check(!recorder.resume(20));
check(!recorder.stop(20));
check(recorder.count()==4);
check(recorder.save("session.mid"));
MidiSequence seq;
check(seq.load("session.mid"));
check(seq.tickAt(0,3)==720);
check(seq.tickAt(0,4)==840);
check(recorder.start(30));
check(recorder.count()==0);
check(recorder.pause(31));
check(recorder.stop(40));
check(recorder.durationAt(40)==1);
check(recorder.sessionState()==3);
recorder.clear();
check(recorder.sessionState()==0);
check(recorder.count()==0);
check(recorder.append(0,144,64,100));
check(recorder.create(960,60));
check(recorder.count()==0);
check(recorder.sessionState()==0);
check(recorder.start(0));
check(!recorder.stop(1000000000.0));
check(recorder.sessionState()==1);
check(recorder.stop(1));
check(recorder.durationAt(1)==1);
""",57,p)


def text_capture_unicode():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("text", r"""
TextCapture buffer;
check(buffer.text()=="");
check(buffer.bytes()==0);
check(buffer.characters()==0);
check(buffer.lines()==1);
check(buffer.cursor()==0);
check(!buffer.backspace());
check(!buffer.deleteForward());
check(!buffer.seek(-1));
check(buffer.insert("A"));
check(buffer.insertCodepoint(233));
check(buffer.insertCodepoint(128512));
check(buffer.text()=="Aé😀");
check(buffer.bytes()==7);
check(buffer.characters()==3);
check(buffer.cursor()==7);
check(!buffer.seek(2));
check(!buffer.seek(4));
check(!buffer.seek(8));
check(buffer.cursor()==7);
check(buffer.backspace());
check(buffer.text()=="Aé");
check(buffer.cursor()==3);
check(buffer.seek(1));
check(buffer.deleteForward());
check(buffer.text()=="A");
check(buffer.cursor()==1);
check(buffer.newline());
check(buffer.lines()==2);
check(buffer.insert("second"));
check(buffer.lines()==2);
check(buffer.characters()==8);
check(buffer.seek(0));
check(buffer.insertCodepoint(937));
check(buffer.text()=="ΩA\nsecond");
check(buffer.cursor()==2);
check(buffer.bytes()==10);
check(buffer.seek(4));
check(buffer.backspace());
check(buffer.text()=="ΩAsecond");
check(buffer.lines()==1);
check(!buffer.insertCodepoint(0));
check(!buffer.insertCodepoint(-1));
check(!buffer.insertCodepoint(55296));
check(!buffer.insertCodepoint(1114112));
check(buffer.text()=="ΩAsecond");
check(buffer.cursor()==3);
check(buffer.insert(""));
check(buffer.cursor()==3);
TextCapture other;
check(other.insert("independent"));
check(buffer.text()=="ΩAsecond");
buffer.clear();
check(buffer.text()=="");
check(buffer.cursor()==0);
check(buffer.lines()==1);
check(other.text()=="independent");
check(buffer.insertCodepoint(101));
check(buffer.insertCodepoint(769));
check(buffer.characters()==2);
check(buffer.bytes()==3);
check(buffer.backspace());
check(buffer.text()=="e");
check(buffer.seek(0));
check(buffer.deleteForward());
check(buffer.bytes()==0);
check(!buffer.deleteForward());
""",64,p)


def text_capture_integration():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("text", r"""
TextCapture capture;
TextStyle style;
TextDocument doc;
TextFlow flow;
check(!capture.inputKey(8));
check(!capture.inputKey(27));
check(!capture.inputKey(233));
check(capture.inputKey(65));
check(capture.insertCodepoint(233));
check(capture.inputKey(127));
check(capture.text()=="A");
check(capture.inputKey(13));
check(capture.insert("Ω😀"));
check(capture.inputKey(10));
check(capture.inputKey(9));
check(capture.inputKey(66));
check(capture.text()=="A\nΩ😀\n\tB");
check(capture.lines()==3);
check(capture.seek(2));
check(flow.importCapture(capture,style));
check(flow.sourceText()==capture.text());
check(flow.selectionAnchor()==2);
check(flow.selectionFocus()==2);
check(doc.addParagraph("prefix",style));
check(doc.appendCapture(capture,style));
check(doc.paragraphCount()==4);
check(doc.paragraphText(0)=="prefix");
check(doc.paragraphText(1)=="A");
check(doc.paragraphText(2)=="Ω😀");
check(doc.paragraphText(3)=="\tB");
check(capture.insert("X"));
check(flow.sourceText()=="A\nΩ😀\n\tB");
check(doc.paragraphText(2)=="Ω😀");
check(doc.saveTXT("capture.txt"));
TextDocument loaded;
check(loaded.loadTXT("capture.txt"));
check(loaded.paragraphCount()==4);
check(loaded.paragraphText(2)=="Ω😀");
capture.clear();
check(capture.insert("one\r\ntwo\rthree\n"));
check(doc.appendCapture(capture,style));
check(doc.paragraphCount()==8);
check(doc.paragraphText(4)=="one");
check(doc.paragraphText(5)=="two");
check(doc.paragraphText(6)=="three");
check(doc.paragraphText(7)=="");
capture.clear();
check(flow.importCapture(capture,style));
check(flow.sourceText()=="");
check(flow.selectionFocus()==0);
check(doc.appendCapture(capture,style));
check(doc.paragraphCount()==9);
check(doc.paragraphText(8)=="");
check(capture.insert("ok"));
check(flow.importCapture(capture,style));
check(flow.selectionFocus()==2);
check(capture.inputKey(8));
check(capture.text()=="o");
check(flow.sourceText()=="ok");
check(!capture.inputKey(-1));
check(capture.cursor()==1);
check(capture.inputKey(32));
check(capture.text()=="o ");
""",56,p)
        assert (p / "capture.txt").read_text(encoding="utf-8") == "prefix\nA\nΩ😀\n\tB"


def raster_capture_foundation():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("raster", """
RasterCapture cap;
check(cap.platform()>=0 && cap.platform()<=3);
check(cap.supported(1)==0 || cap.supported(1)==1);
check(cap.supported(2)==0 || cap.supported(2)==1);
check(!cap.supported(-1));
check(cap.deviceName(1,-1)=="");
check(cap.deviceName(2,-1)=="");
check(cap.deviceCount(0)==-1);
check(!cap.ready());
check(cap.width()==-1);
check(cap.height()==-1);
check(cap.timestamp()==-1);
[255,0,0,255,0,128,255,64] @=> int data[];
int out[8];
int wrong[7];
check(!cap.exportPixels(out));
check(!cap.ingestFrame(0,1,0,data));
check(!cap.ingestFrame(8192,8192,0,data));
check(!cap.ingestFrame(2,1,-1,data));
check(!cap.ingestFrame(2,1,0,wrong));
check(cap.ingestFrame(2,1,0.25,data));
check(cap.ready());
check(cap.width()==2);
check(cap.height()==1);
check(cap.timestamp()==0.25);
check(cap.exportPixels(out));
check(out[0]==255 && out[1]==0 && out[2]==0 && out[3]==255);
check(out[4]==0 && out[5]==128 && out[6]==255 && out[7]==64);
check(!cap.exportPixels(wrong));
256=>data[0];
check(!cap.ingestFrame(2,1,1,data));
check(cap.timestamp()==0.25);
check(cap.exportPixels(out));
check(out[0]==255);
0=>data[0];
check(cap.exportPixels(out));
check(out[0]==255);
check(cap.ingestFrame(1,2,1,data));
check(cap.width()==1 && cap.height()==2);
check(cap.timestamp()==1);
check(cap.exportPixels(out));
check(out[0]==0);
RasterCapture other;
check(!other.ready());
cap.clear();
check(!cap.ready());
check(cap.width()==-1);
check(cap.timestamp()==-1);
check(!cap.exportPixels(out));
check(cap.ingestFrame(2,1,0,data));
check(cap.ready());
""",43,p)


def raster_camera_capture():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("raster", """
RasterCapture cap;
check(cap.supported(1)==0 || cap.supported(1)==1);
check(cap.deviceName(0,0)=="");
check(cap.deviceName(1,-1)=="");
check(cap.deviceName(2,1)=="");
check(!cap.captureCamera("",2,2));
check(!cap.captureCamera("invalid",0,2));
check(!cap.captureCamera("invalid",8192,8192));
check(!cap.ready());
[1,2,3,255] @=> int data[];
int pixels[4];
check(cap.ingestFrame(1,1,0.5,data));
check(!cap.captureCamera("",1,1));
check(!cap.captureCamera("invalid",1,-1));
check(cap.ready());
check(cap.width()==1 && cap.height()==1);
check(cap.timestamp()==0.5);
check(cap.exportPixels(pixels));
check(pixels[0]==1 && pixels[1]==2 && pixels[2]==3 && pixels[3]==255);
cap.clear();
check(!cap.ready());
""",17,p)


def raster_screen_capture():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("raster", """
RasterCapture cap;
check(cap.supported(2)==0 || cap.supported(2)==1);
check(cap.deviceName(2,-1)=="");
check(cap.deviceName(2,1)=="");
check(cap.screenWidth()==-1 || cap.screenWidth()>0);
check(cap.screenHeight()==-1 || cap.screenHeight()>0);
check(!cap.captureScreen(-1,0,1,1));
check(!cap.captureScreen(0,-1,1,1));
check(!cap.captureScreen(0,0,0,1));
check(!cap.captureScreen(0,0,1,0));
check(!cap.captureScreen(0,0,8192,8192));
check(!cap.ready());
[12,34,56,78] @=> int data[];
int pixels[4];
check(cap.ingestFrame(1,1,0.25,data));
check(!cap.captureScreen(2147483647,0,1,1));
check(!cap.captureScreen(0,2147483647,1,1));
check(!cap.captureScreen(0,0,-1,1));
check(cap.ready());
check(cap.width()==1);
check(cap.height()==1);
check(cap.timestamp()==0.25);
check(cap.exportPixels(pixels));
check(pixels[0]==12 && pixels[1]==34 && pixels[2]==56 && pixels[3]==78);
cap.clear();
check(!cap.ready());
""",22,p)


def raster_capture_image():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("raster", """
RasterCapture capture;
RasterImage img;
check(!img.importCapture(capture));
check(!img.loaded());
check(img.create(1,1));
check(img.setPixel(0,0,20,40,60,80));
img.red(0.5);
check(!img.importCapture(capture));
check(img.pixelAt(0,0,0)==20);
check(img.red()==0.5);
[255,0,0,255,0,128,255,128,10,20,30,255,40,50,60,64] @=> int data[];
check(capture.ingestFrame(2,2,0.25,data));
check(img.importCapture(capture));
check(img.loaded());
check(img.width()==2);
check(img.height()==2);
check(img.red()==1);
check(img.green()==1);
check(img.blue()==1);
check(img.opacity()==1);
check(img.brightness()==0);
check(img.pixelAt(0,0,0)==255);
check(img.pixelAt(1,0,1)==128);
check(img.pixelAt(1,0,3)==128);
check(img.pixelAt(0,1,2)==30);
check(img.pixelAt(1,1,3)==64);
int pixels[16];
check(img.exportPixels(pixels));
check(pixels[0]==255 && pixels[15]==64);
check(img.setPixel(0,0,1,2,3,255));
check(capture.exportPixels(pixels));
check(pixels[0]==255);
check(capture.timestamp()==0.25);
check(img.importCapture(capture));
check(img.pixelAt(0,0,0)==255);
capture.clear();
check(img.loaded());
check(!img.importCapture(capture));
check(img.width()==2 && img.height()==2);
check(img.pixelAt(1,0,1)==128);
check(img.save("snapshot.png"));
check(!img.save("snapshot.png"));
RasterImage restored;
check(restored.load("snapshot.png"));
check(restored.width()==2 && restored.height()==2);
check(restored.pixelAt(1,0,1)==128);
check(restored.pixelAt(1,0,3)==128);
check(img.crop(1,0,1,2));
check(img.width()==1 && img.height()==2);
check(img.pixelAt(0,0,2)==255);
check(img.resize(2,4));
check(img.width()==2 && img.height()==4);
check(img.save("edited.png"));
check(capture.ingestFrame(1,1,1,[12,34,56,255]));
check(img.importCapture(capture));
check(img.width()==1 && img.height()==1);
check(img.pixelAt(0,0,0)==12);
check(restored.width()==2);
check(capture.ready());
""",52,p)
        assert (p / "snapshot.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
        assert (p / "edited.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")


def raster_capture_consolidation():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("raster", """
RasterCapture cap;
RasterImage image;
check(!cap.save("empty.png"));
[20,40,60,255,100,120,140,128,160,180,200,255,220,230,240,64] @=> int data[];
check(cap.ingestFrame(2,2,2.5,data));
check(!cap.save("invalid.xyz"));
check(cap.save("original.png"));
check(!cap.save("original.png"));
check(cap.ready());
check(cap.timestamp()==2.5);
check(image.load("original.png"));
check(image.width()==2);
check(image.height()==2);
check(image.pixelAt(0,0,0)==20);
check(image.pixelAt(1,0,1)==120);
check(image.pixelAt(1,0,3)==128);
check(image.pixelAt(0,1,2)==200);
check(image.pixelAt(1,1,3)==64);
check(image.importCapture(cap));
check(image.setPixel(0,0,255,255,255,255));
check(image.save("changed.png"));
check(cap.save("copy.png"));
RasterImage copy;
check(copy.load("copy.png"));
check(copy.pixelAt(0,0,0)==20);
check(copy.pixelAt(1,1,3)==64);
check(cap.timestamp()==2.5);
check(!cap.captureScreen(-1,0,2,2));
check(!cap.captureCamera("",2,2));
check(cap.ready());
check(cap.save("preserved.png"));
check(cap.ingestFrame(1,1,3,[1,2,3,255]));
check(cap.save("next.png"));
check(copy.load("next.png"));
check(copy.width()==1 && copy.height()==1);
check(copy.pixelAt(0,0,0)==1);
check(copy.pixelAt(0,0,1)==2);
check(copy.pixelAt(0,0,2)==3);
check(cap.timestamp()==3);
cap.clear();
check(!cap.save("cleared.png"));
check(!cap.ready());
check(image.loaded());
check(image.width()==2 && image.height()==2);
check(image.pixelAt(0,0,0)==255);
""",40,p)
        for name in ("original.png", "changed.png", "copy.png", "preserved.png", "next.png"):
            assert (p / name).read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
        for name in ("empty.png", "invalid.xyz", "cleared.png"):
            assert not (p / name).exists()

def video_camera_session():
    with tempfile.TemporaryDirectory() as directory:
        run("video", """
VideoCapture cap;
VideoCapture other;
[1,2,3,255,4,5,6,128] @=> int data[];
int out[8];
[99] @=> int wrong[];
check(!cap.active());
check(!cap.ready());
check(cap.frames()==0);
check(cap.width()==-1);
check(cap.height()==-1);
check(cap.timestamp()==-1);
check(!cap.next());
check(!cap.exportPixels(out));
check(!cap.ingestFrame(0,data));
check(cap.cameraSupported()==0 || cap.cameraSupported()==1);
check(cap.cameraName(-1)=="");
check(!cap.openCamera("",2,1,30));
check(!cap.openCamera("invalid",2,1,0));
check(!cap.openCamera("invalid",0,1,30));
check(!cap.openSynthetic(0,1));
check(!cap.openSynthetic(8192,8192));
check(cap.openSynthetic(2,1));
check(cap.active());
check(!cap.ready());
check(cap.width()==2);
check(cap.height()==1);
check(!cap.openSynthetic(1,1));
check(!cap.openCamera("",2,1,30));
check(!cap.next());
check(!cap.ingestFrame(-1,data));
check(!cap.ingestFrame(0,wrong));
check(cap.ingestFrame(0,data));
check(cap.frames()==1);
check(cap.ready());
check(cap.timestamp()==0);
check(cap.exportPixels(out));
check(out[0]==1 && out[7]==128);
check(!cap.exportPixels(wrong));
check(wrong[0]==99);
check(!other.ready());
99 => data[0];
check(cap.exportPixels(out));
check(out[0]==1);
256 => data[1];
check(!cap.ingestFrame(1,data));
check(cap.frames()==1);
2 => data[1];
check(cap.ingestFrame(1,data));
check(cap.frames()==2);
check(cap.timestamp()==1);
check(!cap.ingestFrame(0.5,data));
check(cap.timestamp()==1);
check(cap.exportPixels(out));
check(out[0]==99);
check(cap.ingestFrame(1,data));
check(cap.frames()==3);
cap.close();
check(!cap.active());
check(cap.ready());
check(cap.frames()==3);
check(!cap.ingestFrame(2,data));
check(cap.exportPixels(out));
cap.close();
check(cap.openSynthetic(1,1));
check(cap.frames()==0);
check(!cap.ready());
check(cap.timestamp()==-1);
check(cap.width()==1 && cap.height()==1);
check(cap.ingestFrame(0,[8,9,10,255]));
check(cap.frames()==1);
cap.close();
check(other.openSynthetic(2,1));
check(other.frames()==0);
check(other.ingestFrame(0,data));
check(other.frames()==1);
check(cap.frames()==1);
other.close();
""",65,pathlib.Path(directory))

def video_screen_recording():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("video", """
VideoCapture cap;
VideoClip clip;
[10,20,30,255,40,50,60,255] @=> int pixels[];
int out[8];
check(!cap.recording());
check(cap.recordedFrames()==0);
check(!cap.recordFrame());
check(!cap.finishRecording());
check(!cap.startRecording("idle.mkv",25));
check(cap.screenSupported()==0 || cap.screenSupported()==1);
check(!cap.openScreen(-1,0,2,1));
check(!cap.openScreen(0,-1,2,1));
check(!cap.openScreen(0,0,0,1));
check(!cap.openScreen(0,0,8192,8192));
check(cap.openSynthetic(2,1));
check(!cap.openScreen(0,0,2,1));
check(!cap.startRecording("bad.mp4",25));
check(!cap.startRecording("bad.mkv",0));
check(!cap.startRecording("bad.mkv",121));
check(cap.startRecording("screen.mkv",25));
check(cap.recording());
check(!cap.startRecording("other.mkv",25));
check(!cap.recordFrame());
check(cap.ingestFrame(0,pixels));
check(cap.recordFrame());
check(cap.recordedFrames()==1);
check(!cap.recordFrame());
128 => pixels[3];
check(cap.ingestFrame(0.04,pixels));
check(!cap.recordFrame());
check(cap.recording());
check(cap.recordedFrames()==1);
255 => pixels[3];
90 => pixels[0];
check(cap.ingestFrame(0.08,pixels));
check(cap.recordFrame());
check(cap.recordedFrames()==2);
cap.close();
check(!cap.active());
check(cap.recording());
check(!cap.openSynthetic(2,1));
check(cap.finishRecording());
check(!cap.recording());
check(cap.recordedFrames()==2);
check(!cap.finishRecording());
check(clip.load("screen.mkv"));
check(clip.frameIndex()==0);
check(clip.exportPixels(out));
check(out[0]==10 && out[1]==20 && out[2]==30 && out[3]==255);
check(clip.next());
check(clip.exportPixels(out));
check(out[0]==90 && out[7]==255);
check(clip.next()==0);
check(cap.openSynthetic(2,1));
check(cap.recordedFrames()==0);
check(!cap.startRecording("screen.mkv",25));
check(cap.startRecording("abort.mkv",25));
check(cap.ingestFrame(0,pixels));
check(cap.recordFrame());
cap.abortRecording();
check(!cap.recording());
check(!cap.finishRecording());
check(cap.startRecording("empty.mkv",25));
check(!cap.finishRecording());
check(!cap.recording());
cap.close();
""",56,p)
        assert (p / "screen.mkv").is_file()
        for name in ("idle.mkv", "bad.mp4", "bad.mkv", "other.mkv", "abort.mkv", "empty.mkv"):
            assert not (p / name).exists()

def video_audio_capture():
    with tempfile.TemporaryDirectory() as directory:
        run("video", """
VideoCapture cap;
VideoCapture other;
[0.25,-0.5,1.0,-1.0] @=> float block[];
float out[4];
[77.0] @=> float odd[];
[0.0,2.0] @=> float bad[];
check(!cap.audioActive());
check(cap.audioFrames()==0);
check(cap.audioSampleRate()==0);
check(cap.audioChannels()==0);
check(cap.audioStart()==-1);
check(cap.audioEnd()==-1);
check(!cap.openAudio(8000,2));
check(!cap.ingestAudio(0,block));
check(!cap.readAudio(0,out));
check(cap.openSynthetic(1,1));
check(!cap.openAudio(7999,2));
check(!cap.openAudio(192001,2));
check(!cap.openAudio(8000,0));
check(!cap.openAudio(8000,9));
check(cap.openAudio(8000,2));
check(cap.audioActive());
check(cap.audioSampleRate()==8000);
check(cap.audioChannels()==2);
check(!cap.openAudio(16000,1));
check(!cap.ingestAudio(-1,block));
check(!cap.ingestAudio(86401,block));
check(!cap.ingestAudio(0,odd));
check(!cap.ingestAudio(0,bad));
check(cap.audioFrames()==0);
check(cap.ingestAudio(0.5,block));
check(cap.audioFrames()==2);
check(cap.audioStart()==0.5);
check(Math.fabs(cap.audioEnd()-0.50025)<0.0000001);
check(cap.readAudio(0,out));
check(out[0]==0.25 && out[1]==-0.5 && out[2]==1.0 && out[3]==-1.0);
check(!cap.readAudio(-1,out));
check(!cap.readAudio(1,out));
check(!cap.readAudio(0,odd));
check(odd[0]==77.0);
check(!cap.ingestAudio(0.5,block));
check(!cap.ingestAudio(0.501,block));
check(!cap.ingestAudio(0.50025,bad));
check(cap.audioFrames()==2);
0.75 => block[0];
check(cap.ingestAudio(0.50025,block));
check(cap.audioFrames()==4);
check(cap.readAudio(2,out));
check(out[0]==0.75);
check(cap.readAudio(0,out));
check(out[0]==0.25);
check(cap.ingestFrame(0.5,[1,2,3,255]));
check(cap.timestamp()==cap.audioStart());
check(other.audioFrames()==0);
cap.closeAudio();
check(!cap.audioActive());
check(!cap.ingestAudio(0.5005,block));
check(cap.readAudio(2,out));
check(!cap.openAudio(8000,2));
cap.close();
check(cap.audioFrames()==4);
check(cap.openSynthetic(1,1));
check(cap.audioFrames()==0);
check(cap.audioStart()==-1);
check(cap.openAudio(48000,1));
check(cap.ingestAudio(0,[0.0,0.5]));
check(cap.audioFrames()==2);
cap.close();
check(!cap.audioActive());
check(cap.audioFrames()==2);
""",60,pathlib.Path(directory))

def video_av_recording():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("video", """
VideoCapture cap;
VideoClip clip;
VideoAudio audio;
[10,20,30,255,40,50,60,255] @=> int pixels[];
int out[8];
float block[640];
float readback[640];
check(!cap.startRecordingAV("idle.mkv",25,0));
check(!cap.recordAV());
check(cap.recordedAudioFrames()==0);
check(cap.nextRecordingAudioFrames()==0);
check(cap.openSynthetic(2,1));
check(!cap.startRecordingAV("noaudio.mkv",25,0));
check(cap.openAudio(8000,2));
check(!cap.startRecordingAV("bad.mkv",0,0));
check(!cap.startRecordingAV("bad.mkv",25,-1));
check(!cap.startRecordingAV("bad.mp4",25,0));
check(cap.startRecordingAV("av.mkv",25,0.5));
check(cap.recording());
check(cap.nextRecordingAudioFrames()==320);
check(!cap.startRecordingAV("other.mkv",25,0.5));
check(!cap.recordAV());
check(cap.ingestFrame(0.5,pixels));
check(!cap.recordFrame());
check(!cap.recordAV());
check(cap.recordedFrames()==0);
check(cap.recordedAudioFrames()==0);
for(0=>int i;i<640;i++) 0.25=>block[i];
check(cap.ingestAudio(0.5,block));
check(cap.recordAV());
check(cap.recordedFrames()==1);
check(cap.recordedAudioFrames()==320);
check(!cap.recordAV());
check(cap.nextRecordingAudioFrames()==320);
90=>pixels[0];
check(cap.ingestFrame(0.51,pixels));
check(!cap.recordAV());
for(0=>int i;i<640;i++) -0.5=>block[i];
check(cap.ingestAudio(0.54,block));
check(cap.ingestFrame(0.54,pixels));
check(cap.recordAV());
check(cap.recordedFrames()==2);
check(cap.recordedAudioFrames()==640);
cap.close();
check(cap.recording());
check(cap.finishRecording());
check(!cap.recording());
check(cap.recordedAudioFrames()==640);
check(cap.nextRecordingAudioFrames()==0);
check(clip.load("av.mkv"));
check(clip.exportPixels(out));
check(out[0]==10 && out[3]==255);
check(clip.next()==1);
check(clip.exportPixels(out));
check(out[0]==90);
check(clip.next()==0);
check(audio.load("av.mkv",0));
check(audio.sampleRate()==8000);
check(audio.channels()==2);
check(audio.frames()==640);
check(audio.readSamples(0,readback));
check(Math.fabs(readback[0]-0.25)<0.0001);
check(audio.readSamples(320,readback));
check(Math.fabs(readback[639]+0.5)<0.0001);
check(cap.openSynthetic(2,1));
check(cap.recordedAudioFrames()==0);
check(cap.openAudio(8000,2));
check(!cap.startRecordingAV("av.mkv",25,0));
check(cap.startRecordingAV("abort.mkv",25,0));
cap.abortRecording();
check(!cap.recording());
check(!cap.finishRecording());
cap.close();
""",60,p)
        assert (p / "av.mkv").is_file()
        for name in ("idle.mkv", "noaudio.mkv", "bad.mkv", "bad.mp4", "other.mkv", "abort.mkv"):
            assert not (p / name).exists()

def video_capture_consolidation():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("video", """
VideoCapture cap;
VideoClip clip;
VideoAudio audio;
int pixel[4];
[1,2,3,255] @=> int data[];
float samples[320];
float small[2];
check(cap.sourceMode()==0);
check(cap.clear());
check(!cap.discardAudio(0));
check(cap.openSynthetic(1,1));
check(cap.sourceMode()==2);
check(!cap.clear());
check(cap.openAudio(8000,1));
check(cap.discardAudio(0));
check(!cap.discardAudio(-1));
check(!cap.discardAudio(1));
check(cap.startRecordingAV("rolling.mkv",25,0));
check(cap.ingestAudio(0,samples));
check(!cap.discardAudio(1));
check(cap.ingestFrame(0,data));
check(cap.recordAV());
check(cap.recordedAudioFrames()==320);
check(cap.discardAudio(318));
check(cap.audioFrames()==2);
check(cap.readAudio(0,small));
check(cap.discardAudio(2));
check(cap.audioFrames()==0);
check(cap.audioStart()==-1);
check(!cap.ingestAudio(0,samples));
check(!cap.ingestAudio(0.08,samples));
for(0=>int i;i<320;i++) 0.5=>samples[i];
check(cap.ingestAudio(0.04,samples));
check(Math.fabs(cap.audioStart()-0.04)<0.000001);
check(cap.audioFrames()==320);
check(!cap.discardAudio(1));
9=>data[0];
check(cap.ingestFrame(0.04,data));
check(cap.recordAV());
check(cap.recordedFrames()==2);
check(cap.recordedAudioFrames()==640);
check(cap.discardAudio(320));
check(cap.audioFrames()==0);
cap.close();
check(cap.sourceMode()==0);
check(!cap.audioActive());
check(cap.ready());
check(!cap.clear());
check(cap.finishRecording());
check(cap.recordedFrames()==2);
check(cap.recordedAudioFrames()==640);
check(cap.clear());
check(!cap.ready());
check(cap.frames()==0);
check(cap.recordedFrames()==0);
check(cap.recordedAudioFrames()==0);
check(cap.audioSampleRate()==0);
check(cap.width()==-1);
check(cap.timestamp()==-1);
check(!cap.exportPixels(pixel));
check(clip.load("rolling.mkv"));
check(clip.exportPixels(pixel));
check(pixel[0]==1);
check(clip.next()==1);
check(clip.exportPixels(pixel));
check(pixel[0]==9);
check(clip.next()==0);
check(audio.load("rolling.mkv",0));
check(audio.frames()==640);
check(audio.readSamples(0,small));
check(small[0]==0.0);
check(audio.readSamples(320,small));
check(Math.fabs(small[0]-0.5)<0.0001);
check(cap.openSynthetic(1,1));
check(cap.openAudio(8000,1));
check(cap.ingestAudio(0,samples));
check(cap.discardAudio(320));
check(!cap.ingestAudio(0,samples));
check(cap.ingestAudio(0.04,samples));
cap.close();
check(cap.clear());
check(cap.sourceMode()==0);
""",71,p)
        assert (p / "rolling.mkv").is_file()

def video_opengl_framebuffer():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("video", """
VideoCapture cap;
VideoClip clip;
[1,2,3,4,5,6,7,8,11,12,13,14,15,16,17,18] @=> int data[];
int out[16];
int wrong[4];
check(cap.openGLSupported()==0 || cap.openGLSupported()==1);
check(!cap.openGLCurrent());
check(!cap.openGL(2,2));
check(!cap.openGL(0,2));
check(!cap.readGL(0,0,0));
check(!cap.ingestGLPixels(0,data));
check(cap.openSynthetic(2,2));
check(!cap.openGL(2,2));
check(!cap.readGL(0,0,0));
check(!cap.ingestGLPixels(-1,data));
check(!cap.ingestGLPixels(0,wrong));
check(cap.ingestGLPixels(0,data));
check(cap.sourceMode()==2);
check(cap.ready());
check(cap.frames()==1);
check(cap.timestamp()==0);
check(cap.exportPixels(out));
check(out[0]==11 && out[1]==12 && out[2]==13 && out[3]==255);
check(out[4]==15 && out[5]==16 && out[6]==17 && out[7]==255);
check(out[8]==1 && out[9]==2 && out[10]==3 && out[11]==255);
check(out[12]==5 && out[13]==6 && out[14]==7 && out[15]==255);
check(data[3]==4 && data[15]==18);
256=>data[15];
check(!cap.ingestGLPixels(1,data));
check(cap.frames()==1);
18=>data[15];
check(cap.startRecording("graphics.mkv",25));
check(cap.recordFrame());
check(!cap.recordFrame());
99=>data[8];
check(cap.ingestGLPixels(0.04,data));
check(!cap.ingestGLPixels(0.01,data));
check(cap.recordFrame());
check(cap.recordedFrames()==2);
cap.close();
check(cap.finishRecording());
check(clip.load("graphics.mkv"));
check(clip.exportPixels(out));
check(out[0]==11 && out[8]==1);
check(clip.next()==1);
check(clip.exportPixels(out));
check(out[0]==99 && out[8]==1);
check(clip.next()==0);
check(cap.clear());
check(cap.openSynthetic(1,3));
check(cap.ingestGLPixels(0,[1,2,3,0,4,5,6,0,7,8,9,0]));
int odd[12];
check(cap.exportPixels(odd));
check(odd[0]==7 && odd[4]==4 && odd[8]==1);
check(odd[3]==255 && odd[7]==255 && odd[11]==255);
cap.close();
check(cap.clear());
check(cap.openSynthetic(1,1));
check(cap.ingestGLPixels(0,[3,4,5,0]));
check(cap.exportPixels(wrong));
check(wrong[0]==3 && wrong[3]==255);
cap.close();
""",50,p)
        assert (p / "graphics.mkv").is_file()

def video_chugl_texture():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("video", """
VideoCapture cap;
VideoClip clip;
[1.0,0.0,0.5,0.0,0.0,1.0,0.0,0.5,0.0,0.0,1.0,1.0,0.25,0.75,0.0,1.0] @=> float data[];
int out[16];
float wrong[4];
check(!cap.ingestTexture(0,data));
check(!cap.openTexture(0,2));
check(!cap.openTexture(8192,8192));
check(cap.openTexture(2,2));
check(cap.sourceMode()==5);
check(cap.active());
check(!cap.ready());
check(!cap.openTexture(2,2));
check(!cap.openSynthetic(2,2));
check(!cap.next());
check(!cap.readGL(0,0,0));
check(!cap.ingestTexture(-1,data));
check(!cap.ingestTexture(0,wrong));
check(cap.ingestTexture(0,data));
check(cap.frames()==1);
check(cap.timestamp()==0);
check(cap.exportPixels(out));
check(out[0]==255 && out[1]==0 && out[2]==128 && out[3]==255);
check(out[4]==0 && out[5]==255 && out[6]==0 && out[7]==255);
check(out[8]==0 && out[9]==0 && out[10]==255 && out[11]==255);
check(out[12]==64 && out[13]==191 && out[14]==0 && out[15]==255);
check(data[3]==0.0 && data[7]==0.5);
1.1=>data[15];
check(!cap.ingestTexture(1,data));
check(cap.frames()==1);
1.0=>data[15];
-0.1=>data[0];
check(!cap.ingestTexture(1,data));
1.0=>data[0];
check(cap.startRecording("texture.mkv",25));
check(cap.recordFrame());
check(!cap.recordFrame());
0.0=>data[0];
check(cap.ingestTexture(0.04,data));
check(!cap.ingestTexture(0.01,data));
check(cap.recordFrame());
check(cap.recordedFrames()==2);
cap.close();
check(!cap.active());
check(cap.ready());
check(cap.finishRecording());
check(clip.load("texture.mkv"));
check(clip.exportPixels(out));
check(out[0]==255 && out[8]==0 && out[10]==255);
check(clip.next()==1);
check(clip.exportPixels(out));
check(out[0]==0 && out[2]==128);
check(clip.next()==0);
check(cap.clear());
check(cap.openSynthetic(2,2));
check(!cap.ingestTexture(0,data));
cap.close();
check(cap.openTexture(2,2));
check(cap.frames()==0);
check(cap.openAudio(8000,1));
check(cap.ingestAudio(0,[0.0,0.5]));
check(cap.audioFrames()==2);
check(cap.ingestTexture(0,data));
check(cap.timestamp()==cap.audioStart());
cap.close();
check(cap.clear());
""",53,p)
        assert (p / "texture.mkv").is_file()

def video_offscreen_render():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("video", """
VideoCapture cap;
VideoClip clip;
int out[24];
check(cap.offscreenSupported()==0 || cap.offscreenSupported()==1);
check(!cap.openOffscreen(0,2));
check(!cap.openOffscreen(8192,8192));
check(!cap.renderOffscreen(0,1,2,3));
check(cap.openSynthetic(3,2));
check(!cap.openOffscreen(3,2));
check(!cap.renderOffscreen(0,1,2,3));
cap.close();
check(cap.clear());
if(cap.offscreenSupported()) {
    check(cap.openOffscreen(3,2));
    check(cap.sourceMode()==6);
    check(!cap.ready());
    check(!cap.next());
    check(!cap.renderOffscreen(-1,1,2,3));
    check(!cap.renderOffscreen(0,-1,2,3));
    check(!cap.renderOffscreen(0,1,256,3));
    check(cap.renderOffscreen(0,10,20,30));
    check(cap.frames()==1);
    check(cap.exportPixels(out));
    check(out[0]==10 && out[1]==20 && out[2]==30 && out[3]==255);
    check(out[20]==10 && out[23]==255);
    check(cap.startRecording("offscreen.mkv",25));
    check(cap.recordFrame());
    check(cap.renderOffscreen(0.04,90,80,70));
    check(!cap.renderOffscreen(0.01,1,2,3));
    check(cap.recordFrame());
    cap.close();
    check(cap.finishRecording());
    check(clip.load("offscreen.mkv"));
    check(clip.exportPixels(out));
    check(out[0]==10);
    check(clip.next()==1);
    check(clip.exportPixels(out));
    check(out[0]==90 && out[1]==80 && out[2]==70);
    check(clip.next()==0);
    check(cap.clear());
} else {
    check(!cap.openOffscreen(3,2));
    check(!cap.active());
    check(!cap.ready());
    check(!cap.renderOffscreen(0,1,2,3));
    check(cap.sourceMode()==0);
    check(cap.openTexture(3,2));
    check(cap.sourceMode()==5);
    check(!cap.openOffscreen(3,2));
    float pixels[24];
    for(0=>int i;i<24;i++) 0.5=>pixels[i];
    check(cap.ingestTexture(0,pixels));
    check(cap.exportPixels(out));
    check(out[0]==128 && out[3]==255);
    check(cap.startRecording("offscreen.mkv",25));
    check(cap.recordFrame());
    1.0=>pixels[0];
    check(cap.ingestTexture(0.04,pixels));
    check(!cap.ingestTexture(0.01,pixels));
    check(cap.recordFrame());
    cap.close();
    check(cap.finishRecording());
    check(clip.load("offscreen.mkv"));
    check(clip.exportPixels(out));
    check(out[0]==128);
    check(clip.next()==1);
    check(clip.exportPixels(out));
    check(out[0]==255);
    check(clip.next()==0);
    check(cap.clear());
    check(!cap.ready());
}
""",34,p)
        assert (p / "offscreen.mkv").is_file()

def video_indexed_av():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("video", """
VideoCapture cap;
VideoClip clip;
VideoAudio audio;
[1.0,0.0,0.0,1.0] @=> float pixels[];
int out[4];
float a[267];
float b[266];
float sample[1];
check(!cap.indexedRecording());
check(cap.nextRecordingIndex()==-1);
check(cap.nextRecordingAudioStart()==-1);
check(!cap.startRecordingIndexed("idle.mkv",30,0));
check(!cap.ingestAudioAt(0,a));
check(cap.openTexture(1,1));
check(!cap.startRecordingIndexed("noaudio.mkv",30,0));
check(cap.openAudio(8000,1));
check(!cap.startRecordingIndexed("bad.mkv",0,0));
check(!cap.startRecordingIndexed("bad.mkv",30,-1));
check(!cap.startRecordingIndexed("bad.mp4",30,0));
check(cap.ingestTexture(1,pixels));
check(cap.startRecordingIndexed("indexed.mkv",30,100));
check(cap.indexedRecording());
check(cap.nextRecordingIndex()==0);
check(cap.nextRecordingAudioStart()==100);
check(cap.nextRecordingAudioFrames()==267);
check(!cap.recordIndexed(0));
check(!cap.ingestAudioAt(-1,a));
for(0=>int i;i<267;i++) 0.25=>a[i];
check(cap.ingestAudioAt(100,a));
check(!cap.ingestAudioAt(100,a));
check(!cap.ingestAudioAt(368,b));
check(!cap.discardAudio(1));
check(cap.ingestTexture(10,pixels));
check(!cap.recordAV());
check(!cap.recordFrame());
check(!cap.recordIndexed(1));
check(cap.recordIndexed(0));
check(!cap.recordIndexed(0));
check(cap.recordedFrames()==1);
check(cap.recordedAudioFrames()==267);
check(cap.nextRecordingIndex()==1);
check(cap.nextRecordingAudioStart()==367);
check(cap.nextRecordingAudioFrames()==266);
check(cap.discardAudio(267));
check(!cap.ingestAudioAt(366,b));
for(0=>int i;i<266;i++) -0.5=>b[i];
check(cap.ingestAudioAt(367,b));
0.0=>pixels[0];1.0=>pixels[1];
check(cap.ingestTexture(100,pixels));
check(cap.recordIndexed(1));
check(cap.recordedAudioFrames()==533);
check(cap.nextRecordingAudioStart()==633);
check(cap.nextRecordingAudioFrames()==267);
check(cap.discardAudio(266));
check(cap.ingestTexture(200,pixels));
check(!cap.recordIndexed(2));
check(cap.ingestAudioAt(633,a));
check(cap.recordIndexed(2));
check(cap.recordedAudioFrames()==800);
check(cap.nextRecordingIndex()==3);
cap.close();
check(cap.finishRecording());
check(!cap.indexedRecording());
check(cap.nextRecordingIndex()==-1);
check(cap.recordedAudioFrames()==800);
check(clip.load("indexed.mkv"));
check(clip.exportPixels(out));
check(out[0]==255 && out[1]==0);
check(clip.next()==1);
check(clip.exportPixels(out));
check(out[0]==0 && out[1]==255);
check(clip.next()==1);
check(clip.next()==0);
check(audio.load("indexed.mkv",0));
check(audio.frames()==800);
check(audio.sampleRate()==8000);
check(audio.readSamples(266,sample));
check(Math.fabs(sample[0]-0.25)<0.0001);
check(audio.readSamples(267,sample));
check(Math.fabs(sample[0]+0.5)<0.0001);
check(audio.readSamples(533,sample));
check(Math.fabs(sample[0]-0.25)<0.0001);
check(cap.clear());
check(cap.openSynthetic(1,1));
check(cap.openAudio(8000,1));
check(cap.startRecordingIndexed("skip.mkv",30,0));
check(cap.ingestAudioAt(0,a));
check(cap.ingestFrame(0,[1,2,3,255]));
check(cap.ingestFrame(1,[4,5,6,255]));
check(!cap.recordIndexed(0));
check(cap.recordedFrames()==0);
cap.abortRecording();
check(!cap.indexedRecording());
cap.close();
""",80,p)
        assert (p / "indexed.mkv").is_file()
        for name in ("idle.mkv","noaudio.mkv","bad.mkv","bad.mp4","skip.mkv"):
            assert not (p / name).exists()

def video_graphics_consolidation():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("video", """
VideoCapture cap;
VideoClip clip;
VideoAudio audio;
int out[8];
float block[320];
check(!cap.indexedPending());
check(!cap.indexedReady());
check(!cap.indexedLost());
check(cap.openSynthetic(2,1));
check(cap.openAudio(8000,1));
check(cap.startRecordingIndexed("graphics-av.mkv",25,0));
check(!cap.indexedPending());
check(!cap.indexedReady());
check(cap.ingestGLPixels(0,[10,20,30,0,40,50,60,128]));
check(cap.indexedPending());
check(!cap.indexedReady());
check(!cap.indexedLost());
check(!cap.recordIndexed(0));
for(0=>int i;i<320;i++) 0.25=>block[i];
check(cap.ingestAudioAt(0,block));
check(cap.indexedReady());
check(cap.recordIndexed(0));
check(!cap.indexedPending());
check(!cap.indexedReady());
check(cap.discardAudio(320));
check(cap.ingestFrame(100,[90,80,70,255,1,2,3,255]));
check(cap.indexedPending());
check(!cap.indexedReady());
for(0=>int i;i<320;i++) -0.25=>block[i];
check(cap.ingestAudioAt(320,block));
check(cap.indexedReady());
check(cap.recordIndexed(1));
check(cap.recordedFrames()==2);
check(cap.recordedAudioFrames()==640);
cap.close();
check(!cap.indexedPending());
check(cap.finishRecording());
check(!cap.indexedReady());
check(!cap.indexedLost());
check(clip.load("graphics-av.mkv"));
check(clip.exportPixels(out));
check(out[0]==10 && out[3]==255 && out[7]==255);
check(clip.next()==1);
check(clip.exportPixels(out));
check(out[0]==90 && out[4]==1);
check(clip.next()==0);
check(audio.load("graphics-av.mkv",0));
check(audio.frames()==640);
check(audio.readSamples(320,block));
check(Math.fabs(block[0]+0.25)<0.0001);
check(cap.clear());
check(cap.openTexture(2,1));
check(cap.openAudio(8000,1));
check(cap.startRecordingIndexed("lost.mkv",25,0));
check(cap.ingestAudioAt(0,block));
check(cap.ingestTexture(0,[1.0,0.0,0.0,1.0,0.0,1.0,0.0,1.0]));
check(cap.indexedReady());
check(cap.ingestTexture(1,[0.0,0.0,1.0,1.0,1.0,1.0,1.0,1.0]));
check(cap.indexedLost());
check(!cap.indexedPending());
check(!cap.indexedReady());
check(!cap.recordIndexed(0));
cap.abortRecording();
check(!cap.indexedLost());
check(!cap.indexedPending());
check(!cap.indexedReady());
cap.close();
check(cap.clear());
check(cap.openSynthetic(2,1));
check(cap.openAudio(8000,1));
check(cap.startRecordingIndexed("alpha.mkv",25,0));
check(cap.ingestAudioAt(0,block));
check(cap.ingestFrame(0,[1,2,3,0,4,5,6,255]));
check(cap.indexedPending());
check(!cap.indexedReady());
check(!cap.indexedLost());
check(!cap.recordIndexed(0));
cap.abortRecording();
cap.close();
check(cap.clear());
""",68,p)
        assert (p / "graphics-av.mkv").is_file()
        for name in ("lost.mkv", "alpha.mkv"):
            assert not (p / name).exists()

def transport_framing():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
check(MediaTransport.headerSize()==32);
check(MediaTransport.maxPayload()==1048576);
int packet[36];
int meta[4];
int out[4];
[0,127,128,255] @=> int data[];
for(1=>int t;t<=7;t++) {
    check(MediaTransport.encode(t,42,9,1234567,data,packet));
    check(MediaTransport.decode(packet,meta,out));
    check(meta[0]==t && meta[1]==42 && meta[2]==9 && meta[3]==1234567);
    check(out[0]==0 && out[1]==127 && out[2]==128 && out[3]==255);
}
check(packet[0]==67 && packet[1]==75 && packet[2]==77 && packet[3]==80);
check(packet[4]==1 && packet[5]==7 && packet[6]==0 && packet[7]==0);
check(packet[8]==0 && packet[11]==42 && packet[15]==9 && packet[27]==4);
check(!MediaTransport.encode(0,42,9,0,data,packet));
check(!MediaTransport.encode(8,42,9,0,data,packet));
check(!MediaTransport.encode(7,0,9,0,data,packet));
check(!MediaTransport.encode(7,-1,9,0,data,packet));
check(!MediaTransport.encode(7,42,-1,0,data,packet));
check(!MediaTransport.encode(7,42,9,-1,data,packet));
check(!MediaTransport.encode(7,42,9,0,[-1,0,0,0],packet));
check(!MediaTransport.encode(7,42,9,0,[256,0,0,0],packet));
check(!MediaTransport.encode(7,42,9,0,[1],packet));
check(MediaTransport.decode(packet,meta,out));
check(meta[3]==1234567 && out[3]==255);
for(0=>int i;i<packet.size();i++) {
    packet[i]=>int old;
    (old+1)%256=>packet[i];
    check(!MediaTransport.decode(packet,meta,out));
    old=>packet[i];
}
check(meta[0]==7 && meta[3]==1234567 && out[3]==255);
check(!MediaTransport.decode(packet,meta,meta));
check(!MediaTransport.decode(packet,[0,0,0],out));
check(!MediaTransport.decode(packet,meta,[0]));
check(!MediaTransport.decode([1,2,3],meta,out));
int empty[0];
int emptyPacket[32];
check(MediaTransport.encode(1,1,0,0,empty,emptyPacket));
check(MediaTransport.decode(emptyPacket,meta,empty));
check(meta[0]==1 && meta[1]==1 && meta[2]==0 && meta[3]==0);
int huge[1048577];
check(!MediaTransport.encode(1,1,0,0,huge,packet));
int limit[1048576];
int limitPacket[1048608];
int limitOut[1048576];
255=>limit[1048575];
check(MediaTransport.encode(6,1,0,0,limit,limitPacket));
check(MediaTransport.decode(limitPacket,meta,limitOut));
check(limitOut[0]==0 && limitOut[1048575]==255);
""",92,p)


def transport_loopback():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
MediaLoopback server;
MediaLoopback client;
int packet[36];
int reply[35];
int got[36];
int back[35];
int meta[4];
int payload[4];
int response[3];
check(!server.connected());
check(server.port()==0);
check(server.poll()==-1);
check(server.accept()==-1);
check(!server.listen(-1));
check(!server.listen(65536));
check(!client.connect(0));
check(!client.connect(65536));
check(server.listen(0));
check(server.port()>0 && server.port()<=65535);
check(!server.listen(0));
check(server.accept()==0);
check(client.connect(server.port()));
check(client.connected());
check(!client.connect(server.port()));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
check(server.connected());
check(server.accept()==-1);
check(!client.sendPending());
check(client.packetSize()==0);
check(!client.receivePacket(got));
check(!client.sendPacket([1,2,3]));
check(MediaTransport.encode(7,1,2,99,[0,127,128,255],packet));
check(MediaTransport.encode(5,2,3,100,[144,60,127],reply));
check(client.sendPacket(packet));
check(client.sendPending());
check(!client.sendPacket(packet));
check(server.sendPacket(reply));
for(0=>int i;i<100000 && (!server.packetSize() || !client.packetSize());i++) {
    client.poll();
    server.poll();
}
check(server.packetSize()==36);
check(client.packetSize()==35);
check(!client.sendPending());
check(!server.sendPending());
check(!server.receivePacket([0]));
check(server.packetSize()==36);
check(server.receivePacket(got));
check(server.packetSize()==0);
check(client.receivePacket(back));
check(client.packetSize()==0);
check(MediaTransport.decode(got,meta,payload));
check(meta[0]==7 && meta[1]==1 && meta[2]==2 && meta[3]==99);
check(payload[0]==0 && payload[3]==255);
check(MediaTransport.decode(back,meta,response));
check(meta[0]==5 && meta[1]==2 && meta[2]==3 && meta[3]==100);
check(response[0]==144 && response[2]==127);
int large[131072];
int wire[131104];
int received[131104];
int decoded[131072];
42=>large[0];
255=>large[131071];
check(MediaTransport.encode(6,3,4,101,large,wire));
check(client.sendPacket(wire));
for(0=>int i;i<100000 && !server.packetSize();i++) {
    client.poll();
    server.poll();
}
check(server.packetSize()==131104);
check(server.receivePacket(received));
check(MediaTransport.decode(received,meta,decoded));
check(decoded[0]==42 && decoded[131071]==255 && decoded[65536]==0);
check(meta[0]==6 && meta[2]==4);
client.disconnect();
check(!client.connected());
check(!client.sendPacket(packet));
for(0=>int i;i<100000 && server.connected();i++) server.poll();
check(!server.connected());
check(server.port()>0);
check(client.connect(server.port()));
0=>accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
check(client.sendPacket(packet));
for(0=>int i;i<100000 && !server.packetSize();i++) {
    client.poll();
    server.poll();
}
check(server.receivePacket(got));
check(MediaTransport.decode(got,meta,payload));
check(meta[2]==2 && payload[3]==255);
server.close();
client.close();
check(!server.connected() && server.port()==0);
check(!client.connected());
check(server.packetSize()==0 && !server.sendPending());
check(server.listen(0));
server.close();
check(server.port()==0);
""",66,p)


def transport_session():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
MediaSession server;
MediaSession client;
int packet[36];
int got[36];
int meta[4];
int data[4];
check(server.state()==0);
check(server.error()==0);
check(server.mediaMask()==0);
check(server.payloadLimit()==0);
check(!server.connected());
check(!server.configure(0,100));
check(!server.configure(128,100));
check(!server.configure(1,0));
check(!server.configure(1,1048577));
check(server.configure(80,64));
check(client.configure(96,32));
check(server.listen(0));
check(server.port()>0);
check(client.connect(server.port()));
check(client.state()==1);
check(!client.connected());
check(!client.configure(127,100));
check(MediaTransport.encode(7,1,2,99,[1,2,3,4],packet));
check(!client.sendPacket(packet));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
check(server.state()==1);
check(!server.connected());
for(0=>int i;i<100000 && (!server.connected() || !client.connected());i++) {
    client.poll();
    server.poll();
}
check(server.state()==2);
check(client.state()==2);
check(server.mediaMask()==64);
check(client.mediaMask()==64);
check(server.payloadLimit()==32);
check(client.payloadLimit()==32);
check(server.error()==0 && client.error()==0);
check(server.packetSize()==0 && client.packetSize()==0);
check(MediaTransport.encode(5,1,2,99,[1,2,3,4],packet));
check(!client.sendPacket(packet));
int oversized[33];
int bigPacket[65];
check(MediaTransport.encode(7,1,2,99,oversized,bigPacket));
check(!client.sendPacket(bigPacket));
check(MediaTransport.encode(7,1,2,99,[1,2,3,4],packet));
check(client.sendPacket(packet));
check(server.sendPacket(packet));
for(0=>int i;i<100000 && (!server.packetSize() || !client.packetSize());i++) {
    client.poll();
    server.poll();
}
check(server.packetSize()==36);
check(client.packetSize()==36);
check(server.receivePacket(got));
check(MediaTransport.decode(got,meta,data));
check(meta[0]==7 && meta[2]==2 && data[3]==4);
check(client.receivePacket(got));
check(client.packetSize()==0);
client.disconnect();
check(client.state()==0);
check(client.mediaMask()==0 && client.payloadLimit()==0);
for(0=>int i;i<100000 && server.connected();i++) server.poll();
check(server.state()==3);
check(server.error()==1);
check(server.port()>0);
check(client.configure(1,100));
check(client.connect(server.port()));
0=>accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (server.state()==1 || client.state()==1);i++) {
    client.poll();
    server.poll();
}
check(server.state()==3);
check(client.state()==3);
check(server.error()==3 || client.error()==3);
check(!server.connected() && !client.connected());
check(server.mediaMask()==0 && client.mediaMask()==0);
client.close();
check(client.state()==0 && client.error()==0);
MediaLoopback raw;
check(raw.connect(server.port()));
0=>accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
check(raw.sendPacket(packet));
for(0=>int i;i<100000 && server.state()==1;i++) {
    raw.poll();
    server.poll();
}
check(server.state()==3);
check(server.error()==2);
check(server.packetSize()==0);
raw.close();
server.close();
check(server.state()==0 && server.error()==0);
check(server.port()==0);
""",66,p)


def transport_endpoints():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
MediaLoopback server;
MediaLoopback client;
check(!server.remoteEnabled());
check(!client.remoteEnabled());
check(server.peerAddress()=="");
check(!server.allowRemote(2));
check(!server.listenAt("0.0.0.0",0,"127.0.0.1"));
check(!server.listenAt("localhost",0,"127.0.0.1"));
check(!server.listenAt("127.0.0.1",0,""));
check(!server.listenAt("127.0.0.1",0,"192.0.2.1"));
check(!client.connectAt("192.0.2.1",12345,10));
check(!client.connectAt("127.0.0.1",0,10));
check(!client.connectAt("127.0.0.1",12345,0));
check(!client.connectAt("127.0.0.1",12345,5001));
check(!client.connectAt("127.0.0.1",65536,10));
check(!client.connectAt("127.0.0.256",12345,10));
check(server.allowRemote(1));
check(server.remoteEnabled());
check(!server.listenAt("0.0.0.0",0,"127.0.0.1"));
check(!server.listenAt("127.0.0.1",0,"255.255.255.255"));
check(!server.listenAt("127.0.0.1",0,"224.0.0.1"));
check(!server.listenAt("127.0.0.1",0,"127.0.0.255"));
check(server.listenAt("127.0.0.1",0,"192.0.2.1"));
check(!server.allowRemote(0));
check(client.connectAt("127.0.0.1",server.port(),1000));
check(client.peerAddress()=="127.0.0.1");
check(!client.allowRemote(1));
for(0=>int i;i<100000 && client.connected();i++) {
    server.accept();
    client.poll();
}
check(!server.connected());
check(!client.connected());
check(client.peerAddress()=="");
check(server.peerAddress()=="");
server.close();
check(server.allowRemote(0));
check(!server.remoteEnabled());
check(server.listenAt("127.0.0.1",0,"127.0.0.1"));
check(client.connectAt("127.0.0.1",server.port(),1000));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
check(server.peerAddress()=="127.0.0.1");
check(client.peerAddress()=="127.0.0.1");
int wire[34];
int got[34];
check(MediaTransport.encode(7,1,0,0,[1,2],wire));
check(client.sendPacket(wire));
for(0=>int i;i<100000 && !server.packetSize();i++) {
    client.poll();
    server.poll();
}
check(server.receivePacket(got));
check(got[32]==1 && got[33]==2);
server.close();
client.close();
check(server.peerAddress()=="" && client.peerAddress()=="");
MediaSession s;
MediaSession c;
check(!s.remoteEnabled());
check(!s.listenAt("127.0.0.1",0,"192.0.2.1"));
check(s.allowRemote(1));
check(s.remoteEnabled());
check(s.listenAt("127.0.0.1",0,"127.0.0.1"));
check(c.connectAt("127.0.0.1",s.port(),1000));
0=>accepted;
for(0=>int i;i<100000 && !accepted;i++) s.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (!s.connected()||!c.connected());i++) {
    c.poll();
    s.poll();
}
check(s.connected() && c.connected());
check(s.peerAddress()=="127.0.0.1");
check(c.peerAddress()=="127.0.0.1");
check(!s.allowRemote(0));
check(c.sendPacket(wire));
for(0=>int i;i<100000 && !s.packetSize();i++) {
    c.poll();
    s.poll();
}
check(s.receivePacket(got));
check(got[32]==1 && got[33]==2);
s.close();
c.close();
check(s.peerAddress()=="" && c.peerAddress()=="");
check(s.allowRemote(0));
check(!s.remoteEnabled());
""",58,p)


def transport_consolidation():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
MediaSession s;
MediaSession c;
check(s.sentPackets()==0 && s.receivedPackets()==0);
check(c.sentBytes()==0 && c.receivedBytes()==0);
check(s.transportError()==0);
check(s.listen(0));
check(c.connect(s.port()));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) s.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (!s.connected()||!c.connected());i++) {
    c.poll();
    s.poll();
}
check(s.connected() && c.connected());
check(s.sentPackets()==1 && s.receivedPackets()==1);
check(c.sentPackets()==1 && c.receivedPackets()==1);
check(s.sentBytes()==44 && s.receivedBytes()==44);
check(c.sentBytes()==44 && c.receivedBytes()==44);
int packet[35];
int got[35];
int meta[4];
int data[3];
for(1=>int t;t<=7;t++) {
    check(MediaTransport.encode(t,10+t,t,t*100,[t,0,255],packet));
    check(c.sendPacket(packet));
    for(0=>int i;i<100000 && !s.packetSize();i++) {
        c.poll();
        s.poll();
    }
    check(s.packetSize()==35);
    check(s.receivedPackets()==t+1);
    s.poll();
    check(s.receivedPackets()==t+1);
    check(!s.receivePacket([0]));
    check(s.receivePacket(got));
    check(MediaTransport.decode(got,meta,data));
    check(meta[0]==t && meta[1]==10+t && meta[2]==t && meta[3]==t*100);
    check(data[0]==t && data[1]==0 && data[2]==255);
    check(s.sendPacket(got));
    for(0=>int i;i<100000 && !c.packetSize();i++) {
        s.poll();
        c.poll();
    }
    check(c.receivePacket(got));
    check(got[32]==t);
}
check(s.sentPackets()==8 && s.receivedPackets()==8);
check(c.sentPackets()==8 && c.receivedPackets()==8);
check(s.sentBytes()==289 && s.receivedBytes()==289);
check(c.sentBytes()==289 && c.receivedBytes()==289);
check(s.transportError()==0 && c.transportError()==0);
c.disconnect();
for(0=>int i;i<100000 && s.connected();i++) s.poll();
check(s.state()==3);
check(s.transportError()==1 || s.transportError()==2);
check(s.receivedPackets()==8);
check(c.connect(s.port()));
0=>accepted;
for(0=>int i;i<100000 && !accepted;i++) s.accept()=>accepted;
check(accepted==1);
check(s.sentPackets()==0 && s.receivedPackets()==0);
check(s.transportError()==0);
for(0=>int i;i<100000 && (!s.connected()||!c.connected());i++) {
    c.poll();
    s.poll();
}
check(s.connected() && c.connected());
check(s.sentPackets()==1 && s.receivedPackets()==1);
check(c.sentPackets()==1 && c.receivedPackets()==1);
s.close();
c.close();
check(s.state()==0 && c.state()==0);
MediaLoopback a;
MediaLoopback b;
check(a.listen(0));
check(b.connect(a.port()));
0=>accepted;
for(0=>int i;i<100000 && !accepted;i++) a.accept()=>accepted;
check(accepted==1);
check(b.sendPacket(packet));
for(0=>int i;i<100000 && !a.packetSize();i++) {
    b.poll();
    a.poll();
}
check(a.receivedPackets()==1 && a.receivedBytes()==35);
check(b.sentPackets()==1 && b.sentBytes()==35);
check(a.receivePacket(got));
a.close();
b.close();
""",125,p)


def transport_pcm():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
int packet[60];
int meta[6];
float out[6];
[-1.0,-0.5,0.0,0.25,0.5,1.0] @=> float samples[];
check(MediaPCM.packetSize(48000,2,3)==60);
check(MediaPCM.packetSize(8000,1,1)==50);
check(MediaPCM.packetSize(192000,8,1)==64);
check(MediaPCM.packetSize(7999,1,1)==0);
check(MediaPCM.packetSize(192001,1,1)==0);
check(MediaPCM.packetSize(48000,0,1)==0);
check(MediaPCM.packetSize(48000,9,1)==0);
check(MediaPCM.packetSize(48000,2,0)==0);
check(MediaPCM.packetSize(48000,2,-1)==0);
check(MediaPCM.packetSize(48000,1,524280)==1048608);
check(MediaPCM.packetSize(48000,1,524281)==0);
check(MediaPCM.encode(42,3,123456,48000,2,samples,packet));
check(packet[5]==4);
check(packet[32]==67 && packet[33]==75 && packet[34]==65 && packet[35]==80);
check(packet[36]==1 && packet[37]==1 && packet[38]==2 && packet[39]==0);
check(packet[42]==187 && packet[43]==128);
check(packet[47]==3);
check(packet[48]==128 && packet[49]==0);
check(packet[50]==192 && packet[51]==0);
check(packet[52]==0 && packet[53]==0);
check(packet[54]==32 && packet[55]==0);
check(packet[56]==64 && packet[57]==0);
check(packet[58]==127 && packet[59]==255);
check(MediaPCM.inspect(packet,meta));
check(meta[0]==42 && meta[1]==3 && meta[2]==123456);
check(meta[3]==48000 && meta[4]==2 && meta[5]==3);
check(MediaPCM.decode(packet,out));
for(0=>int i;i<6;i++) check(Math.fabs(samples[i]-out[i])<0.00004);
check(out[0]==-1.0 && out[5]==1.0);
check(!MediaPCM.encode(0,3,0,48000,2,samples,packet));
check(!MediaPCM.encode(-1,3,0,48000,2,samples,packet));
check(!MediaPCM.encode(42,-1,0,48000,2,samples,packet));
check(!MediaPCM.encode(42,3,-1,48000,2,samples,packet));
check(!MediaPCM.encode(42,3,0,7999,2,samples,packet));
check(!MediaPCM.encode(42,3,0,48000,4,samples,packet));
check(!MediaPCM.encode(42,3,0,48000,2,[1.1,0.0,0.0,0.0,0.0,0.0],packet));
check(!MediaPCM.encode(42,3,0,48000,2,[-1.1,0.0,0.0,0.0,0.0,0.0],packet));
check(!MediaPCM.encode(42,3,0,48000,2,samples,[0]));
check(!MediaPCM.inspect(packet,[0]));
check(!MediaPCM.decode(packet,[0.0]));
check(MediaPCM.inspect(packet,meta));
check(meta[2]==123456);
48=>int pos;
packet[pos]=>int old;
(old+1)%256=>packet[pos];
check(!MediaPCM.inspect(packet,meta));
check(!MediaPCM.decode(packet,out));
check(meta[0]==42 && out[0]==-1.0);
old=>packet[pos];
int raw[28];
int genericMeta[4];
check(MediaTransport.decode(packet,genericMeta,raw));
check(genericMeta[0]==4);
int bad[60];
check(MediaTransport.encode(7,42,3,0,raw,bad));
check(!MediaPCM.inspect(bad,meta));
0=>raw[0];
check(MediaTransport.encode(4,42,3,0,raw,bad));
check(!MediaPCM.inspect(bad,meta));
67=>raw[0];
2=>raw[4];
check(MediaTransport.encode(4,42,3,0,raw,bad));
check(!MediaPCM.decode(bad,out));
1=>raw[4];
4=>raw[15];
check(MediaTransport.encode(4,42,3,0,raw,bad));
check(!MediaPCM.inspect(bad,meta));
MediaSession server;
MediaSession client;
check(server.configure(8,64));
check(client.configure(8,64));
check(server.listen(0));
check(client.connect(server.port()));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (!server.connected() || !client.connected());i++) {
    client.poll();
    server.poll();
}
check(server.connected() && client.connected());
check(client.sendPacket(packet));
for(0=>int i;i<100000 && !server.packetSize();i++) {
    client.poll();
    server.poll();
}
check(server.packetSize()==60);
int got[60];
check(server.receivePacket(got));
check(MediaPCM.inspect(got,meta));
check(meta[0]==42 && meta[3]==48000);
check(MediaPCM.decode(got,out));
for(0=>int i;i<6;i++) check(Math.fabs(samples[i]-out[i])<0.00004);
check(server.sendPacket(got));
for(0=>int i;i<100000 && !client.packetSize();i++) {
    server.poll();
    client.poll();
}
check(client.receivePacket(got));
check(MediaPCM.decode(got,out));
check(out[0]==-1.0 && out[5]==1.0);
server.close();
client.close();
""",82,p)


def transport_pcm_stream():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
MediaPCMStream tx;
MediaPCMStream rx;
int p[54];
int q[52];
int meta[6];
float out[3];
float shortOut[2];
check(!tx.configured());
check(tx.frames()==0 && tx.blocks()==0);
check(tx.packetSize(3)==0);
check(!tx.encodeBlock([0.0,0.5,1.0],p));
check(tx.error()==1);
check(!tx.configure(0,44100,1,1000,9));
check(!tx.configure(1,7999,1,1000,9));
check(!tx.configure(1,44100,0,1000,9));
check(!tx.configure(1,44100,1,-1,9));
check(!tx.configure(1,44100,1,1000,-1));
check(tx.configure(1,44100,1,1000,9));
check(rx.configure(1,44100,1,1000,9));
check(tx.configured() && tx.rate()==44100 && tx.channels()==1);
check(tx.nextTimestamp()==1000 && tx.nextSequence()==9);
check(tx.packetSize(3)==54);
check(!tx.encodeBlock([0.0,0.5,1.0],[0]));
check(tx.error()==2 && tx.frames()==0);
check(!tx.encodeBlock([0.0,2.0,1.0],p));
check(tx.frames()==0 && tx.nextSequence()==9);
check(tx.encodeBlock([0.0,0.5,1.0],p));
check(tx.frames()==3 && tx.blocks()==1);
check(tx.nextSequence()==10 && tx.nextTimestamp()==1068);
check(MediaPCM.inspect(p,meta));
check(meta[0]==1 && meta[1]==9 && meta[2]==1000 && meta[5]==3);
check(tx.encodeBlock([-1.0,-0.5],q));
check(tx.frames()==5 && tx.blocks()==2);
check(tx.nextTimestamp()==1113 && tx.nextSequence()==11);
check(!rx.decodeBlock(q,shortOut));
check(rx.error()==4 && rx.frames()==0);
check(!rx.decodeBlock(p,shortOut));
check(rx.error()==2 && rx.frames()==0);
check(rx.decodeBlock(p,out));
check(rx.error()==0 && rx.frames()==3);
check(out[0]==0.0 && out[2]==1.0);
check(!rx.decodeBlock(p,out));
check(rx.error()==4 && rx.frames()==3);
int bad[52];
check(MediaPCM.encode(2,10,1068,44100,1,[-1.0,-0.5],bad));
check(!rx.decodeBlock(bad,shortOut));
check(rx.error()==3 && rx.frames()==3);
check(MediaPCM.encode(1,10,1069,44100,1,[-1.0,-0.5],bad));
check(!rx.decodeBlock(bad,shortOut));
check(rx.error()==5 && rx.frames()==3);
check(MediaPCM.encode(1,10,1068,48000,1,[-1.0,-0.5],bad));
check(!rx.decodeBlock(bad,shortOut));
check(rx.error()==3);
check(rx.decodeBlock(q,shortOut));
check(shortOut[0]==-1.0 && Math.fabs(shortOut[1]+0.5)<0.00004);
check(rx.frames()==5 && rx.blocks()==2);
check(rx.nextTimestamp()==tx.nextTimestamp());
check(rx.nextSequence()==tx.nextSequence());
check(!rx.configure(0,44100,1,0,0));
check(rx.frames()==5);
rx.clear();
check(!rx.configured() && rx.frames()==0 && rx.error()==0);
MediaSession server;
MediaSession client;
check(server.configure(8,64));
check(client.configure(8,64));
check(server.listen(0));
check(client.connect(server.port()));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (!server.connected() || !client.connected());i++) {
    client.poll();
    server.poll();
}
check(server.connected() && client.connected());
check(tx.configure(5,44100,1,0,0));
check(rx.configure(5,44100,1,0,0));
int got[54];
for(0=>int b;b<5;b++) {
    check(tx.encodeBlock([0.0,0.5,1.0],p));
    check(client.sendPacket(p));
    for(0=>int i;i<100000 && !server.packetSize();i++) {
        client.poll();
        server.poll();
    }
    check(server.packetSize()==54);
    check(server.receivePacket(got));
    check(rx.decodeBlock(got,out));
    check(rx.frames()==3*(b+1));
    check(rx.nextSequence()==b+1);
    check(out[0]==0.0 && out[2]==1.0);
}
check(tx.frames()==15 && rx.frames()==15);
check(tx.nextTimestamp()==340 && rx.nextTimestamp()==340);
server.close();
client.close();
""",103,p)


def transport_pcm_buffer():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
MediaPCMStream tx;
MediaPCMBuffer rx;
int p[56];
int q[56];
int r[56];
float out[4];
float pair[2];
check(!rx.configured());
check(!rx.ready());
check(!rx.push(p));
check(rx.error()==1);
check(!rx.pull(out));
check(!rx.configure(1,48000,2,1000,0,0,1));
check(!rx.configure(1,48000,2,1000,0,4,5));
check(!rx.configure(1,48000,2,1000,0,524289,1));
check(rx.configure(1,48000,2,1000,0,4,3));
check(tx.configure(1,48000,2,1000,0));
check(rx.configured() && rx.channels()==2);
check(rx.capacity()==4 && rx.threshold()==3);
check(rx.available()==0 && rx.freeFrames()==4);
check(rx.playTimestamp()==1000 && rx.nextTimestamp()==1000);
check(tx.encodeBlock([0.0,1.0,-1.0,0.5],p));
check(tx.encodeBlock([0.25,-0.25,0.0,-1.0],q));
check(tx.encodeBlock([1.0,0.0,0.5,-0.5],r));
check(rx.push(p));
check(rx.available()==2 && !rx.ready());
check(rx.bufferedUs()==41);
check(!rx.pull(pair));
check(rx.error()==8 && rx.underruns()==0);
check(rx.push(q));
check(rx.ready() && rx.available()==4);
check(rx.nextSequence()==2 && rx.receivedFrames()==4);
check(!rx.push(r));
check(rx.error()==7 && rx.overruns()==1);
check(rx.nextSequence()==2 && rx.available()==4);
check(rx.pull(out));
check(out[0]==0.0 && out[1]==1.0 && out[2]==-1.0);
check(Math.fabs(out[3]-0.5)<0.00004);
check(rx.consumedFrames()==2 && rx.playTimestamp()==1041);
check(rx.push(r));
check(rx.available()==4 && rx.nextSequence()==3);
check(rx.pull(out));
check(Math.fabs(out[0]-0.25)<0.00004 && Math.fabs(out[1]+0.25)<0.00004);
check(out[2]==0.0 && out[3]==-1.0);
check(rx.pull(pair));
check(pair[0]==1.0 && pair[1]==0.0);
check(rx.available()==1 && rx.ready());
check(!rx.pull(out));
check(rx.error()==9 && rx.underruns()==1 && !rx.ready());
check(rx.available()==1 && rx.consumedFrames()==5);
check(!rx.pull(pair));
check(rx.error()==8 && rx.underruns()==1);
check(!rx.push(r));
check(rx.error()==4 && rx.available()==1);
check(tx.encodeBlock([0.0,0.0,1.0,1.0],p));
check(rx.push(p));
check(rx.ready() && rx.available()==3);
check(rx.pull(pair));
check(Math.fabs(pair[0]-0.5)<0.00004 && Math.fabs(pair[1]+0.5)<0.00004);
check(rx.playTimestamp()==1125 && rx.nextTimestamp()==1166);
check(rx.receivedFrames()==8 && rx.consumedFrames()==6);
check(rx.pull(out));
check(out[0]==0.0 && out[1]==0.0 && out[2]==1.0 && out[3]==1.0);
check(rx.available()==0 && rx.bufferedUs()==0);
check(!rx.pull(pair));
check(rx.underruns()==2 && !rx.ready());
rx.clear();
check(!rx.configured() && rx.capacity()==0 && rx.overruns()==0);
MediaSession server;
MediaSession client;
check(server.configure(8,64));
check(client.configure(8,64));
check(server.listen(0));
check(client.connect(server.port()));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (!server.connected() || !client.connected());i++) {
    client.poll();
    server.poll();
}
check(server.connected() && client.connected());
check(tx.configure(5,44100,2,0,0));
check(rx.configure(5,44100,2,0,0,6,4));
int got[56];
for(0=>int b;b<3;b++) {
    check(tx.encodeBlock([0.0,1.0,-1.0,0.0],p));
    check(client.sendPacket(p));
    for(0=>int i;i<100000 && !server.packetSize();i++) {
        client.poll();
        server.poll();
    }
    check(server.receivePacket(got));
    check(rx.push(got));
    check(rx.available()==2*(b+1));
}
check(rx.ready() && rx.bufferedUs()==136);
for(0=>int b;b<3;b++) {
    check(rx.pull(out));
    check(out[0]==0.0 && out[1]==1.0 && out[2]==-1.0 && out[3]==0.0);
}
check(rx.playTimestamp()==136 && rx.nextTimestamp()==136);
check(rx.consumedFrames()==6 && rx.available()==0);
server.close();
client.close();
""",92,p)


def transport_midi():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
int packet[40];
int event[6];
check(MediaMIDI.packetSize()==40);
for(0=>int i;i<7;i++) {
    128+i*16+15=>int status;
    0=>int d2;
    if(i!=4 && i!=5) 127=>d2;
    check(MediaMIDI.encode(7,i,100+i,status,60,d2,packet));
    check(MediaMIDI.decode(packet,event));
    check(event[0]==7 && event[1]==i && event[2]==100+i);
    check(event[3]==status && event[4]==60 && event[5]==d2);
}
check(packet[5]==5 && packet[32]==67 && packet[33]==75 && packet[34]==77 && packet[35]==73);
check(packet[36]==1 && packet[37]==239);
check(!MediaMIDI.encode(0,0,0,144,60,100,packet));
check(!MediaMIDI.encode(-1,0,0,144,60,100,packet));
check(!MediaMIDI.encode(7,-1,0,144,60,100,packet));
check(!MediaMIDI.encode(7,0,-1,144,60,100,packet));
check(!MediaMIDI.encode(7,0,0,127,60,100,packet));
check(!MediaMIDI.encode(7,0,0,240,60,100,packet));
check(!MediaMIDI.encode(7,0,0,144,-1,100,packet));
check(!MediaMIDI.encode(7,0,0,144,128,100,packet));
check(!MediaMIDI.encode(7,0,0,144,60,128,packet));
check(!MediaMIDI.encode(7,0,0,192,60,1,packet));
check(!MediaMIDI.encode(7,0,0,208,60,1,packet));
check(!MediaMIDI.encode(7,0,0,144,60,100,[0]));
check(!MediaMIDI.decode(packet,[0]));
check(MediaMIDI.decode(packet,event));
check(event[1]==6 && event[3]==239);
packet[39]=>int saved;
0=>packet[39];
check(!MediaMIDI.decode(packet,event));
check(event[3]==239 && event[5]==127);
saved=>packet[39];
int raw[8];
int meta[4];
int bad[40];
check(MediaTransport.decode(packet,meta,raw));
check(MediaTransport.encode(4,7,0,0,raw,bad));
check(!MediaMIDI.decode(bad,event));
2=>raw[4];
check(MediaTransport.encode(5,7,0,0,raw,bad));
check(!MediaMIDI.decode(bad,event));
1=>raw[4];
240=>raw[5];
check(MediaTransport.encode(5,7,0,0,raw,bad));
check(!MediaMIDI.decode(bad,event));
MediaMIDIStream tx;
MediaMIDIStream rx;
check(!tx.configured());
check(!tx.encodeEvent(100,144,60,100,packet));
check(tx.error()==1);
check(!tx.configure(0,0,100));
check(!tx.configure(7,-1,100));
check(!tx.configure(7,0,-1));
check(tx.configure(7,9,100));
check(rx.configure(7,9,100));
check(tx.nextSequence()==9 && tx.lastTimestamp()==100);
check(!tx.encodeEvent(99,144,60,100,packet));
check(tx.error()==4 && tx.events()==0);
check(tx.encodeEvent(100,144,60,100,packet));
check(tx.nextSequence()==10 && tx.events()==1);
int secondPacket[40];
check(tx.encodeEvent(100,128,60,0,secondPacket));
check(!rx.decodeEvent(secondPacket,event));
check(rx.error()==3 && rx.events()==0);
check(rx.decodeEvent(packet,event));
check(event[1]==9 && event[3]==144);
check(!rx.decodeEvent(packet,event));
check(rx.error()==3 && rx.events()==1);
check(MediaMIDI.encode(7,10,99,128,60,0,bad));
check(!rx.decodeEvent(bad,event));
check(rx.error()==4 && rx.nextSequence()==10);
check(rx.decodeEvent(secondPacket,event));
check(rx.events()==2 && rx.lastTimestamp()==100);
check(rx.nextSequence()==11 && rx.error()==0);
rx.clear();
check(!rx.configured() && rx.events()==0);
MediaSession server;
MediaSession client;
check(server.configure(24,64));
check(client.configure(24,64));
check(server.listen(0));
check(client.connect(server.port()));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (!server.connected()||!client.connected());i++) {
    server.poll(); client.poll();
}
check(server.connected() && client.connected());
check(client.sendPacket(packet));
int audio[52];
check(MediaPCM.encode(8,0,100,48000,1,[0.0,1.0],audio));
check(server.sendPacket(audio));
for(0=>int i;i<100000 && (!server.packetSize()||!client.packetSize());i++) {
    server.poll(); client.poll();
}
check(server.receivePacket(bad));
check(MediaMIDI.decode(bad,event));
check(event[3]==144 && event[2]==100);
check(client.receivePacket(audio));
float samples[2];
check(MediaPCM.decode(audio,samples));
check(samples[0]==0.0 && samples[1]==1.0);
server.close(); client.close();
""",97,p)


def transport_audio_midi():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
MediaAudioMidi rx;
MediaPCMStream audio;
MediaMIDIStream midi;
int p[56];
int m[40];
int n[40];
int event[6];
float pair[2];
float block[4];
check(!rx.configured());
check(!rx.push(p));
check(rx.error()==1);
check(!rx.configure(1,1,48000,2,1000,0,0,4,2,2));
check(!rx.configure(1,2,48000,2,1000,0,0,4,2,0));
check(rx.configure(1,2,48000,2,1000,0,0,4,2,2));
check(audio.configure(1,48000,2,1000,0));
check(midi.configure(2,0,1000));
check(rx.configured() && rx.channels()==2);
check(!rx.ready() && rx.available()==0);
check(rx.playTimestamp()==1000 && rx.consumedFrames()==0);
check(midi.encodeEvent(1000,144,60,100,m));
check(rx.push(m));
check(rx.pendingMidi()==1 && !rx.midiDue());
check(!rx.popMidi(event));
check(rx.error()==8);
check(audio.encodeBlock([0.0,1.0,-1.0,0.0],p));
check(rx.push(p));
check(rx.ready() && rx.midiDue());
check(!rx.pullAudio(pair));
check(rx.error()==7 && rx.consumedFrames()==0);
check(rx.popMidi(event));
check(event[0]==2 && event[1]==0 && event[2]==1000 && event[3]==144);
check(!rx.midiDue());
check(midi.encodeEvent(1020,128,60,0,m));
check(midi.encodeEvent(1020,144,64,100,n));
check(rx.push(m));
check(rx.push(n));
check(rx.pendingMidi()==2);
check(midi.encodeEvent(1041,128,64,0,m));
check(!rx.push(m));
check(rx.error()==6 && rx.pendingMidi()==2);
check(rx.pullAudio(pair));
check(pair[0]==0.0 && pair[1]==1.0);
check(rx.playTimestamp()==1020 && rx.midiDue());
check(rx.popMidi(event));
check(event[1]==1 && event[3]==128);
check(rx.push(m));
check(rx.popMidi(event));
check(event[1]==2 && event[4]==64);
check(!rx.midiDue());
check(rx.pullAudio(pair));
check(pair[0]==-1.0 && pair[1]==0.0);
check(rx.playTimestamp()==1041 && rx.midiDue());
check(rx.popMidi(event));
check(event[1]==3 && event[2]==1041);
check(!rx.pullAudio(pair));
check(rx.error()==3 && rx.audioError()==9);
check(!rx.ready());
check(rx.playTimestamp()==1041);
check(MediaMIDI.encode(2,4,1040,144,65,100,m));
check(!rx.push(m));
check(rx.error()==4);
check(midi.encodeEvent(1041,144,65,100,m));
check(rx.push(m));
check(!rx.midiDue());
check(audio.encodeBlock([1.0,0.0,0.0,-1.0],p));
check(rx.push(p));
check(rx.midiDue());
check(rx.popMidi(event));
check(event[1]==4 && event[4]==65);
check(rx.pullAudio(block));
check(block[0]==1.0 && block[3]==-1.0);
check(rx.consumedFrames()==4 && rx.pendingMidi()==0);
check(MediaMIDI.encode(2,5,1042,144,65,100,m));
check(!rx.push(m));
check(rx.error()==5);
rx.clear();
check(!rx.configured());
check(rx.pendingMidi()==0 && rx.consumedFrames()==0);
MediaSession server;
MediaSession client;
check(server.configure(24,64));
check(client.configure(24,64));
check(server.listen(0));
check(client.connect(server.port()));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (!server.connected()||!client.connected());i++) {
    server.poll(); client.poll();
}
check(server.connected() && client.connected());
check(rx.configure(3,4,48000,2,0,0,0,4,2,4));
check(audio.configure(3,48000,2,0,0));
check(midi.configure(4,0,0));
check(midi.encodeEvent(0,144,60,100,m));
check(client.sendPacket(m));
for(0=>int i;i<100000 && !server.packetSize();i++) {
    client.poll(); server.poll();
}
check(server.receivePacket(n));
check(rx.push(n));
check(audio.encodeBlock([0.0,1.0,-1.0,0.0],p));
check(client.sendPacket(p));
for(0=>int i;i<100000 && !server.packetSize();i++) {
    client.poll(); server.poll();
}
check(server.receivePacket(p));
check(rx.push(p));
check(rx.midiDue());
check(rx.popMidi(event));
check(event[0]==4 && event[2]==0 && event[3]==144);
check(rx.pullAudio(block));
check(block[1]==1.0 && block[2]==-1.0);
check(rx.playTimestamp()==41);
server.close(); client.close();
""",92,p)


def transport_raster():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
@import "RasterIO";
RasterImage source;
RasterImage target;
[255,0,0,255,0,255,0,128,0,0,255,0,12,34,56,78] @=> int pixels[];
int exported[16];
int recovered[16];
int packet[64];
int metadata[5];
check(source.importPixels(2,2,pixels));
check(source.exportPixels(exported));
check(MediaRaster.packetSize(2,2)==64);
check(MediaRaster.packetSize(1,1)==52);
check(MediaRaster.packetSize(0,2)==0);
check(MediaRaster.packetSize(-1,2)==0);
check(MediaRaster.packetSize(2,0)==0);
check(MediaRaster.packetSize(262140,1)==1048608);
check(MediaRaster.packetSize(262141,1)==0);
check(MediaRaster.packetSize(512,512)==0);
check(MediaRaster.encode(9,12,34567,2,2,exported,packet));
check(packet[5]==1);
check(packet[32]==67 && packet[33]==75 && packet[34]==82 && packet[35]==65);
check(packet[36]==1 && packet[37]==1 && packet[38]==0 && packet[39]==0);
check(packet[43]==2 && packet[47]==2);
check(MediaRaster.inspect(packet,metadata));
check(metadata[0]==9 && metadata[1]==12 && metadata[2]==34567);
check(metadata[3]==2 && metadata[4]==2);
check(MediaRaster.decode(packet,recovered));
for(0=>int i;i<16;i++) check(recovered[i]==pixels[i]);
check(target.importPixels(metadata[3],metadata[4],recovered));
check(target.width()==2 && target.height()==2);
check(target.pixelAt(1,0,3)==128);
check(target.pixelAt(0,1,2)==255 && target.pixelAt(0,1,3)==0);
check(!MediaRaster.encode(0,12,34567,2,2,pixels,packet));
check(!MediaRaster.encode(9,-1,34567,2,2,pixels,packet));
check(!MediaRaster.encode(9,12,-1,2,2,pixels,packet));
check(!MediaRaster.encode(9,12,34567,2,3,pixels,packet));
check(!MediaRaster.encode(9,12,34567,2,2,pixels,[0]));
check(!MediaRaster.encode(9,12,34567,2,2,[0],packet));
256=>pixels[0];
check(!MediaRaster.encode(9,12,34567,2,2,pixels,packet));
255=>pixels[0];
check(!MediaRaster.inspect(packet,[0]));
check(!MediaRaster.decode(packet,[0]));
check(MediaRaster.inspect(packet,metadata));
check(metadata[1]==12);
0=>packet[48];
check(!MediaRaster.inspect(packet,metadata));
check(!MediaRaster.decode(packet,recovered));
check(recovered[0]==255 && metadata[0]==9);
255=>packet[48];
int raw[32];
int genericMeta[4];
int malformed[64];
check(MediaTransport.decode(packet,genericMeta,raw));
check(MediaTransport.encode(2,9,12,34567,raw,malformed));
check(!MediaRaster.inspect(malformed,metadata));
2=>raw[4];
check(MediaTransport.encode(1,9,12,34567,raw,malformed));
check(!MediaRaster.decode(malformed,recovered));
1=>raw[4];
1=>raw[6];
check(MediaTransport.encode(1,9,12,34567,raw,malformed));
check(!MediaRaster.inspect(malformed,metadata));
0=>raw[6];
3=>raw[15];
check(MediaTransport.encode(1,9,12,34567,raw,malformed));
check(!MediaRaster.inspect(malformed,metadata));
MediaSession server;
MediaSession client;
check(server.configure(1,32));
check(client.configure(1,32));
check(server.listen(0));
check(client.connect(server.port()));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (!server.connected()||!client.connected());i++) {
    server.poll(); client.poll();
}
check(server.connected() && client.connected());
check(client.sendPacket(packet));
for(0=>int i;i<100000 && !server.packetSize();i++) {
    client.poll(); server.poll();
}
check(server.packetSize()==64);
int got[64];
check(server.receivePacket(got));
check(MediaRaster.inspect(got,metadata));
check(MediaRaster.decode(got,recovered));
for(0=>int i;i<16;i++) check(recovered[i]==pixels[i]);
check(server.sendPacket(got));
for(0=>int i;i<100000 && !client.packetSize();i++) {
    server.poll(); client.poll();
}
check(client.receivePacket(got));
check(MediaRaster.decode(got,recovered));
check(recovered[7]==128 && recovered[11]==0);
server.close(); client.close();
""",93,p)


def transport_raster_stream():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
MediaRasterStream rx;
int fragA[72];
int fragB[72];
int nextFrag[68];
int recovered[16];
check(!rx.configured());
check(!rx.push(fragA));
check(rx.error()==1);
check(!rx.configure(0,0,0,0));
check(!rx.configure(9,-1,0,0));
check(!rx.configure(9,0,-1,0));
check(!rx.configure(9,0,0,-1));
check(rx.configure(9,3,7,100));
check(rx.configured() && !rx.ready());
check(rx.nextSequence()==3 && rx.nextImage()==7);
check(MediaRasterFragment.packetSize(2)==72);
check(MediaRasterFragment.packetSize(0)==0);
check(MediaRasterFragment.packetSize(262136)==1048608);
check(MediaRasterFragment.packetSize(262137)==0);
check(MediaRasterFragment.encode(9,3,100,2,2,7,0,[255,0,0,255,0,255,0,128],fragA));
check(MediaRasterFragment.encode(9,4,100,2,2,7,2,[0,0,255,0,12,34,56,78],fragB));
check(fragA[32]==67 && fragA[33]==75 && fragA[34]==82 && fragA[35]==70);
check(!MediaRasterFragment.encode(9,3,100,2,2,7,3,[0,0,0,0,0,0,0,0],fragA));
check(!MediaRasterFragment.encode(9,3,100,4096,4096,7,0,[0,0,0,0,0,0,0,0],fragA));
check(!MediaRasterFragment.encode(9,3,100,2,2,7,0,[256,0,0,0,0,0,0,0],fragA));
check(!rx.push(fragB));
check(rx.error()==3 && rx.receivedPixels()==0);
check(rx.push(fragA));
check(rx.receivedPixels()==2 && !rx.ready());
check(rx.width()==2 && rx.height()==2 && rx.timestamp()==100);
check(rx.nextSequence()==4);
check(!rx.push(fragA));
check(rx.error()==3 && rx.receivedPixels()==2);
int wrong[72];
check(MediaRasterFragment.encode(9,4,101,2,2,7,2,[0,0,0,0,0,0,0,0],wrong));
check(!rx.push(wrong));
check(rx.error()==4);
check(rx.push(fragB));
check(rx.ready() && rx.pixelBytes()==16 && rx.completed()==1);
check(!rx.take([0]));
check(rx.ready() && rx.error()==5);
check(MediaRasterFragment.encode(9,5,200,1,1,8,0,[1,2,3,4],nextFrag));
check(!rx.push(nextFrag));
check(rx.error()==5 && rx.nextSequence()==5);
check(rx.take(recovered));
check(recovered[0]==255 && recovered[7]==128 && recovered[10]==255 && recovered[15]==78);
check(!rx.ready() && rx.nextImage()==8 && rx.receivedPixels()==0);
check(rx.push(nextFrag));
check(rx.ready() && rx.width()==1 && rx.height()==1 && rx.timestamp()==200);
int tiny[4];
check(rx.take(tiny));
check(tiny[0]==1 && tiny[1]==2 && tiny[2]==3 && tiny[3]==4);
check(rx.completed()==2 && rx.nextImage()==9 && rx.nextSequence()==6);
rx.clear();
check(!rx.configured() && rx.completed()==0);
MediaSession server;
MediaSession client;
check(server.configure(1,64));
check(client.configure(1,64));
check(server.listen(0));
check(client.connect(server.port()));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (!server.connected()||!client.connected());i++) { server.poll(); client.poll(); }
check(server.connected() && client.connected());
check(rx.configure(9,3,7,100));
check(client.sendPacket(fragA));
for(0=>int i;i<100000 && !server.packetSize();i++) { client.poll(); server.poll(); }
check(server.receivePacket(wrong));
check(rx.push(wrong));
check(client.sendPacket(fragB));
for(0=>int i;i<100000 && !server.packetSize();i++) { client.poll(); server.poll(); }
check(server.receivePacket(wrong));
check(rx.push(wrong));
check(rx.take(recovered));
check(recovered[0]==255 && recovered[15]==78);
server.close();client.close();
check(rx.configure(11,0,0,0));
int largePart[524288];
int largePacket[524352];
int largeImage[1048576];
123=>largePart[0];
77=>largePart[524287];
check(MediaRasterFragment.encode(11,0,0,512,512,0,0,largePart,largePacket));
check(rx.push(largePacket));
check(!rx.ready() && rx.receivedPixels()==131072);
45=>largePart[0];
99=>largePart[524287];
check(MediaRasterFragment.encode(11,1,0,512,512,0,131072,largePart,largePacket));
check(rx.push(largePacket));
check(rx.ready() && rx.pixelBytes()==1048576);
check(rx.take(largeImage));
check(largeImage[0]==123 && largeImage[524287]==77 && largeImage[524288]==45 && largeImage[1048575]==99);
""",71,p)


def transport_vector():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
@import "VectorIO";
"<svg xmlns='http://www.w3.org/2000/svg' width='4' height='4'><rect width='4' height='4' fill='red'/></svg>" => string markup;
int packet[MediaVector.packetSize(markup.length())];
int meta[4];
check(MediaVector.packetSize(0)==0);
check(MediaVector.packetSize(-1)==0);
check(MediaVector.packetSize(1048564)==1048608);
check(MediaVector.packetSize(1048565)==0);
check(packet.size()==44+markup.length());
check(MediaVector.encode(3,7,123,markup,packet));
check(packet[5]==2);
check(packet[32]==67 && packet[33]==75 && packet[34]==86 && packet[35]==71);
check(MediaVector.inspect(packet,meta));
check(meta[0]==3 && meta[1]==7 && meta[2]==123 && meta[3]==markup.length());
check(MediaVector.decode(packet)==markup);
VectorImage img;
check(img.setSvg(MediaVector.decode(packet)));
check(img.loaded());
check(!MediaVector.encode(0,7,123,markup,packet));
check(!MediaVector.encode(3,-1,123,markup,packet));
check(!MediaVector.encode(3,7,-1,markup,packet));
check(!MediaVector.encode(3,7,123,"",packet));
check(!MediaVector.encode(3,7,123,markup,[0]));
check(!MediaVector.inspect(packet,[0]));
check(MediaVector.decode(packet)==markup);
packet[44]=>int saved;
0=>packet[44];
check(!MediaVector.inspect(packet,meta));
check(MediaVector.decode(packet)=="");
check(meta[0]==3 && meta[1]==7);
saved=>packet[44];
int raw[12+markup.length()];
int genericMeta[4];
int malformed[packet.size()];
check(MediaTransport.decode(packet,genericMeta,raw));
check(MediaTransport.encode(1,3,7,123,raw,malformed));
check(MediaVector.decode(malformed)=="");
2=>raw[4];
check(MediaTransport.encode(2,3,7,123,raw,malformed));
check(!MediaVector.inspect(malformed,meta));
1=>raw[4];
0=>raw[12];
check(MediaTransport.encode(2,3,7,123,raw,malformed));
check(MediaVector.decode(malformed)=="");
192=>raw[12];
128=>raw[13];
check(MediaTransport.encode(2,3,7,123,raw,malformed));
check(MediaVector.decode(malformed)=="");
int unicodePayload[14];
[67,75,86,71,1,1,0,0,0,0,0,2,195,169] @=> int unicodeBytes[];
int unicodePacket[46];
check(MediaTransport.encode(2,3,8,124,unicodeBytes,unicodePacket));
check(MediaVector.inspect(unicodePacket,meta));
check(meta[3]==2);
check(MediaVector.decode(unicodePacket)=="é");
MediaSession server;
MediaSession client;
check(server.configure(2,1024));
check(client.configure(2,1024));
check(server.listen(0));
check(client.connect(server.port()));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (!server.connected()||!client.connected());i++) { server.poll(); client.poll(); }
check(server.connected() && client.connected());
check(client.sendPacket(packet));
for(0=>int i;i<100000 && !server.packetSize();i++) { client.poll(); server.poll(); }
int got[packet.size()];
check(server.receivePacket(got));
check(MediaVector.decode(got)==markup);
check(img.setSvg(MediaVector.decode(got)));
check(server.sendPacket(got));
for(0=>int i;i<100000 && !client.packetSize();i++) { server.poll(); client.poll(); }
check(client.receivePacket(got));
check(MediaVector.decode(got)==markup);
server.close();client.close();
""",49,p)


def transport_model():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
@import "ModelIO";
Model3D source;
Model3D target;
[0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,-0.5] @=> float coords[];
[0,1,2] @=> int indices[];
float xyz[9];
int tris[3];
int packet[96];
int meta[5];
check(source.createMesh(coords,indices));
check(source.exportVertices(0,xyz));
check(source.exportTriangles(0,tris));
check(MediaModel.packetSize(3,1)==96);
check(MediaModel.packetSize(2,1)==0);
check(MediaModel.packetSize(3,0)==0);
check(MediaModel.packetSize(-1,1)==0);
check(MediaModel.packetSize(87379,1)==1048608);
check(MediaModel.packetSize(87380,1)==0);
check(MediaModel.encode(7,4,100,xyz,tris,packet));
check(packet[5]==3);
check(packet[32]==67 && packet[33]==75 && packet[34]==77 && packet[35]==68);
check(packet[36]==1 && packet[37]==1);
check(packet[43]==3 && packet[47]==1);
check(packet[60]==63 && packet[61]==128);
check(MediaModel.inspect(packet,meta));
check(meta[0]==7 && meta[1]==4 && meta[2]==100);
check(meta[3]==3 && meta[4]==1);
check(MediaModel.decode(packet,xyz,tris));
for(0=>int i;i<9;i++) check(xyz[i]==coords[i]);
for(0=>int i;i<3;i++) check(tris[i]==indices[i]);
check(target.createMesh(xyz,tris));
check(target.meshes()==1 && target.vertices()==3);
check(target.faces(0)==1 && target.faceIndex(0,0,2)==2);
check(!MediaModel.encode(0,4,100,xyz,tris,packet));
check(!MediaModel.encode(7,-1,100,xyz,tris,packet));
check(!MediaModel.encode(7,4,-1,xyz,tris,packet));
check(!MediaModel.encode(7,4,100,xyz,[0,1,3],packet));
check(!MediaModel.encode(7,4,100,xyz,[0,-1,2],packet));
check(!MediaModel.encode(7,4,100,[0.0,1.0],tris,packet));
check(!MediaModel.encode(7,4,100,xyz,tris,[0]));
check(!MediaModel.inspect(packet,[0]));
check(!MediaModel.decode(packet,[0.0],tris));
check(!MediaModel.decode(packet,xyz,[0]));
check(MediaModel.inspect(packet,meta));
check(meta[1]==4);
int raw[64];
int genericMeta[4];
int malformed[96];
check(MediaTransport.decode(packet,genericMeta,raw));
check(MediaTransport.encode(2,7,4,100,raw,malformed));
check(!MediaModel.decode(malformed,xyz,tris));
2=>raw[4];
check(MediaTransport.encode(3,7,4,100,raw,malformed));
check(!MediaModel.inspect(malformed,meta));
1=>raw[4];
127=>raw[16];
128=>raw[17];
check(MediaTransport.encode(3,7,4,100,raw,malformed));
check(!MediaModel.decode(malformed,xyz,tris));
0=>raw[16];
0=>raw[17];
3=>raw[63];
check(MediaTransport.encode(3,7,4,100,raw,malformed));
check(!MediaModel.inspect(malformed,meta));
check(xyz[0]==0.0 && tris[2]==2);
MediaSession server;
MediaSession client;
check(server.configure(4,128));
check(client.configure(4,128));
check(server.listen(0));
check(client.connect(server.port()));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (!server.connected()||!client.connected());i++) { server.poll(); client.poll(); }
check(server.connected() && client.connected());
check(client.sendPacket(packet));
for(0=>int i;i<100000 && !server.packetSize();i++) { client.poll(); server.poll(); }
int got[96];
check(server.receivePacket(got));
check(MediaModel.decode(got,xyz,tris));
check(target.createMesh(xyz,tris));
check(target.vertexAt(0,2,2)==-0.5);
check(server.sendPacket(got));
for(0=>int i;i<100000 && !client.packetSize();i++) { server.poll(); client.poll(); }
check(client.receivePacket(got));
check(MediaModel.inspect(got,meta));
check(meta[0]==7 && meta[3]==3);
server.close();client.close();
""",71,p)


def transport_graphics():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
int rasterPacket[52];
int fragPacket[68];
int modelPacket[96];
"<svg xmlns='http://www.w3.org/2000/svg'/>" => string markup;
int vectorPacket[MediaVector.packetSize(markup.length())];
int metadata[6];
check(MediaRaster.encode(1,0,100,1,1,[1,2,3,4],rasterPacket));
check(MediaRasterFragment.encode(2,0,100,1,1,0,0,[5,6,7,8],fragPacket));
check(MediaVector.encode(3,0,100,markup,vectorPacket));
check(MediaModel.encode(4,0,100,[0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,0.0],[0,1,2],modelPacket));
check(MediaGraphics.inspect(rasterPacket,metadata)==1);
check(metadata[0]==1 && metadata[1]==1 && metadata[2]==0 && metadata[3]==100 && metadata[4]==1 && metadata[5]==1);
check(MediaGraphics.inspect(fragPacket,metadata)==2);
check(metadata[0]==2 && metadata[1]==2 && metadata[4]==1 && metadata[5]==1);
check(MediaGraphics.inspect(vectorPacket,metadata)==3);
check(metadata[0]==3 && metadata[1]==3 && metadata[4]==markup.length() && metadata[5]==0);
check(MediaGraphics.inspect(modelPacket,metadata)==4);
check(metadata[0]==4 && metadata[1]==4 && metadata[4]==3 && metadata[5]==1);
check(!MediaGraphics.inspect(modelPacket,[0]));
int midiPacket[40];
check(MediaMIDI.encode(8,0,100,144,60,100,midiPacket));
check(!MediaGraphics.inspect(midiPacket,metadata));
check(metadata[0]==4 && metadata[1]==4);
int malformed[52];
check(MediaTransport.encode(1,1,0,100,[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],malformed));
check(!MediaGraphics.inspect(malformed,metadata));
check(metadata[0]==4);
// Construct uint32 boundaries at runtime: ChucK decimal tokens use atoi.
65536 * 65536 => int exhausted;
exhausted - 1 => int lastIndex;
MediaRasterStream rx;
check(rx.configure(2,lastIndex,lastIndex,0));
int lastFrag[68];
check(MediaRasterFragment.encode(2,lastIndex,100,2,1,lastIndex,0,[5,6,7,8],lastFrag));
check(!rx.push(lastFrag));
check(rx.error()==6 && rx.receivedPixels()==0);
check(MediaRasterFragment.encode(2,lastIndex,100,1,1,lastIndex,0,[5,6,7,8],lastFrag));
check(rx.push(lastFrag));
int pixels[4];
check(rx.take(pixels));
check(rx.nextSequence()==exhausted && rx.nextImage()==exhausted);
check(!rx.push(fragPacket));
check(rx.error()==6);
check(rx.configure(2,0,0,0));
MediaSession server;
MediaSession client;
check(server.configure(7,256));
check(client.configure(7,256));
check(server.listen(0));
check(client.connect(server.port()));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (!server.connected()||!client.connected());i++) { server.poll(); client.poll(); }
check(server.connected() && client.connected());
check(server.mediaMask()==7 && client.mediaMask()==7);
check(!client.sendPacket(midiPacket));
[rasterPacket,fragPacket,vectorPacket,modelPacket] @=> int packets[][];
for(0=>int j;j<4;j++) {
    check(client.sendPacket(packets[j]));
    for(0=>int i;i<100000 && !server.packetSize();i++) { client.poll(); server.poll(); }
    check(server.packetSize()==packets[j].size());
    int got[packets[j].size()];
    check(server.receivePacket(got));
    check(MediaGraphics.inspect(got,metadata)==j+1);
    check(metadata[1]==j+1 && metadata[3]==100);
    if(j==0) {
        check(MediaRaster.decode(got,pixels));
        check(pixels[0]==1 && pixels[3]==4);
    } else if(j==1) {
        check(rx.push(got));
        check(rx.take(pixels));
        check(pixels[0]==5 && pixels[3]==8);
    } else if(j==2) {
        check(MediaVector.decode(got)==markup);
    } else {
        float xyz[9]; int tris[3];
        check(MediaModel.decode(got,xyz,tris));
        check(xyz[3]==1.0 && tris[2]==2);
    }
    check(server.sendPacket(got));
    for(0=>int i;i<100000 && !client.packetSize();i++) { server.poll(); client.poll(); }
    check(client.receivePacket(got));
    check(MediaGraphics.inspect(got,metadata)==j+1);
}
server.close();client.close();
""",78,p)


def transport_video_frame():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
check(MediaVideoFrame.packetSize(1,1)==52);
check(MediaVideoFrame.packetSize(2,2)==64);
check(MediaVideoFrame.packetSize(0,1)==0);
check(MediaVideoFrame.packetSize(1,0)==0);
check(MediaVideoFrame.packetSize(-1,1)==0);
check(MediaVideoFrame.packetSize(1,-1)==0);
check(MediaVideoFrame.packetSize(512,512)==0);
check(MediaVideoFrame.packetSize(262140,1)==1048608);
check(MediaVideoFrame.packetSize(262141,1)==0);
[0,1,2,255,10,20,30,40,50,60,70,80,90,100,110,120] @=> int rgba[];
int packet[64];
int meta[5];
int decoded[16];
check(MediaVideoFrame.encode(9,7,123456,2,2,rgba,packet));
check(packet[5]==6);
check(packet[32]==67 && packet[33]==75 && packet[34]==86 && packet[35]==70);
check(packet[36]==1 && packet[37]==1 && packet[38]==0 && packet[39]==0);
check(packet[43]==2 && packet[47]==2);
check(MediaVideoFrame.inspect(packet,meta));
check(meta[0]==9 && meta[1]==7 && meta[2]==123456);
check(meta[3]==2 && meta[4]==2);
check(MediaVideoFrame.decode(packet,decoded));
for(0=>int i;i<16;i++) check(decoded[i]==rgba[i]);
check(!MediaVideoFrame.encode(0,7,123456,2,2,rgba,packet));
check(!MediaVideoFrame.encode(-1,7,123456,2,2,rgba,packet));
65536*65536=>int uintLimit;
check(!MediaVideoFrame.encode(uintLimit-1,7,123456,2,2,rgba,packet));
check(!MediaVideoFrame.encode(9,-1,123456,2,2,rgba,packet));
check(!MediaVideoFrame.encode(9,uintLimit,123456,2,2,rgba,packet));
check(!MediaVideoFrame.encode(9,7,-1,2,2,rgba,packet));
check(!MediaVideoFrame.encode(9,7,123456,1,2,rgba,packet));
check(!MediaVideoFrame.encode(9,7,123456,2,2,[1,2,3,4],packet));
check(!MediaVideoFrame.encode(9,7,123456,2,2,rgba,[0]));
256=>rgba[0];
check(!MediaVideoFrame.encode(9,7,123456,2,2,rgba,packet));
0=>rgba[0];
-1=>rgba[0];
check(!MediaVideoFrame.encode(9,7,123456,2,2,rgba,packet));
0=>rgba[0];
check(MediaVideoFrame.inspect(packet,meta));
check(meta[1]==7);
check(!MediaVideoFrame.inspect(packet,[0]));
check(!MediaVideoFrame.decode(packet,[0]));
check(!MediaRaster.inspect(packet,meta));
int raster[64];
check(MediaRaster.encode(9,7,123456,2,2,rgba,raster));
check(!MediaVideoFrame.inspect(raster,meta));
check(!MediaVideoFrame.decode(raster,decoded));
packet[63]^1=>packet[63];
check(!MediaVideoFrame.inspect(packet,meta));
check(!MediaVideoFrame.decode(packet,decoded));
check(meta[0]==9 && decoded[15]==120);
packet[63]^1=>packet[63];
check(MediaVideoFrame.inspect(packet,meta));
int payload[32];
int outer[4];
check(MediaTransport.decode(packet,outer,payload));
int malformed[64];
1=>payload[6];
check(MediaTransport.encode(6,9,7,123456,payload,malformed));
check(!MediaVideoFrame.inspect(malformed,meta));
0=>payload[6];
2=>payload[4];
check(MediaTransport.encode(6,9,7,123456,payload,malformed));
check(!MediaVideoFrame.inspect(malformed,meta));
1=>payload[4];
0=>payload[11];
check(MediaTransport.encode(6,9,7,123456,payload,malformed));
check(!MediaVideoFrame.inspect(malformed,meta));
2=>payload[11];
check(MediaTransport.encode(1,9,7,123456,payload,malformed));
check(!MediaVideoFrame.inspect(malformed,meta));
MediaSession server;
MediaSession client;
check(server.configure(32,128));
check(client.configure(33,256));
check(server.listen(0));
check(client.connect(server.port()));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (!server.connected()||!client.connected());i++) { server.poll(); client.poll(); }
check(server.connected() && client.connected());
check(server.mediaMask()==32 && client.mediaMask()==32);
check(server.payloadLimit()==128 && client.payloadLimit()==128);
check(!client.sendPacket(raster));
check(client.sendPacket(packet));
for(0=>int i;i<100000 && !server.packetSize();i++) { client.poll(); server.poll(); }
check(server.packetSize()==64);
int got[64];
check(server.receivePacket(got));
check(MediaVideoFrame.inspect(got,meta));
check(meta[0]==9 && meta[1]==7 && meta[2]==123456);
check(MediaVideoFrame.decode(got,decoded));
for(0=>int i;i<16;i++) check(decoded[i]==rgba[i]);
check(server.sendPacket(got));
for(0=>int i;i<100000 && !client.packetSize();i++) { server.poll(); client.poll(); }
check(client.receivePacket(got));
check(MediaVideoFrame.inspect(got,meta));
check(MediaVideoFrame.decode(got,decoded));
check(decoded[0]==0 && decoded[15]==120);
server.close();client.close();
""",102,p)


def transport_video_stream():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
check(MediaVideoFragment.packetSize(1)==68);
check(MediaVideoFragment.packetSize(2)==72);
check(MediaVideoFragment.packetSize(0)==0);
check(MediaVideoFragment.packetSize(-1)==0);
check(MediaVideoFragment.packetSize(262136)==1048608);
check(MediaVideoFragment.packetSize(262137)==0);
int a[68]; int b[68]; int c[68]; int bad[68];
[1,2,3,4] @=> int px[];
[5,6,7,8] @=> int py[];
check(MediaVideoFragment.encode(9,10,100,2,1,4,0,px,a));
check(MediaVideoFragment.encode(9,11,100,2,1,4,1,py,b));
check(MediaVideoFragment.encode(9,12,200,1,1,5,0,py,c));
check(a[5]==6 && a[32]==67 && a[33]==75 && a[34]==86 && a[35]==83);
check(!MediaVideoFragment.encode(9,10,100,2,1,4,2,px,bad));
check(!MediaVideoFragment.encode(9,10,100,4097,1024,4,0,px,bad));
check(!MediaVideoFragment.encode(9,10,100,2,1,4,0,[256,0,0,0],bad));
check(!MediaVideoFragment.encode(9,10,100,2,1,4,0,[1,2,3],bad));
MediaVideoStream rx;
int out[8]; int small[4];
check(!rx.push(a));
check(rx.error()==1);
check(!rx.configure(0,10,4,0));
check(rx.configure(9,10,4,100));
check(rx.configured() && !rx.ready());
check(rx.nextSequence()==10 && rx.nextFrame()==4);
check(!rx.push(b));
check(rx.error()==3 && rx.receivedPixels()==0);
check(rx.push(a));
check(rx.receivedPixels()==1 && !rx.ready());
check(rx.width()==2 && rx.height()==1 && rx.timestamp()==100);
check(rx.nextSequence()==11 && rx.nextFrame()==4);
check(!rx.take(out));
check(rx.error()==5);
check(!rx.push(a));
check(rx.error()==3);
check(MediaVideoFragment.encode(9,11,101,2,1,4,1,py,bad));
check(!rx.push(bad));
check(rx.error()==4);
check(MediaVideoFragment.encode(9,11,100,1,2,4,1,py,bad));
check(!rx.push(bad));
check(rx.error()==4);
check(rx.receivedPixels()==1 && rx.nextSequence()==11);
check(rx.push(b));
check(rx.ready() && rx.completed()==1 && rx.pixelBytes()==8);
check(!rx.push(c));
check(rx.error()==5);
check(!rx.take(small));
check(rx.ready());
check(rx.take(out));
for(0=>int i;i<8;i++) check(out[i]==i+1);
check(!rx.ready() && rx.nextFrame()==5 && rx.nextSequence()==12);
check(MediaVideoFragment.encode(9,12,100,1,1,5,0,py,bad));
check(!rx.push(bad));
check(rx.error()==4);
check(rx.push(c));
check(rx.take(small));
check(small[0]==5 && small[3]==8);
check(rx.completed()==2 && rx.timestamp()==200);
check(rx.nextFrame()==6 && rx.nextSequence()==13);
rx.clear();
check(!rx.configured() && !rx.ready() && rx.completed()==0);
65536*65536=>int uintLimit;
check(rx.configure(9,uintLimit-1,uintLimit-1,0));
check(MediaVideoFragment.encode(9,uintLimit-1,100,2,1,uintLimit-1,0,px,bad));
check(!rx.push(bad));
check(rx.error()==6 && rx.receivedPixels()==0);
check(MediaVideoFragment.encode(9,uintLimit-1,100,1,1,uintLimit-1,0,px,bad));
check(rx.push(bad));
check(rx.take(small));
check(rx.nextSequence()==uintLimit && rx.nextFrame()==uintLimit);
check(!rx.push(a));
check(rx.error()==6);
check(rx.configure(9,10,4,100));
int raster[68];
check(MediaRasterFragment.encode(9,10,100,2,1,4,0,px,raster));
check(!rx.push(raster));
check(rx.error()==2);
int whole[52];
check(MediaVideoFrame.encode(9,10,100,1,1,px,whole));
check(!rx.push(whole));
check(rx.error()==2);
MediaSession server; MediaSession client;
check(server.configure(32,128));
check(client.configure(32,128));
check(server.listen(0));
check(client.connect(server.port()));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (!server.connected()||!client.connected());i++) { server.poll(); client.poll(); }
check(server.connected() && client.connected());
[a,b,c] @=> int packets[][];
for(0=>int j;j<3;j++) {
    check(client.sendPacket(packets[j]));
    for(0=>int i;i<100000 && !server.packetSize();i++) { client.poll(); server.poll(); }
    int got[68];
    check(server.receivePacket(got));
    check(rx.push(got));
    if(j==0) check(!rx.ready());
    else if(j==1) { check(rx.take(out)); check(out[0]==1 && out[7]==8); }
    else { check(rx.take(small)); check(small[0]==5 && small[3]==8); }
}
check(rx.completed()==2);
server.close();client.close();
check(rx.configure(9,0,0,0));
int half[524288];
1=>half[0]; 77=>half[524287];
int large[524352];
check(MediaVideoFragment.encode(9,0,100,512,512,0,0,half,large));
check(rx.push(large));
check(!rx.ready() && rx.receivedPixels()==131072);
2=>half[0]; 88=>half[524287];
check(MediaVideoFragment.encode(9,1,100,512,512,0,131072,half,large));
check(rx.push(large));
check(rx.ready() && rx.pixelBytes()==1048576);
int full[1048576];
check(rx.take(full));
check(full[0]==1 && full[524287]==77 && full[524288]==2 && full[1048575]==88);
""",109,p)


def transport_video_buffer():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
MediaVideoBuffer buf;
int a[68]; int b[68]; int c[68]; int d[68]; int e[68]; int f[68];
int out[4]; int wide[8];
check(!buf.configured());
check(!buf.push(a));
check(buf.error()==1);
check(!buf.pull(out));
check(buf.error()==1);
check(!buf.configure(0,0,0,0,2,2,12));
check(!buf.configure(9,0,0,0,0,1,12));
check(!buf.configure(9,0,0,0,257,1,12));
check(!buf.configure(9,0,0,0,2,0,12));
check(!buf.configure(9,0,0,0,2,3,12));
check(!buf.configure(9,0,0,0,2,2,4));
check(!buf.configure(9,0,0,0,2,1,3));
check(!buf.configure(9,0,0,0,2,1,67108865));
check(buf.configure(9,0,0,0,2,2,12));
check(buf.configured() && !buf.ready());
check(buf.capacity()==2 && buf.threshold()==2 && buf.budget()==12);
check(buf.available()==0 && buf.freeFrames()==2);
check(buf.bufferedBytes()==0 && buf.reservedBytes()==0);
check(MediaVideoFragment.encode(9,0,100,2,1,0,0,[1,2,3,4],a));
check(MediaVideoFragment.encode(9,1,100,2,1,0,1,[5,6,7,8],b));
check(MediaVideoFragment.encode(9,2,200,1,1,1,0,[9,10,11,12],c));
check(MediaVideoFragment.encode(9,3,300,1,1,2,0,[13,14,15,16],d));
check(MediaVideoFragment.encode(9,4,400,1,1,3,0,[17,18,19,20],e));
check(MediaVideoFragment.encode(9,5,500,1,1,4,0,[21,22,23,24],f));
check(!buf.pull(out));
check(buf.error()==8);
check(buf.push(a));
check(buf.reservedBytes()==8 && buf.bufferedBytes()==0);
check(buf.freeFrames()==1 && buf.available()==0);
check(buf.nextSequence()==1 && buf.nextFrame()==0);
check(!buf.push(a));
check(buf.error()==3 && buf.reservedBytes()==8);
check(buf.push(b));
check(buf.receivedFrames()==1 && buf.available()==1 && !buf.ready());
check(buf.bufferedBytes()==8 && buf.reservedBytes()==0);
check(buf.pixelBytes()==8 && buf.width()==2 && buf.height()==1);
check(buf.timestamp()==100 && buf.frameIndex()==0);
check(!buf.pull(wide));
check(buf.error()==8);
check(buf.push(c));
check(buf.available()==2 && buf.ready() && buf.freeFrames()==0);
check(buf.bufferedBytes()==12);
check(!buf.push(d));
check(buf.error()==7 && buf.overruns()==1);
check(buf.nextSequence()==3 && buf.nextFrame()==2);
check(!buf.pull(out));
check(buf.error()==2 && buf.available()==2);
check(buf.pull(wide));
check(wide[0]==1 && wide[7]==8);
check(buf.consumedFrames()==1 && buf.bufferedBytes()==4);
check(buf.frameIndex()==1 && buf.timestamp()==200 && buf.pixelBytes()==4);
check(buf.push(d));
check(buf.available()==2 && buf.bufferedBytes()==8);
check(buf.pull(out));
check(out[0]==9 && out[3]==12);
check(buf.pull(out));
check(out[0]==13 && out[3]==16);
check(buf.available()==0 && buf.pixelBytes()==0 && buf.ready());
check(!buf.pull(out));
check(buf.error()==9 && buf.underruns()==1 && !buf.ready());
check(!buf.pull(out));
check(buf.error()==8 && buf.underruns()==1);
check(buf.push(e));
check(!buf.ready());
check(buf.push(f));
check(buf.ready());
check(buf.pull(out));
check(out[0]==17 && out[3]==20);
check(buf.pull(out));
check(out[0]==21 && out[3]==24);
check(buf.receivedFrames()==5 && buf.consumedFrames()==5);
check(buf.configure(9,0,0,0,3,1,8));
check(buf.push(a));
check(buf.push(b));
check(!buf.push(c));
check(buf.error()==7 && buf.overruns()==1 && buf.freeFrames()==2);
check(buf.nextSequence()==2);
check(buf.pull(wide));
check(buf.push(c));
check(buf.pull(out));
check(out[0]==9);
check(buf.configure(9,0,0,0,2,1,4));
check(!buf.push(a));
check(buf.error()==2 && buf.reservedBytes()==0 && buf.nextSequence()==0);
buf.clear();
check(!buf.configured() && buf.capacity()==0 && buf.receivedFrames()==0);
check(buf.configure(9,0,0,0,2,2,12));
MediaSession server; MediaSession client;
check(server.configure(32,128));
check(client.configure(32,128));
check(server.listen(0));
check(client.connect(server.port()));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (!server.connected()||!client.connected());i++) { server.poll(); client.poll(); }
check(server.connected() && client.connected());
[a,b,c] @=> int packets[][];
for(0=>int j;j<3;j++) {
    check(client.sendPacket(packets[j]));
    for(0=>int i;i<100000 && !server.packetSize();i++) { client.poll(); server.poll(); }
    int got[68];
    check(server.receivePacket(got));
    check(buf.push(got));
}
check(buf.ready() && buf.available()==2);
check(buf.pull(wide));
check(wide[0]==1 && wide[7]==8);
check(buf.pull(out));
check(out[0]==9 && out[3]==12);
server.close();client.close();
""",106,p)


def transport_audio_video():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
MediaAudioVideo rx;
MediaPCMStream audio;
int p[60]; int v0[68]; int v1[68]; int v2[68]; int v3[68]; int pixels[4];
float pair[2]; float block[4];
check(!rx.configured());
check(!rx.push(p));
check(rx.error()==1);
check(!rx.configure(1,1,48000,2,1000,0,0,0,8,2,3,1,12));
check(!rx.configure(1,2,48000,2,1000,0,0,0,8,2,3,0,12));
check(rx.configure(1,2,48000,2,1000,0,0,0,8,2,3,1,12));
check(audio.configure(1,48000,2,1000,0));
check(rx.configured() && !rx.started() && !rx.ready());
check(rx.playTimestamp()==1000 && rx.channels()==2);
check(audio.encodeBlock([0.0,1.0,-1.0,0.0,0.5,-0.5],p));
check(rx.push(p));
check(rx.availableAudio()==3 && !rx.started());
check(!rx.pullAudio(pair));
check(rx.error()==5);
check(MediaVideoFragment.encode(2,0,1000,1,1,0,0,[1,2,3,4],v0));
check(MediaVideoFragment.encode(2,1,1021,1,1,1,0,[5,6,7,8],v1));
check(MediaVideoFragment.encode(2,2,1060,1,1,2,0,[9,10,11,12],v2));
check(MediaVideoFragment.encode(2,3,1061,1,1,3,0,[13,14,15,16],v3));
check(rx.push(v0));
check(rx.started() && rx.ready() && rx.videoDue());
check(rx.pendingVideo()==1 && rx.videoBytes()==4);
check(rx.videoWidth()==1 && rx.videoHeight()==1 && rx.videoIndex()==0);
check(rx.videoTimestamp()==1000 && rx.videoLateness()==0);
check(rx.audioLimit()==0);
check(!rx.pullAudio(pair));
check(rx.error()==6 && rx.consumedFrames()==0);
check(!rx.pullVideo([0]));
check(rx.error()==4 && rx.videoError()==2);
check(rx.pullVideo(pixels));
check(pixels[0]==1 && pixels[3]==4);
check(!rx.videoDue() && rx.pendingVideo()==0);
check(rx.push(v1));
check(rx.audioLimit()==2);
check(rx.pullAudio(pair));
check(pair[0]==0.0 && pair[1]==1.0);
check(rx.playTimestamp()==1020 && !rx.videoDue());
check(rx.audioLimit()==1);
check(!rx.pullAudio(block));
check(rx.error()==6 && rx.consumedFrames()==1);
check(rx.pullAudio(pair));
check(pair[0]==-1.0 && pair[1]==0.0);
check(rx.playTimestamp()==1041 && rx.videoDue());
check(rx.videoLateness()==20 && rx.videoIndex()==1);
check(rx.pullVideo(pixels));
check(pixels[0]==5 && pixels[3]==8);
check(rx.pullAudio(pair));
// PCM16 positive full scale is 32767; negative full scale is 32768.
check(Math.fabs(pair[0]-16384.0/32767.0)<0.000000001 && pair[1]==-0.5);
check(rx.playTimestamp()==1062 && rx.consumedFrames()==3);
check(rx.push(v2));
check(rx.videoDue() && rx.videoLateness()==2);
check(rx.pullVideo(pixels));
check(pixels[0]==9 && pixels[3]==12);
check(!rx.pullAudio(pair));
check(rx.error()==3 && rx.audioError()==9 && !rx.ready());
check(rx.playTimestamp()==1062 && rx.started());
check(rx.push(v3));
check(!rx.videoDue());
check(!rx.pullVideo(pixels));
check(rx.error()==7);
check(audio.encodeBlock([1.0,0.0,0.0,-1.0,0.5,0.5],p));
check(rx.push(p));
check(rx.ready() && rx.videoDue());
check(rx.videoLateness()==1);
check(rx.pullVideo(pixels));
check(pixels[0]==13 && pixels[3]==16);
check(rx.audioLimit()==3);
check(rx.pullAudio(block));
check(rx.playTimestamp()==1104 && rx.consumedFrames()==5);
check(!rx.push(v3));
check(rx.error()==4 && rx.videoError()==3);
rx.clear();
check(!rx.configured() && !rx.started());
check(rx.pendingVideo()==0 && rx.consumedFrames()==0);
check(rx.configure(1,2,48000,2,1000,0,0,0,8,2,3,1,12));
check(audio.configure(1,48000,2,1000,0));
check(audio.encodeBlock([0.0,1.0,-1.0,0.0,0.5,-0.5],p));
MediaSession server; MediaSession client;
check(server.configure(40,128));
check(client.configure(40,128));
check(server.listen(0));
check(client.connect(server.port()));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (!server.connected()||!client.connected());i++) { server.poll(); client.poll(); }
check(server.connected() && client.connected());
check(client.sendPacket(v0));
for(0=>int i;i<100000 && !server.packetSize();i++) { client.poll(); server.poll(); }
int got[68];
check(server.receivePacket(got));
check(rx.push(got));
check(!rx.started());
check(client.sendPacket(p));
for(0=>int i;i<100000 && !server.packetSize();i++) { client.poll(); server.poll(); }
check(server.receivePacket(p));
check(rx.push(p));
check(rx.videoDue());
check(rx.pullVideo(pixels));
check(pixels[3]==4);
check(rx.pullAudio(block));
check(rx.playTimestamp()==1041);
server.close();client.close();
""",93,p)


def transport_av_consolidation():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
int pcm[56]; int framePacket[52]; int fragA[68]; int fragB[68]; int nextFrag[68];
int meta[9]; int pixels[8]; int small[4]; float stereo[2]; float both[4];
check(MediaPCM.encode(1,0,0,48000,2,[0.0,1.0,-1.0,0.0],pcm));
check(MediaVideoFrame.encode(2,0,0,1,1,[1,2,3,4],framePacket));
check(MediaVideoFragment.encode(2,0,0,2,1,0,0,[1,2,3,4],fragA));
check(MediaVideoFragment.encode(2,1,0,2,1,0,1,[5,6,7,8],fragB));
check(MediaVideoFragment.encode(2,2,41,1,1,1,0,[9,10,11,12],nextFrag));
check(MediaAVPacket.inspect(pcm,meta)==1);
check(meta[0]==1 && meta[1]==1 && meta[2]==0 && meta[3]==0);
check(meta[4]==48000 && meta[5]==2 && meta[6]==2 && meta[7]==0 && meta[8]==0);
check(MediaAVPacket.inspect(framePacket,meta)==2);
check(meta[0]==2 && meta[1]==2 && meta[4]==1 && meta[5]==1 && meta[6]==0 && meta[7]==0 && meta[8]==1);
check(MediaAVPacket.inspect(fragB,meta)==3);
check(meta[0]==3 && meta[1]==2 && meta[2]==1 && meta[3]==0);
check(meta[4]==2 && meta[5]==1 && meta[6]==0 && meta[7]==1 && meta[8]==1);
check(!MediaAVPacket.inspect(fragA,[0]));
int midi[40];
check(MediaMIDI.encode(3,0,0,144,60,100,midi));
check(!MediaAVPacket.inspect(midi,meta));
check(meta[0]==3 && meta[2]==1);
fragA[67]^1=>fragA[67];
check(!MediaAVPacket.inspect(fragA,meta));
check(meta[0]==3 && meta[7]==1);
fragA[67]^1=>fragA[67];
check(MediaAVPacket.inspect(fragA,meta)==3);
MediaAudioVideo rx;
check(rx.configure(1,2,48000,2,0,0,0,0,4,2,2,1,12));
check(rx.push(pcm));
check(!rx.started());
check(!rx.push(framePacket));
check(rx.error()==4 && rx.videoError()==2);
check(rx.push(fragA));
check(!rx.started());
check(rx.push(fragB));
check(rx.started() && rx.videoDue());
check(rx.pullVideo(pixels));
check(pixels[0]==1 && pixels[7]==8);
check(rx.push(nextFrag));
check(rx.audioLimit()==2);
float tooMany[6];
check(!rx.pullAudio(tooMany));
check(rx.error()==6 && rx.ready() && rx.consumedFrames()==0);
check(rx.pullAudio(both));
check(rx.playTimestamp()==41 && rx.videoDue());
check(rx.pullVideo(small));
check(small[0]==9 && small[3]==12);
check(!rx.pullAudio(stereo));
check(rx.error()==3 && rx.audioError()==9 && !rx.ready());
check(rx.playTimestamp()==41);
check(MediaPCM.encode(1,1,41,48000,2,[1.0,0.0,0.0,-1.0],pcm));
check(rx.push(pcm));
check(rx.ready() && rx.audioLimit()==2);
check(rx.pullAudio(both));
check(rx.playTimestamp()==83 && rx.consumedFrames()==4);
check(both[0]==1.0 && both[3]==-1.0);
rx.clear();
check(rx.configure(1,2,48000,2,0,0,0,0,4,2,2,1,12));
check(MediaPCM.encode(1,0,0,48000,2,[0.0,1.0,-1.0,0.0],pcm));
MediaSession server; MediaSession client;
check(server.configure(40,128));
check(client.configure(40,128));
check(server.listen(0));
check(client.connect(server.port()));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (!server.connected()||!client.connected());i++) {server.poll();client.poll();}
check(server.connected() && client.connected());
check(server.mediaMask()==40 && client.mediaMask()==40);
[fragA,pcm,fragB,nextFrag] @=> int packets[][];
for(0=>int j;j<4;j++) {
    check(client.sendPacket(packets[j]));
    for(0=>int i;i<100000 && !server.packetSize();i++) {client.poll();server.poll();}
    int got[packets[j].size()];
    check(server.receivePacket(got));
    if(j==1) check(MediaAVPacket.inspect(got,meta)==1);
    else check(MediaAVPacket.inspect(got,meta)==3);
    check(rx.push(got));
}
check(rx.ready() && rx.pendingVideo()==2 && rx.videoDue());
check(rx.pullVideo(pixels));
check(pixels[7]==8);
check(rx.pullAudio(both));
check(rx.videoDue() && rx.playTimestamp()==41);
check(rx.pullVideo(small));
check(small[3]==12 && rx.pendingVideo()==0);
server.close();client.close();
""",80,p)


def transport_text():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
"Ciao, 世界! é 😀" => string message;
int packet[MediaText.packetSize(message.length())];
int meta[4];
check(MediaText.packetSize(0)==0);
check(MediaText.packetSize(-1)==0);
check(MediaText.packetSize(1048564)==1048608);
check(MediaText.packetSize(1048565)==0);
check(packet.size()==44+message.length());
check(MediaText.encode(9,7,123,message,packet));
check(packet[5]==7);
check(packet[32]==67 && packet[33]==75 && packet[34]==84 && packet[35]==88);
check(packet[36]==1 && packet[37]==1 && packet[38]==0 && packet[39]==0);
check(MediaText.inspect(packet,meta));
check(meta[0]==9 && meta[1]==7 && meta[2]==123 && meta[3]==message.length());
check(MediaText.decode(packet)==message);
check(!MediaText.encode(0,7,123,message,packet));
check(!MediaText.encode(9,-1,123,message,packet));
check(!MediaText.encode(9,7,-1,message,packet));
65536*65536=>int uintLimit;
check(!MediaText.encode(uintLimit-1,7,123,message,packet));
check(!MediaText.encode(9,uintLimit,123,message,packet));
check(!MediaText.encode(9,7,123,"",packet));
check(!MediaText.encode(9,7,123,message,[0]));
check(!MediaText.inspect(packet,[0]));
check(MediaText.decode(packet)==message);
packet[44]^1=>packet[44];
check(!MediaText.inspect(packet,meta));
check(MediaText.decode(packet)=="");
check(meta[0]==9 && meta[1]==7);
packet[44]^1=>packet[44];
check(MediaText.inspect(packet,meta));
int raw[12+message.length()];
int outer[4];
int malformed[packet.size()];
check(MediaTransport.decode(packet,outer,raw));
check(MediaTransport.encode(2,9,7,123,raw,malformed));
check(!MediaText.inspect(malformed,meta));
2=>raw[4];
check(MediaTransport.encode(7,9,7,123,raw,malformed));
check(!MediaText.inspect(malformed,meta));
1=>raw[4];
1=>raw[6];
check(MediaTransport.encode(7,9,7,123,raw,malformed));
check(!MediaText.inspect(malformed,meta));
0=>raw[6];
0=>raw[12];
check(MediaTransport.encode(7,9,7,123,raw,malformed));
check(MediaText.decode(malformed)=="");
192=>raw[12];128=>raw[13];
check(MediaTransport.encode(7,9,7,123,raw,malformed));
check(!MediaText.inspect(malformed,meta));
237=>raw[12];160=>raw[13];128=>raw[14];
check(MediaTransport.encode(7,9,7,123,raw,malformed));
check(!MediaText.inspect(malformed,meta));
244=>raw[12];144=>raw[13];128=>raw[14];128=>raw[15];
check(MediaTransport.encode(7,9,7,123,raw,malformed));
check(!MediaText.inspect(malformed,meta));
[67,75,84,88,1,1,0,0,0,0,0,1,195] @=> int truncated[];
int shortPacket[45];
check(MediaTransport.encode(7,9,8,124,truncated,shortPacket));
check(!MediaText.inspect(shortPacket,meta));
[67,75,84,88,1,1,0,0,0,0,0,4,240,159,152,128] @=> int emojiBytes[];
int emojiPacket[48];
check(MediaTransport.encode(7,9,8,124,emojiBytes,emojiPacket));
check(MediaText.inspect(emojiPacket,meta));
check(meta[3]==4 && MediaText.decode(emojiPacket)=="😀");
int vectorPacket[MediaVector.packetSize(message.length())];
check(MediaVector.encode(9,7,123,message,vectorPacket));
check(!MediaText.inspect(vectorPacket,meta));
check(!MediaVector.inspect(packet,meta));
MediaSession server; MediaSession client;
check(server.configure(64,1024));
check(client.configure(66,1024));
check(server.listen(0));
check(client.connect(server.port()));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (!server.connected()||!client.connected());i++) {server.poll();client.poll();}
check(server.connected() && client.connected());
check(server.mediaMask()==64 && client.mediaMask()==64);
check(!client.sendPacket(vectorPacket));
check(client.sendPacket(packet));
for(0=>int i;i<100000 && !server.packetSize();i++) {client.poll();server.poll();}
int got[packet.size()];
check(server.receivePacket(got));
check(MediaText.inspect(got,meta));
check(MediaText.decode(got)==message);
check(server.sendPacket(got));
for(0=>int i;i<100000 && !client.packetSize();i++) {server.poll();client.poll();}
check(client.receivePacket(got));
check(MediaText.decode(got)==message);
server.close();client.close();
""",63,p)


def transport_text_fragments():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
"A😀éZ" => string message;
int a[MediaTextFragment.packetSize(3)];
int b[MediaTextFragment.packetSize(message.length()-3)];
int wrong[b.size()];
int meta[7];
MediaTextAssembler rx;
check(MediaTextFragment.packetSize(0)==0);
check(MediaTextFragment.packetSize(-1)==0);
check(MediaTextFragment.packetSize(1048556)==1048608);
check(MediaTextFragment.packetSize(1048557)==0);
check(a.size()==55);
check(MediaTextFragment.encode(9,10,123,10,0,3,message,a));
check(MediaTextFragment.encode(9,11,123,10,3,message.length()-3,message,b));
check(MediaTextFragment.inspect(a,meta));
check(meta[0]==9 && meta[1]==10 && meta[2]==123 && meta[3]==10);
check(meta[4]==message.length() && meta[5]==0 && meta[6]==3);
check(!MediaTextFragment.inspect(a,[0]));
check(MediaText.decode(a)=="");
check(!MediaTextFragment.encode(0,10,123,10,0,3,message,a));
check(!MediaTextFragment.encode(9,10,123,10,1,3,message,a));
check(!MediaTextFragment.encode(9,10,123,10,0,3,message,[0]));
check(!rx.ready() && rx.size()==0 && rx.text()=="");
check(rx.push(b)==0);
check(rx.push(a)==1);
check(!rx.ready() && rx.size()==3 && rx.text()=="");
check(rx.push(a)==0);
check(MediaTextFragment.encode(9,12,123,10,3,message.length()-3,message,wrong));
check(rx.push(wrong)==0);
check(MediaTextFragment.encode(8,11,123,10,3,message.length()-3,message,wrong));
check(rx.push(wrong)==0);
check(MediaTextFragment.encode(9,11,124,10,3,message.length()-3,message,wrong));
check(rx.push(wrong)==0);
b[52]^1=>b[52];
check(rx.push(b)==0);
b[52]^1=>b[52];
check(rx.size()==3);
check(rx.push(b)==2);
check(rx.ready() && rx.size()==message.length());
check(rx.text()==message);
check(rx.push(a)==0);
rx.reset();
check(!rx.ready() && rx.size()==0 && rx.text()=="");
check(rx.push(a)==1);
check(rx.push(b)==2);
check(rx.text()==message);
rx.reset();
int single[MediaTextFragment.packetSize(message.length())];
check(MediaTextFragment.encode(9,12,125,12,0,message.length(),message,single));
check(rx.push(single)==2);
check(rx.text()==message);
rx.reset();
MediaSession server; MediaSession client;
check(server.configure(64,1024));
check(client.configure(64,1024));
check(server.listen(0));
check(client.connect(server.port()));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (!server.connected()||!client.connected());i++){server.poll();client.poll();}
check(server.connected() && client.connected());
check(client.sendPacket(a));
for(0=>int i;i<100000 && !server.packetSize();i++){client.poll();server.poll();}
int gotA[a.size()];
check(server.receivePacket(gotA));
check(rx.push(gotA)==1);
for(0=>int i;i<100000 && client.sendPending();i++){client.poll();server.poll();}
check(client.sendPacket(b));
for(0=>int i;i<100000 && !server.packetSize();i++){client.poll();server.poll();}
int gotB[b.size()];
check(server.receivePacket(gotB));
check(rx.push(gotB)==2);
check(rx.text()==message);
server.close();client.close();
""",52,p)


def transport_text_buffer():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
MediaTextBuffer rx;
"hello" => string msg;
"A😀Z" => string unicode;
int a[MediaText.packetSize(msg.length())];
int b[MediaTextFragment.packetSize(2)];
int c[MediaTextFragment.packetSize(unicode.length()-2)];
int d[MediaText.packetSize(1)];
int bad[d.size()];
check(MediaText.encode(9,10,100,msg,a));
check(MediaTextFragment.encode(9,11,200,11,0,2,unicode,b));
check(MediaTextFragment.encode(9,12,200,11,2,unicode.length()-2,unicode,c));
check(MediaText.encode(9,13,300,"x",d));
check(!rx.push(a));
check(!rx.advance(0));
check(!rx.configure(0,10,50,2,20));
check(!rx.configure(9,10,50,257,20));
check(!rx.configure(9,10,50,2,0));
check(rx.configure(9,10,50,2,20));
check(rx.nextSequence()==10);
check(!rx.ready() && rx.pull()=="");
check(!rx.push(b));
check(rx.push(a));
check(rx.available()==1 && rx.bufferedBytes()==msg.length());
check(rx.timestamp()==100 && rx.nextSequence()==11);
check(!rx.push(a));
check(rx.push(b));
check(rx.reservedBytes()==unicode.length() && rx.available()==1);
check(rx.nextSequence()==12);
check(!rx.push(d));
check(rx.push(c));
check(rx.reservedBytes()==0 && rx.available()==2);
check(rx.bufferedBytes()==msg.length()+unicode.length());
check(!rx.push(d));
check(rx.nextSequence()==13);
check(rx.advance(149));
check(!rx.ready() && rx.pull()=="");
check(rx.advance(150));
check(rx.ready());
check(rx.pull()==msg);
check(rx.available()==1 && rx.timestamp()==200);
check(!rx.ready());
check(!rx.advance(149));
check(rx.push(d));
check(rx.advance(249));
check(!rx.ready());
check(rx.advance(250));
check(rx.pull()==unicode);
check(rx.bufferedBytes()==1 && rx.available()==1);
check(rx.advance(350));
check(rx.pull()=="x");
check(!rx.ready() && rx.available()==0 && rx.bufferedBytes()==0);
check(MediaText.encode(9,14,299,"z",bad));
check(!rx.push(bad));
check(MediaText.encode(8,14,400,"z",bad));
check(!rx.push(bad));
check(MediaText.encode(9,14,400,"z",bad));
check(rx.push(bad));
check(rx.advance(450));
check(rx.pull()=="z");
check(rx.configure(9,11,0,1,unicode.length()-1));
check(!rx.push(b));
check(rx.reservedBytes()==0 && rx.nextSequence()==11);
check(rx.configure(9,11,0,1,unicode.length()));
check(rx.push(b));
check(rx.advance(200));
check(!rx.ready());
check(rx.push(c));
check(rx.ready() && rx.pull()==unicode);
rx.clear();
check(rx.available()==0 && rx.reservedBytes()==0 && !rx.push(a));
check(rx.configure(9,10,0,1,20));
check(rx.advance(100));
check(rx.push(a) && rx.ready());
check(rx.pull()==msg);
""",65,p)


def transport_heterogeneous():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
int raster[52]; int rasterFrag[68]; int model[96];
"<svg xmlns='http://www.w3.org/2000/svg'/>" => string markup;
int vector[MediaVector.packetSize(markup.length())];
int pcm[56]; int midi[40]; int video[52]; int videoFrag[68];
"Hi😀" => string msg;
int textPacket[MediaText.packetSize(msg.length())];
int textA[MediaTextFragment.packetSize(3)];
int textB[MediaTextFragment.packetSize(msg.length()-3)];
int meta[6];
check(MediaRaster.encode(1,0,100,1,1,[1,2,3,4],raster));
check(MediaRasterFragment.encode(1,1,101,1,1,0,0,[5,6,7,8],rasterFrag));
check(MediaVector.encode(2,0,90,markup,vector));
check(MediaModel.encode(3,0,80,[0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,0.0],[0,1,2],model));
check(MediaPCM.encode(4,0,70,48000,2,[0.0,1.0,-1.0,0.0],pcm));
check(MediaMIDI.encode(5,0,60,144,60,100,midi));
check(MediaVideoFrame.encode(6,0,50,1,1,[1,2,3,4],video));
check(MediaVideoFragment.encode(6,1,51,1,1,0,0,[5,6,7,8],videoFrag));
check(MediaText.encode(7,0,40,msg,textPacket));
check(MediaTextFragment.encode(7,1,41,1,0,3,msg,textA));
check(MediaTextFragment.encode(7,2,41,1,3,msg.length()-3,msg,textB));
[raster,rasterFrag,vector,model,pcm,midi,video,videoFrag,textPacket,textA,textB] @=> int packets[][];
[1,2,3,4,5,6,7,8,9,10,10] @=> int kinds[];
[1,1,2,3,4,5,6,6,7,7,7] @=> int types[];
for(0=>int j;j<11;j++){
    check(MediaPacket.inspect(packets[j],meta)==kinds[j]);
    check(meta[0]==kinds[j] && meta[1]==types[j] && meta[2]==types[j]);
    check(meta[5]==packets[j].size()-32);
}
check(!MediaPacket.inspect(raster,[0]));
raster[51]^1=>raster[51];
check(!MediaPacket.inspect(raster,meta));
raster[51]^1=>raster[51];
int malformed[36];
check(MediaTransport.encode(7,7,0,40,[1,2,3,4],malformed));
check(!MediaPacket.inspect(malformed,meta));
MediaRouter rx;
check(!rx.addStream(1,1,0,0));
check(!rx.configure(0));
check(rx.configure(64));
check(!rx.addStream(1,1,0,0));
check(rx.configure(127));
for(1=>int j;j<=7;j++) check(rx.addStream(j,j,0,0));
check(rx.streams()==7);
check(!rx.addStream(1,2,0,0));
check(!rx.route(textB,meta));
check(!rx.route(raster,[0]));
check(rx.nextSequence(1)==0);
MediaSession server; MediaSession client;
check(server.configure(127,256));
check(client.configure(127,512));
check(server.listen(0));
check(client.connect(server.port()));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (!server.connected()||!client.connected());i++){server.poll();client.poll();}
check(server.connected() && client.connected());
check(server.mediaMask()==127 && client.mediaMask()==127);
[0,9,2,4,5,6,3,1,7,8,10] @=> int order[];
// Preserve text stream order while interleaving unrelated streams.
8=>order[1];9=>order[9];
MediaTextBuffer tb;
check(tb.configure(7,0,0,4,64));
for(0=>int j;j<11;j++){
    order[j]=>int k;
    for(0=>int i;i<100000 && client.sendPending();i++){client.poll();server.poll();}
    check(client.sendPacket(packets[k]));
    for(0=>int i;i<100000 && !server.packetSize();i++){client.poll();server.poll();}
    int got[packets[k].size()];
    check(server.receivePacket(got));
    check(rx.route(got,meta)==kinds[k]);
    check(meta[2]==types[k] && meta[0]==kinds[k]);
    if(types[k]==7) check(tb.push(got));
}
check(rx.packets(1)==2 && rx.packets(7)==3);
check(rx.nextSequence(7)==3 && rx.nextSequence(4)==1);
check(!rx.route(textB,meta));
check(tb.available()==2);
check(tb.advance(40));
check(tb.pull()==msg && !tb.ready());
check(tb.advance(41));
check(tb.pull()==msg && tb.available()==0);
rx.clear();
check(rx.streams()==0 && !rx.route(raster,meta));
server.close();client.close();
""",129,p)


def transport_raster_lossless():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
int pixels[520]; int restored[520];
for(0=>int i;i<130;i++){ 17=>pixels[4*i]; 99=>pixels[4*i+1]; 255=>pixels[4*i+2]; 3=>pixels[4*i+3]; }
check(MediaRasterCompressed.packetSize(130,1,pixels)==58);
int packet[MediaRasterCompressed.packetSize(130,1,pixels)];
int meta[5]; int routeMeta[6];
check(MediaRasterCompressed.packetSize(0,1,pixels)==0);
check(MediaRasterCompressed.packetSize(130,2,pixels)==0);
check(MediaRasterCompressed.packetSize(-1,1,pixels)==0);
check(MediaRasterCompressed.encode(1,0,100,130,1,pixels,packet));
check(packet[32]==67 && packet[33]==75 && packet[34]==82 && packet[35]==67);
check(packet[48]==255 && packet[53]==129);
check(MediaRasterCompressed.inspect(packet,meta));
check(meta[0]==1 && meta[1]==0 && meta[2]==100 && meta[3]==130 && meta[4]==1);
check(MediaRasterCompressed.decode(packet,restored));
1=>int identical;
for(0=>int i;i<pixels.size();i++) if(pixels[i]!=restored[i]) 0=>identical;
check(identical);
check(!MediaRasterCompressed.decode(packet,[0]));
check(!MediaRasterCompressed.inspect(packet,[0]));
check(!MediaRasterCompressed.encode(0,0,100,130,1,pixels,packet));
check(!MediaRasterCompressed.encode(1,-1,100,130,1,pixels,packet));
check(!MediaRasterCompressed.encode(1,0,-1,130,1,pixels,packet));
check(!MediaRasterCompressed.encode(1,0,100,130,1,pixels,[0]));
256=>pixels[0];
check(MediaRasterCompressed.packetSize(130,1,pixels)==0);
17=>pixels[0];
check(MediaGraphics.inspect(packet,routeMeta)==5);
check(MediaPacket.inspect(packet,routeMeta)==11);
check(routeMeta[0]==11 && routeMeta[1]==1 && routeMeta[2]==1 && routeMeta[5]==26);
MediaRouter router;
check(router.configure(1));
check(router.addStream(1,1,0,0));
check(router.route(packet,routeMeta)==11);
check(!router.route(packet,routeMeta));
packet[49]^1=>packet[49];
check(!MediaRasterCompressed.inspect(packet,meta));
packet[49]^1=>packet[49];
int raw[26]; int outer[4]; int malformed[58];
check(MediaTransport.decode(packet,outer,raw));
127=>raw[16];
check(MediaTransport.encode(1,1,0,100,raw,malformed));
check(!MediaRasterCompressed.inspect(malformed,meta));
255=>raw[16]; 130=>raw[21];
check(MediaTransport.encode(1,1,0,100,raw,malformed));
check(!MediaRasterCompressed.inspect(malformed,meta));
129=>raw[21]; 1=>raw[6];
check(MediaTransport.encode(1,1,0,100,raw,malformed));
check(!MediaRasterCompressed.inspect(malformed,meta));
[1,2,3,4,5,6,7,8,9,10,11,12] @=> int literal[];
int lp[MediaRasterCompressed.packetSize(3,1,literal)];
int output[12];
check(lp.size()==61);
check(MediaRasterCompressed.encode(1,1,101,3,1,literal,lp));
check(lp[48]==2);
check(MediaRasterCompressed.decode(lp,output));
1=>identical;
for(0=>int i;i<12;i++) if(literal[i]!=output[i]) 0=>identical;
check(identical);
check(router.route(lp,routeMeta)==11);
MediaSession server; MediaSession client;
check(server.configure(1,128));
check(client.configure(1,128));
check(server.listen(0));
check(client.connect(server.port()));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (!server.connected()||!client.connected());i++){server.poll();client.poll();}
check(server.connected() && client.connected());
check(client.sendPacket(packet));
for(0=>int i;i<100000 && !server.packetSize();i++){client.poll();server.poll();}
int got[packet.size()];
check(server.receivePacket(got));
check(MediaRasterCompressed.decode(got,restored));
1=>identical;
for(0=>int i;i<pixels.size();i++) if(pixels[i]!=restored[i]) 0=>identical;
check(identical);
server.close();client.close();
""",49,p)


def transport_raster_png():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
[255,0,0,255,0,255,0,0,0,0,255,128,12,34,56,78] @=> int pixels[];
int recovered[16]; int meta[5]; int routeMeta[6];
check(MediaRasterPng.packetSize(0,2,pixels)==0);
check(MediaRasterPng.packetSize(2,3,pixels)==0);
check(MediaRasterPng.packetSize(-1,2,pixels)==0);
MediaRasterPng.packetSize(2,2,pixels)=>int size;
check(size>56);
int packet[size];
check(MediaRasterPng.encode(1,0,100,2,2,pixels,packet));
check(packet[32]==67 && packet[33]==75 && packet[34]==80 && packet[35]==78);
check(packet[48]==137 && packet[49]==80 && packet[50]==78 && packet[51]==71);
check(MediaRasterPng.inspect(packet,meta));
check(meta[0]==1 && meta[1]==0 && meta[2]==100 && meta[3]==2 && meta[4]==2);
check(MediaRasterPng.decode(packet,recovered));
for(0=>int i;i<16;i++) check(recovered[i]==pixels[i]);
check(!MediaRasterPng.decode(packet,[0]));
check(!MediaRasterPng.inspect(packet,[0]));
check(!MediaRasterPng.encode(0,0,100,2,2,pixels,packet));
check(!MediaRasterPng.encode(1,-1,100,2,2,pixels,packet));
check(!MediaRasterPng.encode(1,0,-1,2,2,pixels,packet));
check(!MediaRasterPng.encode(1,0,100,2,2,pixels,[0]));
check(MediaGraphics.inspect(packet,routeMeta)==6);
check(MediaPacket.inspect(packet,routeMeta)==12);
check(routeMeta[0]==12 && routeMeta[1]==1 && routeMeta[2]==1 && routeMeta[5]==size-32);
MediaRouter router;
check(router.configure(1));
check(router.addStream(1,1,0,0));
check(router.route(packet,routeMeta)==12);
check(!router.route(packet,routeMeta));
packet[48]^1=>packet[48];
check(!MediaRasterPng.inspect(packet,meta));
packet[48]^1=>packet[48];
int raw[size-32]; int outer[4]; int malformed[size];
check(MediaTransport.decode(packet,outer,raw));
raw[11]=>int widthByte;
3=>raw[11];
check(MediaTransport.encode(1,1,0,100,raw,malformed));
check(!MediaRasterPng.inspect(malformed,meta));
widthByte=>raw[11];
raw[16]^1=>raw[16];
check(MediaTransport.encode(1,1,0,100,raw,malformed));
check(!MediaRasterPng.inspect(malformed,meta));
raw[16]^1=>raw[16];
int trailing[size-31];
for(0=>int i;i<raw.size();i++) raw[i]=>trailing[i];
int extra[size+1];
check(MediaTransport.encode(1,1,0,100,trailing,extra));
check(!MediaRasterPng.inspect(extra,meta));
MediaSession server; MediaSession client;
check(server.configure(1,1048576));
check(client.configure(1,1048576));
check(server.listen(0));
check(client.connect(server.port()));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (!server.connected()||!client.connected());i++){server.poll();client.poll();}
check(server.connected() && client.connected());
check(client.sendPacket(packet));
for(0=>int i;i<100000 && !server.packetSize();i++){client.poll();server.poll();}
int got[size];
check(server.receivePacket(got));
check(MediaRasterPng.decode(got,recovered));
1=>int identical;
for(0=>int i;i<16;i++) if(pixels[i]!=recovered[i]) 0=>identical;
check(identical);
server.close();client.close();
""",57,p)


def transport_raster_photo():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
[90,90,90,255,90,90,90,255,90,90,90,255,90,90,90,255] @=> int opaque[];
[255,0,0,0,0,255,0,128,0,0,255,255,12,34,56,78] @=> int alpha[];
int decoded[16]; int jm[5]; int wm[6]; int meta[6];
check(MediaRasterJpeg.packetSize(2,2,0,opaque)==0);
check(MediaRasterJpeg.packetSize(2,2,101,opaque)==0);
check(MediaRasterJpeg.packetSize(2,2,90,alpha)==0);
check(MediaRasterJpeg.packetSize(3,2,90,opaque)==0);
check(MediaRasterWebp.packetSize(2,2,0,1,alpha)==0);
check(MediaRasterWebp.packetSize(2,2,90,2,alpha)==0);
check(MediaRasterWebp.packetSize(0,2,90,1,alpha)==0);
MediaRasterJpeg.packetSize(2,2,90,opaque)=>int js;
MediaRasterWebp.packetSize(2,2,90,1,alpha)=>int ws;
MediaRasterWebp.packetSize(2,2,90,0,alpha)=>int ls;
check(js>48 && ws>48 && ls>48);
int jp[js]; int wp[ws]; int lp[ls];
check(MediaRasterJpeg.encode(1,0,100,2,2,90,opaque,jp));
check(MediaRasterWebp.encode(1,1,101,2,2,90,1,alpha,wp));
check(MediaRasterWebp.encode(1,2,102,2,2,90,0,alpha,lp));
check(jp[34]==74 && jp[35]==80 && jp[48]==255 && jp[49]==216);
check(wp[34]==87 && wp[35]==80 && wp[38]==1);
check(lp[38]==0);
check(MediaRasterJpeg.inspect(jp,jm));
check(jm[0]==1 && jm[1]==0 && jm[2]==100 && jm[3]==2 && jm[4]==2);
check(MediaRasterWebp.inspect(wp,wm));
check(wm[0]==1 && wm[1]==1 && wm[2]==101 && wm[3]==2 && wm[4]==2 && wm[5]==1);
check(MediaRasterWebp.inspect(lp,wm) && wm[5]==0);
check(MediaRasterJpeg.decode(jp,decoded));
for(0=>int i;i<4;i++){
    check(decoded[4*i+3]==255);
    check(decoded[4*i]>=85 && decoded[4*i]<=95);
}
check(MediaRasterWebp.decode(wp,decoded));
for(0=>int i;i<16;i++) check(decoded[i]==alpha[i]);
check(MediaRasterWebp.decode(lp,decoded));
for(0=>int i;i<4;i++) check(decoded[4*i+3]==alpha[4*i+3]);
check(!MediaRasterJpeg.decode(wp,decoded));
check(!MediaRasterWebp.decode(jp,decoded));
check(!MediaRasterWebp.decode(wp,[0]));
check(!MediaRasterJpeg.inspect(jp,[0]));
check(!MediaRasterWebp.inspect(wp,[0]));
check(!MediaRasterJpeg.encode(0,0,100,2,2,90,opaque,jp));
check(!MediaRasterWebp.encode(1,-1,101,2,2,90,1,alpha,wp));
check(!MediaRasterWebp.encode(1,1,101,2,2,90,1,alpha,[0]));
check(MediaGraphics.inspect(jp,meta)==7);
check(MediaGraphics.inspect(wp,meta)==8);
check(MediaPacket.inspect(jp,meta)==13);
check(MediaPacket.inspect(wp,meta)==14);
check(MediaPacket.inspect(lp,meta)==14);
MediaRouter router;
check(router.configure(1));
check(router.addStream(1,1,0,0));
check(router.route(jp,meta)==13);
check(router.route(wp,meta)==14);
check(router.route(lp,meta)==14);
check(!router.route(lp,meta));
int raw[ws-32]; int outer[4]; int malformed[ws];
check(MediaTransport.decode(wp,outer,raw));
0=>raw[6];
check(MediaTransport.encode(1,1,1,101,raw,malformed));
check(!MediaRasterWebp.inspect(malformed,wm));
1=>raw[6];3=>raw[11];
check(MediaTransport.encode(1,1,1,101,raw,malformed));
check(!MediaRasterWebp.inspect(malformed,wm));
wp[48]^1=>wp[48];
check(!MediaRasterWebp.inspect(wp,wm));
wp[48]^1=>wp[48];
MediaSession server; MediaSession client;
check(server.configure(1,1048576));
check(client.configure(1,1048576));
check(server.listen(0));
check(client.connect(server.port()));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (!server.connected()||!client.connected());i++){server.poll();client.poll();}
check(server.connected() && client.connected());
check(client.sendPacket(jp));
for(0=>int i;i<100000 && !server.packetSize();i++){client.poll();server.poll();}
int gotJ[js];
check(server.receivePacket(gotJ));
check(MediaRasterJpeg.decode(gotJ,decoded));
for(0=>int i;i<100000 && client.sendPending();i++){client.poll();server.poll();}
check(client.sendPacket(wp));
for(0=>int i;i<100000 && !server.packetSize();i++){client.poll();server.poll();}
int gotW[ws];
check(server.receivePacket(gotW));
check(MediaRasterWebp.decode(gotW,decoded));
1=>int identical;
for(0=>int i;i<16;i++) if(decoded[i]!=alpha[i]) 0=>identical;
check(identical);
server.close();client.close();
""",88,p)


def transport_raster_compressed_fragments():
    with tempfile.TemporaryDirectory() as directory:
        p = pathlib.Path(directory)
        run("transport", """
[255,0,0,0,200,100,50,128,10,20,30,255,0,0,255,64] @=> int src[];
int opaque[16];
check(MediaRasterJpeg.flatten(2,2,20,40,60,src,opaque));
check(opaque[0]==20 && opaque[1]==40 && opaque[2]==60 && opaque[3]==255);
check(opaque[4]==110 && opaque[5]==70 && opaque[6]==55 && opaque[7]==255);
check(opaque[8]==10 && opaque[9]==20 && opaque[10]==30 && opaque[11]==255);
check(opaque[12]==15 && opaque[13]==30 && opaque[14]==109 && opaque[15]==255);
check(src[0]==255 && src[3]==0 && src[7]==128);
check(!MediaRasterJpeg.flatten(2,2,-1,40,60,src,opaque));
check(!MediaRasterJpeg.flatten(2,2,256,40,60,src,opaque));
check(!MediaRasterJpeg.flatten(3,2,20,40,60,src,opaque));
check(!MediaRasterJpeg.flatten(2,2,20,40,60,src,[0]));
check(opaque[0]==20 && opaque[15]==255);
check(MediaRasterJpeg.packetSize(2,2,90,opaque)>48);
check(MediaRasterJpeg.packetSize(2,2,90,src)==0);
[10,20,30,255,10,20,30,255] @=> int top[];
[50,60,70,255,50,60,70,255] @=> int bottom[];
MediaRasterCompressedAssembler rx;
int meta[9]; int routed[6]; int output[16];
check(!rx.ready() && rx.rows()==0 && rx.size()==0);
check(MediaRasterCompressedFragment.packetSize(2,2,2,1,1,90,top)==0);
check(MediaRasterCompressedFragment.packetSize(2,2,0,0,1,90,top)==0);
check(MediaRasterCompressedFragment.packetSize(2,2,0,1,6,90,top)==0);
check(MediaRasterCompressedFragment.packetSize(2,2,0,2,1,90,top)==0);
for(1=>int codec;codec<=5;codec++){
    rx.reset();
    MediaRasterCompressedFragment.packetSize(2,2,0,1,codec,90,top)=>int sa;
    MediaRasterCompressedFragment.packetSize(2,2,1,1,codec,90,bottom)=>int sb;
    check(sa>64 && sb>64);
    int a[sa]; int b[sb];
    check(MediaRasterCompressedFragment.encode(3,10,100,2,2,7,0,1,codec,90,top,a));
    check(MediaRasterCompressedFragment.encode(3,11,100,2,2,7,1,1,codec,90,bottom,b));
    check(MediaRasterCompressedFragment.inspect(a,meta));
    check(meta[0]==3 && meta[1]==10 && meta[2]==100 && meta[3]==2 && meta[4]==2 && meta[5]==7 && meta[6]==0 && meta[7]==1 && meta[8]==codec);
    check(MediaGraphics.inspect(a,routed)==9);
    check(MediaPacket.inspect(a,routed)==15);
    check(!rx.push(b));
    check(rx.push(a)==1 && rx.rows()==1 && !rx.ready());
    check(!rx.pixels(output));
    check(!rx.push(a));
    check(rx.push(b)==2 && rx.ready() && rx.size()==16);
    check(rx.pixels(output));
    check(output[3]==255 && output[7]==255 && output[11]==255 && output[15]==255);
    if(codec==1 || codec==2 || codec==5) check(output[0]==10 && output[1]==20 && output[2]==30 && output[8]==50 && output[9]==60 && output[10]==70);
    else check(output[0]>=0 && output[0]<=255 && output[8]>=0 && output[8]<=255);
    check(!rx.push(b));
    check(!rx.pixels([0]));
}
rx.reset();
check(!rx.ready() && rx.rows()==0);
MediaRasterCompressedFragment.packetSize(2,2,0,1,1,90,top)=>int n;
int a[n]; int b[n]; int wrong[n];
check(MediaRasterCompressedFragment.encode(3,10,100,2,2,7,0,1,1,90,top,a));
check(MediaRasterCompressedFragment.encode(3,11,100,2,2,7,1,1,1,90,bottom,b));
check(MediaRasterCompressedFragment.encode(3,12,100,2,2,7,1,1,1,90,bottom,wrong));
check(rx.push(a)==1);
check(!rx.push(wrong) && rx.rows()==1);
check(MediaRasterCompressedFragment.encode(3,11,101,2,2,7,1,1,1,90,bottom,wrong));
check(!rx.push(wrong));
check(MediaRasterCompressedFragment.encode(3,11,100,2,2,8,1,1,1,90,bottom,wrong));
check(!rx.push(wrong));
b[64]^1=>b[64];
check(!rx.push(b));
b[64]^1=>b[64];
check(rx.push(b)==2);
check(rx.pixels(output) && output[8]==50);
MediaSession server; MediaSession client;
check(server.configure(1,256));
check(client.configure(1,256));
check(server.listen(0));
check(client.connect(server.port()));
0=>int accepted;
for(0=>int i;i<100000 && !accepted;i++) server.accept()=>accepted;
check(accepted==1);
for(0=>int i;i<100000 && (!server.connected()||!client.connected());i++){server.poll();client.poll();}
check(server.connected() && client.connected());
rx.reset();
check(client.sendPacket(a));
for(0=>int i;i<100000 && !server.packetSize();i++){client.poll();server.poll();}
int got[n];
check(server.receivePacket(got));
check(rx.push(got)==1);
for(0=>int i;i<100000 && client.sendPending();i++){client.poll();server.poll();}
check(client.sendPacket(b));
for(0=>int i;i<100000 && !server.packetSize();i++){client.poll();server.poll();}
check(server.receivePacket(got));
check(rx.push(got)==2);
check(rx.pixels(output) && output[0]==10 && output[8]==50);
server.close();client.close();
""",129,p)


if __name__ == "__main__":
    groups = {"legacy": legacy, "text-core": text_core, "text-recent": text_recent, "text-page-breaks": text_page_breaks, "text-page-furniture": text_page_furniture, "text-page-numbers": text_page_numbers, "text-split-paragraphs": text_split_paragraphs, "text-split-tables": text_split_tables, "text-frame-foundation": text_frame_foundation, "text-frame-flow": text_frame_flow, "text-frame-mixed": text_frame_mixed, "text-frame-render": text_frame_render, "text-frame-native": text_frame_native, "text-accessibility": text_accessibility, "text-flow-txt": text_flow_txt, "text-flow-interop": text_flow_interop, "text-flow-performance": text_flow_performance, "text-flow-consolidation": text_flow_consolidation, "text-flow-hit-test": text_flow_hit_test, "text-flow-selection": text_flow_selection, "text-flow-edit": text_flow_edit, "text-flow-styled-edit": text_flow_styled_edit, "text-flow-edit-consolidation": text_flow_edit_consolidation, "vector-shape-edit": vector_shape_edit, "vector-shape-transform": vector_shape_transform, "vector-shape-style": vector_shape_style, "vector-shape-group": vector_shape_group, "vector-editable-svg": vector_editable_svg, "midi-capture-events": midi_capture_events, "midi-capture-save": midi_capture_save, "midi-capture-session": midi_capture_session, "text-capture-unicode": text_capture_unicode, "text-capture-integration": text_capture_integration, "raster-capture-foundation": raster_capture_foundation, "raster-camera-capture": raster_camera_capture, "raster-screen-capture": raster_screen_capture, "raster-capture-image": raster_capture_image, "raster-capture-consolidation": raster_capture_consolidation, "video-camera-session": video_camera_session, "video-screen-recording": video_screen_recording, "video-audio-capture": video_audio_capture, "video-av-recording": video_av_recording, "video-capture-consolidation": video_capture_consolidation, "video-opengl-framebuffer": video_opengl_framebuffer, "video-chugl-texture": video_chugl_texture, "video-offscreen-render": video_offscreen_render, "video-indexed-av": video_indexed_av, "video-graphics-consolidation": video_graphics_consolidation, "transport-framing": transport_framing, "transport-loopback": transport_loopback, "transport-session": transport_session, "transport-endpoints": transport_endpoints, "transport-consolidation": transport_consolidation, "transport-pcm": transport_pcm, "transport-pcm-stream": transport_pcm_stream, "transport-pcm-buffer": transport_pcm_buffer, "transport-midi": transport_midi, "transport-audio-midi": transport_audio_midi, "transport-raster": transport_raster, "transport-raster-stream": transport_raster_stream, "transport-vector": transport_vector, "transport-model": transport_model, "transport-graphics": transport_graphics, "transport-video-frame": transport_video_frame, "transport-video-stream": transport_video_stream, "transport-video-buffer": transport_video_buffer, "transport-audio-video": transport_audio_video, "transport-av-consolidation": transport_av_consolidation, "transport-text": transport_text, "transport-text-fragments": transport_text_fragments, "transport-text-buffer": transport_text_buffer, "transport-heterogeneous": transport_heterogeneous, "transport-raster-lossless": transport_raster_lossless, "transport-raster-png": transport_raster_png, "transport-raster-photo": transport_raster_photo, "transport-raster-compressed-fragments": transport_raster_compressed_fragments}
    selected = os.environ.get("MULTIMEDIA_TEST_GROUPS", ",".join(groups)).split(",")
    if not selected or len(set(selected)) != len(selected) or any(g not in groups for g in selected):
        raise SystemExit("Invalid multimedia test groups")
    for group in selected:
        print("SMOKE_GROUP " + group, flush=True)
        groups[group]()

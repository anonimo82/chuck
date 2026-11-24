#!/usr/bin/env python3
"""Optional offline multimedia conversion backend for ChucK."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

AV = {".wav", ".aif", ".aiff", ".flac", ".ogg", ".opus", ".mp3", ".aac", ".m4a", ".au", ".mp4", ".ogv", ".avi", ".mkv", ".webm", ".mov", ".mpg", ".mpeg", ".png", ".jpg", ".jpeg", ".bmp", ".tiff", ".tif", ".webp", ".gif", ".tga"}
MODEL = {".obj", ".fbx", ".gltf", ".glb", ".dae", ".stl", ".ply", ".3ds"}
IMAGE = {".png", ".jpg", ".jpeg", ".bmp", ".tiff", ".tif", ".webp", ".gif", ".tga"}

def execute(args):
    if not shutil.which(args[0]):
        raise RuntimeError("Missing executable: " + args[0])
    subprocess.run(args, check=True)

def convert(source, destination):
    source, destination = Path(source).resolve(), Path(destination).resolve()
    if not source.is_file():
        raise ValueError("Input must be an existing file")
    if destination.exists():
        raise ValueError("Output already exists")
    a, b = source.suffix.lower(), destination.suffix.lower()
    if a in MODEL and b in MODEL:
        # Assimp may produce sidecar files; export directly to the target directory.
        execute(["assimp", "export", str(source), str(destination)])
        return
    if a == ".svg" and b in IMAGE:
        with tempfile.TemporaryDirectory() as tmp:
            png = Path(tmp) / "render.png"
            execute(["rsvg-convert", "-o", str(png), str(source)])
            if b == ".png":
                with destination.open("xb") as out, png.open("rb") as inp:
                    shutil.copyfileobj(inp, out)
            else:
                convert(png, destination)
        return
    if a in AV and b in AV:
        args = ["ffmpeg", "-nostdin", "-n", "-i", str(source)]
        if b in IMAGE and b != ".gif":
            args += ["-frames:v", "1", "-update", "1"]
        execute(args + [str(destination)])
        return
    raise ValueError("Unsupported conversion pair: " + a + " -> " + b)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source")
    parser.add_argument("destination")
    args = parser.parse_args()
    try:
        convert(args.source, args.destination)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as exc:
        parser.exit(1, str(exc) + "\n")

if __name__ == "__main__":
    main()

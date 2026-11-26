"""Independent check of the FFV1/FLAC command used by modern PAK export."""
import pathlib
import random
import struct
import subprocess
import sys
import tempfile
import wave

with tempfile.TemporaryDirectory() as work:
    root = pathlib.Path(work)
    rng = random.Random(42)
    rgb = bytes(rng.randrange(256) for _ in range(32 * 24 * 3 * 9))
    samples = [rng.randrange(-32768, 32768) for _ in range(4410)]
    pcm = struct.pack("<" + "h" * len(samples), *samples)
    (root / "frames.rgb").write_bytes(rgb)
    with wave.open(str(root / "audio.wav"), "wb") as stream:
        stream.setparams((1, 2, 11025, 0, "NONE", ""))
        stream.writeframes(pcm)

    def ffmpeg(*args):
        subprocess.run(
            [sys.argv[1], "-nostdin", "-hide_banner", "-loglevel", "error", "-y", *args],
            cwd=root, check=True, timeout=60
        )

    ffmpeg("-f", "rawvideo", "-pixel_format", "rgb24", "-video_size", "32x24",
           "-framerate", "15", "-i", "frames.rgb", "-i", "audio.wav",
           "-map", "0:v:0", "-c:v", "ffv1", "-level", "3", "-pix_fmt", "bgr0",
           "-threads", "1", "-map", "1:a:0", "-c:a", "flac", "-sample_fmt", "s16",
           "-map_metadata", "-1", "movie.mkv")
    ffmpeg("-i", "movie.mkv", "-map", "0:v:0", "-vsync", "0", "-f", "rawvideo",
           "-pix_fmt", "rgb24", "verified.rgb")
    ffmpeg("-i", "movie.mkv", "-map", "0:a:0", "-c:a", "pcm_s16le", "verified.wav")
    assert (root / "verified.rgb").read_bytes() == rgb
    with wave.open(str(root / "verified.wav"), "rb") as stream:
        assert stream.getframerate() == 11025
        assert stream.getnchannels() == 1
        assert stream.getsampwidth() == 2
        assert stream.readframes(stream.getnframes()) == pcm

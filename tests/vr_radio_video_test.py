"""Run with the path to pcvr_music_test and an ffmpeg executable on PATH."""
from pathlib import Path
import subprocess
import sys
import tempfile


def color_clip(path, size, audio):
    command = ["ffmpeg", "-nostdin", "-v", "error"]
    for color in ("red", "blue"):
        command += ["-f", "lavfi", "-i", f"color=c={color}:s={size}:r=12:d=0.5"]
    if audio:
        command += ["-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100:duration=1.4"]
    command += ["-filter_complex", "[0:v][1:v]concat=n=2:v=1:a=0[v]", "-map", "[v]"]
    if audio:
        command += ["-map", "2:a", "-c:a", "aac"]
    subprocess.run(command + ["-c:v", "libx264", "-pix_fmt", "yuv420p", str(path)], check=True)


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as folder:
        music = Path(folder) / "Music"
        music.mkdir()
        color_clip(music / "Portrait.MP4", "120x160", True)
        color_clip(music / "Silent.mp4", "320x180", False)
        subprocess.run(["ffmpeg", "-nostdin", "-v", "error", "-f", "lavfi", "-i",
                        "sine=frequency=1500:sample_rate=48000:duration=1", "-c:a", "aac",
                        str(music / "Music.mp4")], check=True)
        (music / "Broken.mp4").write_bytes(b"broken")
        subprocess.run([sys.argv[1], "--video", folder], check=True)

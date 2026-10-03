"""Offline conversion checks using real yt-dlp and the app's bundled FFmpeg.

Run with: uv run --no-project python tests/media.py
"""

import functools
import http.server
import json
import pathlib
import subprocess
import tempfile
import threading

ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOLS = ROOT / "dist/vidya-dev.app/Contents/Resources/ffmpeg"


def run(*args):
    result = subprocess.run(args, capture_output=True, text=True, timeout=60)
    if result.returncode:
        raise RuntimeError(f"Command failed: {args}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def probe(path):
    return json.loads(run(str(TOOLS / "ffprobe"), "-v", "error", "-show_streams", "-of", "json", str(path)))["streams"]


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


with tempfile.TemporaryDirectory(prefix="vidya-media-") as directory:
    temp = pathlib.Path(directory)
    macos = temp / "Contents/MacOS"
    resources = temp / "Contents/Resources"
    fixtures = temp / "fixtures"
    for folder in (macos, resources, fixtures):
        folder.mkdir(parents=True, exist_ok=True)
    (resources / "ffmpeg").symlink_to(TOOLS, target_is_directory=True)
    (macos / "yt-dlp").symlink_to(ROOT / "yt-dlp")
    worker = macos / "test"
    run("cc", "-std=c11", "-Wall", "-Wextra", str(ROOT / "tests/media_test.c"), "-lpthread", "-o", str(worker))
    ffmpeg = str(TOOLS / "ffmpeg")
    common = (ffmpeg, "-nostdin", "-v", "error", "-y")
    run(*common, "-f", "lavfi", "-i", "anullsrc=channel_layout=5.1:sample_rate=48000", "-t", "1", "-c:a", "libopus", str(fixtures / "surround.opus"))
    for extension in ("webm", "mp4"):
        run(*common, "-f", "lavfi", "-i", "testsrc2=size=320x240:rate=10", "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000", "-t", "1", "-c:v", "libvpx-vp9", "-c:a", "libopus", str(fixtures / f"incompatible.{extension}"))
    run(*common, "-f", "lavfi", "-i", "testsrc=size=321x241:rate=10", "-t", "1", "-c:v", "libvpx-vp9", "-pix_fmt", "yuv444p", str(fixtures / "odd-silent.webm"))
    (fixtures / "broken.webm").write_bytes(b"not a video")
    mock_tools = temp / "mock-tools"
    mock_tools.mkdir()
    mock_ffmpeg = mock_tools / "ffmpeg"
    mock_ffmpeg.write_text("#!/bin/sh\nsleep 30\n")
    mock_ffmpeg.chmod(0o755)
    run(str(worker), str(fixtures / "incompatible.webm"), str(mock_tools), "cancel")
    assert not list(fixtures.glob("*.converted*.mp4"))
    print("Passed conversion cancellation cleanup and source preservation")

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(QuietHandler, directory=str(fixtures)))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        base = f"http://127.0.0.1:{server.server_port}"
        for name in ("surround.opus", "incompatible.webm", "incompatible.mp4", "odd-silent.webm"):
            output = temp / name
            output.mkdir()
            run(str(worker), f"{base}/{name}", str(output), "wav" if name.endswith("opus") else "mp4")
            if name.endswith("opus"):
                audio = probe(next(output.glob("*.wav")))[0]
                assert (audio["codec_name"], audio["channels"], audio["sample_rate"]) == ("pcm_s24le", 2, "48000"), audio
            else:
                converted = next(output.glob("*.converted.mp4"))
                streams = probe(converted)
                video = next(s for s in streams if s["codec_type"] == "video")
                assert (video["codec_name"], video["pix_fmt"]) == ("h264", "yuv420p"), video
                assert video["width"] % 2 == 0 and video["height"] % 2 == 0
                assert list(output.glob("*." + name.rsplit(".", 1)[1]))
                if "silent" not in name:
                    audio = next(s for s in streams if s["codec_type"] == "audio")
                    assert (audio["codec_name"], audio["channels"]) == ("aac", 2), audio
                if name == "incompatible.mp4":
                    saved = converted.read_bytes()
                    run(str(worker), f"{base}/{name}", str(output), "mp4")
                    assert converted.read_bytes() == saved
                    assert list(output.glob("*.converted-1.mp4"))
            print(f"Passed real conversion: {name}")

        output = temp / "failure"
        output.mkdir()
        result = subprocess.run((str(worker), f"{base}/broken.webm", str(output), "mp4"), capture_output=True, text=True, timeout=60)
        assert result.returncode == 1, result.stdout
        assert "Conversion could not finish" in result.stdout, result.stdout
        assert list(output.glob("*.webm"))
        assert not list(output.glob("*.converted*.mp4"))
        print("Passed conversion failure cleanup and source preservation")
    finally:
        server.shutdown()

print("Real media conversion tests passed.")

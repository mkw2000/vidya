# Vidya

Paste an HTTP(S) link, choose video or audio, and click Download (or press
Enter in the link field). Files go to Downloads unless you choose another folder.
Convert to is on the main screen and enabled by default: audio becomes WAV
(24-bit PCM, stereo, 48 kHz) and video becomes H.264 MP4 with AAC audio.
Choose MP3, M4A, FLAC, AAC or Opus for audio, or MKV/WebM for video. Turn off
Convert to (or choose Original) to keep the downloaded format. Bitrate applies
only to compressed audio; WAV and FLAC ignore that setting.

MP4 conversion also handles files already named `.mp4` with incompatible codecs.
It keeps the source and saves a separate `.converted.mp4` beside it, adding a
number if that filename exists. Failed/cancelled MP4 conversions remove their
incomplete output. Conversion runs on successfully downloaded playlist entries
even if another entry fails. WAV conversion is handled by yt-dlp's audio extractor.
Optional thumbnails are converted to JPG; WAV/AAC use a separate thumbnail
file instead of unsupported embedded cover art.

Advanced options contain quality, bitrate, metadata, playlist, naming and
browser-session controls. Show details exposes the diagnostic output and Copy
details copies it for troubleshooting.

Automatic browser mode first tries without cookies. On authentication or cookie
errors it tries existing Chrome, Firefox, Edge, Brave and Safari profiles, once
each. Browser access may trigger macOS permission/keychain prompts. The app does
not change permissions or sign in for you. Select Never use cookies or a specific
browser in Advanced to override automatic browser selection.

Unavailable formats get one combined-stream fallback; temporary connection
errors get one IPv4 retry. Each command has finite transport retries and a
15-second socket timeout. There are at most eight attempts per download.
Rate limits, disk failures and unsupported/unavailable links stop with guidance.
Conversion/output choices are retained on retries. Quality may be lower when
only the fallback stream is available. Playlist downloads can partially succeed;
errors are not presented as complete success. Cancel stops the process group and
escalates to SIGKILL if it does not stop promptly.

Build: `make`. Regression tests: `make test` (uses isolated simulated yt-dlp
processes; does not read real browser cookies or download network media).
`make test-media` checks real Opus/WAV and video conversions using generated
fixtures, a temporary localhost server, and the tools in `dist/vidya-dev.app`.
`make macos-app` builds the release package using the existing packaging script.
The local `dist/vidya-dev.app` is an ad-hoc signed development build, not a
notarized release. Existing release zip/pkg artifacts are not updated by `make`.

Download options follow https://github.com/yt-dlp/yt-dlp#usage-and-options.

# Vidya

Paste an HTTP(S) link, choose video or audio, and click Download (or press
Enter in the link field). Files go to Downloads unless you choose another folder.
Advanced options contain quality, conversion, metadata, playlist, naming and
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
`make macos-app` builds the release package using the existing packaging script.
The local `dist/vidya-dev.app` is an ad-hoc signed development build, not a
notarized release. Existing release zip/pkg artifacts are not updated by `make`.

Download options follow https://github.com/yt-dlp/yt-dlp#usage-and-options.

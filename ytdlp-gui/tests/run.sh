#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d /tmp/vidya-test.XXXXXX)
trap 'rm -rf "$test_dir"' EXIT
cc -std=c11 -Wall -Wextra -Isrc tests/downloader_test.c -lpthread -o "$test_dir/test"
mkdir -p "$test_dir/Library/Application Support/Google/Chrome" "$test_dir/Library/Application Support/Firefox/Profiles"
cat > "$test_dir/yt-dlp" <<'MOCK'
#!/bin/sh
printf '%s\n' "$@" >> "$HOME/arguments"
echo 'ATTEMPT' >> "$HOME/arguments"
case "$*" in
  *'https://example.com/watch?v=abc&list=keep&signature=xyz'*) ;;
  *) echo 'ERROR: URL changed'; exit 1;;
esac
case "$SCENARIO" in
  cancel) sleep 30;;
  rate) echo "ERROR: HTTP Error 429: Too Many Requests"; exit 1;;
  disk) echo "ERROR: No space left on device"; exit 1;;
  auth)
    case "$*" in
      *'--cookies-from-browser firefox'*) exit 0;;
      *'--cookies-from-browser chrome'*) echo 'ERROR: Could not copy cookie database'; exit 1;;
      *) echo 'ERROR: Sign in to confirm your age'; exit 1;;
    esac;;
  format)
    case "$*" in *'-f best '*) exit 0;; *) echo 'ERROR: Requested format is not available'; exit 1;; esac;;
  network)
    case "$*" in *'--force-ipv4'*) exit 0;; *) echo 'ERROR: Connection reset'; exit 1;; esac;;
esac
MOCK
chmod +x "$test_dir/yt-dlp"
for scenario in auth format network; do
    env HOME="$test_dir" SCENARIO="$scenario" "$test_dir/test"
done
for mode in manual never; do
    env HOME="$test_dir" SCENARIO=auth "$test_dir/test" "$mode"
done
env HOME="$test_dir" SCENARIO=cancel "$test_dir/test" cancel
for scenario in rate disk; do
    : > "$test_dir/arguments"
    env HOME="$test_dir" SCENARIO="$scenario" "$test_dir/test" terminal
    test "$(grep -c '^ATTEMPT$' "$test_dir/arguments")" = 1
done
for mode in wav mp3 original mkv; do
    : > "$test_dir/arguments"
    env HOME="$test_dir" SCENARIO=format "$test_dir/test" "$mode"
    case "$mode" in
        wav)
            grep -qx 'wav' "$test_dir/arguments"
            grep -qx 'ExtractAudio+ffmpeg_o:-c:a pcm_s24le -ac 2 -ar 48000' "$test_dir/arguments"
            grep -qx -- '--convert-thumbnails' "$test_dir/arguments"
            if grep -qx -- '--audio-quality' "$test_dir/arguments"; then exit 1; fi
            if grep -qx -- '--embed-thumbnail' "$test_dir/arguments"; then exit 1; fi
            ;;
        mp3)
            grep -qx 'mp3' "$test_dir/arguments"
            grep -qx '320K' "$test_dir/arguments"
            ;;
        original)
            if grep -qx 'wav' "$test_dir/arguments"; then exit 1; fi
            if grep -qx -- '--audio-quality' "$test_dir/arguments"; then exit 1; fi
            ;;
        mkv)
            grep -qx -- '--recode-video' "$test_dir/arguments"
            grep -qx 'mkv' "$test_dir/arguments"
            grep -qx 'jpg' "$test_dir/arguments"
            ;;
    esac
    test "$(grep -c '^ATTEMPT$' "$test_dir/arguments")" = 2
done
printf 'Downloader integration tests passed.\n'

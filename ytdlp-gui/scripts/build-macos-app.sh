#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_NAME="${APP_NAME:-vidya}"
BUNDLE_ID="${BUNDLE_ID:-com.michaelkw.vidya}"
APP_VERSION="${APP_VERSION:-0.1.0}"
SIGN_IDENTITY="${MACOS_SIGN_IDENTITY:-}"
PKG_SIGN_IDENTITY="${MACOS_PKG_SIGN_IDENTITY:-}"
NOTARYTOOL_KEYCHAIN_PROFILE="${NOTARYTOOL_KEYCHAIN_PROFILE:-${MACOS_NOTARY_PROFILE:-}}"
APPLE_ID="${APPLE_ID:-${MACOS_NOTARY_APPLE_ID:-}}"
APPLE_TEAM_ID="${APPLE_TEAM_ID:-${MACOS_NOTARY_TEAM_ID:-}}"
APPLE_APP_PASSWORD="${APPLE_APP_PASSWORD:-${MACOS_NOTARY_APP_PASSWORD:-}}"
MACOS_NOTARIZE="${MACOS_NOTARIZE:-auto}"

APP_DIR="$ROOT_DIR/dist/$APP_NAME.app"
CONTENTS_DIR="$APP_DIR/Contents"
MACOS_DIR="$CONTENTS_DIR/MacOS"
FRAMEWORKS_DIR="$CONTENTS_DIR/Frameworks"
RESOURCES_DIR="$CONTENTS_DIR/Resources"
FFMPEG_DIR="$RESOURCES_DIR/ffmpeg"
PKG_PATH="$ROOT_DIR/dist/$APP_NAME.pkg"
ZIP_PATH="$ROOT_DIR/dist/$APP_NAME.zip"
YTDLP_ENTITLEMENTS="$ROOT_DIR/dist/yt-dlp.entitlements"

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

[[ "$(uname -s)" == "Darwin" ]] || die "macOS app packaging must run on macOS"

require_file() {
    [[ -f "$1" ]] || die "missing required file: $1"
}

find_raylib_dylib() {
    local linked
    linked="$(otool -L "$ROOT_DIR/ytdlp-gui" | awk '/libraylib.*dylib/ { print $1; exit }')"
    if [[ -n "$linked" && -f "$linked" ]]; then
        printf '%s\n' "$linked"
        return
    fi

    for candidate in \
        /opt/homebrew/lib/libraylib.dylib \
        /usr/local/lib/libraylib.dylib; do
        if [[ -f "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return
        fi
    done

    die "could not find libraylib.dylib; install raylib or set up the linker path first"
}

ffmpeg_arch() {
    case "$(uname -m)" in
        arm64|aarch64)
            printf 'arm64\n'
            ;;
        x86_64)
            printf 'amd64\n'
            ;;
        *)
            die "unsupported macOS architecture for bundled ffmpeg: $(uname -m)"
            ;;
    esac
}

generate_icns() {
    local src="$1"
    local iconset_dir="$TEMP_DIR/AppIcon.iconset"
    local icns_path="$RESOURCES_DIR/AppIcon.icns"

    mkdir -p "$iconset_dir"

    sips -z 16 16     "$src" --out "$iconset_dir/icon_16x16.png"      >/dev/null
    sips -z 32 32     "$src" --out "$iconset_dir/icon_16x16@2x.png"   >/dev/null
    sips -z 32 32     "$src" --out "$iconset_dir/icon_32x32.png"      >/dev/null
    sips -z 64 64     "$src" --out "$iconset_dir/icon_32x32@2x.png"   >/dev/null
    sips -z 128 128   "$src" --out "$iconset_dir/icon_128x128.png"    >/dev/null
    sips -z 256 256   "$src" --out "$iconset_dir/icon_128x128@2x.png" >/dev/null
    sips -z 256 256   "$src" --out "$iconset_dir/icon_256x256.png"    >/dev/null
    sips -z 512 512   "$src" --out "$iconset_dir/icon_256x256@2x.png" >/dev/null
    sips -z 512 512   "$src" --out "$iconset_dir/icon_512x512.png"    >/dev/null
    sips -z 1024 1024 "$src" --out "$iconset_dir/icon_512x512@2x.png" >/dev/null

    iconutil -c icns "$iconset_dir" -o "$icns_path"
}

bundle_ffmpeg() {
    local arch="$1"
    local temp_dir="$2"
    local ffmpeg_zip="$temp_dir/ffmpeg.zip"
    local ffprobe_zip="$temp_dir/ffprobe.zip"
    local base_url="${FFMPEG_STATIC_BASE_URL:-https://ffmpeg.martin-riedl.de/redirect/latest/macos/$arch/release}"

    mkdir -p "$FFMPEG_DIR"

    printf 'Downloading static ffmpeg for macOS %s...\n' "$arch"
    curl -fsSL -o "$ffmpeg_zip" "$base_url/ffmpeg.zip"
    curl -fsSL -o "$ffprobe_zip" "$base_url/ffprobe.zip"

    unzip -q -o "$ffmpeg_zip" -d "$FFMPEG_DIR"
    unzip -q -o "$ffprobe_zip" -d "$FFMPEG_DIR"

    require_file "$FFMPEG_DIR/ffmpeg"
    require_file "$FFMPEG_DIR/ffprobe"
    chmod +x "$FFMPEG_DIR/ffmpeg" "$FFMPEG_DIR/ffprobe"
}

find_developer_id_application() {
    security find-identity -v -p codesigning 2>/dev/null \
        | awk -F '"' '/Developer ID Application:/ { print $2; exit }'
}

find_developer_id_installer() {
    security find-identity -v 2>/dev/null \
        | awk -F '"' '/Developer ID Installer:/ { print $2; exit }'
}

detect_notary_profile() {
    local profile="$1"
    local output

    if output="$(xcrun notarytool history --keychain-profile "$profile" 2>&1)"; then
        return 0
    fi

    if printf '%s' "$output" | grep -q "No Keychain password item found for profile"; then
        return 1
    fi

    return 0
}

cd "$ROOT_DIR"

if [[ -d "$ROOT_DIR/dist" && "$(/usr/bin/stat -f %u "$ROOT_DIR/dist")" == "0" ]]; then
    printf 'error: dist/ is owned by root. Run this once to fix it:\n'
    printf '  sudo chown -R %s:%s dist/\n' "$(id -u)" "$(id -g)"
    printf '  # or: sudo rm -rf dist/\n'
    exit 1
fi

make bundle-ytdlp

require_file "$ROOT_DIR/ytdlp-gui"
require_file "$ROOT_DIR/yt-dlp"

if [[ -z "$SIGN_IDENTITY" ]]; then
    SIGN_IDENTITY="$(find_developer_id_application)"
fi
if [[ -z "$SIGN_IDENTITY" ]]; then
    SIGN_IDENTITY="-"
fi
if [[ -z "$PKG_SIGN_IDENTITY" ]]; then
    PKG_SIGN_IDENTITY="$(find_developer_id_installer)"
fi
if [[ -z "$NOTARYTOOL_KEYCHAIN_PROFILE" ]]; then
    for profile in vidya-notary yt-dlp-gui-notary driftplayer-notary DriftPlayer-notary myroneman-notary Myroneman-notary notarytool-default; do
        if detect_notary_profile "$profile"; then
            NOTARYTOOL_KEYCHAIN_PROFILE="$profile"
            break
        fi
    done
fi

NOTARIZATION_ARGS=()
if [[ -n "$NOTARYTOOL_KEYCHAIN_PROFILE" ]]; then
    NOTARIZATION_ARGS=(--keychain-profile "$NOTARYTOOL_KEYCHAIN_PROFILE")
elif [[ -n "$APPLE_ID" && -n "$APPLE_TEAM_ID" && -n "$APPLE_APP_PASSWORD" ]]; then
    NOTARIZATION_ARGS=(--apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_PASSWORD")
fi

DO_NOTARIZE=false
case "$MACOS_NOTARIZE" in
    1|true|yes)
        DO_NOTARIZE=true
        ;;
    0|false|no)
        DO_NOTARIZE=false
        ;;
    auto)
        if [[ "${#NOTARIZATION_ARGS[@]}" -gt 0 ]]; then
            DO_NOTARIZE=true
        fi
        ;;
    *)
        die "MACOS_NOTARIZE must be auto, true, or false"
        ;;
esac

if [[ "$DO_NOTARIZE" == true && "${#NOTARIZATION_ARGS[@]}" -eq 0 ]]; then
    die "notarization requested but no notarytool credentials were found; set NOTARYTOOL_KEYCHAIN_PROFILE or APPLE_ID/APPLE_TEAM_ID/APPLE_APP_PASSWORD"
fi

if [[ "$SIGN_IDENTITY" == "-" ]]; then
    CODESIGN_ARGS=(--force --timestamp=none --sign "$SIGN_IDENTITY")
else
    CODESIGN_ARGS=(--force --options runtime --timestamp --sign "$SIGN_IDENTITY")
fi

RAYLIB_DYLIB="$(find_raylib_dylib)"
RAYLIB_DYLIB_NAME="$(basename "$RAYLIB_DYLIB")"
mkdir -p "$ROOT_DIR/dist"
TEMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/vidya-build-macos.XXXXXX")"
trap 'rm -rf "$TEMP_DIR"' EXIT

rm -rf "$APP_DIR" "$PKG_PATH" "$ZIP_PATH" "$YTDLP_ENTITLEMENTS" 2>/dev/null || {
    printf 'warning: could not remove old build artifacts, fixing permissions...\n'
    chmod -R u+w "$APP_DIR" 2>/dev/null || true
    rm -rf "$APP_DIR" "$PKG_PATH" "$ZIP_PATH" "$YTDLP_ENTITLEMENTS" 2>/dev/null || true
}
mkdir -p "$MACOS_DIR" "$FRAMEWORKS_DIR" "$RESOURCES_DIR"

cp "$ROOT_DIR/ytdlp-gui" "$MACOS_DIR/ytdlp-gui"
cp "$ROOT_DIR/yt-dlp" "$MACOS_DIR/yt-dlp"
cp "$RAYLIB_DYLIB" "$FRAMEWORKS_DIR/$RAYLIB_DYLIB_NAME"
chmod +x "$MACOS_DIR/ytdlp-gui" "$MACOS_DIR/yt-dlp"
bundle_ffmpeg "$(ffmpeg_arch)" "$TEMP_DIR"
generate_icns "$ROOT_DIR/icon.png"

cat > "$CONTENTS_DIR/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>ytdlp-gui</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
    <key>CFBundleIdentifier</key>
    <string>$BUNDLE_ID</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>$APP_NAME</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>$APP_VERSION</string>
    <key>CFBundleVersion</key>
    <string>$APP_VERSION</string>
    <key>LSMinimumSystemVersion</key>
    <string>12.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
</dict>
</plist>
PLIST

cat > "$YTDLP_ENTITLEMENTS" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>com.apple.security.cs.disable-library-validation</key>
    <true/>
</dict>
</plist>
PLIST

install_name_tool \
    -change "$RAYLIB_DYLIB" "@executable_path/../Frameworks/$RAYLIB_DYLIB_NAME" \
    "$MACOS_DIR/ytdlp-gui"

printf 'Signing app with identity: %s\n' "$SIGN_IDENTITY"
codesign "${CODESIGN_ARGS[@]}" "$FRAMEWORKS_DIR/$RAYLIB_DYLIB_NAME"
codesign "${CODESIGN_ARGS[@]}" "$FFMPEG_DIR/ffmpeg"
codesign "${CODESIGN_ARGS[@]}" "$FFMPEG_DIR/ffprobe"
codesign "${CODESIGN_ARGS[@]}" --entitlements "$YTDLP_ENTITLEMENTS" "$MACOS_DIR/yt-dlp"
codesign "${CODESIGN_ARGS[@]}" "$MACOS_DIR/ytdlp-gui"
codesign "${CODESIGN_ARGS[@]}" "$APP_DIR"
codesign --verify --deep --strict --verbose=2 "$APP_DIR"

ditto -c -k --keepParent "$APP_DIR" "$ZIP_PATH"

if [[ -n "$PKG_SIGN_IDENTITY" ]]; then
    productbuild --component "$APP_DIR" /Applications --sign "$PKG_SIGN_IDENTITY" "$PKG_PATH"
    printf 'Built signed installer %s\n' "$PKG_PATH"
else
    productbuild --component "$APP_DIR" /Applications "$PKG_PATH"
    printf 'Built unsigned installer %s\n' "$PKG_PATH"
    printf 'Set MACOS_PKG_SIGN_IDENTITY to a Developer ID Installer identity to sign the pkg.\n'
fi

if [[ "$DO_NOTARIZE" == true ]]; then
    if [[ -n "$NOTARYTOOL_KEYCHAIN_PROFILE" ]]; then
        printf 'Notary profile: %s\n' "$NOTARYTOOL_KEYCHAIN_PROFILE"
    else
        printf 'Notary mode: Apple ID credentials\n'
    fi

    printf 'Submitting app archive for notarization: %s\n' "$ZIP_PATH"
    xcrun notarytool submit "$ZIP_PATH" "${NOTARIZATION_ARGS[@]}" --wait
    xcrun stapler staple "$APP_DIR"

    printf 'Submitting installer for notarization: %s\n' "$PKG_PATH"
    xcrun notarytool submit "$PKG_PATH" "${NOTARIZATION_ARGS[@]}" --wait
    xcrun stapler staple "$PKG_PATH"

    xcrun stapler validate "$APP_DIR"
    xcrun stapler validate "$PKG_PATH"
else
    printf 'Skipping notarization. Set MACOS_NOTARIZE=true with NOTARYTOOL_KEYCHAIN_PROFILE or Apple ID credentials to notarize and staple.\n'
fi

printf 'Built %s\n' "$APP_DIR"
printf 'Built signed app archive %s\n' "$ZIP_PATH"

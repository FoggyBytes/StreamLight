#!/usr/bin/env bash
#
# StreamLight installer
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/FoggyBytes/StreamLight/main/install.sh | bash
#   ./install.sh            # install the latest prebuilt AppImage (default)
#   ./install.sh --build    # clone the source and build natively with qmake6
#
set -euo pipefail

REPO="${STREAMLIGHT_REPO:-FoggyBytes/StreamLight}"
APP="StreamLight"
BIN_NAME="streamlight"
INSTALL_BIN_DIR="/usr/local/bin"
DESKTOP_DIR="/usr/local/share/applications"
ICON_DIR="/usr/local/share/icons/hicolor/256x256/apps"
API="https://api.github.com/repos/${REPO}"

# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------
info()  { printf '\033[1;34m[StreamLight]\033[0m %s\n' "$*"; }
warn()  { printf '\033[1;33m[StreamLight]\033[0m %s\n' "$*" >&2; }
die()   { printf '\033[1;31m[StreamLight]\033[0m %s\n' "$*" >&2; exit 1; }

have() { command -v "$1" >/dev/null 2>&1; }

fetch() { # fetch <url> <outfile>
    if have curl; then
        curl -fsSL "$1" -o "$2"
    elif have wget; then
        wget -qO "$2" "$1"
    else
        die "Neither curl nor wget is available. Install one and retry."
    fi
}

fetch_json_field() { # fetch_json_field <url> <python-expr>
    local url="$1" expr="$2" tmp
    tmp="$(mktemp)"
    if have curl; then
        curl -fsSL "$url" -o "$tmp" 2>/dev/null || { rm -f "$tmp"; return 1; }
    else
        wget -qO "$tmp" "$url" 2>/dev/null || { rm -f "$tmp"; return 1; }
    fi
    python3 -c "import json,sys; d=json.load(open('$tmp')); print($expr)" 2>/dev/null || {
        rm -f "$tmp"; return 1; }
    rm -f "$tmp"
}

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    have sudo && SUDO="sudo" || die "Root privileges required (sudo not found)."
fi

ARCH="$(uname -m)"
case "$ARCH" in
    x86_64|aarch64) ;;
    *) warn "Unsupported architecture '$ARCH' — prebuilt binaries are x86_64 and aarch64 only." ;;
esac

# ---------------------------------------------------------------------------
# mode: --build
# ---------------------------------------------------------------------------
build_from_source() {
    info "Building StreamLight from source (this takes a while)..."

    local pkgs_apt="git build-essential cmake nasm python3 python3-pip meson libtool autoconf automake \
qt6-base-dev qt6-declarative-dev libqt6svg6-dev \
qml6-module-qtquick-controls qml6-module-qtquick-templates qml6-module-qtquick-layouts \
qml6-module-qtqml-workerscript qml6-module-qtquick-window qml6-module-qtquick \
libgbm-dev libdrm-dev libfreetype-dev libasound2-dev libdbus-1-dev libegl1-mesa-dev \
libgl1-mesa-dev libgles2-mesa-dev libglu1-mesa-dev libpulse-dev libudev-dev \
libx11-dev libxcursor-dev libxext-dev libxi-dev libxinerama-dev libxkbcommon-dev \
libxrandr-dev libxss-dev libxt-dev libxv-dev libxxf86vm-dev libxcb-dri3-dev \
libx11-xcb-dev libxfixes-dev libxtst-dev wayland-protocols libopus-dev \
libvdpau-dev libgl-dev libpipewire-0.3-dev liburing-dev"

    if have apt-get; then
        info "Installing build dependencies (apt)..."
        $SUDO apt-get update
        $SUDO apt-get install -y $pkgs_apt || warn "Some apt packages failed — the build may complain later."
    elif have dnf; then
        warn "dnf detected: automatic dependency install is Ubuntu-tuned."
        warn "Install Qt6/SDL/FFmpeg development packages manually, then re-run with --skip-deps."
        exit 1
    elif have pacman; then
        warn "pacman detected: automatic dependency install is Ubuntu-tuned."
        warn "Install qt6-base sdl2 ffmpeg etc. manually, then re-run with --skip-deps."
        exit 1
    else
        die "No supported package manager found (apt/dnf/pacman)."
    fi

    local work
    work="$(mktemp -d)"
    trap 'rm -rf "$work"' RETURN 2>/dev/null || true

    info "Cloning ${REPO}..."
    git clone --recursive --depth 1 "https://github.com/${REPO}.git" "$work/StreamLight"

    # SDL3 (moonlight-qt links SDL2 via sdl2-compat on top of SDL3)
    if ! pkg-config --exists sdl3 2>/dev/null; then
        info "Building SDL3..."
        git clone --depth 1 --branch release-3.4.14 https://github.com/libsdl-org/SDL.git "$work/SDL"
        cmake -DSDL_KMSDRM=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_INSTALL_DOCS=OFF \
              -S "$work/SDL" -B "$work/SDL/build"
        cmake --build "$work/SDL/build" -j"$(nproc)"
        $SUDO cmake --install "$work/SDL/build"
    fi

    info "Building StreamLight with qmake6..."
    mkdir -p "$work/build"
    pushd "$work/build" >/dev/null
    qmake6 "$work/StreamLight/moonlight-qt.pro" CONFIG+=release PREFIX="$INSTALL_BIN_DIR"
    make -j"$(nproc)"
    $SUDO make install
    popd >/dev/null

    info "Desktop entry and icon installed by make install."
    info "Build complete — launch '${APP}' from your application menu."
}

# ---------------------------------------------------------------------------
# mode: default (prebuilt AppImage)
# ---------------------------------------------------------------------------
install_prebuilt() {
    info "Looking up the latest release..."
    local asset_url version
    asset_url="$(fetch_json_field "${API}/releases/latest" \
        "next(a['browser_download_url'] for a in d['assets'] if a['name'].endswith('-${ARCH}.AppImage'))")" \
        || die "No AppImage asset found in the latest release.
Is there a release published yet? Check https://github.com/${REPO}/releases
(You can also build from source: install.sh --build)"

    version="$(fetch_json_field "${API}/releases/latest" "d['tag_name']" || echo "unknown")"
    info "Latest release: ${version}"

    local tmp
    tmp="$(mktemp --suffix=.AppImage)"
    info "Downloading AppImage..."
    fetch "$asset_url" "$tmp"
    chmod +x "$tmp"

    $SUDO mkdir -p "$INSTALL_BIN_DIR" "$DESKTOP_DIR" "$ICON_DIR"
    $SUDO mv "$tmp" "${INSTALL_BIN_DIR}/${BIN_NAME}.AppImage"
    $SUDO chmod 755 "${INSTALL_BIN_DIR}/${BIN_NAME}.AppImage"

    # Wrapper so the desktop entry stays simple (AppImages dislike being run via symlink)
    $SUDO tee "${INSTALL_BIN_DIR}/${BIN_NAME}" >/dev/null <<WRAPPER
#!/bin/sh
exec "${INSTALL_BIN_DIR}/${BIN_NAME}.AppImage" "\$@"
WRAPPER
    $SUDO chmod 755 "${INSTALL_BIN_DIR}/${BIN_NAME}"

    info "Installing desktop entry and icon..."
    fetch "https://raw.githubusercontent.com/${REPO}/main/app/deploy/linux/io.github.FoggyBytes.StreamLight.desktop" \
        "/tmp/streamlight.desktop.tmp"
    $SUDO sed -i "s|^Exec=.*|Exec=${BIN_NAME}|; s|^Icon=.*|Icon=${BIN_NAME}|" /tmp/streamlight.desktop.tmp
    $SUDO mv /tmp/streamlight.desktop.tmp "${DESKTOP_DIR}/io.github.FoggyBytes.StreamLight.desktop"

    fetch "https://raw.githubusercontent.com/${REPO}/main/app/res/streamlight.png" \
        "/tmp/streamlight-icon.png"
    $SUDO mv /tmp/streamlight-icon.png "${ICON_DIR}/${BIN_NAME}.png"

    info "Done! '${APP}' is now in your application menu."
    info "To remove: $SUDO rm -f ${INSTALL_BIN_DIR}/${BIN_NAME}* ${DESKTOP_DIR}/io.github.FoggyBytes.StreamLight.desktop ${ICON_DIR}/${BIN_NAME}.png"
}

# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
SKIP_DEPS=0
MODE="prebuilt"
for arg in "$@"; do
    case "$arg" in
        --build) MODE="build" ;;
        --skip-deps) SKIP_DEPS=1 ;;
        -h|--help)
            echo "StreamLight installer"
            echo "  (no args)   install the latest prebuilt AppImage"
            echo "  --build     clone and build from source with qmake6"
            exit 0
            ;;
        *) die "Unknown option: $arg (try --help)" ;;
    esac
done

have python3 || die "python3 is required for release lookups."

if [ "$MODE" = "build" ]; then
    build_from_source
else
    install_prebuilt
fi

#!/usr/bin/env bash
BUILD_CONFIG="release"

fail()
{
	echo "$1" 1>&2
	exit 1
}

BUILD_ROOT=$PWD/build
SOURCE_ROOT=$PWD
BUILD_FOLDER=$BUILD_ROOT/build-$BUILD_CONFIG
DEPLOY_FOLDER=$BUILD_ROOT/deploy-$BUILD_CONFIG
INSTALLER_FOLDER=$BUILD_ROOT/installer-$BUILD_CONFIG

LINUXDEPLOY=linuxdeploy-$(uname -m).AppImage

if [ -n "$CI_VERSION" ]; then
  VERSION=$CI_VERSION
else
  # version.txt carries a UTF-8 BOM (written on Windows); keep it out of the file name.
  VERSION=$(sed '1s/^\xEF\xBB\xBF//' $SOURCE_ROOT/app/version.txt | tr -d '\r\n')
fi

command -v qmake6 >/dev/null 2>&1 || fail "Unable to find 'qmake6' in your PATH!"
command -v $LINUXDEPLOY >/dev/null 2>&1 || fail "Unable to find '$LINUXDEPLOY' in your PATH!"

echo Cleaning output directories
rm -rf $BUILD_FOLDER
rm -rf $DEPLOY_FOLDER
rm -rf $INSTALLER_FOLDER
mkdir $BUILD_ROOT
mkdir $BUILD_FOLDER
mkdir $DEPLOY_FOLDER
mkdir $INSTALLER_FOLDER

# Wayland support is ENABLED for the AppImage (unlike upstream Moonlight).
# Upstream disables it to avoid bundling an older libwayland-client.so that
# breaks host EGL symbol resolution. We keep Wayland but force linuxdeploy to
# EXCLUDE libwayland-client/cursor/egl from the bundle, so the Qt Wayland
# platform plugin links against the host libwayland at runtime - the same
# libraries KWin/GNOME already provide. Native Wayland, no XWayland fallback,
# no NVIDIA black window.
# libvulkan.so.1 MUST NOT be bundled: linuxdeploy picks up the loader from the
# CI base image, which predates Vulkan Video (VK_KHR_video_decode_*). Inside the
# AppImage it shadows the host's modern loader, so libplacebo can never see the
# GPU's Vulkan Video/AV1 decode support and falls back to VAAPI/VDPAU (or dies).
# The host loader is always the right one - same policy as libwayland above.
#
# The glib family (libglib/libgobject/libgio/libgmodule/libgthread) MUST NOT be
# bundled either: libva's driver plugins (notably nvidia_drv_video.so) link
# glib, and a bundled copy shadows the host's - on 1.1.1 the bundled gobject
# predated the host's and was missing g_string_copy, so the NVIDIA VA-API driver
# failed to dlopen and SDL reported "no functioning hardware accelerated video
# decoder" on every NVIDIA host. Glib is universal on desktop distros; the host
# copy is always the right one.
WAYLAND_EXCLUDES="--exclude-library libwayland-client.so.0 --exclude-library libwayland-cursor.so.0 --exclude-library libwayland-egl.so.1 --exclude-library libvulkan.so.1 --exclude-library libglib-2.0.so.0 --exclude-library libgobject-2.0.so.0 --exclude-library libgio-2.0.so.0 --exclude-library libgmodule-2.0.so.0 --exclude-library libgthread-2.0.so.0"

# Enable LTO for official builds
export CFLAGS=-flto=auto
export CXXFLAGS=-flto=auto
export LDFLAGS=-flto=auto

echo Configuring the project
pushd $BUILD_FOLDER
# NOTE: Wayland support is now ENABLED (this differs from upstream Moonlight,
# which disables it here). The historical concern was libwayland-client.so
# version skew breaking host EGL symbol resolution; we handle that by
# excluding the bundled libwayland libraries entirely (see WAYLAND_EXCLUDES),
# so the app always uses the host libwayland. See the block above.
#
# We disable DRM support because linuxdeploy doesn't bundle the appropriate libraries for Qt EGLFS.
qmake6 $SOURCE_ROOT/moonlight-qt.pro CONFIG+=disable-libdrm PREFIX=$DEPLOY_FOLDER/usr DEFINES+=APP_IMAGE || fail "Qmake failed!"
popd

echo Compiling Moonlight in $BUILD_CONFIG configuration
pushd $BUILD_FOLDER
make -j$(nproc) $(echo "$BUILD_CONFIG" | tr '[:upper:]' '[:lower:]') || fail "Make failed!"
popd

echo Deploying to staging directory
pushd $BUILD_FOLDER
make install || fail "Make install failed!"
popd

# Pre-seed the QML modules the app imports but linuxdeploy-plugin-qt's bundle
# step has historically missed when the host's Qt install lacks them (the 1.0.0
# AppImage shipped without QtQuick/Shapes and bounced on launch on every
# machine). Copying explicitly from the Qt we build with makes the bundle
# deterministic. Their library dependencies (libQt6QuickShapes etc.) are picked
# up by linuxdeploy's dependency walk.
QT_QML_DIR=$(qmake6 -query QT_INSTALL_QML) || fail "qmake -query failed!"
for MODULE in QtQuick/Shapes QtQuick/Effects QtQuick/Dialogs QtQuick/Window QtQuick/Layouts QtQuick/Templates QtQuick/Controls QtQuickControls2; do
    if [ -d "$QT_QML_DIR/$MODULE" ]; then
        echo "Bundling QML module: $MODULE"
        mkdir -p $DEPLOY_FOLDER/usr/qml/$(dirname $MODULE)
        cp -r "$QT_QML_DIR/$MODULE" $DEPLOY_FOLDER/usr/qml/$MODULE/ || fail "Failed to bundle QML module $MODULE"
    else
        echo "QML module not present in Qt install (skipping): $MODULE"
    fi
done

# Pre-seed the Qt Wayland platform plugin the same way we pre-seed QML
# modules: linuxdeploy-plugin-qt does not reliably bundle it, and without it
# Wayland-desktop users get XWayland fallback (black window on NVIDIA).
QT_PLUGIN_DIR=$(qmake6 -query QT_INSTALL_PLUGINS) || fail "qmake -query failed!"
# NB: glob against $QT_PLUGIN_DIR itself (the old form `for PLUG in
# platforms/libqwayland*.so` expanded the glob against the script's CWD, never
# matched, stayed a literal string, and bundled nothing — CI rounds 2026-09-02).
for PLUG in "$QT_PLUGIN_DIR"/platforms/libqwayland*.so; do
    [ -e "$PLUG" ] || continue
    echo "Bundling Qt platform plugin: $(basename "$PLUG")"
    mkdir -p $DEPLOY_FOLDER/usr/plugins/platforms
    cp "$PLUG" $DEPLOY_FOLDER/usr/plugins/platforms/ || fail "Failed to bundle $PLUG"
done
WAYLAND_PLUGS=$(ls $DEPLOY_FOLDER/usr/plugins/platforms/libqwayland* 2>/dev/null | wc -l)
[ "$WAYLAND_PLUGS" -gt 0 ] || fail "No Qt Wayland platform plugins found to bundle - check aqt Qt build"

# Bundle the Wayland shell-integration plugins too. Without these the
# wayland platform plugin loads but finds no shell integration ("No shell
# integration named xdg-shell found"), fails to initialize, and Qt silently
# falls back to XWayland -> black window (round 15, 2026-09-03).
for SHELLPLUG in "$QT_PLUGIN_DIR"/wayland-shell-integration/*.so; do
    [ -e "$SHELLPLUG" ] || continue
    echo "Bundling Qt Wayland shell integration: $(basename "$SHELLPLUG")"
    mkdir -p $DEPLOY_FOLDER/usr/plugins/wayland-shell-integration
    cp -L "$SHELLPLUG" $DEPLOY_FOLDER/usr/plugins/wayland-shell-integration/ || fail "Failed to bundle $SHELLPLUG"
done
SHELL_PLUGS=$(ls $DEPLOY_FOLDER/usr/plugins/wayland-shell-integration/*.so 2>/dev/null | wc -l)
[ "$SHELL_PLUGS" -gt 0 ] || fail "No Qt Wayland shell-integration plugins found to bundle - check aqt Qt build"

# ── Qt Virtual Keyboard ────────────────────────────────────────────────────────
# The app's on-screen keyboard for handhelds (app/gui/OnScreenKeyboard.qml). It is NOT in
# aqt's desktop binary archives — the archive set for $QT_VERSION is icu, qtbase,
# qtdeclarative, qtsvg, qttools, qttranslations, qtwayland — so CI builds the module from
# source into $QT_DIR before this script runs (see the "Build Qt Virtual Keyboard" step in
# .github/workflows/build-linux.yml).
#
# ⚠️ Hard failure, not the skip the loop above uses. A build that quietly ships without
# this module ships without a keyboard on a Steam Deck while looking completely normal,
# and that is precisely the bug the module was added to fix.
VK_QML="$QT_QML_DIR/QtQuick/VirtualKeyboard"
[ -d "$VK_QML" ] || fail "QtQuick/VirtualKeyboard missing from $QT_QML_DIR - the on-screen keyboard would be absent from this build (was the module built into \$QT_DIR?)"
echo "Bundling QML module: QtQuick/VirtualKeyboard"
mkdir -p $DEPLOY_FOLDER/usr/qml/QtQuick
cp -r "$VK_QML" $DEPLOY_FOLDER/usr/qml/QtQuick/VirtualKeyboard/ || fail "Failed to bundle QtQuick/VirtualKeyboard"

# The platform input context plugin is what makes QT_IM_MODULE=qtvirtualkeyboard resolve
# at runtime (main.cpp sets it). The QML types load without it, but nothing ever activates
# them, so the keyboard simply never appears.
VK_PLUGIN=$(ls "$QT_PLUGIN_DIR"/platforminputcontexts/libqtvirtualkeyboardplugin.so 2>/dev/null | head -1)
[ -n "$VK_PLUGIN" ] || fail "libqtvirtualkeyboardplugin.so missing from $QT_PLUGIN_DIR/platforminputcontexts"
echo "Bundling platform input context: $(basename "$VK_PLUGIN")"
mkdir -p $DEPLOY_FOLDER/usr/plugins/platforminputcontexts
cp "$VK_PLUGIN" $DEPLOY_FOLDER/usr/plugins/platforminputcontexts/ || fail "Failed to bundle $VK_PLUGIN"

# The module's own libraries. linuxdeploy's dependency walk starts from the executable,
# which links none of these — they are reached only from the plugin and from the QML
# plugins inside the module tree — so they have to be copied in by hand.
QT_LIB_DIR=$(qmake6 -query QT_INSTALL_LIBS) || fail "qmake -query failed!"
VK_LIBS=0
for LIB in "$QT_LIB_DIR"/libQt6VirtualKeyboard.so* "$QT_LIB_DIR"/libQt6HunspellInputMethod.so*; do
    [ -e "$LIB" ] || continue
    mkdir -p $DEPLOY_FOLDER/usr/lib
    cp -P "$LIB" $DEPLOY_FOLDER/usr/lib/ || fail "Failed to bundle $LIB"
    VK_LIBS=$((VK_LIBS + 1))
done
[ "$VK_LIBS" -gt 0 ] || fail "No libQt6VirtualKeyboard* libraries found in $QT_LIB_DIR"

# Layout / word-list data, where the Qt install has any.
QT_DATA_DIR=$(qmake6 -query QT_INSTALL_DATA) || fail "qmake -query failed!"
if [ -d "$QT_DATA_DIR/qtvirtualkeyboard" ]; then
    echo "Bundling Qt Virtual Keyboard data"
    cp -r "$QT_DATA_DIR/qtvirtualkeyboard" $DEPLOY_FOLDER/usr/ || fail "Failed to bundle Qt Virtual Keyboard data"
fi

# Bundle the Wayland client buffer / graphics-integration plugins. Without
# these Qt loads the wayland platform plugin + shell integrations but has no
# way to hand GPU buffers to the compositor: "Failed to load client buffer
# integration: wayland-egl / Available client buffer integrations: QList()"
# -> SIGABRT (round 16 diagnosis, 2026-09-03).
for GICLIENT in "$QT_PLUGIN_DIR"/wayland-graphics-integration-client/*.so; do
    [ -e "$GICLIENT" ] || continue
    echo "Bundling Qt Wayland graphics-integration-client: $(basename "$GICLIENT")"
    mkdir -p $DEPLOY_FOLDER/usr/plugins/wayland-graphics-integration-client
    cp -L "$GICLIENT" $DEPLOY_FOLDER/usr/plugins/wayland-graphics-integration-client/ || fail "Failed to bundle $GICLIENT"
done
GI_PLUGS=$(ls $DEPLOY_FOLDER/usr/plugins/wayland-graphics-integration-client/*.so 2>/dev/null | wc -l)
[ "$GI_PLUGS" -gt 0 ] || fail "No Qt Wayland graphics-integration-client plugins found to bundle - check aqt Qt build"

# Bundle Qt's own libQt6WaylandClient too. The platform plugins link against
# it, but linuxdeploy does not see the dependency (it resolves at dlopen time),
# so without this the runtime loader grabs the HOST's libQt6WaylandClient -
# which may be built against a different Qt (e.g. system Qt 6.11) and demands
# Qt_6.11 symbols our bundled Qt 6.8.3 Core does not export. Result:
# "Could not load the Qt platform plugin wayland even though it was found"
# -> XWayland fallback -> black window. Ship the matching Qt 6.8.3 copy.
QT_LIB_DIR=$(qmake6 -query QT_INSTALL_LIBS) || fail "qmake -query failed!"
# Bundle libQt6WaylandEglClientHwIntegration too — libqt-plugin-wayland-egl
# links against it (ldd: "libQt6WaylandEglClientHwIntegration.so.6 => not
# found"), and without it the wayland-egl client buffer integration fails to
# dlopen -> "Failed to load client buffer integration" -> SIGABRT (round 17).
for HWLIB in "$QT_LIB_DIR"/libQt6WaylandEglClientHwIntegration.so.*.*.*; do
    [ -e "$HWLIB" ] || continue
    echo "Bundling Qt Wayland EGL client HW integration: $(basename "$HWLIB")"
    mkdir -p $DEPLOY_FOLDER/usr/lib
    cp -L "$HWLIB" $DEPLOY_FOLDER/usr/lib/ || fail "Failed to bundle $HWLIB"
done
# The loader needs the SONAME symlink (.so.6), not just the versioned file —
# dlopen("libQt6WaylandEglClientHwIntegration.so.6") fails without it and Qt
# aborts with "Failed to load client buffer integration: wayland-egl".
HWBASE=$(basename $(ls $DEPLOY_FOLDER/usr/lib/libQt6WaylandEglClientHwIntegration.so.*.*.* | head -1))
ln -sf "$HWBASE" "$DEPLOY_FOLDER/usr/lib/libQt6WaylandEglClientHwIntegration.so.6"
ls "$DEPLOY_FOLDER/usr/lib/libQt6WaylandEglClientHwIntegration.so.6" >/dev/null 2>&1 \
    || fail "libQt6WaylandEglClientHwIntegration.so.6 symlink missing - loader will dlopen-fail"
# usr/lib does not exist yet at this point (linuxdeploy creates it later), so
# create it or the cp below dies with "cannot create regular file ... Not a
# directory" (round 12).
mkdir -p $DEPLOY_FOLDER/usr/lib
# Copy the real versioned file with -L; the unversioned symlinks in the aqt
# Qt tree dangle/are not present, and cp of a symlink failed round 11.
for WLIB in "$QT_LIB_DIR"/libQt6WaylandClient.so.*.*.*; do
    [ -e "$WLIB" ] || continue
    echo "Bundling Qt Wayland client library: $(basename "$WLIB")"
    cp -L "$WLIB" $DEPLOY_FOLDER/usr/lib/ || fail "Failed to bundle $WLIB"
done
# The plugin's DT_NEEDED entry is libQt6WaylandClient.so.6, so make sure the
# runtime name resolves inside the bundle.
if [ ! -e "$DEPLOY_FOLDER/usr/lib/libQt6WaylandClient.so.6" ]; then
    REAL=$(ls $DEPLOY_FOLDER/usr/lib/libQt6WaylandClient.so.*.*.* 2>/dev/null | head -1)
    [ -n "$REAL" ] || fail "No versioned libQt6WaylandClient bundled"
    ln -s "$(basename "$REAL")" "$DEPLOY_FOLDER/usr/lib/libQt6WaylandClient.so.6" || fail "symlink failed"
fi
[ -e "$DEPLOY_FOLDER/usr/lib/libQt6WaylandClient.so.6" ] || fail "libQt6WaylandClient.so.6 missing after bundling"

# linuxdeploy resolves dependencies with ldd semantics. Our pre-seeded
# libQt6WaylandClient has no DT_RPATH, so when linuxdeploy walks it in the
# "existing files" pass it searches system paths for libQt6Gui.so.6 etc. and
# dies with "Could not find dependency: libQt6Gui.so.6" (round 13) - even
# though identical copies are already in the appdir. Put the Qt lib dir on
# LD_LIBRARY_PATH so ldd resolves Qt deps; libraries already present in the
# appdir are reused, not bundled twice.
export LD_LIBRARY_PATH="$QT_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

export QML_SOURCES_PATHS=$SOURCE_ROOT/app/gui
# Point linuxdeploy-plugin-qt at our aqtinstall Qt (it does not inherit PATH
# reliably); an empty QMAKE overrides its own fallback search, so only export
# when actually set.
export QMAKE="$(command -v qmake6)"

echo "Deploying with linuxdeploy (deploy-only pass; AppImage is packed AFTER"
echo "the strip + hard checks below, so nothing can sneak into the final image)"
pushd $INSTALLER_FOLDER
VERSION=$VERSION $LINUXDEPLOY --appdir $DEPLOY_FOLDER \
  --library=/usr/local/lib/libSDL3.so.0 \
  $WAYLAND_EXCLUDES \
  --plugin qt || fail "linuxdeploy failed!"
popd

# linuxdeploy-plugin-qt deploys Qt's own dependency tree (including glib) via
# its internal copy step, where the main linuxdeploy's --exclude-library flags
# do NOT reach it. Physically strip the excluded family from the bundle here.
# CRITICAL (2026-09-04): the strip + hard checks MUST run while the AppImage
# does not exist yet. The previous version packed the AppImage first and
# stripped the appdir afterwards - the shipped artifact kept the glib family
# (missing g_string_copy -> NVIDIA VA-API driver dlopen failure) while the
# checks passed on the already-post-processed appdir. The AppImage is only
# packed at the very bottom of this script, from the verified appdir.
rm -f $DEPLOY_FOLDER/usr/lib/libglib-2.0.so* \
      $DEPLOY_FOLDER/usr/lib/libgobject-2.0.so* \
      $DEPLOY_FOLDER/usr/lib/libgio-2.0.so* \
      $DEPLOY_FOLDER/usr/lib/libgmodule-2.0.so* \
      $DEPLOY_FOLDER/usr/lib/libgthread-2.0.so* \
   || true

# Hard check: the bundle must NOT contain libvulkan.so* (see WAYLAND_EXCLUDES
# comment above). A silent regression here breaks Vulkan Video/AV1 on every host.
if ls $DEPLOY_FOLDER/usr/lib/libvulkan.so* >/dev/null 2>&1; then
    fail "Bundled libvulkan.so detected in AppImage dir - it must be excluded so the host loader (with Vulkan Video support) is used!"
fi

# Hard check: the glib family must NOT be bundled either (see WAYLAND_EXCLUDES
# comment). A bundled glib shadows the host's and breaks libva driver plugins
# (nvidia_drv_video.so) -> "no functioning hardware accelerated video decoder"
# on NVIDIA hosts. Same silent-regression class as the libvulkan check above.
for GLIBLIB in libglib-2.0.so libgobject-2.0.so libgio-2.0.so libgmodule-2.0.so libgthread-2.0.so; do
    if ls $DEPLOY_FOLDER/usr/lib/$GLIBLIB* >/dev/null 2>&1; then
        fail "Bundled $GLIBLIB detected in AppImage dir - it must be excluded so the host glib is used (bundled glib breaks libva driver dlopen on NVIDIA hosts)!"
    fi
done

# Pack the AppImage LAST, from the verified appdir. Second linuxdeploy pass
# WITHOUT the qt plugin: every dependency is already deployed, so this pass
# only validates and packs. Re-running the qt plugin here would re-import
# glib (its internal copy step ignores --exclude-library) and re-poison the
# bundle - so it must never be part of the packing pass.
echo Creating AppImage from verified appdir
pushd $INSTALLER_FOLDER
VERSION=$VERSION $LINUXDEPLOY --appdir $DEPLOY_FOLDER \
  --library=/usr/local/lib/libSDL3.so.0 \
  $WAYLAND_EXCLUDES \
  --output appimage || fail "linuxdeploy (pack) failed!"
popd
# Final gate: inspect the artifact itself, not just the appdir. If glib or
# libvulkan somehow made it into the packed image, fail here - never ship.
PACKED_APPIMAGE=$(ls -t $INSTALLER_FOLDER/*.AppImage | head -1)
[ -n "$PACKED_APPIMAGE" ] || fail "No AppImage produced!"
EXTRACT_CHECK=$(mktemp -d)
# --appimage-extract always unpacks to ./squashfs-root relative to CWD,
# so run it from inside the temp dir.
( cd "$EXTRACT_CHECK" && "$PACKED_APPIMAGE" --appimage-extract >/dev/null 2>&1 ) \
    || fail "Could not extract packed AppImage for final verification"
ls "$EXTRACT_CHECK/squashfs-root/usr/lib/" >/dev/null 2>&1 \
    || fail "Extraction produced no squashfs-root - cannot verify artifact"
if ls $EXTRACT_CHECK/squashfs-root/usr/lib/libglib-2.0.so* >/dev/null 2>&1 \
   || ls $EXTRACT_CHECK/squashfs-root/usr/lib/libgobject-2.0.so* >/dev/null 2>&1 \
   || ls $EXTRACT_CHECK/squashfs-root/usr/lib/libgio-2.0.so* >/dev/null 2>&1; then
    fail "Packed AppImage contains the glib family - SHIP-BLOCKER (breaks NVIDIA VA-API driver dlopen)"
fi
if ls $EXTRACT_CHECK/squashfs-root/usr/lib/libvulkan.so* >/dev/null 2>&1; then
    fail "Packed AppImage contains libvulkan - SHIP-BLOCKER (shadows host Vulkan Video loader)"
fi
rm -rf $EXTRACT_CHECK
echo "Final artifact verification passed: $(basename "$PACKED_APPIMAGE")"
echo Build successful
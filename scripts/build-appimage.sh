#!/bin/bash
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

BINARY_NAME=streamlight
DESKTOP_ID=com.foggybytes.StreamLight

if [ -n "$CI_VERSION" ]; then
  VERSION=$CI_VERSION
else
  # Strip a possible UTF-8 BOM and any stray whitespace. version.txt is edited on
  # Windows too, and those bytes would otherwise end up in the AppImage filename.
  VERSION=`sed -e '1s/^\xEF\xBB\xBF//' -e 's/[[:space:]]//g' $SOURCE_ROOT/app/version.txt`
fi

[ -n "$VERSION" ] || fail "Unable to determine the version to build!"

command -v qmake6 >/dev/null 2>&1 || fail "Unable to find 'qmake6' in your PATH!"
command -v linuxdeployqt >/dev/null 2>&1 || fail "Unable to find 'linuxdeployqt' in your PATH!"

echo Cleaning output directories
rm -rf $BUILD_FOLDER
rm -rf $DEPLOY_FOLDER
rm -rf $INSTALLER_FOLDER
mkdir -p $BUILD_ROOT
mkdir -p $BUILD_FOLDER
mkdir -p $DEPLOY_FOLDER
mkdir -p $INSTALLER_FOLDER

echo Configuring the project
pushd $BUILD_FOLDER
# Building with Wayland support will cause linuxdeployqt to include libwayland-client.so in the AppImage.
# Since we always use the host implementation of EGL, this can cause libEGL_mesa.so to fail to load due
# to missing symbols from the host's version of libwayland-client.so that aren't present in the older
# version of libwayland-client.so from our AppImage build environment. When this happens, EGL fails to
# work even in X11. To avoid this, we will disable Wayland support for the AppImage.
#
# We disable DRM support because linuxdeployqt doesn't bundle the appropriate libraries for Qt EGLFS.
qmake6 $SOURCE_ROOT/moonlight-qt.pro CONFIG+=disable-wayland CONFIG+=disable-libdrm PREFIX=$DEPLOY_FOLDER/usr DEFINES+=APP_IMAGE || fail "Qmake failed!"
popd

echo Compiling StreamLight in $BUILD_CONFIG configuration
pushd $BUILD_FOLDER
make -j$(nproc) $(echo "$BUILD_CONFIG" | tr '[:upper:]' '[:lower:]') || fail "Make failed!"
popd

echo Deploying to staging directory
pushd $BUILD_FOLDER
make install || fail "Make install failed!"
popd

# sdl2-compat is only a shim in front of SDL3, and it loads SDL3 with dlopen at
# runtime, so that dependency shows up in no ldd output and linuxdeployqt cannot
# see it. It has to be staged and passed explicitly. Real SDL2 needs none of this.
SDL2_LIB=`ldd $DEPLOY_FOLDER/usr/bin/$BINARY_NAME | awk '/libSDL2-2\.0\.so/ { print $3; exit }'`
[ -f "$SDL2_LIB" ] || fail "Unable to resolve the SDL2 library linked into $BINARY_NAME!"

EXTRA_EXEC_ARGS=""
if grep -aq libSDL3.so.0 "$SDL2_LIB"; then
  SDL3_LIB=`ldconfig -p | awk '$1 == "libSDL3.so.0" { print $NF; exit }'`
  [ -f "$SDL3_LIB" ] || fail "$SDL2_LIB is sdl2-compat, but libSDL3.so.0 is not on the library path!"

  echo Staging SDL3 library from $SDL3_LIB
  mkdir -p $DEPLOY_FOLDER/usr/lib
  cp $SDL3_LIB $DEPLOY_FOLDER/usr/lib/ || fail "Unable to stage the SDL3 library!"
  EXTRA_EXEC_ARGS="-executable=$DEPLOY_FOLDER/usr/lib/`basename $SDL3_LIB`"
else
  echo "$SDL2_LIB does not load SDL3 - nothing to stage"
fi

echo Creating AppImage
pushd $INSTALLER_FOLDER
VERSION=$VERSION linuxdeployqt $DEPLOY_FOLDER/usr/share/applications/$DESKTOP_ID.desktop \
  -qmake=qmake6 -qmldir=$SOURCE_ROOT/app/gui -appimage -extra-plugins=tls \
  $EXTRA_EXEC_ARGS || fail "linuxdeployqt failed!"
popd

echo Build successful

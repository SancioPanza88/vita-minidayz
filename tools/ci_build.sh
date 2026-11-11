#!/bin/sh
# Builds the VPK inside the vitasdk/vitasdk Docker image. Run from the repository root.
set -e
export VITASDK=${VITASDK:-/usr/local/vitasdk}
export PATH=$VITASDK/bin:$PATH
LIB=$VITASDK/arm-vita-eabi/lib
need() { command -v "$1" >/dev/null 2>&1; }
if ! need git || ! need cmake || ! need make; then
  (apt-get update && apt-get install -y git cmake make) || apk add --no-cache git cmake make
fi
DEPS=/tmp/vitadeps
mkdir -p $DEPS
# vitaGL and its dependencies, built from source when the image does not ship them
if [ ! -f $LIB/libmathneon.a ]; then
  git clone --depth 1 https://github.com/Rinnegatamante/math-neon $DEPS/math-neon
  make -C $DEPS/math-neon -j4 install
fi
if [ ! -f $LIB/libvitashark.a ]; then
  git clone --depth 1 https://github.com/Rinnegatamante/vitaShaRK $DEPS/vitaShaRK
  make -C $DEPS/vitaShaRK -j4 install
fi
if [ ! -f $LIB/libSceShaccCgExt.a ]; then
  git clone --depth 1 https://github.com/bythos14/SceShaccCgExt $DEPS/SceShaccCgExt
  (cd $DEPS/SceShaccCgExt && mkdir -p build && cd build && cmake .. && make -j4 install)
fi
if [ ! -f $LIB/libvitaGL.a ]; then
  git clone --depth 1 https://github.com/Rinnegatamante/vitaGL $DEPS/vitaGL
  make -C $DEPS/vitaGL -j4 NO_DEBUG=1 HAVE_GLSL_SUPPORT=1 install
fi
cmake -S . -B build-vita -DCMAKE_BUILD_TYPE=Release
cmake --build build-vita -j4
ls -la build-vita/*.vpk

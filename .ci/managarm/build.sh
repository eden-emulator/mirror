#!/usr/local/bin/bash -ex

# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

WORK_PATH="$PWD"

export CFLAGS="-U__FreeBSD__ -U__MMX__ -U__SSE__ -U__SSE2__ -U__SSE3__ -D_DEFAULT_SOURCE=1 -D__managarm__=1 -I$MANAGARM_ROOTFS/usr/include -I$MANAGARM_ROOTFS/usr/share/freestnd-cxx-hdrs/x86_64/include -I$MANAGARM_ROOTFS/usr/share/freestnd-c-hdrs/x86_64/include"
export CXXFLAGS="-U__FreeBSD__ -U__MMX__ -U__SSE__ -U__SSE2__ -U__SSE3__ -D_DEFAULT_SOURCE=1 -D__managarm__=1 -I$MANAGARM_ROOTFS/usr/include/c++/14.2.0 -I$MANAGARM_ROOTFS/usr/include -I$MANAGARM_ROOTFS/usr/include/c++/14.2.0/x86_64-managarm -I$MANAGARM_ROOTFS/usr/share/freestnd-c-hdrs/x86_64/include"
export CC=clang
export CXX=clang++
export LD=ld.lld

[ -z ${MANAGARM_ROOTFS+x} ] && exit

[ -f "managarm-toolchain.cmake" ] || cat << EOF >"managarm-toolchain.cmake"
set(CMAKE_SYSROOT "$MANAGARM_ROOTFS/usr")
set(CMAKE_STAGING_PREFIX "$MANAGARM_ROOTFS/usr")
set(CMAKE_SYSTEM_NAME "managarm")
set(UNIX TRUE)

set(CMAKE_C_FLAGS "$CFLAGS")
set(CMAKE_CXX_FLAGS "$CXXFLAGS")

set(CMAKE_EXE_LINKER_FLAGS "-L$MANAGARM_ROOTFS/usr/lib64 -L$MANAGARM_ROOTFS/usr/lib -L$MANAGARM_ROOTFS/usr/lib/gcc/x86_64-managarm/14.2.0")
set(CMAKE_C_LINK_FLAGS "-L$MANAGARM_ROOTFS/usr/lib64 -L$MANAGARM_ROOTFS/usr/lib -L$MANAGARM_ROOTFS/usr/lib/gcc/x86_64-managarm/14.2.0")
set(CMAKE_CXX_LINK_FLAGS "-L$MANAGARM_ROOTFS/usr/lib64 -L$MANAGARM_ROOTFS/usr/lib -L$MANAGARM_ROOTFS/usr/lib/gcc/x86_64-managarm/14.2.0")

set(CMAKE_C_COMPILER "$CC")
set(CMAKE_CXX_COMPILER "$CXX")
set(CMAKE_LINKER "$LD")
set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_LINKER> <CMAKE_C_LINK_FLAGS> <OBJECTS> -o <TARGET> -lc -lgcc <LINK_LIBRARIES>")
set(CMAKE_CXX_LINK_EXECUTABLE "<CMAKE_LINKER> <CMAKE_CXX_LINK_FLAGS> <OBJECTS> -o <TARGET> -lc -lstdc++ -lgcc <LINK_LIBRARIES>")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# TODO: Why does cmake not set this?
set(CMAKE_SIZEOF_VOID_P 8)
EOF

[ -z ${NPROC+x} ] && NPROC=$(nproc || 1)

build_openssl() {
    [ -d "$WORK_PATH/openssl" ] || git clone --depth=1 https://github.com/openssl/openssl "$WORK_PATH/openssl"
    cd "$WORK_PATH/openssl"
    [ -f Makefile ] || ./Configure -ffreestanding \
        -L$MANAGARM_ROOTFS/usr/lib -lc \
        -I$MANAGARM_ROOTFS/usr/include \
        no-threads no-shared no-afalgeng no-async no-capieng no-cmp no-cms no-comp no-ct \
        no-docs no-dgram no-dso no-dynamic-engine no-engine no-filenames no-gost \
        no-http no-legacy no-module no-nextprotoneg no-ocsp no-padlockeng no-quic \
        no-srp no-srtp no-ssl-trace no-static-engine no-tests no-thread-pool no-ts \
        no-ui-console no-uplink no-ssl3-method no-tls1-method no-tls1_1-method \
        no-dtls1-method no-dtls1_2-method no-argon2 no-bf no-blake2 no-cast \
        no-dsa no-idea no-md4 no-mdc2 no-ocb no-rc2 no-rc4 no-rmd160 no-scrypt \
        no-siphash no-siv no-sm2 no-sm3 no-sm4 no-whirlpool no-apps \
        --prefix=$MANAGARM_ROOTFS/usr BSD-generic64
    gmake -j8
    sudo gmake install -j8
    cd "$WORK_PATH"
}
[ -f "$MANAGARM_ROOTFS/usr/lib/libssl.a" ] || build_openssl

# Normally a platform has a package manager
# but managarm has one thats so underdeveloped
# we may as well do it ourselves
export EXTRA_CMAKE_FLAGS=("${EXTRA_CMAKE_FLAGS[@]}" $@)
cmake -S . -B build -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="managarm-toolchain.cmake" \
    -DENABLE_QT_TRANSLATION=OFF \
    -DENABLE_CUBEB=OFF \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_CXX_FLAGS="$ARCH_FLAGS -pipe" \
    -DCMAKE_C_FLAGS="$ARCH_FLAGS -pipe" \
    -DENABLE_SDL2=ON \
    -DENABLE_LIBUSB=OFF \
    -DENABLE_UPDATE_CHECKER=OFF \
    -DENABLE_QT=OFF \
    -DENABLE_OPENGL=OFF \
    -DENABLE_OPENSSL=OFF \
    -DENABLE_WEB_SERVICE=OFF \
    -DUSE_DISCORD_PRESENCE=OFF \
    -DCPMUTIL_FORCE_BUNDLED=ON \
    -DOPENSSL_ROOT_DIR="$MANAGARM_ROOTFS/usr" \
    -DOPENSSL_SSL_LIBRARY="$MANAGARM_ROOTFS/usr/lib/libssl.a" \
    -DOPENSSL_CRYPTO_LIBRARY="$MANAGARM_ROOTFS/usr/lib/libcrypto.a" \
    -DOPENSSL_INCLUDE_DIR="$MANAGARM_ROOTFS/usr/include/openssl" \
    -DYUZU_USE_EXTERNAL_FFMPEG=ON \
    -DYUZU_USE_CPM=ON \
    -DDYNARMIC_ENABLE_NO_EXECUTE_SUPPORT=OFF \
    -DDYNARMIC_TESTS=ON \
    -DYUZU_TESTS=ON \
    -DYUZU_USE_EXTERNAL_SDL2=ON \
    -DHAVE_SDL_THREADS=ON \
    -DSDL_THREADS=ON \
    -DSDL_PTHREADS=ON \
    -DSDL_THREAD_PTHREAD=ON \
    -DSDL_PTHREADS_SEM=OFF \
    -DSDL_ALTIVEC=OFF \
    -DSDL_DISKAUDIO=OFF \
    -DSDL_DIRECTFB=ON \
    -DSDL_OPENGL=ON \
    -DSDL_OPENGLES=ON \
    -DSDL_PTHREADS=ON \
    -DSDL_PTHREADS_SEM=OFF \
    -DSDL_STATIC=ON \
    "${EXTRA_CMAKE_FLAGS[@]}" || exit

cmake --build build -t yuzu-cmd -- -j$NPROC

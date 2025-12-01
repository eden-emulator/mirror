#!/usr/local/bin/bash -ex

# Define global vars
# These flags are used everywhere, so let's reuse them.
export OO_PS4_TOOLCHAIN="$PWD/prefix"
export PREFIX="$OO_PS4_TOOLCHAIN"
export CC="clang"
export CXX="clang++"
export AR="llvm-ar"
export CFLAGS="-fPIC -DPS4 -D_LIBUNWIND_IS_BAREMETAL=1"
export CXXFLAGS="$CFLAGS -D__STDC_VERSION__=0"
export TARGET="x86_64-scei-ps4"
export LLVM_ROOT="$PWD/llvm-project"
export LLVM_PATH="$PWD/llvm-project/llvm"
export WORK_PATH="$PWD"

prepare_prefix() {
    [ -d OpenOrbis-PS4-Toolchain ] || git clone --depth=1 https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain
    [ -d musl ] || git clone --depth=1 https://github.com/OpenOrbis/musl
    [ -d llvm-project ] || git clone --depth=1 --branch openorbis/20.x https://github.com/seuros/llvm-project
    [ -d create-fself ] || git clone --depth=1 https://github.com/OpenOrbis/create-fself
    [ -d create-gp4 ] || git clone --depth=1 https://github.com/OpenOrbis/create-gp4
    [ -d readoelf ] || git clone --depth=1 https://github.com/OpenOrbis/readoelf

    mkdir -p $PREFIX "$PREFIX/bin" "$PREFIX/include"
    [ -f "$PREFIX/include/orbis/libkernel.h" ] || cp -r OpenOrbis-PS4-Toolchain/include/* "$PREFIX/include/"
    mkdir -p $PREFIX/usr
    [ -L "$PREFIX/usr/include" ] || ln -s $PREFIX/include $PREFIX/usr/include || echo 1
    [ -L "$PREFIX/usr/share" ] || ln -s $PREFIX/share $PREFIX/usr/share || echo 1
    [ -L "$PREFIX/usr/lib" ] || ln -s $PREFIX/lib $PREFIX/usr/lib || echo 1
    [ -L "$PREFIX/usr/bin" ] || ln -s $PREFIX/bin $PREFIX/usr/bin || echo 1
}

build_musl() {
    mkdir -p musl-build
    cd musl-build
    ../musl/configure --target=$TARGET --disable-shared CC="$CC" CFLAGS="$CFLAGS" --prefix=$PREFIX
    gmake -j8 && gmake install
    cd ..
}

build_llvm() {
    # Build compiler-rt
    cmake "$LLVM_ROOT/compiler-rt" -B "$WORK_PATH/llvm-build/compiler-rt" \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
        -DCMAKE_C_FLAGS="$CFLAGS" -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
        -DCMAKE_ASM_COMPILER="$CC" -DCMAKE_ASM_FLAGS="$CFLAGS -x assembler-with-cpp" \
        -DLLVM_PATH="$LLVM_PATH" -DCOMPILER_RT_DEFAULT_TARGET_TRIPLE="$TARGET" \
        -DCOMPILER_RT_BAREMETAL_BUILD=YES -DCOMPILER_RT_BUILD_BUILTINS=ON \
        -DCOMPILER_RT_BUILD_CRT=OFF -DCOMPILER_RT_BUILD_SANITIZERS=OFF \
        -DCOMPILER_RT_BUILD_XRAY=OFF -DCOMPILER_RT_BUILD_LIBFUZZER=OFF \
        -DCOMPILER_RT_BUILD_PROFILE=OFF -DCOMPILER_RT_STANDALONE_BUILD=ON
    # Build libunwind
    cmake "$LLVM_ROOT/libunwind" -B "$WORK_PATH/llvm-build/libunwind" \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
        -DCMAKE_C_FLAGS="$CFLAGS -fcxx-exceptions" -DCMAKE_CXX_FLAGS="$CXXFLAGS -fcxx-exceptions" \
        -DCMAKE_ASM_COMPILER="$CC" -DCMAKE_ASM_FLAGS="$CFLAGS -x assembler-with-cpp" \
        -DLLVM_PATH="$LLVM_PATH" -DLIBUNWIND_USE_COMPILER_RT=YES \
        -DLIBUNWIND_BUILD_32_BITS=NO -DLIBUNWIND_ENABLE_STATIC=ON \
        -DLIBUNWIND_ENABLE_SHARED=OFF -DLIBUNWIND_IS_BAREMETAL=ON
    # Build libcxxabi
    cmake "$LLVM_ROOT/libcxxabi" -B "$WORK_PATH/llvm-build/libcxxabi" \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
        -DCMAKE_C_FLAGS="$CFLAGS -D_GNU_SOURCE=1 -isysroot $PREFIX -isystem $LLVM_ROOT/libcxx/include -isystem $PREFIX/include -isystem $WORK_PATH/llvm-build/libcxx/include/c++/v1" \
        -DCMAKE_CXX_FLAGS="$CXXFLAGS -D_GNU_SOURCE=1 -isysroot $PREFIX -isystem $LLVM_ROOT/libcxx/include -isystem $PREFIX/include -isystem $WORK_PATH/llvm-build/libcxx/include/c++/v1" \
        -DCMAKE_ASM_COMPILER="$CC" -DCMAKE_ASM_FLAGS="$CFLAGS -x assembler-with-cpp" \
        -DLLVM_PATH="$LLVM_PATH" -DLIBCXXABI_ENABLE_SHARED=NO \
        -DLLVM_ENABLE_RUNTIMES="rt;libunwind" \
        -DLIBCXXABI_ENABLE_STATIC=YES -DLIBCXXABI_ENABLE_EXCEPTIONS=YES \
        -DLIBCXXABI_USE_COMPILER_RT=YES -DLIBCXXABI_USE_LLVM_UNWINDER=YES \
        -DLIBCXXABI_LIBUNWIND_PATH="$LLVM_ROOT/libunwind" \
        -DLIBCXXABI_LIBCXX_INCLUDES="$LLVM_ROOT/libcxx/include" \
        -DLIBCXXABI_ENABLE_PIC=YES
    # Build libcxx
    cmake "$LLVM_ROOT/libcxx" -B "$WORK_PATH/llvm-build/libcxx" \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
        -DCMAKE_C_FLAGS="$CFLAGS -D_LIBCPP_HAS_MUSL_LIBC=1 -D_GNU_SOURCE=1 -isysroot $PREFIX -isystem $PREFIX/include/c++/v1 -isystem $PREFIX/include" \
        -DCMAKE_CXX_FLAGS="$CXXFLAGS -D_LIBCPP_HAS_MUSL_LIBC=1 -D_GNU_SOURCE=1 -isysroot $PREFIX -isystem $PREFIX/include/c++/v1 -isystem $PREFIX/include" \
        -DCMAKE_ASM_COMPILER="$CC" -DCMAKE_ASM_FLAGS="$CFLAGS -x assembler-with-cpp" \
        -DLLVM_PATH="$LLVM_PATH" -DLIBCXX_ENABLE_RTTI=YES \
        -DLIBCXX_HAS_MUSL_LIBC=YES -DLIBCXX_ENABLE_SHARED=NO \
        -DLIBCXX_CXX_ABI=libcxxabi -DLIBCXX_CXX_ABI_INCLUDE_PATHS="$LLVM_ROOT/libcxxabi/include" \
        -DLIBCXX_CXX_ABI_LIBRARY_PATH="$LLVM_ROOT/libcxxabi/build/lib"
    
    cmake --build "$WORK_PATH/llvm-build/compiler-rt" --parallel
    cmake --install "$WORK_PATH/llvm-build/compiler-rt"

    cmake --build "$WORK_PATH/llvm-build/libunwind" --parallel
    cmake --install "$WORK_PATH/llvm-build/libunwind"

    cmake --build "$WORK_PATH/llvm-build/libcxxabi" --parallel
    cmake --install "$WORK_PATH/llvm-build/libcxxabi"

    touch "$WORK_PATH/llvm-build/libcxx/include/c++/v1/libcxx.imp"
    cmake --build "$WORK_PATH/llvm-build/libcxx" --parallel
    cmake --install "$WORK_PATH/llvm-build/libcxx"
}

build_tools() {
    # Build create-fself
    cd create-fself/cmd/create-fself
    cp go-linux.mod go.mod
    go build -o create-fself
    mv ./create-fself $PREFIX/bin/create-fself
    cd ../../../

    # Build create-gp4
    cd create-gp4/cmd/create-gp4
    go build -o create-gp4
    mv ./create-gp4 $PREFIX/bin/create-gp4
    cd ../../../

    # Build readoelf
    cd readoelf/cmd/readoelf
    go build -o readoelf
    mv ./readoelf $PREFIX/bin/readoelf
    cd ../../../

    # Pull maxton's publishing tools (<3)
    # Sadly maxton has passed on, we have forked the repository and will continue to update it in the future. RIP <3
    cd $PREFIX/bin
    [ -f PkgTool.Core-linux-x64-0.2.231.zip ] || wget https://github.com/maxton/LibOrbisPkg/releases/download/v0.2/PkgTool.Core-linux-x64-0.2.231.zip
    [ -f PkgTool.Core ] || unzip PkgTool.Core-linux-x64-0.2.231.zip
    chmod +x PkgTool.Core
}

finish_prefix() {
    as $WORK_PATH/OpenOrbis-PS4-Toolchain/src/crt/crtlib.S -o $PREFIX/lib/crtlib.o
    cp -a $WORK_PATH/OpenOrbis-PS4-Toolchain/link.x $PREFIX/

    cp -a ~/OpenOrbis/PS4Toolchain/lib/libkernel* $PREFIX/lib/
    cp -a ~/OpenOrbis/PS4Toolchain/lib/libSce* $PREFIX/lib/

    cp -a ~/OpenOrbis/PS4Toolchain/lib/libSDL* $PREFIX/lib/
    cp -r ~/OpenOrbis/PS4Toolchain/include/SDL2 $PREFIX/include/SDL2

    cp $WORK_PATH/llvm-build/compiler-rt/lib/freebsd/libclang_rt.builtins-x86_64.a $PREFIX/lib/

    # Combine libc++, libc++abi and libunwind into a single archive
    cat << EOF >"mri.txt"
CREATE $PREFIX/lib/libc++M.a
ADDLIB $PREFIX/lib/libunwind.a
ADDLIB $PREFIX/lib/libc++abi.a
ADDLIB $PREFIX/lib/libc++.a
SAVE
END
EOF
    $AR -M < mri.txt
    cp $PREFIX/lib/libc++M.a $PREFIX/lib/libc++.a
    # Merge compiler-rt into libc
    cat << EOF >"mri.txt"
CREATE $PREFIX/lib/libcM.a
ADDLIB $PREFIX/lib/libc.a
ADDLIB $PREFIX/lib/libclang_rt.builtins-x86_64.a
SAVE
END
EOF
    $AR -M < mri.txt
    cp $PREFIX/lib/libcM.a $PREFIX/lib/libc.a
    rm mri.txt
}

prepare_prefix
build_musl
build_llvm
build_tools
finish_prefix

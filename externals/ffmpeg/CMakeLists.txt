# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2021 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

set(FFmpeg_HWACCEL_LIBRARIES)
set(FFmpeg_HWACCEL_FLAGS)
set(FFmpeg_HWACCEL_INCLUDE_DIRS)
set(FFmpeg_HWACCEL_LDFLAGS)

if (NOT YUZU_USE_BUNDLED_FFMPEG)
    set(FFmpeg_CROSS_COMPILE_FLAGS "")
    if (CMAKE_CROSSCOMPILING)
        message(STATUS "ffmpeg: Enabling cross compilation ${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
        # TODO: Maybe use CMAKE_SYSROOT? and probably provide a toolchain file for android
        # I mean isn't that the "proper" way anyways?
        if (ANDROID)
            string(TOLOWER "${CMAKE_HOST_SYSTEM_NAME}" FFmpeg_HOST_SYSTEM_NAME)
            set(TOOLCHAIN "${ANDROID_NDK}/toolchains/llvm/prebuilt/${FFmpeg_HOST_SYSTEM_NAME}-${CMAKE_HOST_SYSTEM_PROCESSOR}")
            set(SYSROOT "${TOOLCHAIN}/sysroot")
            set(FFmpeg_CPU "armv8-a")
            list(APPEND FFmpeg_CROSS_COMPILE_FLAGS
                --enable-cross-compile
                --arch=arm64
                #--cpu=${FFmpeg_CPU}
                --cross-prefix="${TOOLCHAIN}/bin/aarch64-linux-android-"
                --sysroot="${SYSROOT}"
                --target-os=android
                --extra-ldflags="--ld-path=${TOOLCHAIN}/bin/ld.lld"
                --extra-ldflags="-nostdlib")
            set(FFmpeg_IS_CROSS_COMPILING TRUE)
        else()
            string(TOLOWER "${CMAKE_SYSTEM_NAME}" FFmpeg_SYSTEM_NAME)
            # systems FFmpeg natively supports
            if (NOT (NETBSD OR SOLARIS OR FREEBSD
            OR OPENBSD OR DRAGONFLYBSD OR HAIKU
            OR LINUX OR MSYS OR WIN32 OR ANDROID OR APPLE))
                set(FFmpeg_SYSTEM_NAME "none")
            endif()
            # TODO: Can we really do better? Auto-detection? Something clever?
            list(APPEND FFmpeg_CROSS_COMPILE_FLAGS
                --enable-cross-compile
                --arch="${CMAKE_SYSTEM_PROCESSOR}"
                --target-os="${FFmpeg_SYSTEM_NAME}"
                --sysroot="${CMAKE_SYSROOT}")
            if (DEFINED FFmpeg_CROSS_PREFIX)
                list(APPEND FFmpeg_CROSS_COMPILE_FLAGS --cross-prefix="${FFmpeg_CROSS_PREFIX}")
            else()
                message(FATAL_ERROR "Please set FFmpeg_CROSS_PREFIX to your cross toolchain prefix, for example: \${CMAKE_STAGING_PREFIX}/bin/${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_SYSTEM_NAME}-")
            endif()
            set(FFmpeg_IS_CROSS_COMPILING TRUE)
        endif()
    endif()
endif()

if (WIN32)
    list(APPEND FFmpeg_HWACCEL_FLAGS
        --enable-d3d11va
        --enable-d3d12va
        --enable-dxva2
        --enable-hwaccel=d3d11va_h264
        --enable-hwaccel=d3d11va2_h264
        --enable-hwaccel=d3d12va_h264
        --enable-hwaccel=dxva2_h264
        --enable-hwaccel=d3d11va_vp9
        --enable-hwaccel=d3d11va2_vp9
        --enable-hwaccel=d3d12va_vp9
        --enable-hwaccel=dxva2_vp9)
elseif (ANDROID)
    list(APPEND FFmpeg_HWACCEL_FLAGS
        --enable-mediacodec
        --enable-jni
        --enable-decoder=h264_mediacodec
        --enable-decoder=vp8_mediacodec
        --enable-decoder=vp9_mediacodec)
elseif (APPLE)
    list(APPEND FFmpeg_HWACCEL_FLAGS
        --enable-videotoolbox
        --enable-hwaccel=videotoolbox_h264
        --enable-hwaccel=videotoolbox_vp9)
endif()

find_package(X11)
if(X11_FOUND)
    # Include X11 if possible, VDPAU lists it as complementary
    list(APPEND FFmpeg_HWACCEL_LIBRARIES ${X11_LIBRARIES})
    list(APPEND FFmpeg_HWACCEL_INCLUDE_DIRS ${X11_INCLUDE_DIRS})
    list(APPEND FFmpeg_HWACCEL_LDFLAGS ${X11_LDFLAGS})
else()
    list(APPEND FFmpeg_HWACCEL_FLAGS --disable-vaapi)
endif()
message(STATUS "ffmpeg: X11 ${X11_FOUND} ${X11_VERSION}")

find_package(PkgConfig)
if (PkgConfig_FOUND)
    pkg_check_modules(LIBDRM libdrm)
    if (LIBDRM_FOUND)
        # Solaris needs explicit linking for ffmpeg which links to /lib/amd64/libX11.so
        if(SOLARIS)
            list(APPEND FFmpeg_HWACCEL_LIBRARIES
                X11
                "${CMAKE_SYSROOT}/usr/lib/xorg/amd64/libdrm.so")
        else()
            list(APPEND FFmpeg_HWACCEL_LIBRARIES ${LIBDRM_LIBRARIES})
            list(APPEND FFmpeg_HWACCEL_INCLUDE_DIRS ${LIBDRM_INCLUDE_DIRS})
            list(APPEND FFmpeg_HWACCEL_LDFLAGS ${LIBDRM_LDFLAGS})
        endif()
        list(APPEND FFmpeg_HWACCEL_FLAGS --enable-libdrm)
    endif()

    pkg_check_modules(LIBVA libva)
    if(LIBVA_FOUND)
        list(APPEND FFmpeg_HWACCEL_LIBRARIES ${LIBVA_LIBRARIES})
        list(APPEND FFmpeg_HWACCEL_INCLUDE_DIRS ${LIBVA_INCLUDE_DIRS})
        list(APPEND FFmpeg_HWACCEL_LDFLAGS ${LIBVA_LDFLAGS})
        list(APPEND FFmpeg_HWACCEL_FLAGS
            --enable-vaapi
            --enable-hwaccel=h264_vaapi
            --enable-hwaccel=vp8_vaapi
            --enable-hwaccel=vp9_vaapi)
        # Logically, they can only exist if libva itself exists
        pkg_check_modules(LIBVA-DRM libva-drm)
        if (LIBVA-DRM_FOUND)
            list(APPEND FFmpeg_HWACCEL_LIBRARIES ${LIBVA-DRM_LIBRARIES})
            list(APPEND FFmpeg_HWACCEL_INCLUDE_DIRS ${LIBVA-DRM_INCLUDE_DIRS})
            list(APPEND FFmpeg_HWACCEL_LDFLAGS ${LIBVA-DRM_LDFLAGS})
        endif()
        message(STATUS "ffmpeg: LIBVA-DRM ${LIBVA-DRM_FOUND} ${LIBVA-DRM_VERSION}")
        pkg_check_modules(LIBVA-X11 libva-x11)
        if (LIBVA-X11_FOUND)
            list(APPEND FFmpeg_HWACCEL_LIBRARIES ${LIBVA-X11_LIBRARIES})
            list(APPEND FFmpeg_HWACCEL_INCLUDE_DIRS ${LIBVA-X11_INCLUDE_DIRS})
            list(APPEND FFmpeg_HWACCEL_LDFLAGS ${LIBVA-X11_LDFLAGS})
        endif()
        message(STATUS "ffmpeg: LIBVA-X11 ${LIBVA-X11_FOUND} ${LIBVA-X11_VERSION}")
    else()
        list(APPEND FFmpeg_HWACCEL_FLAGS --disable-vaapi)
    endif()
    message(STATUS "ffmpeg: LIBVA ${LIBVA_FOUND} ${LIBVA_VERSION}")

    pkg_check_modules(VDPAU vdpau)
    if (VDPAU_FOUND)
        list(APPEND FFmpeg_HWACCEL_FLAGS
            --enable-vdpau
            --enable-hwaccel=h264_vdpau
            --enable-hwaccel=vp9_vdpau)
        list(APPEND FFmpeg_HWACCEL_LIBRARIES ${VDPAU_LIBRARIES})
        list(APPEND FFmpeg_HWACCEL_INCLUDE_DIRS ${VDPAU_INCLUDE_DIRS})
        list(APPEND FFmpeg_HWACCEL_LDFLAGS ${VDPAU_LDFLAGS})
    else()
        list(APPEND FFmpeg_HWACCEL_FLAGS --disable-vdpau)
    endif()
    message(STATUS "ffmpeg: VDPAU ${VDPAU_FOUND} ${VDPAU_VERSION}")

    pkg_check_modules(FFNVCODEC ffnvcodec)
    if (FFNVCODEC_FOUND)
        list(APPEND FFmpeg_HWACCEL_FLAGS
            --enable-cuvid
            --enable-ffnvcodec
            --enable-nvdec
            --enable-hwaccel=h264_nvdec
            --enable-hwaccel=vp8_nvdec
            --enable-hwaccel=vp9_nvdec)
        list(APPEND FFmpeg_HWACCEL_LIBRARIES ${FFNVCODEC_LIBRARIES})
        list(APPEND FFmpeg_HWACCEL_INCLUDE_DIRS ${FFNVCODEC_INCLUDE_DIRS})
        list(APPEND FFmpeg_HWACCEL_LDFLAGS ${FFNVCODEC_LDFLAGS})
        # ffnvenc could load CUDA libraries at the runtime using dlopen/dlsym or LoadLibrary/GetProcAddress
        # here we handle the hard-linking scenario where CUDA is linked during compilation
        find_package(CUDAToolkit)
        if (CUDAToolkit_FOUND)
            list(APPEND FFmpeg_HWACCEL_LIBRARIES ${CUDAToolkit_LIBRARIES})
            list(APPEND FFmpeg_HWACCEL_INCLUDE_DIRS ${CUDAToolkit_INCLUDE_DIRS})
            list(APPEND FFmpeg_HWACCEL_LDFLAGS ${CUDAToolkit_LDFLAGS})
        endif()
        message(STATUS "ffmpeg: CUDAToolkit ${CUDAToolkit_FOUND} ${CUDAToolkit_VERSION}")
    endif()
    message(STATUS "ffmpeg: FFNVCODEC ${FFNVCODEC_FOUND} ${FFNVCODEC_VERSION}")
endif()
message(STATUS "ffmpeg: PkgConfig ${PkgConfig_FOUND} ${PkgConfig_VERSION}")

find_package(Vulkan)
if (Vulkan_FOUND)
    list(APPEND FFmpeg_HWACCEL_FLAGS
        --enable-vulkan
        --enable-hwaccel=h264_vulkan
        --enable-hwaccel=vp9_vulkan)
    list(APPEND FFmpeg_HWACCEL_LIBRARIES ${Vulkan_LIBRARIES})
    list(APPEND FFmpeg_HWACCEL_INCLUDE_DIRS ${Vulkan_INCLUDE_DIRS})
endif()
message(STATUS "ffmpeg: Vulkan ${Vulkan_FOUND} ${Vulkan_VERSION}")

find_package(spirv-headers)
if (spirv-headers_FOUND)
    list(APPEND FFmpeg_HWACCEL_LIBRARIES ${spirv-headers_LIBRARIES})
    list(APPEND FFmpeg_HWACCEL_INCLUDE_DIRS ${spirv-headers_INCLUDE_DIRS})
    list(APPEND FFmpeg_HWACCEL_LDFLAGS ${spirv-headers_LDFLAGS})
endif()
message(STATUS "ffmpeg: spirv-headers ${spirv-headers_FOUND} ${spirv-headers_VERSION}")

if (OPENORBIS)
    list(APPEND FFmpeg_CROSS_COMPILE_LIBS
        -lkernel
        -lSceUserService
        -lSceSysmodule
        -lSceNet
        -lSceLibcInternal)
    list(APPEND FFmpeg_CROSS_COMPILE_FLAGS
        --disable-pthreads
        --extra-cflags=${CMAKE_SYSROOT}/usr/include
        --extra-cxxflags=${CMAKE_SYSROOT}/usr/include
        --extra-libs="${FFmpeg_CROSS_COMPILE_LIBS}")
elseif (MANAGARM)
    # Required for proper stuff
    list(APPEND FFmpeg_CROSS_COMPILE_FLAGS 
        --disable-pthreads
        --extra-libs="${FFmpeg_CROSS_COMPILE_LIBS}")
endif()

# Usually used by Emscripten
if (DEFINED CMAKE_AR)
    list(APPEND FFmpeg_CROSS_COMPILE_FLAGS --ar=${CMAKE_AR})
endif()
if (DEFINED CMAKE_NM)
    list(APPEND FFmpeg_CROSS_COMPILE_FLAGS --nm=${CMAKE_NM})
endif()
if (DEFINED CMAKE_RANLIB)
    list(APPEND FFmpeg_CROSS_COMPILE_FLAGS --ranlib=${CMAKE_RANLIB})
endif()

if (YUZU_USE_BUNDLED_FFMPEG)
    AddJsonPackage(ffmpeg-ci)

    set(FFmpeg_INCLUDE_DIR
        "${FFmpeg_SOURCE_DIR}/include;${FFmpeg_HWACCEL_INCLUDE_DIRS}"
        PARENT_SCOPE)

    set(FFmpeg_PATH
        "${FFmpeg_SOURCE_DIR}"
        PARENT_SCOPE)

    set(FFmpeg_LIBRARY_DIR
        "${FFmpeg_SOURCE_DIR}/bin"
        PARENT_SCOPE)

    set(FFmpeg_LIBRARIES
        FFmpeg::FFmpeg
        ${FFmpeg_HWACCEL_LIBRARIES}
        PARENT_SCOPE)
    
    set(FFmpeg_FOUND YES)
else()
    # Build FFmpeg from externals
    message(STATUS "Using FFmpeg from externals")
    AddJsonPackage(ffmpeg)

    # FFmpeg has source that requires one of nasm or yasm to assemble it.
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|amd64)")
        find_program(ASSEMBLER NAMES nasm yasm REQUIRED)
    endif()
    find_program(AUTOCONF autoconf REQUIRED)

    set(FFmpeg_PREFIX ${ffmpeg_SOURCE_DIR})
    set(FFmpeg_BUILD_DIR ${ffmpeg_BINARY_DIR})
    set(FFmpeg_MAKEFILE ${FFmpeg_BUILD_DIR}/Makefile)
    make_directory(${FFmpeg_BUILD_DIR})

    # Read version string from external
    file(READ ${FFmpeg_PREFIX}/RELEASE FFmpeg_VERSION)
    set(FFmpeg_FOUND NO)
    if (NOT FFmpeg_VERSION STREQUAL "")
        set(FFmpeg_FOUND YES)
    endif()

    unset(FFmpeg_LIBRARIES CACHE)

    foreach(COMPONENT ${FFmpeg_COMPONENTS})
        set(FFmpeg_${COMPONENT}_PREFIX "${FFmpeg_BUILD_DIR}/lib${COMPONENT}")
        set(FFmpeg_${COMPONENT}_LIB_NAME "lib${COMPONENT}.a")
        set(FFmpeg_${COMPONENT}_LIBRARY "${FFmpeg_${COMPONENT}_PREFIX}/${FFmpeg_${COMPONENT}_LIB_NAME}")

        set(FFmpeg_LIBRARIES
            ${FFmpeg_LIBRARIES}
            ${FFmpeg_${COMPONENT}_LIBRARY}
            CACHE PATH "Paths to FFmpeg libraries" FORCE)
    endforeach()

    find_program(BASH_PROGRAM bash REQUIRED)

    # `configure` parameters builds only exactly what yuzu needs from FFmpeg
    # `--disable-vdpau` is needed to avoid linking issues
    set(FFmpeg_CC ${CMAKE_C_COMPILER_LAUNCHER} ${CMAKE_C_COMPILER})
    set(FFmpeg_CXX ${CMAKE_CXX_COMPILER_LAUNCHER} ${CMAKE_CXX_COMPILER})
    set(FFmpeg_HOST_CC ${CMAKE_C_HOST_COMPILER})
    if (FFmpeg_IS_CROSS_COMPILING)
        list(APPEND FFmpeg_CROSS_COMPILE_FLAGS --ld=${CMAKE_LINKER})
    endif ()
    # TODO: This is a bit fragile, it depends on FFmpeg_HWACCEL_LDFLAGS being a list
    # whereas CMAKE_C_FLAGS (and likewise) is meant to NOT be a list, but rather
    # an elongated piece of string
    add_custom_command(
        OUTPUT
            ${FFmpeg_MAKEFILE}
        COMMAND
            ${BASH_PROGRAM} ${FFmpeg_PREFIX}/configure
                --disable-avdevice
                --disable-avformat
                --disable-doc
                --disable-everything
                --disable-autodetect
                --disable-ffmpeg
                --disable-ffprobe
                --disable-network
                --disable-swresample
                --disable-iconv
                --enable-decoder=h264
                --enable-decoder=vp8
                --enable-decoder=vp9
                --enable-filter=yadif,scale
                --enable-pic
                --cc=${FFmpeg_CC}
                --cxx=${FFmpeg_CXX}
                --extra-cflags=${CMAKE_C_FLAGS}
                --extra-cxxflags=${CMAKE_CXX_FLAGS}
                --extra-ldflags="${FFmpeg_HWACCEL_LDFLAGS}"
                --host-cc=${FFmpeg_HOST_CC}
                ${FFmpeg_HWACCEL_FLAGS}
                ${FFmpeg_CROSS_COMPILE_FLAGS}
        WORKING_DIRECTORY
            ${FFmpeg_BUILD_DIR})
    unset(FFmpeg_CC)
    unset(FFmpeg_CXX)
    unset(FFmpeg_HOST_CC)
    unset(FFmpeg_LD)
    unset(FFmpeg_HWACCEL_FLAGS)
    unset(FFmpeg_CROSS_COMPILE_FLAGS)

    # Workaround for Ubuntu 18.04's older version of make not being able to call make as a child
    # with context of the jobserver. Also helps ninja users.
    cmake_host_system_information(RESULT SYSTEM_THREADS QUERY NUMBER_OF_LOGICAL_CORES)
    set(FFmpeg_MAKE_ARGS "")
    if (DEFINED SYSTEM_THREADS)
        set(FFmpeg_MAKE_ARGS -j ${SYSTEM_THREADS})
    endif()

    set(FFmpeg_BUILD_LIBRARIES ${FFmpeg_LIBRARIES})

    # Most distros use 'gmake', Arch uses 'make'
    find_program(MAKE NAMES gmake make REQUIRED)
    add_custom_command(
        OUTPUT
            ${FFmpeg_BUILD_LIBRARIES}
        COMMAND
            ${MAKE} ${FFmpeg_MAKE_ARGS}
        WORKING_DIRECTORY
            ${FFmpeg_BUILD_DIR})

    set(FFmpeg_INCLUDE_DIR
        "${FFmpeg_PREFIX};${FFmpeg_BUILD_DIR};${FFmpeg_HWACCEL_INCLUDE_DIRS}"
        CACHE PATH "Path to FFmpeg headers" FORCE)

    set(FFmpeg_LDFLAGS
        "${FFmpeg_HWACCEL_LDFLAGS}"
        CACHE STRING "FFmpeg linker flags" FORCE)

    # ALL makes this custom target build every time
    # but it won't actually build if the DEPENDS parameter is up to date
    add_custom_target(ffmpeg-configure ALL DEPENDS ${FFmpeg_MAKEFILE})
    add_custom_target(ffmpeg-build ALL DEPENDS ${FFmpeg_BUILD_LIBRARIES} ffmpeg-configure)
    link_libraries(${FFmpeg_LIBVA_LIBRARIES})
    set(FFmpeg_LIBRARIES ${FFmpeg_BUILD_LIBRARIES} ${FFmpeg_HWACCEL_LIBRARIES}
        CACHE PATH "Paths to FFmpeg libraries" FORCE)
    unset(FFmpeg_BUILD_LIBRARIES)
    unset(FFmpeg_HWACCEL_FLAGS)
    unset(FFmpeg_HWACCEL_INCLUDE_DIRS)
    unset(FFmpeg_HWACCEL_LDFLAGS)
    unset(FFmpeg_HWACCEL_LIBRARIES)
endif()
unset(FFmpeg_COMPONENTS)

if (NOT FFmpeg_FOUND)
    message(FATAL_ERROR "FFmpeg not found")
endif()
message(STATUS "Found FFmpeg version ${FFmpeg_VERSION}")

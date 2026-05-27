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
    if (ANDROID)
        # TODO: Maybe use CMAKE_SYSROOT? and probably provide a toolchain file for android
        # I mean isn't that the "proper" way anyways?
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
            --extra-ldflags="-nostdlib"
        )
        set(FFmpeg_IS_CROSS_COMPILING TRUE)
    # User attempts to do a FFmpeg cross compilation because...
    # Here we just quickly test against host/system processors not matching
    # TODO: Test for versions not matching as well?
    elseif (NOT ("${CMAKE_HOST_SYSTEM_PROCESSOR}" STREQUAL "${CMAKE_SYSTEM_PROCESSOR}"
    AND "${CMAKE_HOST_SYSTEM_NAME}" STREQUAL "${CMAKE_SYSTEM_NAME}"))
        string(TOLOWER "${CMAKE_SYSTEM_NAME}" FFmpeg_SYSTEM_NAME)
        # All of these platforms are supported by ffmpeg as native build OSes
        # anything else (like Redox or Managarm or PS4) is NOT natively supported
        # hence, assume the "unix like" is just "none" for the sake of OUR sanity.
        # If YOUR OS/platform has actual native support:
        # 1. congrats
        # 2. feel free to add it on the condition below
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
            --sysroot="${CMAKE_SYSROOT}"
        )
        if (DEFINED FFmpeg_CROSS_PREFIX)
            list(APPEND FFmpeg_CROSS_COMPILE_FLAGS --cross-prefix="${FFmpeg_CROSS_PREFIX}")
        else()
            message(FATAL_ERROR "Please set FFmpeg_CROSS_PREFIX to your cross toolchain prefix, for example: \${CMAKE_STAGING_PREFIX}/bin/${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_SYSTEM_NAME}-"
                "If this is not intended, check that:"
                "${CMAKE_HOST_SYSTEM_PROCESSOR} = ${CMAKE_SYSTEM_PROCESSOR}"
                "${CMAKE_HOST_SYSTEM_NAME} = ${CMAKE_SYSTEM_NAME}")
        endif()
        set(FFmpeg_IS_CROSS_COMPILING TRUE)
    endif()
endif()

if (OPENORBIS OR MANAGARM OR EMSCRIPTEN)
    # Doesn't support VA-API, don't go thru the embarrassment of trying to enable it
    list(APPEND FFmpeg_HWACCEL_FLAGS
        --disable-vaapi
        --disnable-cuvid
        --disnable-ffnvcodec
        --disnable-nvdec
        --disable-vulkan
        --disable-libdrm)
elseif (UNIX)
    find_package(PkgConfig REQUIRED)

    # X11 is "optional", some distros may opt for a super-fully wayland install...
    find_package(X11)
    if(X11_FOUND)
        # Include X11 if possible, some APIs such as VDPAU heavily depend
        # upon it's existence!
        list(APPEND FFmpeg_HWACCEL_LIBRARIES ${X11_LIBRARIES})
        list(APPEND FFmpeg_HWACCEL_INCLUDE_DIRS ${X11_INCLUDE_DIRS})
        list(APPEND FFmpeg_HWACCEL_LDFLAGS ${X11_LDFLAGS})
        if (NOT APPLE)
            # In Solaris needs explicit linking for ffmpeg which links to /lib/amd64/libX11.so
            if(SOLARIS)
                list(APPEND FFmpeg_HWACCEL_LIBRARIES
                    X11
                    "${CMAKE_SYSROOT}/usr/lib/xorg/amd64/libdrm.so")
            else()
                pkg_check_modules(LIBDRM libdrm REQUIRED)
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

            # Logically, they can only exist if libva itself exists
            pkg_check_modules(LIBVA-DRM libva-drm REQUIRED)
            pkg_check_modules(LIBVA-X11 libva-x11 REQUIRED)
            if (LIBVA-DRM_FOUND)
                list(APPEND FFmpeg_HWACCEL_LIBRARIES ${LIBVA-DRM_LIBRARIES})
                list(APPEND FFmpeg_HWACCEL_INCLUDE_DIRS ${LIBVA-DRM_INCLUDE_DIRS})
                list(APPEND FFmpeg_HWACCEL_LDFLAGS ${LIBVA-DRM_LDFLAGS})
            endif()
            if (LIBVA-X11_FOUND)
                list(APPEND FFmpeg_HWACCEL_LIBRARIES ${LIBVA-X11_LIBRARIES})
                list(APPEND FFmpeg_HWACCEL_INCLUDE_DIRS ${LIBVA-X11_INCLUDE_DIRS})
                list(APPEND FFmpeg_HWACCEL_LDFLAGS ${LIBVA-X11_LDFLAGS})
            endif()

            list(APPEND FFmpeg_HWACCEL_FLAGS
                --enable-hwaccel=h264_vaapi
                --enable-hwaccel=vp8_vaapi
                --enable-hwaccel=vp9_vaapi)
            message(STATUS "ffmpeg: va-api libraries version ${LIBVA_VERSION} found")
        else()
            list(APPEND FFmpeg_HWACCEL_FLAGS --disable-vaapi)
            message(WARNING "ffmpeg: libva-dev not found, disabling Video Acceleration API (VA-API)...")
        endif()
    else()
        list(APPEND FFmpeg_HWACCEL_FLAGS --disable-vaapi)
        message(WARNING "ffmpeg: X11 libraries not found, disabling VA-API...")
    endif()

    pkg_check_modules(CUDA cuda)
    pkg_check_modules(FFNVCODEC ffnvcodec)
    pkg_check_modules(VDPAU vdpau)
    pkg_check_modules(VULKAN vulkan)
    find_package(spirv-headers)

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
        message(STATUS "ffmpeg: ffnvcodec libraries version ${FFNVCODEC_VERSION} found")
        # ffnvenc could load CUDA libraries at the runtime using dlopen/dlsym or LoadLibrary/GetProcAddress
        # here we handle the hard-linking scenario where CUDA is linked during compilation
        if (CUDA_FOUND)
            # This line causes build error if CUDA_INCLUDE_DIRS is anything but a single non-empty value
            #list(APPEND FFmpeg_HWACCEL_FLAGS --extra-cflags=-I${CUDA_INCLUDE_DIRS})
            list(APPEND FFmpeg_HWACCEL_LIBRARIES ${CUDA_LIBRARIES})
            list(APPEND FFmpeg_HWACCEL_INCLUDE_DIRS ${CUDA_INCLUDE_DIRS})
            list(APPEND FFmpeg_HWACCEL_LDFLAGS ${CUDA_LDFLAGS})
            message(STATUS "ffmpeg: CUDA libraries version ${CUDA_VERSION} found, hard-linking will be performed")
        endif(CUDA_FOUND)
    endif()

    if (VDPAU_FOUND AND NOT APPLE)
        list(APPEND FFmpeg_HWACCEL_FLAGS
            --enable-vdpau
            --enable-hwaccel=h264_vdpau
            --enable-hwaccel=vp9_vdpau)
        list(APPEND FFmpeg_HWACCEL_LIBRARIES ${VDPAU_LIBRARIES})
        list(APPEND FFmpeg_HWACCEL_INCLUDE_DIRS ${VDPAU_INCLUDE_DIRS})
        list(APPEND FFmpeg_HWACCEL_LDFLAGS ${VDPAU_LDFLAGS})
        message(STATUS "ffmpeg: vdpau libraries version ${VDPAU_VERSION} found")
    else()
        list(APPEND FFmpeg_HWACCEL_FLAGS --disable-vdpau)
        message(WARNING "ffmpeg: libvdpau-dev not found, disabling Video Decode and Presentation API for Unix (VDPAU)...")
    endif()

    if (VULKAN_FOUND AND NOT APPLE)
        list(APPEND FFmpeg_HWACCEL_FLAGS
            --enable-vulkan
            --enable-hwaccel=h264_vulkan
            --enable-hwaccel=vp9_vulkan)
        list(APPEND FFmpeg_HWACCEL_LIBRARIES ${VULKAN_LIBRARIES})
        list(APPEND FFmpeg_HWACCEL_INCLUDE_DIRS ${VULKAN_INCLUDE_DIRS})
        list(APPEND FFmpeg_HWACCEL_LDFLAGS ${VULKAN_LDFLAGS})
        message(STATUS "ffmpeg: vulkan libraries version ${VULKAN_VERSION} found")
    endif()

    if (SPIRV_HEADERS_FOUND)
        list(APPEND FFmpeg_HWACCEL_LIBRARIES ${SPIRV_HEADERS_LIBRARIES})
        list(APPEND FFmpeg_HWACCEL_INCLUDE_DIRS ${SPIRV_HEADERS_INCLUDE_DIRS})
        list(APPEND FFmpeg_HWACCEL_LDFLAGS ${SPIRV_HEADERS_LDFLAGS})
        message(STATUS "ffmpeg: spirv-headers libraries version ${SPIRV_HEADERS_VERSION} found")
    endif()
endif()

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
        PARENT_SCOPE
    )

    set(FFmpeg_PATH
        "${FFmpeg_SOURCE_DIR}"
        PARENT_SCOPE
    )

    set(FFmpeg_LIBRARY_DIR
        "${FFmpeg_SOURCE_DIR}/bin"
        PARENT_SCOPE
    )

    set(FFmpeg_LIBRARIES
        FFmpeg::FFmpeg
        ${FFmpeg_HWACCEL_LIBRARIES}
        PARENT_SCOPE
    )
else()
    # Build FFmpeg from externals
    message(STATUS "Using FFmpeg from externals")

    if (CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|amd64)")
        # FFmpeg has source that requires one of nasm or yasm to assemble it.
        # REQUIRED throws an error if not found here during configuration rather than during compilation.
        find_program(ASSEMBLER NAMES nasm yasm)
        if ("${ASSEMBLER}" STREQUAL "ASSEMBLER-NOTFOUND")
            message(FATAL_ERROR "One of either `nasm` or `yasm` not found but is required.")
        endif()
    endif()

    find_program(AUTOCONF autoconf)
    if ("${AUTOCONF}" STREQUAL "AUTOCONF-NOTFOUND")
        message(FATAL_ERROR "Required program `autoconf` not found.")
    endif()

    AddJsonPackage(ffmpeg)

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
                ${FFmpeg_HWACCEL_FLAGS}
                ${FFmpeg_CROSS_COMPILE_FLAGS}
        WORKING_DIRECTORY
            ${FFmpeg_BUILD_DIR}
    )
    unset(FFmpeg_CC)
    unset(FFmpeg_CXX)
    unset(FFmpeg_LD)
    unset(FFmpeg_HWACCEL_FLAGS)
    unset(FFmpeg_CROSS_COMPILE_FLAGS)

    # Workaround for Ubuntu 18.04's older version of make not being able to call make as a child
    # with context of the jobserver. Also helps ninja users.
    cmake_host_system_information(RESULT SYSTEM_THREADS QUERY NUMBER_OF_LOGICAL_CORES)

    set(FFmpeg_BUILD_LIBRARIES ${FFmpeg_LIBRARIES})

    # BSD make or Solaris make don't support ffmpeg make-j8
    if (DEFINED SYSTEM_THREADS AND (LINUX OR ANDROID OR APPLE OR WIN32))
        set(FFmpeg_MAKE_ARGS -j${SYSTEM_THREADS})
    else()
        set(FFmpeg_MAKE_ARGS "")
    endif()
    find_program(MAKE gmake)
    if (NOT MAKE_FOUND)
        # Arch famously names 'gmake' as 'make'
        find_program(MAKE make REQUIRED)
    endif()

    add_custom_command(
        OUTPUT
            ${FFmpeg_BUILD_LIBRARIES}
        COMMAND
            ${MAKE} ${FFmpeg_MAKE_ARGS}
        WORKING_DIRECTORY
            ${FFmpeg_BUILD_DIR}
    )

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

    if (FFmpeg_FOUND)
        message(STATUS "Found FFmpeg version ${FFmpeg_VERSION}")
    else()
        message(FATAL_ERROR "FFmpeg not found")
    endif()
endif()

unset(FFmpeg_COMPONENTS)

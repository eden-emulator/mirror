# SPDX-License-Identifier: GPL-3.0-or-later

include(FetchContent)

set(LIBGAV1_ENABLE_TESTS OFF)
set(LIBGAV1_ENABLE_EXAMPLES OFF)
set(LIBGAV1_THREADPOOL_USE_STD_MUTEX 1)
FetchContent_Declare(libgav1
    GIT_REPOSITORY https://chromium.googlesource.com/codecs/libgav1
    GIT_TAG c1deec657b32b911920c78e078cfd089faa77200
)
FetchContent_MakeAvailable(libgav1)

set_property(GLOBAL APPEND PROPERTY CPM_PACKAGE_NAMES libgav1)
set_property(GLOBAL APPEND PROPERTY CPM_PACKAGE_URLS https://chromium.googlesource.com/codecs/libgav1)

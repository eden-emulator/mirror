// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// TODO: httplib port can't use OpenSSL because of package misconfigs on OO toolchain
// right now the solution is to tell httplib that OpenSSL doesn't exist, but that's bad
// and the issue is a bit more of how OpenSSL lacks some essential functionality
// which is a bit annoying to reconfigure atm.
#ifndef __OPENORBIS__
#define CPPHTTPLIB_DISABLE_MACOSX_AUTOMATIC_ROOT_CERTIFICATES 1
#define CPPHTTPLIB_OPENSSL_SUPPORT 1
#endif

#ifdef __GNUC__
#pragma GCC diagnostic push
#ifndef __clang__
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#endif
#include <httplib.h>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

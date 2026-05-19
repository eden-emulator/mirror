// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// You must ensure this matches with src/common/x64/xbyak.h on root dir
#include "common/container/unordered_map.h"
#include "common/container/unordered_set.h"
#include <boost/unordered_map.hpp>
#define XBYAK_NO_EXCEPTION 1
#define XBYAK_STD_UNORDERED_SET ::Common::unordered_set
#define XBYAK_STD_UNORDERED_MAP ::Common::unordered_map
#define XBYAK_STD_UNORDERED_MULTIMAP boost::unordered_multimap
#include <xbyak/xbyak.h>
#include <xbyak/xbyak_util.h>

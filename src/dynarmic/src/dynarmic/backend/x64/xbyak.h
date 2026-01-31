// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#define XBYAK_STD_UNORDERED_SET ankerl::unordered_dense::set
#define XBYAK_STD_UNORDERED_MAP ankerl::unordered_dense::map
#define XBYAK_STD_UNORDERED_MULTIMAP std::unordered_multimap

#include <ankerl/unordered_dense.h>
#include <unordered_map>

#include <xbyak/xbyak.h>
#include <xbyak/xbyak_util.h>

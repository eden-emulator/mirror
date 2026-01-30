#!/bin/sh

# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
$CC data2code.c -o data2code && ./data2code data.txt >powah_gen_base.hpp
$CC --target=powerpc64le test.S -c -o test && objdump -SC test

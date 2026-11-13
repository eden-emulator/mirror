// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import java.util.concurrent.atomic.AtomicBoolean

object GameFolderOperations {
    private val gameFoldersChanged = AtomicBoolean(false)

    fun markGameFoldersChanged() {
        gameFoldersChanged.set(true)
    }

    fun consumeGameFoldersChanged(): Boolean = gameFoldersChanged.getAndSet(false)
}

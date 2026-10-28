// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import android.content.Context
import java.io.File

object InstallCacheHelper {
    /** Staging directory for firmware zip extraction (must not use the name `registered`). */
    const val FIRMWARE_STAGING_DIR = "firmware_staging"

    /**
     * Removes transient install artifacts from [Context.getCacheDir].
     * Leftovers (especially a `registered/` folder) can survive if install is interrupted.
     */
    fun cleanTransientInstallCache(context: Context) {
        val cacheDir = context.cacheDir ?: return
        listOf(
            "registered",
            FIRMWARE_STAGING_DIR,
            "saves",
        ).forEach { name ->
            File(cacheDir, name).takeIf { it.exists() }?.deleteRecursively()
        }
    }
}

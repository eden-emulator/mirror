// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import android.app.Activity
import android.content.Context
import android.content.Intent
import org.yuzu.yuzu_emu.features.settings.model.BooleanSetting

object LegacyUi {
    fun isEnabled(): Boolean = BooleanSetting.LEGACY_YUZU_UI.getBoolean()

    /** Restart the task so MainActivity picks up legacy vs Eden shell. Do not kill the process. */
    fun relaunchApp(context: Context) {
        val appContext = context.applicationContext
        val intent = appContext.packageManager.getLaunchIntentForPackage(appContext.packageName)
            ?: return
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
        appContext.startActivity(intent)
        if (context is Activity) {
            context.finishAffinity()
        }
    }
}

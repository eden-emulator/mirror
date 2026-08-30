// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.features.settings.model

import org.yuzu.yuzu_emu.utils.NativeConfig

enum class UShortSetting(override val key: String) : AbstractIntSetting {
    DEBUG_KNOBS("debug_knobs")
    ;

    override fun getInt(needsGlobal: Boolean): Int =
        NativeConfig.getUnsignedShort(key, needsGlobal)

    override fun setInt(value: Int) {
        if (NativeConfig.isPerGameConfigLoaded()) {
            global = false
        }
        NativeConfig.setUnsignedShort(key, value)
    }

    override val defaultValue: Int by lazy { NativeConfig.getDefaultToString(key).toInt() }

    override fun getValueAsString(needsGlobal: Boolean): String = getInt(needsGlobal).toString()

    override fun reset() = NativeConfig.setUnsignedShort(key, defaultValue)
}

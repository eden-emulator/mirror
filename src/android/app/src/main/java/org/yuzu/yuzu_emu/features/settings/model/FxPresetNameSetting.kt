// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.features.settings.model

class FxPresetNameSetting(private val onNamed: (String) -> Unit) : AbstractStringSetting {
    override val key: String
        get() = "fx_preset_name"

    override val defaultValue: Any
        get() = ""

    override val isRuntimeModifiable: Boolean
        get() = true

    override val pairedSettingKey: String
        get() = ""

    override val isSwitchable: Boolean
        get() = false

    override val isSaveable: Boolean
        get() = true

    override var global: Boolean
        get() = true
        set(_) {}

    override fun getString(needsGlobal: Boolean): String = ""

    override fun setString(value: String) = onNamed(value)

    override fun getValueAsString(needsGlobal: Boolean): String = ""

    override fun reset() {}
}

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.features.settings.model.view

import androidx.annotation.StringRes

class FxToolbarSetting(
    @StringRes val addLabelId: Int,
    val listOpen: Boolean,
    val presetLabel: String,
    val hasEffects: Boolean,
    val createPreset: StringInputSetting,
    val onAdd: () -> Unit,
    val onPresets: () -> Unit,
    val onRemoveAll: () -> Unit
) : SettingsItem(emptySetting, 0, "", 0, "") {
    override val type = TYPE_FX_TOOLBAR
}

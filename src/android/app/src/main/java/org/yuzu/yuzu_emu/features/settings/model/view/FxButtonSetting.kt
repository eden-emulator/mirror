// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.features.settings.model.view

import androidx.annotation.StringRes

class FxButtonSetting(
    @StringRes titleId: Int,
    val onClick: () -> Unit
) : SettingsItem(emptySetting, titleId, "", 0, "") {
    override val type = TYPE_FX_BUTTON
}

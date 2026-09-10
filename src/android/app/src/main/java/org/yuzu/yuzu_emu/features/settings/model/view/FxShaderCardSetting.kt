// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.features.settings.model.view

import org.yuzu.yuzu_emu.utils.NativePostProcessing

class FxShaderCardSetting(
    titleString: String,
    descriptionString: String,
    val index: Int,
    val expanded: Boolean,
    val uniforms: List<NativePostProcessing.Uniform>,
    val onToggle: () -> Unit,
    val onRemove: () -> Unit,
    val onReset: () -> Unit
) : SettingsItem(emptySetting, 0, titleString, 0, descriptionString) {
    override val type = TYPE_FX_SHADER
}

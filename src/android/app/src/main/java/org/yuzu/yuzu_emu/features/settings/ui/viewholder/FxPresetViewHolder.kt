// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.features.settings.ui.viewholder

import android.view.View
import org.yuzu.yuzu_emu.databinding.ListItemSettingFxPresetBinding
import org.yuzu.yuzu_emu.features.settings.model.view.FxPresetSetting
import org.yuzu.yuzu_emu.features.settings.model.view.SettingsItem
import org.yuzu.yuzu_emu.features.settings.ui.SettingsAdapter
import org.yuzu.yuzu_emu.utils.ViewUtils.setVisible

class FxPresetViewHolder(
    val binding: ListItemSettingFxPresetBinding,
    adapter: SettingsAdapter
) : SettingViewHolder(binding.root, adapter) {
    private lateinit var setting: FxPresetSetting

    override fun bind(item: SettingsItem) {
        setting = item as FxPresetSetting

        binding.presetName.text = item.title
        binding.presetDescription.text = item.description
        binding.presetDescription.setVisible(item.description.isNotEmpty())

        binding.presetRow.setOnClickListener { setting.onApply.invoke() }

        binding.presetDelete.setVisible(setting.deletable)
        binding.presetDelete.setOnClickListener { setting.onDelete.invoke() }
    }

    override fun onClick(clicked: View) {}

    override fun onLongClick(clicked: View): Boolean = true
}

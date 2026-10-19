// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.yuzu.yuzu_emu.features.settings.ui.viewholder

import android.view.View
import androidx.core.content.res.ResourcesCompat
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.databinding.ListItemSettingBinding
import org.yuzu.yuzu_emu.features.settings.model.view.SettingsItem
import org.yuzu.yuzu_emu.features.settings.model.view.SubmenuSetting
import org.yuzu.yuzu_emu.features.settings.ui.SettingsAdapter
import org.yuzu.yuzu_emu.utils.ViewUtils.setVisible

class SubmenuViewHolder(val binding: ListItemSettingBinding, adapter: SettingsAdapter) :
    SettingViewHolder(binding.root, adapter) {
    private lateinit var setting: SubmenuSetting

    override fun bind(item: SettingsItem) {
        setting = item as SubmenuSetting
        binding.icon.setVisible(setting.iconId != 0)
        if (setting.iconId != 0) {
            binding.icon.setImageDrawable(
                ResourcesCompat.getDrawable(
                    binding.icon.resources,
                    setting.iconId,
                    binding.icon.context.theme
                )
            )
        }

        binding.textSettingName.text = setting.title
        binding.textSettingDescription.setVisible(setting.description.isNotEmpty())
        binding.textSettingDescription.text = setting.description
        if (setting.showChevron) {
            binding.textSettingValue.setVisible(true)
            binding.textSettingValue.text = ""
            binding.textSettingValue.setCompoundDrawablesRelativeWithIntrinsicBounds(
                0,
                0,
                R.drawable.ic_arrow_forward,
                0
            )
        } else {
            binding.textSettingValue.setVisible(false)
            binding.textSettingValue.setCompoundDrawablesRelativeWithIntrinsicBounds(0, 0, 0, 0)
        }
        binding.buttonClear.setVisible(false)
    }

    override fun onClick(clicked: View) {
        adapter.onSubmenuClick(setting)
    }

    override fun onLongClick(clicked: View): Boolean {
        // no-op
        return true
    }
}

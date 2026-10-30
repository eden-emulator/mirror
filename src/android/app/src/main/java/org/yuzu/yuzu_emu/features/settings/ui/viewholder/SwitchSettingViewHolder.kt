// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.yuzu.yuzu_emu.features.settings.ui.viewholder

import android.view.View
import android.widget.CompoundButton
import android.widget.TextView
import com.google.android.material.button.MaterialButton
import com.google.android.material.materialswitch.MaterialSwitch
import org.yuzu.yuzu_emu.databinding.ListItemSettingSwitchBinding
import org.yuzu.yuzu_emu.features.settings.model.view.SettingsItem
import org.yuzu.yuzu_emu.features.settings.model.view.SwitchSetting
import org.yuzu.yuzu_emu.features.settings.ui.SettingsAdapter
import org.yuzu.yuzu_emu.utils.ViewUtils.setVisible

class SwitchSettingViewHolder private constructor(
    private val textSettingName: TextView,
    private val textSettingDescription: TextView,
    private val switchWidget: MaterialSwitch,
    private val buttonClear: MaterialButton,
    adapter: SettingsAdapter,
    itemView: View
) : SettingViewHolder(itemView, adapter) {

    constructor(binding: ListItemSettingSwitchBinding, adapter: SettingsAdapter) : this(
        binding.textSettingName,
        binding.textSettingDescription,
        binding.switchWidget,
        binding.buttonClear,
        adapter,
        binding.root
    )

    private lateinit var setting: SwitchSetting

    override fun bind(item: SettingsItem) {
        setting = item as SwitchSetting
        textSettingName.text = setting.title
        textSettingDescription.setVisible(setting.description.isNotEmpty())
        textSettingDescription.text = setting.description

        switchWidget.setOnCheckedChangeListener(null)
        switchWidget.isChecked = setting.getIsChecked(setting.needsRuntimeGlobal)
        switchWidget.setOnCheckedChangeListener { _: CompoundButton, _: Boolean ->
            adapter.onBooleanClick(setting, switchWidget.isChecked, bindingAdapterPosition)
        }

        buttonClear.setVisible(setting.clearable)
        buttonClear.setOnClickListener {
            adapter.onClearClick(setting, bindingAdapterPosition)
        }

        setStyle(
            setting.isEditable,
            textSettingName,
            textSettingDescription,
            switchWidget,
            buttonClear
        )
    }

    override fun onClick(clicked: View) {
        if (setting.isEditable) {
            switchWidget.toggle()
        }
    }

    override fun onLongClick(clicked: View): Boolean {
        if (setting.isEditable) {
            return adapter.onLongClick(setting, bindingAdapterPosition)
        }
        return false
    }
}

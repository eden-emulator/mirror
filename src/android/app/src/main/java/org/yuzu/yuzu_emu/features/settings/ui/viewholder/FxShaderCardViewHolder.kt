// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.features.settings.ui.viewholder

import android.view.LayoutInflater
import android.view.View
import org.yuzu.yuzu_emu.databinding.ItemSettingFxActionsBinding
import org.yuzu.yuzu_emu.databinding.ItemSettingFxSliderBinding
import org.yuzu.yuzu_emu.databinding.ListItemSettingFxShaderBinding
import org.yuzu.yuzu_emu.features.settings.model.FxUniformSliderSetting
import org.yuzu.yuzu_emu.features.settings.model.view.FxShaderCardSetting
import org.yuzu.yuzu_emu.features.settings.model.view.SettingsItem
import org.yuzu.yuzu_emu.features.settings.ui.SettingsAdapter
import org.yuzu.yuzu_emu.utils.NativePostProcessing
import org.yuzu.yuzu_emu.utils.ViewUtils.setVisible

class FxShaderCardViewHolder(
    val binding: ListItemSettingFxShaderBinding,
    adapter: SettingsAdapter
) : SettingViewHolder(binding.root, adapter) {
    private lateinit var setting: FxShaderCardSetting

    override fun bind(item: SettingsItem) {
        setting = item as FxShaderCardSetting

        binding.shaderTitle.text = item.title
        binding.shaderSummary.text = item.description
        binding.shaderSummary.setVisible(item.description.isNotEmpty())

        var rotation = 0f
        if (setting.expanded) {
            rotation = 180f
        }
        binding.shaderExpand.rotation = rotation

        binding.shaderHeader.setOnClickListener { setting.onToggle.invoke() }
        binding.shaderRemove.setOnClickListener { setting.onRemove.invoke() }

        binding.shaderBody.removeAllViews()
        binding.shaderBody.setVisible(setting.expanded)
        if (!setting.expanded) {
            return
        }

        val inflater = LayoutInflater.from(binding.root.context)
        for (uniform in setting.uniforms) {
            if (uniform.uiType == NativePostProcessing.UI_HIDDEN) {
                continue
            }
            for (component in 0 until uniform.components) {
                addSlider(inflater, uniform, component)
            }
        }
        addActions(inflater)
    }

    private fun addSlider(
        inflater: LayoutInflater,
        uniform: NativePostProcessing.Uniform,
        component: Int
    ) {
        val row = ItemSettingFxSliderBinding.inflate(inflater, binding.shaderBody, false)
        val value = FxUniformSliderSetting(setting.index, uniform, component)

        var title = uniform.label
        if (uniform.components > 1) {
            title = uniform.label + " [" + component + "]"
        }
        row.fxSliderTitle.text = title

        val steps = uniform.steps.toFloat()
        row.fxSlider.valueFrom = 0f
        row.fxSlider.valueTo = steps
        row.fxSlider.stepSize = 1f
        row.fxSlider.value = value.getInt(false).toFloat().coerceIn(0f, steps)
        row.fxSliderValue.text = uniform.describe(row.fxSlider.value.toInt())

        row.fxSlider.addOnChangeListener { _, position, fromUser ->
            if (fromUser) {
                value.setInt(position.toInt())
                row.fxSliderValue.text = uniform.describe(position.toInt())
            }
        }

        binding.shaderBody.addView(row.root)
    }

    private fun addActions(inflater: LayoutInflater) {
        val row = ItemSettingFxActionsBinding.inflate(inflater, binding.shaderBody, false)

        row.fxReset.setOnClickListener { setting.onReset.invoke() }

        binding.shaderBody.addView(row.root)
    }

    override fun onClick(clicked: View) {}

    override fun onLongClick(clicked: View): Boolean = true
}

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.dialogs

import android.content.res.ColorStateList
import android.graphics.Rect
import android.view.LayoutInflater
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.RadioGroup
import android.widget.TextView
import androidx.drawerlayout.widget.DrawerLayout
import com.google.android.material.color.MaterialColors
import com.google.android.material.materialswitch.MaterialSwitch
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.YuzuApplication
import org.yuzu.yuzu_emu.features.settings.model.BooleanSetting
import org.yuzu.yuzu_emu.features.settings.model.IntSetting
import org.yuzu.yuzu_emu.fragments.EmulationFragment
import org.yuzu.yuzu_emu.utils.NativeConfig
import org.yuzu.yuzu_emu.utils.NativePostProcessing
import org.yuzu.yuzu_emu.features.settings.model.AbstractSetting
import org.yuzu.yuzu_emu.features.settings.model.AbstractShortSetting
import org.yuzu.yuzu_emu.features.settings.model.AbstractIntSetting

class QuickSettings(val emulationFragment: EmulationFragment) {
    private val expandedShaders = mutableSetOf<Int>()

    private fun forgetShaderSlot(index: Int) {
        val shifted = mutableSetOf<Int>()
        for (slot in expandedShaders) {
            if (slot < index) {
                shifted.add(slot)
            }
            if (slot > index) {
                shifted.add(slot - 1)
            }
        }
        expandedShaders.clear()
        expandedShaders.addAll(shifted)
    }

    private fun saveSettings() {
        if (emulationFragment.shouldUseCustom) {
            NativeConfig.savePerGameConfig()
        } else {
            NativeConfig.saveGlobalConfig()
        }
    }

    fun addPerGameConfigStatusIndicator(container: ViewGroup) {
        val inflater = LayoutInflater.from(emulationFragment.requireContext())
        val statusView = inflater.inflate(R.layout.item_quick_settings_status, container, false)

        val statusIcon = statusView.findViewById<android.widget.ImageView>(R.id.status_icon)
        val statusText = statusView.findViewById<TextView>(R.id.status_text)

        statusIcon.setImageResource(R.drawable.ic_settings_outline)
        statusText.text = emulationFragment.getString(R.string.using_per_game_config)
        statusText.setTextColor(
            MaterialColors.getColor(
                statusText,
                com.google.android.material.R.attr.colorPrimary
            )
        )

        container.addView(statusView)
    }

    // settings

    fun addIntSetting(
        name: Int,
        container: ViewGroup,
        setting: IntSetting,
        namesArrayId: Int,
        valuesArrayId: Int,
        onValueChanged: ((Int) -> Unit)? = null
    ) {
        val inflater = LayoutInflater.from(emulationFragment.requireContext())
        val itemView = inflater.inflate(R.layout.item_quick_settings_menu, container, false)
        val headerView = itemView.findViewById<ViewGroup>(R.id.setting_header)
        val titleView = itemView.findViewById<TextView>(R.id.setting_title)
        val valueView = itemView.findViewById<TextView>(R.id.setting_value)
        val expandIcon = itemView.findViewById<android.widget.ImageView>(R.id.expand_icon)
        val radioGroup = itemView.findViewById<RadioGroup>(R.id.radio_group)

        titleView.text = YuzuApplication.appContext.getString(name)

        val names = emulationFragment.resources.getStringArray(namesArrayId)
        val values = emulationFragment.resources.getIntArray(valuesArrayId)
        val currentIndex = values.indexOf(setting.getInt())

        valueView.text = if (currentIndex >= 0) names[currentIndex] else "Null"
        headerView.visibility = View.VISIBLE

        var isExpanded = false
        names.forEachIndexed { index, name ->
            val radioButton = com.google.android.material.radiobutton.MaterialRadioButton(emulationFragment.requireContext())
            radioButton.text = name
            radioButton.id = View.generateViewId()
            radioButton.isChecked = index == currentIndex
            radioButton.setPadding(16, 8, 16, 8)

            radioButton.setOnCheckedChangeListener { _, isChecked ->
                if (isChecked) {
                    setting.setInt(values[index])
                    saveSettings()
                    valueView.text = name
                    onValueChanged?.invoke(values[index])
                }
            }
            radioGroup.addView(radioButton)
        }

        headerView.setOnClickListener {
            isExpanded = !isExpanded
            if (isExpanded) {
                radioGroup.visibility = View.VISIBLE
                expandIcon.animate().rotation(180f).setDuration(200).start()
            } else {
                radioGroup.visibility = View.GONE
                expandIcon.animate().rotation(0f).setDuration(200).start()
            }
        }

        container.addView(itemView)
    }

    fun addBooleanSetting(
        name: Int,

        container: ViewGroup,
        setting: BooleanSetting
    ) {
        val inflater = LayoutInflater.from(emulationFragment.requireContext())
        val itemView = inflater.inflate(R.layout.item_quick_settings_menu, container, false)

        val switchContainer = itemView.findViewById<ViewGroup>(R.id.switch_container)
        val titleView = itemView.findViewById<TextView>(R.id.switch_title)
        val switchView = itemView.findViewById<com.google.android.material.materialswitch.MaterialSwitch>(R.id.setting_switch)

        titleView.text = YuzuApplication.appContext.getString(name)
        switchContainer.visibility = View.VISIBLE
        switchView.isChecked = setting.getBoolean()

        switchView.setOnCheckedChangeListener { _, isChecked ->
            setting.setBoolean(isChecked)
            saveSettings()
        }

        switchContainer.setOnClickListener {
            switchView.toggle()
        }
        container.addView(itemView)
    }

    fun addCustomToggle(
        name: Int,
        isChecked: Boolean,
        isEnabled: Boolean,

        container: ViewGroup,
        callback: (Boolean) -> Unit
    ): MaterialSwitch? {
        val inflater = LayoutInflater.from(emulationFragment.requireContext())
        val itemView = inflater.inflate(R.layout.item_quick_settings_menu, container, false)

        val switchContainer = itemView.findViewById<ViewGroup>(R.id.switch_container)
        val titleView = itemView.findViewById<TextView>(R.id.switch_title)
        val switchView = itemView.findViewById<MaterialSwitch>(R.id.setting_switch)

        titleView.text = YuzuApplication.appContext.getString(name)
        switchContainer.visibility = View.VISIBLE

        switchView.isChecked = isChecked

        switchView.setOnCheckedChangeListener { _, checked ->
            callback(checked)
            saveSettings()
        }

        switchContainer.setOnClickListener {
            switchView.toggle()
        }
        container.addView(itemView)

        return switchView
    }

    fun addSliderSetting(
        name: Int,
        container: ViewGroup,
        setting: AbstractSetting,
        minValue: Int = 0,
        maxValue: Int = 100,
        units: String = ""
    ) {
        val inflater = LayoutInflater.from(emulationFragment.requireContext())
        val itemView = inflater.inflate(R.layout.item_quick_settings_menu, container, false)

        val sliderContainer = itemView.findViewById<ViewGroup>(R.id.slider_container)
        val titleView = itemView.findViewById<TextView>(R.id.slider_title)
        val valueDisplay = itemView.findViewById<TextView>(R.id.slider_value_display)
        val slider = itemView.findViewById<com.google.android.material.slider.Slider>(R.id.setting_slider)


        titleView.text = YuzuApplication.appContext.getString(name)
        sliderContainer.visibility = View.VISIBLE

        slider.valueFrom = minValue.toFloat()
        slider.valueTo = maxValue.toFloat()
        slider.stepSize = 1f
        val currentValue = when (setting) {
            is AbstractShortSetting -> setting.getShort(needsGlobal = false).toInt()
            is AbstractIntSetting -> setting.getInt(needsGlobal = false)
            else -> 0
        }
        slider.value = currentValue.toFloat().coerceIn(minValue.toFloat(), maxValue.toFloat())

        val displayValue = "${slider.value.toInt()}$units"
        valueDisplay.text = displayValue

        slider.addOnChangeListener { _, value, chanhed ->
            if (chanhed) {
                val intValue = value.toInt()
                when (setting) {
                    is AbstractShortSetting -> setting.setShort(intValue.toShort())
                    is AbstractIntSetting -> setting.setInt(intValue)
                }
                saveSettings()
                valueDisplay.text = "$intValue$units"
            }
        }

        slider.setOnTouchListener { _, event ->
            val drawer = emulationFragment.view?.findViewById<DrawerLayout>(R.id.drawer_layout)
            when (event.action) {
                MotionEvent.ACTION_DOWN -> {
                    drawer?.requestDisallowInterceptTouchEvent(true)
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    drawer?.requestDisallowInterceptTouchEvent(false)
                }
            }
            false
        }

        container.addView(itemView)
    }

    fun addChoice(
        title: String,
        container: ViewGroup,
        choices: List<String>,
        selectedIndex: Int,
        onSelected: (Int) -> Unit
    ) {
        val inflater = LayoutInflater.from(emulationFragment.requireContext())
        val itemView = inflater.inflate(R.layout.item_quick_settings_menu, container, false)
        val headerView = itemView.findViewById<ViewGroup>(R.id.setting_header)
        val titleView = itemView.findViewById<TextView>(R.id.setting_title)
        val valueView = itemView.findViewById<TextView>(R.id.setting_value)
        val expandIcon = itemView.findViewById<android.widget.ImageView>(R.id.expand_icon)
        val radioGroup = itemView.findViewById<RadioGroup>(R.id.radio_group)

        titleView.text = title

        var current = ""
        if (selectedIndex in choices.indices) {
            current = choices[selectedIndex]
        }
        valueView.text = current
        headerView.visibility = View.VISIBLE

        var isExpanded = false
        choices.forEachIndexed { index, name ->
            val radioButton = com.google.android.material.radiobutton.MaterialRadioButton(
                emulationFragment.requireContext()
            )
            radioButton.text = name
            radioButton.id = View.generateViewId()
            radioButton.isChecked = index == selectedIndex
            radioButton.setPadding(16, 8, 16, 8)

            radioButton.setOnCheckedChangeListener { _, isChecked ->
                if (isChecked) {
                    valueView.text = name
                    onSelected(index)
                }
            }
            radioGroup.addView(radioButton)
        }

        headerView.setOnClickListener {
            isExpanded = !isExpanded
            if (isExpanded) {
                radioGroup.visibility = View.VISIBLE
                expandIcon.animate().rotation(180f).setDuration(200).start()
            } else {
                radioGroup.visibility = View.GONE
                expandIcon.animate().rotation(0f).setDuration(200).start()
            }
        }

        container.addView(itemView)
    }

    fun addStepSlider(
        title: String,
        container: ViewGroup,
        steps: Int,
        selectedStep: Int,
        describe: (Int) -> String,
        onCommitted: (Int) -> Unit,
        onChanged: (Int) -> Unit
    ) {
        val inflater = LayoutInflater.from(emulationFragment.requireContext())
        val itemView = inflater.inflate(R.layout.item_quick_settings_menu, container, false)

        val sliderContainer = itemView.findViewById<ViewGroup>(R.id.slider_container)
        val titleView = itemView.findViewById<TextView>(R.id.slider_title)
        val valueDisplay = itemView.findViewById<TextView>(R.id.slider_value_display)
        val slider = itemView.findViewById<com.google.android.material.slider.Slider>(
            R.id.setting_slider
        )

        titleView.text = title
        sliderContainer.visibility = View.VISIBLE

        slider.valueFrom = 0f
        slider.valueTo = steps.toFloat()
        slider.stepSize = 1f
        slider.value = selectedStep.toFloat().coerceIn(0f, steps.toFloat())
        valueDisplay.text = describe(slider.value.toInt())

        slider.addOnChangeListener { _, value, fromUser ->
            if (fromUser) {
                val step = value.toInt()
                onChanged(step)
                valueDisplay.text = describe(step)
            }
        }

        var pressedValue = slider.value

        slider.setOnTouchListener { _, event ->
            val drawer = emulationFragment.view?.findViewById<DrawerLayout>(R.id.drawer_layout)
            when (event.action) {
                MotionEvent.ACTION_DOWN -> {
                    drawer?.requestDisallowInterceptTouchEvent(true)
                    pressedValue = slider.value
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    drawer?.requestDisallowInterceptTouchEvent(false)
                    if (slider.value != pressedValue) {
                        onCommitted(slider.value.toInt())
                    }
                }
            }
            false
        }

        container.addView(itemView)
    }

    fun addShaderCard(
        index: Int,
        title: String,
        summary: String,
        container: ViewGroup,
        onRemove: () -> Unit
    ): ViewGroup {
        val inflater = LayoutInflater.from(emulationFragment.requireContext())
        val itemView = inflater.inflate(R.layout.item_quick_settings_shader, container, false)

        val headerView = itemView.findViewById<ViewGroup>(R.id.shader_header)
        val titleView = itemView.findViewById<TextView>(R.id.shader_title)
        val summaryView = itemView.findViewById<TextView>(R.id.shader_summary)
        val removeView = itemView.findViewById<android.widget.ImageView>(R.id.shader_remove)
        val expandIcon = itemView.findViewById<android.widget.ImageView>(R.id.shader_expand)
        val bodyView = itemView.findViewById<ViewGroup>(R.id.shader_body)

        titleView.text = title
        if (summary.isEmpty()) {
            summaryView.visibility = View.GONE
        } else {
            summaryView.text = summary
        }

        var isExpanded = expandedShaders.contains(index)
        if (isExpanded) {
            bodyView.visibility = View.VISIBLE
            expandIcon.rotation = 180f
        }

        headerView.setOnClickListener {
            isExpanded = !isExpanded
            if (isExpanded) {
                expandedShaders.add(index)
                bodyView.visibility = View.VISIBLE
                expandIcon.animate().rotation(180f).setDuration(200).start()
            } else {
                expandedShaders.remove(index)
                bodyView.visibility = View.GONE
                expandIcon.animate().rotation(0f).setDuration(200).start()
            }
        }

        removeView.setOnClickListener {
            onRemove()
        }

        container.addView(itemView)
        return bodyView
    }

    fun addEffectPicker(
        container: ViewGroup,
        choices: List<String>,
        hasEffects: Boolean,
        onRemoveAll: () -> Unit,
        onPicked: (Int) -> Unit
    ) {
        val context = emulationFragment.requireContext()
        val inflater = LayoutInflater.from(context)
        val itemView = inflater.inflate(R.layout.item_quick_settings_add, container, false)

        val button = itemView.findViewById<com.google.android.material.button.MaterialButton>(
            R.id.add_button
        )
        val removeButton =
            itemView.findViewById<com.google.android.material.button.MaterialButton>(
                R.id.remove_all_button
            )
        val choiceGroup = itemView.findViewById<RadioGroup>(R.id.add_choices)

        choices.forEachIndexed { index, name ->
            val radioButton = com.google.android.material.radiobutton.MaterialRadioButton(context)
            radioButton.text = name
            radioButton.id = View.generateViewId()
            radioButton.setPadding(16, 8, 16, 8)
            radioButton.setOnCheckedChangeListener { _, isChecked ->
                if (isChecked) {
                    onPicked(index)
                }
            }
            choiceGroup.addView(radioButton)
        }

        var closedLabel = R.string.post_processing_add
        if (hasEffects) {
            closedLabel = R.string.post_processing_open_list
            removeButton.visibility = View.VISIBLE
        }
        button.setText(closedLabel)

        val removeBackground = MaterialColors.getColor(
            removeButton,
            com.google.android.material.R.attr.colorErrorContainer
        )
        val removeForeground = MaterialColors.getColor(
            removeButton,
            com.google.android.material.R.attr.colorOnErrorContainer
        )
        removeButton.backgroundTintList = ColorStateList.valueOf(removeBackground)
        removeButton.setTextColor(removeForeground)
        removeButton.iconTint = ColorStateList.valueOf(removeForeground)
        removeButton.setOnClickListener {
            onRemoveAll()
        }

        val slide = context.resources.displayMetrics.density * 24.0f

        var isOpen = false
        button.setOnClickListener {
            isOpen = !isOpen
            if (isOpen) {
                choiceGroup.alpha = 0.0f
                choiceGroup.translationY = slide
                choiceGroup.visibility = View.VISIBLE
                choiceGroup.animate()
                    .alpha(1.0f)
                    .translationY(0.0f)
                    .setDuration(220)
                    .withEndAction {
                        choiceGroup.requestRectangleOnScreen(
                            Rect(0, 0, choiceGroup.width, choiceGroup.height),
                            false
                        )
                    }
                    .start()

                button.setText(R.string.post_processing_close_list)
                button.setIconResource(R.drawable.ic_clear)
            } else {
                choiceGroup.animate()
                    .alpha(0.0f)
                    .translationY(slide)
                    .setDuration(160)
                    .withEndAction {
                        choiceGroup.visibility = View.GONE
                    }
                    .start()

                button.setText(closedLabel)
                button.setIconResource(R.drawable.ic_add)
            }
        }

        container.addView(itemView)
    }

    fun addPresetBand(container: ViewGroup, name: String, summary: String) {
        val inflater = LayoutInflater.from(emulationFragment.requireContext())
        val itemView = inflater.inflate(R.layout.item_quick_settings_preset, container, false)

        val titleView = itemView.findViewById<TextView>(R.id.preset_title)
        val summaryView = itemView.findViewById<TextView>(R.id.preset_summary)
        val switchView = itemView.findViewById<MaterialSwitch>(R.id.preset_switch)

        titleView.text = name
        if (summary.isEmpty()) {
            summaryView.visibility = View.GONE
        } else {
            summaryView.text = summary
        }

        switchView.isChecked = NativePostProcessing.isEnabled()
        switchView.setOnCheckedChangeListener { _, checked ->
            emulationFragment.editPostProcessing {
                NativePostProcessing.setEnabled(checked)
            }
        }

        container.addView(itemView)
    }

    fun addPostProcessing(container: ViewGroup, onStructureChanged: () -> Unit) {
        val usable = NativePostProcessing.catalog().filter { it.valid }
        if (usable.isEmpty()) {
            return
        }

        val preset = NativePostProcessing.getActivePreset()
        if (preset.isNotEmpty()) {
            addDivider(container)

            var summary = ""
            val described = NativePostProcessing.presets().firstOrNull { it.name == preset }
            if (described != null) {
                summary = described.description
            }
            if (summary.isEmpty()) {
                summary =
                    YuzuApplication.appContext.getString(R.string.post_processing_preset_locked)
            }
            if (NativePostProcessing.isPresetModified()) {
                summary = summary + "\n" +
                    YuzuApplication.appContext.getString(R.string.post_processing_preset_modified)
            }

            addPresetBand(container, preset, summary)
            return
        }

        val labels = mutableListOf<String>()
        val files = mutableListOf<String>()
        val techniques = mutableListOf<String>()

        for (effect in usable) {
            for (technique in effect.techniques) {
                var label = effect.label
                if (effect.techniques.size > 1) {
                    label = effect.label + " \u00b7 " + technique
                }
                labels.add(label)
                files.add(effect.file)
                techniques.add(technique)
            }
        }

        addDivider(container)

        val chain = NativePostProcessing.chain()
        chain.forEachIndexed { index, entry ->
            val effect = usable.firstOrNull { it.file == entry.file }

            var title = entry.file
            var summary = ""
            if (effect != null) {
                title = effect.label
                if (effect.techniques.size > 1) {
                    title = effect.label + " \u00b7 " + entry.technique
                }
                summary = effect.description
            }

            val body = addShaderCard(index, title, summary, container) {
                emulationFragment.editPostProcessing {
                    NativePostProcessing.remove(index)
                }
                forgetShaderSlot(index)
                onStructureChanged()
            }

            if (effect != null) {
                for (uniform in effect.uniforms) {
                    addUniformSliders(body, index, uniform)
                }
            }
        }

        addEffectPicker(
            container,
            labels,
            chain.isNotEmpty(),
            {
                emulationFragment.editPostProcessing {
                    NativePostProcessing.clearChain()
                }
                expandedShaders.clear()
                onStructureChanged()
            }
        ) { picked ->
            emulationFragment.editPostProcessing {
                NativePostProcessing.append(files[picked], techniques[picked])
            }
            onStructureChanged()
        }
    }

    private fun addUniformSliders(
        container: ViewGroup,
        index: Int,
        uniform: NativePostProcessing.Uniform
    ) {
        if (uniform.uiType == NativePostProcessing.UI_HIDDEN) {
            return
        }

        for (component in 0 until uniform.components) {
            var title = uniform.label
            if (uniform.components > 1) {
                title = uniform.label + " [" + component + "]"
            }

            var value = uniform.defaultAt(component)
            if (NativePostProcessing.hasValue(index, uniform.name)) {
                value = NativePostProcessing.getValue(index, uniform.name, component)
            }

            val steps = uniform.steps
            val step = Math.round((value - uniform.min) / uniform.step)

            addStepSlider(
                title,
                container,
                steps,
                step,
                { position -> uniform.describe(position) },
                { emulationFragment.persistPostProcessing() }
            ) { position ->
                NativePostProcessing.setValue(
                    index,
                    uniform.name,
                    component,
                    uniform.min + position * uniform.step
                )
            }
        }
    }

    fun addDivider(container: ViewGroup) {
        val inflater = LayoutInflater.from(emulationFragment.requireContext())
        val dividerView = inflater.inflate(R.layout.item_quick_settings_divider, container, false)
        container.addView(dividerView)
    }
}
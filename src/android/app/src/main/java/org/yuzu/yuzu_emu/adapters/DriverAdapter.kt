// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.yuzu.yuzu_emu.adapters

import android.view.LayoutInflater
import android.view.ViewGroup
import org.yuzu.yuzu_emu.R
import androidx.viewbinding.ViewBinding
import org.yuzu.yuzu_emu.databinding.CardDriverOptionBinding
import org.yuzu.yuzu_emu.databinding.CardDriverOptionLegacyBinding
import org.yuzu.yuzu_emu.utils.LegacyUi
import android.view.View
import android.widget.RadioButton
import android.widget.TextView
import org.yuzu.yuzu_emu.features.settings.model.StringSetting
import org.yuzu.yuzu_emu.model.Driver
import org.yuzu.yuzu_emu.model.DriverViewModel
import org.yuzu.yuzu_emu.utils.ViewUtils.marquee
import org.yuzu.yuzu_emu.utils.ViewUtils.setVisible
import org.yuzu.yuzu_emu.viewholder.AbstractViewHolder

class DriverAdapter(private val driverViewModel: DriverViewModel) :
    AbstractSingleSelectionList<Driver, DriverAdapter.DriverViewHolder>(
        driverViewModel.driverList.value
    ) {
    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): DriverViewHolder {
        val inflater = LayoutInflater.from(parent.context)
        val binding: ViewBinding = if (LegacyUi.isEnabled()) {
            CardDriverOptionLegacyBinding.inflate(inflater, parent, false)
        } else {
            CardDriverOptionBinding.inflate(inflater, parent, false)
        }
        return DriverViewHolder(binding)
    }

    inner class DriverViewHolder(val binding: ViewBinding) :
        AbstractViewHolder<Driver>(binding) {
        override fun bind(model: Driver) {
            val root = binding.root
            val radioButton = root.findViewById<RadioButton>(R.id.radio_button)
            val buttonDelete = root.findViewById<View>(R.id.button_delete)
            val title = root.findViewById<TextView>(R.id.title)
            val version = root.findViewById<TextView>(R.id.version)
            val description = root.findViewById<TextView>(R.id.description)

            radioButton.isChecked = model.selected
            root.setOnClickListener {
                selectItem(bindingAdapterPosition) {
                    driverViewModel.onDriverSelected(it)
                    driverViewModel.showClearButton(!StringSetting.DRIVER_PATH.global)
                }
            }
            buttonDelete.setOnClickListener {
                removeSelectableItem(
                    bindingAdapterPosition
                ) { removedPosition: Int, selectedPosition: Int ->
                    driverViewModel.onDriverRemoved(removedPosition, selectedPosition)
                    driverViewModel.showClearButton(!StringSetting.DRIVER_PATH.global)
                }
            }

            title.marquee()
            version.marquee()
            description.marquee()
            title.text = model.title
            version.text = model.version
            description.text = model.description
            buttonDelete.setVisible(
                model.title != root.context.getString(R.string.system_gpu_driver)
            )
        }
    }
}

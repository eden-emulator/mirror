// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.yuzu.yuzu_emu.adapters

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.viewbinding.ViewBinding
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.databinding.CardInstallableBinding
import org.yuzu.yuzu_emu.databinding.CardInstallableLegacyTintedBinding
import org.yuzu.yuzu_emu.model.Installable
import org.yuzu.yuzu_emu.utils.LegacyUi
import org.yuzu.yuzu_emu.utils.ViewUtils.setVisible
import org.yuzu.yuzu_emu.viewholder.AbstractViewHolder

class InstallableAdapter(installables: List<Installable>) :
    AbstractListAdapter<Installable, InstallableAdapter.InstallableViewHolder>(installables) {
    override fun onCreateViewHolder(
        parent: ViewGroup,
        viewType: Int
    ): InstallableAdapter.InstallableViewHolder {
        val inflater = LayoutInflater.from(parent.context)
        val binding: ViewBinding = if (LegacyUi.isEnabled()) {
            CardInstallableLegacyTintedBinding.inflate(inflater, parent, false)
        } else {
            CardInstallableBinding.inflate(inflater, parent, false)
        }
        return InstallableViewHolder(binding)
    }

    inner class InstallableViewHolder(binding: ViewBinding) :
        AbstractViewHolder<Installable>(binding) {
        private val title = binding.root.findViewById<TextView>(R.id.title)
        private val description = binding.root.findViewById<TextView>(R.id.description)
        private val buttonInstall = binding.root.findViewById<View>(R.id.button_install)
        private val buttonExport = binding.root.findViewById<View>(R.id.button_export)

        override fun bind(model: Installable) {
            title.setText(model.titleId)
            description.setText(model.descriptionId)

            buttonInstall.setVisible(model.install != null)
            buttonInstall.setOnClickListener { model.install?.invoke() }
            buttonExport.setVisible(model.export != null)
            buttonExport.setOnClickListener { model.export?.invoke() }
        }
    }
}

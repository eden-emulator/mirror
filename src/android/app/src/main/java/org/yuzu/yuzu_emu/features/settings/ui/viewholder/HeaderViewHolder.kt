// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.yuzu.yuzu_emu.features.settings.ui.viewholder

import android.view.View
import android.widget.TextView
import org.yuzu.yuzu_emu.databinding.ListItemSettingsHeaderBinding
import org.yuzu.yuzu_emu.features.settings.model.view.SettingsItem
import org.yuzu.yuzu_emu.features.settings.ui.SettingsAdapter

class HeaderViewHolder private constructor(
    private val titleView: TextView,
    adapter: SettingsAdapter,
    itemView: View
) : SettingViewHolder(itemView, adapter) {

    constructor(binding: ListItemSettingsHeaderBinding, adapter: SettingsAdapter) : this(
        binding.textHeaderName,
        adapter,
        binding.root
    )

    init {
        itemView.setOnClickListener(null)
    }

    override fun bind(item: SettingsItem) {
        titleView.text = item.title
    }

    override fun onClick(clicked: View) {
        // no-op
    }

    override fun onLongClick(clicked: View): Boolean {
        return true
    }
}

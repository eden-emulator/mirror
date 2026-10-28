// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.yuzu.yuzu_emu.adapters

import android.net.Uri
import android.view.LayoutInflater
import android.view.ViewGroup
import android.widget.Button
import android.widget.TextView
import androidx.fragment.app.FragmentActivity
import androidx.viewbinding.ViewBinding
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.databinding.CardFolderBinding
import org.yuzu.yuzu_emu.databinding.CardFolderLegacyTintedBinding
import org.yuzu.yuzu_emu.fragments.GameFolderPropertiesDialogFragment
import org.yuzu.yuzu_emu.model.DirectoryType
import org.yuzu.yuzu_emu.model.GameDir
import org.yuzu.yuzu_emu.model.GamesViewModel
import org.yuzu.yuzu_emu.utils.LegacyUi
import org.yuzu.yuzu_emu.utils.ViewUtils.marquee
import org.yuzu.yuzu_emu.viewholder.AbstractViewHolder

class FolderAdapter(val activity: FragmentActivity, val gamesViewModel: GamesViewModel) :
    AbstractDiffAdapter<GameDir, FolderAdapter.FolderViewHolder>() {
    override fun onCreateViewHolder(
        parent: ViewGroup,
        viewType: Int
    ): FolderAdapter.FolderViewHolder {
        val inflater = LayoutInflater.from(parent.context)
        val binding: ViewBinding = if (LegacyUi.isEnabled()) {
            CardFolderLegacyTintedBinding.inflate(inflater, parent, false)
        } else {
            CardFolderBinding.inflate(inflater, parent, false)
        }
        return FolderViewHolder(binding)
    }

    inner class FolderViewHolder(binding: ViewBinding) :
        AbstractViewHolder<GameDir>(binding) {
        private val path = binding.root.findViewById<TextView>(R.id.path)
        private val typeIndicator = binding.root.findViewById<TextView>(R.id.type_indicator)
        private val buttonEdit = binding.root.findViewById<Button>(R.id.button_edit)
        private val buttonDelete = binding.root.findViewById<Button>(R.id.button_delete)

        override fun bind(model: GameDir) {
            path.text = Uri.parse(model.uriString).path
            path.marquee()

            typeIndicator.text = when (model.type) {
                DirectoryType.GAME -> activity.getString(R.string.games)
                DirectoryType.EXTERNAL_CONTENT -> activity.getString(R.string.external_content)
            }

            buttonEdit.setOnClickListener {
                GameFolderPropertiesDialogFragment.newInstance(model)
                    .show(
                        activity.supportFragmentManager,
                        GameFolderPropertiesDialogFragment.TAG
                    )
            }

            buttonDelete.setOnClickListener {
                gamesViewModel.removeFolder(model)
            }
        }
    }
}

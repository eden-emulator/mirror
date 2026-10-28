// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.yuzu.yuzu_emu.adapters

import android.view.LayoutInflater
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.TextView
import android.widget.Toast
import androidx.core.content.res.ResourcesCompat
import androidx.core.os.bundleOf
import androidx.fragment.app.FragmentActivity
import androidx.navigation.findNavController
import androidx.viewbinding.ViewBinding
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.YuzuApplication
import org.yuzu.yuzu_emu.databinding.CardSimpleOutlinedBinding
import org.yuzu.yuzu_emu.databinding.CardSimpleOutlinedLegacyTintedBinding
import org.yuzu.yuzu_emu.model.Applet
import org.yuzu.yuzu_emu.model.AppletInfo
import org.yuzu.yuzu_emu.model.Game
import org.yuzu.yuzu_emu.utils.LegacyUi
import org.yuzu.yuzu_emu.viewholder.AbstractViewHolder

class AppletAdapter(val activity: FragmentActivity, applets: List<Applet>) :
    AbstractListAdapter<Applet, AppletAdapter.AppletViewHolder>(applets) {
    override fun onCreateViewHolder(
        parent: ViewGroup,
        viewType: Int
    ): AppletAdapter.AppletViewHolder {
        val inflater = LayoutInflater.from(parent.context)
        val binding: ViewBinding = if (LegacyUi.isEnabled()) {
            CardSimpleOutlinedLegacyTintedBinding.inflate(inflater, parent, false)
        } else {
            CardSimpleOutlinedBinding.inflate(inflater, parent, false)
        }
        return AppletViewHolder(binding)
    }

    inner class AppletViewHolder(binding: ViewBinding) :
        AbstractViewHolder<Applet>(binding) {
        private val root = binding.root
        private val title = root.findViewById<TextView>(R.id.title)
        private val description = root.findViewById<TextView>(R.id.description)
        private val icon = root.findViewById<ImageView>(R.id.icon)

        override fun bind(model: Applet) {
            title.setText(model.titleId)
            description.setText(model.descriptionId)
            icon.setImageDrawable(
                ResourcesCompat.getDrawable(
                    icon.context.resources,
                    model.iconId,
                    icon.context.theme
                )
            )

            root.setOnClickListener { onClick(model) }
        }

        fun onClick(applet: Applet) {
            val appletPath = NativeLibrary.getAppletLaunchPath(applet.appletInfo.entryId)
            if (appletPath.isEmpty()) {
                Toast.makeText(
                    root.context,
                    R.string.applets_error_applet,
                    Toast.LENGTH_SHORT
                ).show()
                return
            }

            if (applet.appletInfo == AppletInfo.Cabinet) {
                root.findNavController()
                    .navigate(R.id.action_appletLauncherFragment_to_cabinetLauncherDialogFragment)
                return
            }

            NativeLibrary.setCurrentAppletId(applet.appletInfo.appletId)
            val appletGame = Game(
                title = YuzuApplication.appContext.getString(applet.titleId),
                path = appletPath
            )
            root.findNavController().navigate(
                R.id.action_global_emulationActivity,
                bundleOf(
                    "game" to appletGame,
                    "custom" to false
                )
            )
        }
    }
}

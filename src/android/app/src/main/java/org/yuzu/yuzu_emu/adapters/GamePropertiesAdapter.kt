// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.yuzu.yuzu_emu.adapters

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.TextView
import androidx.core.content.res.ResourcesCompat
import androidx.lifecycle.LifecycleOwner
import androidx.viewbinding.ViewBinding
import com.google.android.material.button.MaterialButton
import com.google.android.material.divider.MaterialDivider
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.databinding.CardInstallableIconBinding
import org.yuzu.yuzu_emu.databinding.CardInstallableIconLegacyTintedBinding
import org.yuzu.yuzu_emu.databinding.CardSimpleOutlinedBinding
import org.yuzu.yuzu_emu.databinding.CardSimpleOutlinedLegacyTintedBinding
import org.yuzu.yuzu_emu.model.GameProperty
import org.yuzu.yuzu_emu.model.InstallableProperty
import org.yuzu.yuzu_emu.model.SubmenuProperty
import org.yuzu.yuzu_emu.utils.LegacyUi
import org.yuzu.yuzu_emu.utils.ViewUtils.marquee
import org.yuzu.yuzu_emu.utils.ViewUtils.setVisible
import org.yuzu.yuzu_emu.utils.collect
import org.yuzu.yuzu_emu.viewholder.AbstractViewHolder

class GamePropertiesAdapter(
    private val viewLifecycle: LifecycleOwner,
    private var properties: List<GameProperty>
) : AbstractListAdapter<GameProperty, AbstractViewHolder<GameProperty>>(properties) {
    override fun onCreateViewHolder(
        parent: ViewGroup,
        viewType: Int
    ): AbstractViewHolder<GameProperty> {
        val inflater = LayoutInflater.from(parent.context)
        return when (viewType) {
            PropertyType.Submenu.ordinal -> {
                val binding: ViewBinding = if (LegacyUi.isEnabled()) {
                    CardSimpleOutlinedLegacyTintedBinding.inflate(inflater, parent, false)
                } else {
                    CardSimpleOutlinedBinding.inflate(inflater, parent, false)
                }
                SubmenuPropertyViewHolder(binding)
            }

            else -> {
                val binding: ViewBinding = if (LegacyUi.isEnabled()) {
                    CardInstallableIconLegacyTintedBinding.inflate(inflater, parent, false)
                } else {
                    CardInstallableIconBinding.inflate(inflater, parent, false)
                }
                InstallablePropertyViewHolder(binding)
            }
        }
    }

    override fun getItemViewType(position: Int): Int {
        return when (properties[position]) {
            is SubmenuProperty -> PropertyType.Submenu.ordinal
            else -> PropertyType.Installable.ordinal
        }
    }

    inner class SubmenuPropertyViewHolder(binding: ViewBinding) :
        AbstractViewHolder<GameProperty>(binding) {
        private val root = binding.root
        private val title = root.findViewById<TextView>(R.id.title)
        private val description = root.findViewById<TextView>(R.id.description)
        private val icon = root.findViewById<ImageView>(R.id.icon)
        private val details = root.findViewById<TextView>(R.id.details)
        private val dividerSecondaryActions =
            root.findViewById<MaterialDivider>(R.id.dividerSecondaryActions)
        private val layoutSecondaryActions =
            root.findViewById<ViewGroup>(R.id.layoutSecondaryActions)

        override fun bind(model: GameProperty) {
            val submenuProperty = model as SubmenuProperty

            root.setOnClickListener {
                submenuProperty.action.invoke()
            }

            title.setText(submenuProperty.titleId)
            description.setText(submenuProperty.descriptionId)
            icon.setImageDrawable(
                ResourcesCompat.getDrawable(
                    icon.context.resources,
                    submenuProperty.iconId,
                    icon.context.theme
                )
            )

            details.marquee()
            if (submenuProperty.details != null) {
                details.setVisible(true)
                details.text = submenuProperty.details.invoke()
            } else if (submenuProperty.detailsFlow != null) {
                details.setVisible(true)
                submenuProperty.detailsFlow.collect(viewLifecycle) { details.text = it }
            } else {
                details.setVisible(false)
            }

            val hasVisibleActions = submenuProperty.secondaryActions?.any { it.isShown } == true
            layoutSecondaryActions.removeAllViews()
            dividerSecondaryActions.setVisible(false)

            if (hasVisibleActions) {
                layoutSecondaryActions.setVisible(true)

                val visibleActions = submenuProperty.secondaryActions!!.filter { it.isShown }
                val inflater = LayoutInflater.from(root.context)
                visibleActions.forEachIndexed { index, secondaryAction ->
                    val button = inflater.inflate(
                        R.layout.item_secondary_action_button,
                        layoutSecondaryActions,
                        false
                    ) as MaterialButton
                    button.setIconResource(secondaryAction.iconId)
                    button.text = ""
                    button.contentDescription = root.context
                        .getString(secondaryAction.descriptionId)
                    button.tooltipText = root.context
                        .getString(secondaryAction.descriptionId)
                    if (index == visibleActions.lastIndex) {
                        (button.layoutParams as ViewGroup.MarginLayoutParams).marginEnd = 0
                    }
                    button.setOnClickListener { secondaryAction.action.invoke() }
                    layoutSecondaryActions.addView(button)
                }
            } else {
                layoutSecondaryActions.setVisible(false)
            }
        }
    }

    inner class InstallablePropertyViewHolder(binding: ViewBinding) :
        AbstractViewHolder<GameProperty>(binding) {
        private val root = binding.root
        private val title = root.findViewById<TextView>(R.id.title)
        private val description = root.findViewById<TextView>(R.id.description)
        private val icon = root.findViewById<ImageView>(R.id.icon)
        private val buttonInstall = root.findViewById<View>(R.id.button_install)
        private val buttonExport = root.findViewById<View>(R.id.button_export)

        override fun bind(model: GameProperty) {
            val installableProperty = model as InstallableProperty

            title.setText(installableProperty.titleId)
            description.setText(installableProperty.descriptionId)
            icon.setImageDrawable(
                ResourcesCompat.getDrawable(
                    icon.context.resources,
                    installableProperty.iconId,
                    icon.context.theme
                )
            )

            buttonInstall.setVisible(installableProperty.install != null)
            buttonInstall.setOnClickListener { installableProperty.install?.invoke() }
            buttonExport.setVisible(installableProperty.export != null)
            buttonExport.setOnClickListener { installableProperty.export?.invoke() }
        }
    }

    enum class PropertyType {
        Submenu,
        Installable
    }
}

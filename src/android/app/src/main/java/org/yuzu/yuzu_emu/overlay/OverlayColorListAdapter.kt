// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.overlay

import android.graphics.drawable.GradientDrawable
import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.RecyclerView
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.databinding.ListItemOverlayPresetBinding
import org.yuzu.yuzu_emu.databinding.ListItemSettingBinding
import org.yuzu.yuzu_emu.databinding.ListItemSettingsHeaderBinding
import org.yuzu.yuzu_emu.utils.ViewUtils.setVisible

internal fun bindOverlayPresetRow(
    binding: ListItemOverlayPresetBinding,
    title: String,
    body: Int,
    face: String,
    overrides: Map<String, Int>,
    onClick: () -> Unit,
    onLongClick: (() -> Unit)? = null
) {
    binding.presetTitle.text = title
    binding.presetPreview.setPreset(body, face, overrides)
    val apply = { onClick() }
    binding.root.setOnClickListener { apply() }
    binding.presetPreview.setOnClickListener { apply() }
    binding.root.setOnLongClickListener {
        onLongClick?.invoke()
        onLongClick != null
    }
    binding.presetPreview.setOnLongClickListener {
        onLongClick?.invoke()
        onLongClick != null
    }
}

class OverlayColorListAdapter(
    private val colorState: () -> OverlayColorState
) : RecyclerView.Adapter<RecyclerView.ViewHolder>() {
    private val rows = mutableListOf<OverlayColorRow>()

    fun submit(next: List<OverlayColorRow>) {
        rows.clear()
        rows.addAll(next)
        notifyDataSetChanged()
    }

    override fun getItemViewType(position: Int): Int = when (rows[position]) {
        is OverlayColorRow.Header -> 0
        is OverlayColorRow.PresetDesign -> 1
        is OverlayColorRow.Setting -> 2
        is OverlayColorRow.Control -> 3
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RecyclerView.ViewHolder {
        val inflater = LayoutInflater.from(parent.context)
        return when (viewType) {
            0 -> HeaderHolder(ListItemSettingsHeaderBinding.inflate(inflater, parent, false))
            1 -> PresetHolder(ListItemOverlayPresetBinding.inflate(inflater, parent, false))
            2 -> SettingHolder(ListItemSettingBinding.inflate(inflater, parent, false))
            else -> ControlHolder(ListItemSettingBinding.inflate(inflater, parent, false), colorState)
        }
    }

    override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
        when (val row = rows[position]) {
            is OverlayColorRow.Header -> (holder as HeaderHolder).bind(row)
            is OverlayColorRow.PresetDesign -> (holder as PresetHolder).bind(row)
            is OverlayColorRow.Setting -> (holder as SettingHolder).bind(row)
            is OverlayColorRow.Control -> (holder as ControlHolder).bind(row)
        }
    }

    override fun getItemCount(): Int = rows.size

    private class HeaderHolder(
        private val binding: ListItemSettingsHeaderBinding
    ) : RecyclerView.ViewHolder(binding.root) {
        fun bind(row: OverlayColorRow.Header) {
            binding.textHeaderName.setText(row.titleRes)
        }
    }

    private class PresetHolder(
        private val binding: ListItemOverlayPresetBinding
    ) : RecyclerView.ViewHolder(binding.root) {
        fun bind(row: OverlayColorRow.PresetDesign) {
            bindOverlayPresetRow(
                binding,
                row.title,
                row.body,
                row.face,
                row.controlOverrides,
                row.onClick,
                row.onLongClick
            )
        }
    }

    private class SettingHolder(
        private val binding: ListItemSettingBinding
    ) : RecyclerView.ViewHolder(binding.root) {
        fun bind(row: OverlayColorRow.Setting) {
            binding.textSettingName.text = row.title
            binding.textSettingDescription.text = row.description
            binding.textSettingDescription.setVisible(row.description.isNotEmpty())
            binding.textSettingValue.setVisible(row.showChevron)
            binding.textSettingValue.text = ""
            binding.textSettingValue.setCompoundDrawablesRelativeWithIntrinsicBounds(
                0,
                0,
                if (row.showChevron) R.drawable.ic_arrow_forward else 0,
                0
            )
            binding.buttonClear.setVisible(false)
            binding.icon.setVisible(row.iconRes != 0)
            if (row.iconRes != 0) {
                binding.icon.background = null
                binding.icon.setImageDrawable(
                    ContextCompat.getDrawable(binding.root.context, row.iconRes)
                )
            } else {
                binding.icon.setImageDrawable(null)
                binding.icon.background = null
            }
            binding.root.setOnClickListener { row.onClick() }
            binding.root.setOnLongClickListener(null)
        }
    }

    private class ControlHolder(
        private val binding: ListItemSettingBinding,
        private val colorState: () -> OverlayColorState
    ) : RecyclerView.ViewHolder(binding.root) {
        fun bind(row: OverlayColorRow.Control) {
            val state = colorState()
            binding.textSettingName.text = row.title
            binding.textSettingDescription.text = row.subtitle
            binding.textSettingDescription.setVisible(true)
            binding.textSettingValue.setVisible(true)
            binding.textSettingValue.text = ""
            binding.textSettingValue.setCompoundDrawablesRelativeWithIntrinsicBounds(
                0,
                0,
                R.drawable.ic_arrow_forward,
                0
            )
            binding.buttonClear.setVisible(false)
            binding.icon.setVisible(true)
            binding.icon.imageTintList = null
            val tinted = OverlayControlIcons.tintedIcon(
                binding.root.context,
                row.controlId,
                state.body,
                state.face,
                state.overrides,
                OverlayControlIcons.DESIGN_PREVIEW_ICON_DP
            )
            if (tinted != null) {
                binding.icon.background = null
                binding.icon.setImageDrawable(tinted)
            } else {
                binding.icon.setImageDrawable(null)
                binding.icon.background = swatch(row.color)
            }
            binding.root.setOnClickListener { row.onClick() }
            binding.root.setOnLongClickListener(null)
        }

        private fun swatch(color: Int): GradientDrawable =
            GradientDrawable().apply {
                shape = GradientDrawable.OVAL
                setColor(color)
                setStroke(
                    (1f * binding.root.resources.displayMetrics.density).toInt().coerceAtLeast(1),
                    0x33000000
                )
            }
    }
}

data class OverlayColorState(
    val body: Int,
    val face: String,
    val overrides: Map<String, Int>
)

sealed class OverlayColorRow {
    data class Header(val titleRes: Int) : OverlayColorRow()
    data class PresetDesign(
        val title: String,
        val body: Int,
        val face: String,
        val controlOverrides: Map<String, Int>,
        val onClick: () -> Unit,
        val onLongClick: (() -> Unit)? = null
    ) : OverlayColorRow()
    data class Setting(
        val title: String,
        val description: String,
        val showChevron: Boolean = false,
        val iconRes: Int = 0,
        val onClick: () -> Unit
    ) : OverlayColorRow()
    data class Control(
        val controlId: String,
        val title: String,
        val subtitle: String,
        val color: Int,
        val onClick: () -> Unit
    ) : OverlayColorRow()
}

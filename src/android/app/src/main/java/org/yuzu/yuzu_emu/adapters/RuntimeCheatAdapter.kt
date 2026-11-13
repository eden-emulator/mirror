// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.adapters

import android.annotation.SuppressLint
import android.graphics.Typeface
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.core.view.ViewCompat
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.checkbox.MaterialCheckBox
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.NativeLibrary.RuntimeCheat
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.databinding.ListItemRuntimeCheatBinding

class RuntimeCheatAdapter(private val cheats: List<RuntimeCheat>) :
    RecyclerView.Adapter<RuntimeCheatAdapter.CheatViewHolder>() {
    private val byId = cheats.associateBy { it.id }
    private val enabled = cheats.associate { it.id to it.enabled }.toMutableMap()
    private val expanded = mutableSetOf<Int>()
    private var rows = visibleRows()

    private fun descendants(id: Int) = cheats.filter { cheat ->
        var parent = cheat.parentId
        while (parent != RuntimeCheat.ROOT && parent != id) {
            parent = byId.getValue(parent).parentId
        }
        cheat.type == RuntimeCheat.CHEAT && parent == id
    }

    private fun visibleRows() = cheats.filter { cheat ->
        var parent = cheat.parentId
        while (parent != RuntimeCheat.ROOT && parent in expanded) {
            parent = byId.getValue(parent).parentId
        }
        parent == RuntimeCheat.ROOT
    }

    fun setAllEnabled(value: Boolean) = setEnabled(RuntimeCheat.ROOT, value)

    private fun setEnabled(id: Int, value: Boolean) {
        NativeLibrary.setRuntimeCheatEnabled(id, value)
        NativeLibrary.getRuntimeCheats().forEach { enabled[it.id] = it.enabled }
        notifyItemRangeChanged(0, rows.size)
    }

    @SuppressLint("NotifyDataSetChanged")
    private fun toggleExpanded(id: Int) {
        if (!expanded.remove(id)) expanded.add(id)
        rows = visibleRows()
        notifyDataSetChanged()
    }

    override fun getItemCount() = rows.size

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int) = CheatViewHolder(
        ListItemRuntimeCheatBinding.inflate(LayoutInflater.from(parent.context), parent, false)
    )

    override fun onBindViewHolder(holder: CheatViewHolder, position: Int) {
        holder.bind(rows[position])
    }

    inner class CheatViewHolder(private val binding: ListItemRuntimeCheatBinding) :
        RecyclerView.ViewHolder(binding.root) {
        fun bind(cheat: RuntimeCheat) = with(binding) {
            val isGroup = cheat.type == RuntimeCheat.SUBMENU
            val children = if (isGroup) descendants(cheat.id) else emptyList()
            val checked = children.count { enabled[it.id] == true }
            val label = cheat.name.ifEmpty { root.context.getString(R.string.unnamed_cheat) }
            name.text = if (isGroup) "$label ($checked/${children.size})" else label
            name.setTypeface(null, if (isGroup) Typeface.BOLD else Typeface.NORMAL)
            ViewCompat.setAccessibilityHeading(name, isGroup)
            expandIcon.visibility = if (isGroup) View.VISIBLE else View.GONE
            expandIcon.rotation = if (cheat.id in expanded) 0f else -90f
            checkbox.visibility = if (isGroup && children.isEmpty()) {
                View.GONE
            } else {
                View.VISIBLE
            }
            checkbox.isEnabled = !cheat.isMaster
            checkbox.checkedState = when {
                isGroup && checked == 0 -> MaterialCheckBox.STATE_UNCHECKED
                isGroup && checked == children.size -> MaterialCheckBox.STATE_CHECKED
                isGroup -> MaterialCheckBox.STATE_INDETERMINATE
                enabled[cheat.id] == true -> MaterialCheckBox.STATE_CHECKED
                else -> MaterialCheckBox.STATE_UNCHECKED
            }
            checkbox.contentDescription = label
            checkbox.setOnClickListener {
                setEnabled(cheat.id, checkbox.checkedState != MaterialCheckBox.STATE_UNCHECKED)
            }
            title.setOnClickListener(
                if (isGroup) View.OnClickListener { toggleExpanded(cheat.id) } else null
            )
            title.isClickable = isGroup
            title.isFocusable = isGroup
            ViewCompat.setStateDescription(
                title,
                if (isGroup) {
                    root.context.getString(
                        if (cheat.id in expanded) {
                            R.string.cheat_submenu_expanded
                        } else {
                            R.string.cheat_submenu_collapsed
                        }
                    )
                } else {
                    null
                }
            )
            root.setOnClickListener(
                if (cheat.type == RuntimeCheat.CHEAT && !cheat.isMaster) {
                    View.OnClickListener { checkbox.performClick() }
                } else {
                    null
                }
            )
            root.isClickable = cheat.type == RuntimeCheat.CHEAT && !cheat.isMaster
            root.isFocusable = root.isClickable
            root.alpha = if (cheat.isMaster) 0.38f else 1f
            var depth = 0
            var parent = cheat.parentId
            while (parent != RuntimeCheat.ROOT) {
                ++depth
                parent = byId.getValue(parent).parentId
            }
            val insetDp = 16 + minOf(depth, 6) * 16
            val inset = (insetDp * root.resources.displayMetrics.density).toInt()
            root.setPaddingRelative(inset, root.paddingTop, root.paddingEnd, root.paddingBottom)
        }
    }
}

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.overlay

import android.net.Uri
import android.provider.OpenableColumns
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.snackbar.Snackbar
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.databinding.ListItemOverlayPresetBinding

/**
 * Import/export overlay color JSON (single design per file) from settings or overlay color UI.
 */
class OverlayColorConfigTransfer(
    private val activity: AppCompatActivity,
    private val snackbarAnchor: View,
    private val onImportApplied: () -> Unit = {}
) {
    private var pendingExport: String = ""
    private var presetPickDialog: AlertDialog? = null

    fun launchImport(openDocument: (Array<String>) -> Unit) {
        openDocument(OverlayPresetLibrary.importMimeTypes)
    }

    fun launchExport(createDocument: (String) -> Unit) {
        val choices = buildExportChoices()
        if (choices.isEmpty()) {
            Toast.makeText(activity, R.string.overlay_export_none, Toast.LENGTH_LONG).show()
            return
        }
        showPresetPicker(
            titleRes = R.string.overlay_export_pick_one,
            presets = choices
        ) { preset ->
            pendingExport = OverlayPresetLibrary.exportJson(listOf(preset))
            createDocument(OverlayPresetLibrary.exportFilenameForPreset(preset.name))
        }
    }

    fun handleImportUri(uri: Uri) {
        if (!OverlayPresetLibrary.isEdenovlUriName(resolveDisplayName(uri))) {
            Toast.makeText(activity, R.string.overlay_import_edenovl_only, Toast.LENGTH_LONG).show()
            return
        }
        try {
            val text = activity.contentResolver.openInputStream(uri)?.use {
                it.readBytes().toString(Charsets.UTF_8)
            } ?: throw IllegalArgumentException("stream")
            val candidates = OverlayPresetLibrary.parseImportFile(text)
            if (candidates.isEmpty()) {
                throw IllegalArgumentException("empty")
            }
            showPresetPicker(
                titleRes = R.string.overlay_import_pick_one,
                presets = candidates
            ) { finishImport(it) }
        } catch (_: Exception) {
            Toast.makeText(activity, R.string.overlay_preset_import_failed, Toast.LENGTH_SHORT).show()
        }
    }

    fun handleExportUri(uri: Uri) {
        try {
            activity.contentResolver.openOutputStream(uri)?.use { stream ->
                stream.write(pendingExport.toByteArray(Charsets.UTF_8))
            } ?: throw IllegalStateException("stream")
            Snackbar.make(snackbarAnchor, R.string.overlay_preset_exported, Snackbar.LENGTH_SHORT).show()
        } catch (_: Exception) {
            Toast.makeText(activity, R.string.overlay_preset_import_failed, Toast.LENGTH_SHORT).show()
        }
    }

    private fun buildExportChoices(): List<OverlayPresetLibrary.Preset> {
        val body = OverlayColors.loadBody()
        val overrides = OverlayColors.loadOverrides()
        val face = OverlayColors.loadFace()
        val choices = mutableListOf<OverlayPresetLibrary.Preset>()
        choices += OverlayPresetLibrary.load()
        if (OverlayColors.matchingPalette(body, overrides, face) == null) {
            choices += OverlayPresetLibrary.Preset(
                OverlayPresetLibrary.LIVE_COLORS_EXPORT_BASENAME,
                body,
                face,
                overrides.toMap()
            )
        }
        return choices
    }

    private fun reservedNameKeys(): Set<String> {
        val labels = OverlayColors.palettes.map { activity.getString(it.labelRes) }
        return OverlayColors.reservedPresetNameKeys(labels)
    }

    private fun finishImport(preset: OverlayPresetLibrary.Preset) {
        val body = preset.body or 0xFF000000.toInt()
        OverlayColors.write(body, preset.overrides, preset.face)
        val matchesBuiltin = OverlayColors.matchingPalette(body, preset.overrides, preset.face) != null
        val reservedName = OverlayPresetLibrary.isReservedName(preset.name, reservedNameKeys())
        val persistToLibrary = {
            OverlayPresetLibrary.importSelected(preset)
            onImportApplied()
        }
        if (!matchesBuiltin && !reservedName) {
            if (OverlayPresetLibrary.findSavedByExactName(preset.name) != null) {
                MaterialAlertDialogBuilder(activity)
                    .setTitle(R.string.overlay_preset_overwrite)
                    .setMessage(
                        activity.getString(R.string.overlay_preset_overwrite_message, preset.name)
                    )
                    .setNegativeButton(android.R.string.cancel) { _, _ -> onImportApplied() }
                    .setPositiveButton(R.string.overlay_preset_overwrite_confirm) { _, _ ->
                        persistToLibrary()
                    }
                    .show()
            } else {
                persistToLibrary()
            }
        } else {
            onImportApplied()
        }
        Snackbar.make(
            snackbarAnchor,
            activity.getString(R.string.overlay_preset_imported_one, preset.name),
            Snackbar.LENGTH_SHORT
        ).show()
    }

    private fun showPresetPicker(
        titleRes: Int,
        presets: List<OverlayPresetLibrary.Preset>,
        onPick: (OverlayPresetLibrary.Preset) -> Unit
    ) {
        presetPickDialog?.dismiss()
        val list = RecyclerView(activity).apply {
            layoutManager = LinearLayoutManager(activity)
            adapter = PresetPickAdapter(presets, onPick) {
                presetPickDialog?.dismiss()
            }
            val pad = (8 * activity.resources.displayMetrics.density).toInt()
            setPadding(0, pad, 0, pad)
        }
        presets.forEach { preset ->
            OverlayPresetThumbnailCache.preload(
                activity,
                preset.body,
                preset.face,
                preset.overrides
            )
        }
        presetPickDialog = MaterialAlertDialogBuilder(activity)
            .setTitle(titleRes)
            .setView(list)
            .setNegativeButton(android.R.string.cancel, null)
            .create()
        presetPickDialog?.show()
    }

    private fun resolveDisplayName(uri: Uri): String? {
        activity.contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            if (index >= 0 && cursor.moveToFirst()) {
                return cursor.getString(index)
            }
        }
        return uri.lastPathSegment
    }

    private class PresetPickAdapter(
        private val presets: List<OverlayPresetLibrary.Preset>,
        private val onPick: (OverlayPresetLibrary.Preset) -> Unit,
        private val onDismissDialog: () -> Unit
    ) : RecyclerView.Adapter<PresetPickAdapter.Holder>() {

        class Holder(val binding: ListItemOverlayPresetBinding) : RecyclerView.ViewHolder(binding.root)

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): Holder {
            val inflater = LayoutInflater.from(parent.context)
            return Holder(ListItemOverlayPresetBinding.inflate(inflater, parent, false))
        }

        override fun onBindViewHolder(holder: Holder, position: Int) {
            val preset = presets[position]
            holder.binding.presetTitle.text = if (
                preset.name == OverlayPresetLibrary.LIVE_COLORS_EXPORT_BASENAME
            ) {
                holder.binding.root.context.getString(R.string.overlay_export_current)
            } else {
                preset.name
            }
            holder.binding.presetPreview.setPreset(preset.body, preset.face, preset.overrides)
            val pick = {
                onDismissDialog()
                onPick(preset)
            }
            holder.binding.root.setOnClickListener { pick() }
            holder.binding.presetPreview.setOnClickListener { pick() }
        }

        override fun getItemCount(): Int = presets.size
    }
}

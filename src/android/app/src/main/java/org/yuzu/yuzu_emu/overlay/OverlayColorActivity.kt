// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.overlay

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.content.res.Configuration
import android.os.Bundle
import android.text.Editable
import android.text.InputType
import android.text.TextWatcher
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.InputMethodManager
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import kotlinx.coroutines.launch
import kotlinx.coroutines.yield
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.slider.Slider
import com.google.android.material.snackbar.Snackbar
import java.util.Locale
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.YuzuApplication
import org.yuzu.yuzu_emu.databinding.ActivityOverlayColorBinding
import org.yuzu.yuzu_emu.databinding.DialogEditTextBinding
import org.yuzu.yuzu_emu.databinding.ListItemOverlayPresetBinding
import org.yuzu.yuzu_emu.databinding.ListItemSettingBinding
import org.yuzu.yuzu_emu.databinding.ListItemSettingsHeaderBinding
import org.yuzu.yuzu_emu.utils.ThemeHelper
import org.yuzu.yuzu_emu.utils.ViewUtils.setVisible
import org.yuzu.yuzu_emu.utils.ViewUtils.updateMargins

class OverlayColorActivity : AppCompatActivity() {
    private lateinit var binding: ActivityOverlayColorBinding
    private val overrides = linkedMapOf<String, Int>()
    private val adapter = ColorAdapter()

    private var body = OverlayColors.DEFAULT_COLOR
    private var face = OverlayColors.FACE_LETTERS
    private var destination = Destination.HUB
    private var editingId: String? = null
    private var suppress = false
    private var savedPresets = emptyList<OverlayPresetLibrary.Preset>()
    private lateinit var colorConfigTransfer: OverlayColorConfigTransfer
    private var lastPreviewLayoutSignature: String? = null
    /** Opened from settings straight to template/presets; back leaves the activity instead of the hub. */
    private var entrySkipsHub = false
    /** Ignores stale [View.post] grid relayout callbacks after leaving the presets screen. */
    private var listLayoutGeneration = 0
    private var presetGridLayoutRunnable: Runnable? = null

    private enum class Destination {
        HUB,
        PRESETS,
        TEMPLATE
    }

    private val importLauncher = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) {
            colorConfigTransfer.handleImportUri(uri)
        }
    }

    private val exportLauncher = registerForActivityResult(
        ActivityResultContracts.CreateDocument(OverlayPresetLibrary.EXPORT_MIME_TYPE)
    ) { uri ->
        if (uri != null) {
            colorConfigTransfer.handleExportUri(uri)
        }
    }

    override fun attachBaseContext(base: Context) {
        super.attachBaseContext(YuzuApplication.applyLanguage(base))
    }

    override fun onResume() {
        super.onResume()
        val layoutSignature = OverlayPreviewLayout.layoutSignature()
        if (lastPreviewLayoutSignature != null && lastPreviewLayoutSignature != layoutSignature) {
            OverlayPresetThumbnailCache.evictAll()
            if (destination == Destination.PRESETS) {
                showPresets()
            }
        }
        lastPreviewLayoutSignature = layoutSignature
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        ThemeHelper.setTheme(this)
        super.onCreate(savedInstanceState)
        binding = ActivityOverlayColorBinding.inflate(layoutInflater)
        setContentView(binding.root)

        body = OverlayColors.loadBody()
        overrides.clear()
        overrides.putAll(OverlayColors.loadOverrides())
        face = OverlayColors.loadFace()
        savedPresets = OverlayPresetLibrary.load()

        if (savedInstanceState != null) {
            body = savedInstanceState.getInt(STATE_BODY) or 0xFF000000.toInt()
            overrides.clear()
            overrides.putAll(OverlayColors.parse(savedInstanceState.getString(STATE_MAP).orEmpty()))
            face = savedInstanceState.getString(STATE_FACE) ?: OverlayColors.FACE_LETTERS
            editingId = savedInstanceState.getString(STATE_EDITING)
            destination = Destination.entries[savedInstanceState.getInt(STATE_DESTINATION)]
            entrySkipsHub = savedInstanceState.getBoolean(STATE_ENTRY_SKIPS_HUB)
        } else {
            val destOrdinal = intent.getIntExtra(EXTRA_DESTINATION, Destination.HUB.ordinal)
            destination = Destination.entries.getOrElse(destOrdinal) { Destination.HUB }
            entrySkipsHub = destination != Destination.HUB
        }

        binding.toolbarOverlayColor.setNavigationOnClickListener { onNavigateBack() }

        colorConfigTransfer = OverlayColorConfigTransfer(
            this,
            binding.overlayColorRoot,
            onImportApplied = { reloadColorsFromStorage() }
        )

        onBackPressedDispatcher.addCallback(
            this,
            object : OnBackPressedCallback(true) {
                override fun handleOnBackPressed() {
                    onNavigateBack()
                }
            }
        )

        binding.listOverlayColor.layoutManager = LinearLayoutManager(this)
        binding.listOverlayColor.adapter = adapter
        binding.listOverlayColor.setHasFixedSize(true)
        binding.listOverlayColor.itemAnimator = null
        binding.listOverlayColor.setItemViewCacheSize(20)
        binding.colorPreview.setImageDrawable(null)
        wireEditor()
        applyInsets()
        if (editingId != null) {
            showEditor(editingId!!)
        } else {
            navigateTo(destination)
        }
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putInt(STATE_BODY, body)
        outState.putString(STATE_MAP, OverlayColors.serialize(overrides, face))
        outState.putString(STATE_FACE, face)
        outState.putString(STATE_EDITING, editingId)
        outState.putInt(STATE_DESTINATION, destination.ordinal)
        outState.putBoolean(STATE_ENTRY_SKIPS_HUB, entrySkipsHub)
    }

    private fun applyInsets() {
        ViewCompat.setOnApplyWindowInsetsListener(binding.overlayColorRoot) { _, windowInsets ->
            val bars = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutout = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())
            val left = bars.left + cutout.left
            val right = bars.right + cutout.right
            binding.listOverlayColor.updateMargins(left = left, right = right)
            binding.listOverlayColor.updatePadding(bottom = bars.bottom)
            binding.editorScroll.updateMargins(left = left, right = right)
            binding.editorScroll.updatePadding(bottom = bars.bottom)
            binding.appbarOverlayColor.updateMargins(left = left, right = right)
            windowInsets
        }
    }

    /** Persist like other settings screens: changes apply immediately. */
    private fun commitColors(
        showFeedback: Boolean = false,
        feedbackRes: Int? = null,
        feedbackFormatArgs: Array<out Any> = emptyArray()
    ) {
        OverlayColors.write(body, overrides, face)
        setResult(Activity.RESULT_OK)
        if (showFeedback && feedbackRes != null) {
            val message = if (feedbackFormatArgs.isEmpty()) {
                getString(feedbackRes)
            } else {
                getString(feedbackRes, *feedbackFormatArgs)
            }
            Snackbar.make(binding.overlayColorRoot, message, Snackbar.LENGTH_SHORT).show()
        }
    }

    private fun navigateTo(target: Destination) {
        destination = target
        when (target) {
            Destination.HUB -> showHub()
            Destination.PRESETS -> showPresets()
            Destination.TEMPLATE -> showTemplate()
        }
    }

    private fun showListScreen(titleRes: Int, rows: List<Row>) {
        dismissColorEditorIme()
        editingId = null
        val layoutGeneration = ++listLayoutGeneration
        cancelPendingPresetGridLayout()
        binding.editorScroll.visibility = View.GONE
        binding.listOverlayColor.visibility = View.VISIBLE
        binding.toolbarOverlayColor.setTitle(titleRes)
        configureListLayout(rows)
        adapter.submit(rows)
        if (destination == Destination.PRESETS && rows.any { it is Row.PresetDesign }) {
            preloadPresetThumbnails(rows)
            val runnable = Runnable {
                presetGridLayoutRunnable = null
                if (layoutGeneration != listLayoutGeneration) {
                    return@Runnable
                }
                if (destination != Destination.PRESETS) {
                    return@Runnable
                }
                val currentRows = adapter.currentRows()
                refreshPresetGridLayout(currentRows)
            }
            presetGridLayoutRunnable = runnable
            binding.listOverlayColor.post(runnable)
        }
    }

    private fun cancelPendingPresetGridLayout() {
        presetGridLayoutRunnable?.let { binding.listOverlayColor.removeCallbacks(it) }
        presetGridLayoutRunnable = null
    }

    private fun effectivePresetListWidthPx(): Int {
        val listWidth = binding.listOverlayColor.width
        if (listWidth > 0) {
            return listWidth
        }
        return resources.displayMetrics.widthPixels
    }

    private fun presetColumnCount(): Int {
        if (resources.configuration.orientation != Configuration.ORIENTATION_LANDSCAPE) {
            return 1
        }
        val minColumnPx = resources.getDimensionPixelSize(R.dimen.overlay_preset_grid_min_column_width)
        val widthPx = effectivePresetListWidthPx()
        return if (widthPx >= minColumnPx * 2) 2 else 1
    }

    private fun refreshPresetGridLayout(rows: List<Row>) {
        if (destination != Destination.PRESETS || !rows.any { it is Row.PresetDesign }) {
            return
        }
        val beforeColumns = adapter.presetGridColumns
        val beforeGrid = binding.listOverlayColor.layoutManager is GridLayoutManager
        configureListLayout(rows)
        val afterGrid = binding.listOverlayColor.layoutManager is GridLayoutManager
        if (beforeColumns != adapter.presetGridColumns || beforeGrid != afterGrid) {
            adapter.notifyDataSetChanged()
        }
    }

    private fun configureListLayout(rows: List<Row>) {
        val presetGrid = destination == Destination.PRESETS && rows.any { it is Row.PresetDesign }
        if (!presetGrid) {
            adapter.presetGridColumns = 0
            binding.listOverlayColor.layoutManager =
                LinearLayoutManager(this, LinearLayoutManager.VERTICAL, false)
            adapter.spanSizeLookup.invalidateSpanIndexCache()
            return
        }
        val columns = presetColumnCount()
        adapter.presetGridColumns = columns
        if (columns == 1) {
            binding.listOverlayColor.layoutManager =
                LinearLayoutManager(this, LinearLayoutManager.VERTICAL, false)
            adapter.spanSizeLookup.invalidateSpanIndexCache()
            return
        }
        val existing = binding.listOverlayColor.layoutManager
        if (existing is GridLayoutManager && existing.spanCount == columns) {
            existing.spanSizeLookup = adapter.spanSizeLookup
            adapter.spanSizeLookup.invalidateSpanIndexCache()
            return
        }
        val grid = GridLayoutManager(this, columns)
        grid.spanSizeLookup = adapter.spanSizeLookup
        binding.listOverlayColor.layoutManager = grid
        adapter.spanSizeLookup.invalidateSpanIndexCache()
    }

    private fun preloadPresetThumbnails(rows: List<Row>) {
        lifecycleScope.launch {
            for (row in rows) {
                if (row is Row.PresetDesign) {
                    OverlayPresetThumbnailCache.preload(
                        this@OverlayColorActivity,
                        row.body,
                        row.face,
                        row.controlOverrides
                    )
                    yield()
                }
            }
        }
    }

    private fun builtInLabels(): List<String> =
        OverlayColors.palettes.map { getString(it.labelRes) }

    private fun reservedNameKeys(): Set<String> =
        OverlayColors.reservedPresetNameKeys(builtInLabels())

    private fun showHub() {
        destination = Destination.HUB
        showListScreen(
            R.string.overlay_color,
            listOf(
                Row.Setting(
                    title = getString(R.string.overlay_color_presets),
                    description = getString(R.string.overlay_color_presets_summary),
                    showChevron = true,
                    onClick = { navigateTo(Destination.PRESETS) }
                ),
                Row.Setting(
                    title = getString(R.string.overlay_color_create_template),
                    description = getString(R.string.overlay_color_create_template_summary),
                    showChevron = true,
                    onClick = { navigateTo(Destination.TEMPLATE) }
                )
            )
        )
    }

    private fun showPresets() {
        destination = Destination.PRESETS
        savedPresets = OverlayPresetLibrary.load()
        val rows = mutableListOf<Row>()
        rows += Row.Header(R.string.overlay_color_builtin_header)
        OverlayColors.palettes.forEach { palette ->
            val title = getString(palette.labelRes)
            rows += Row.PresetDesign(
                title = title,
                body = palette.body,
                face = palette.face,
                controlOverrides = palette.overrides,
                onClick = {
                    applyPalette(
                        palette.body,
                        palette.overrides,
                        palette.face,
                        getString(palette.labelRes)
                    )
                }
            )
        }
        val savedRows = savedPresets.filter { preset ->
            OverlayColors.matchingPalette(preset.body, preset.overrides, preset.face) == null
        }
        if (savedRows.isNotEmpty()) {
            rows += Row.Header(R.string.overlay_preset_saved_header)
            savedRows.forEach { preset ->
                rows += Row.PresetDesign(
                    title = preset.name,
                    body = preset.body,
                    face = preset.face,
                    controlOverrides = preset.overrides,
                    onClick = {
                        applyPalette(preset.body, preset.overrides, preset.face, preset.name)
                    },
                    onLongClick = { confirmDelete(preset.name) }
                )
            }
        }
        showListScreen(R.string.overlay_color_presets, rows)
    }

    private fun showTemplate() {
        destination = Destination.TEMPLATE
        val rows = mutableListOf<Row>()
        rows += Row.Setting(
            title = getString(R.string.overlay_preset_save),
            description = getString(R.string.overlay_color_save_preset_hint),
            showChevron = true,
            onClick = { promptSavePreset() }
        )
        rows += Row.Header(R.string.overlay_color_buttons)
        rows += controlRow(OverlayColors.BASE, R.string.overlay_color_base, body, usingBaseColor = false)
        OverlayColors.targets.forEach { target ->
            val custom = overrides.containsKey(target.id)
            val color = OverlayColors.resolved(body, overrides, target.id)
            rows += controlRow(target.id, OverlayColors.labelFor(target.id, face), color, !custom)
        }
        showListScreen(R.string.overlay_color_create_template, rows)
    }

    private fun showEditor(id: String) {
        editingId = id
        binding.listOverlayColor.visibility = View.GONE
        binding.editorScroll.visibility = View.VISIBLE
        binding.toolbarOverlayColor.setTitle(
            if (id == OverlayColors.BASE) {
                R.string.overlay_color_base
            } else {
                OverlayColors.labelFor(id, face)
            }
        )
        renderEditor(pushSliders = true, updateHex = true)
    }

    private fun controlRow(id: String, labelRes: Int, color: Int, usingBaseColor: Boolean): Row.Control {
        val subtitle = if (id == OverlayColors.BASE) {
            OverlayColors.hexString(color)
        } else if (usingBaseColor) {
            getString(R.string.overlay_color_using_base)
        } else {
            OverlayColors.hexString(color)
        }
        return Row.Control(
            controlId = id,
            title = getString(labelRes),
            subtitle = subtitle,
            color = color,
            onClick = { showEditor(id) }
        )
    }

    private fun applyPalette(
        newBody: Int,
        newOverrides: Map<String, Int>,
        newFace: String,
        appliedName: String
    ) {
        body = newBody or 0xFF000000.toInt()
        face = newFace
        overrides.clear()
        overrides.putAll(newOverrides)
        commitColors(
            showFeedback = true,
            feedbackRes = R.string.overlay_preset_applied,
            feedbackFormatArgs = arrayOf(appliedName)
        )
    }

    private fun wireEditor() {
        val sliderListener = Slider.OnChangeListener { _, _, fromUser ->
            if (suppress || !fromUser) {
                return@OnChangeListener
            }
            assign(
                Color.rgb(
                    binding.redSlider.value.toInt(),
                    binding.greenSlider.value.toInt(),
                    binding.blueSlider.value.toInt()
                ),
                pushSliders = false,
                updateHex = true,
                commit = false
            )
        }
        binding.redSlider.addOnChangeListener(sliderListener)
        binding.greenSlider.addOnChangeListener(sliderListener)
        binding.blueSlider.addOnChangeListener(sliderListener)

        val touchListener = object : Slider.OnSliderTouchListener {
            override fun onStartTrackingTouch(slider: Slider) {}
            override fun onStopTrackingTouch(slider: Slider) {
                commitColors()
            }
        }
        binding.redSlider.addOnSliderTouchListener(touchListener)
        binding.greenSlider.addOnSliderTouchListener(touchListener)
        binding.blueSlider.addOnSliderTouchListener(touchListener)

        binding.hexInput.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) {
                if (suppress) {
                    return
                }
                val text = s?.toString()?.trim().orEmpty()
                val parsed = text.toIntOrNull(16)
                if (parsed == null || text.length != 6) {
                    return
                }
                assign(parsed or 0xFF000000.toInt(), pushSliders = true, updateHex = false, commit = true)
            }
        })
        binding.matchBaseColorButton.setOnClickListener {
            val id = editingId ?: return@setOnClickListener
            if (id == OverlayColors.BASE) {
                return@setOnClickListener
            }
            overrides.remove(id)
            renderEditor(pushSliders = true, updateHex = true)
            commitColors()
        }
    }

    private fun assign(color: Int, pushSliders: Boolean, updateHex: Boolean, commit: Boolean) {
        val id = editingId ?: return
        val opaque = color or 0xFF000000.toInt()
        if (id == OverlayColors.BASE) {
            body = opaque
        } else {
            overrides[id] = opaque
        }
        renderEditor(pushSliders, updateHex)
        if (commit) {
            commitColors()
        }
    }

    private fun currentColor(): Int {
        val id = editingId ?: return body
        return if (id == OverlayColors.BASE) body else OverlayColors.resolved(body, overrides, id)
    }

    private fun renderEditor(pushSliders: Boolean, updateHex: Boolean) {
        val id = editingId ?: return
        val color = currentColor()
        val editingBaseColor = id == OverlayColors.BASE
        binding.matchBaseColorButton.visibility =
            if (editingBaseColor || !overrides.containsKey(id)) View.GONE else View.VISIBLE
        if (editingBaseColor) {
            binding.colorPreview.visibility = View.GONE
            binding.colorPreview.setImageDrawable(null)
            binding.colorPreviewBar.visibility = View.VISIBLE
            binding.colorPreviewBar.setImageDrawable(
                OverlayControlIcons.tintedBaseColorEditorPreview(this, color)
            )
        } else {
            binding.colorPreviewBar.visibility = View.GONE
            binding.colorPreviewBar.setImageDrawable(null)
            binding.colorPreview.visibility = View.VISIBLE
            binding.colorPreview.setImageDrawable(
                OverlayControlIcons.tintedIcon(
                    this,
                    id,
                    body,
                    face,
                    overrides,
                    OverlayControlIcons.EDITOR_PREVIEW_ICON_DP
                )
            )
        }
        binding.redLabel.text = getString(R.string.red) + "  " + Color.red(color)
        binding.greenLabel.text = getString(R.string.green) + "  " + Color.green(color)
        binding.blueLabel.text = getString(R.string.blue) + "  " + Color.blue(color)
        if (pushSliders) {
            suppress = true
            binding.redSlider.value = Color.red(color).toFloat()
            binding.greenSlider.value = Color.green(color).toFloat()
            binding.blueSlider.value = Color.blue(color).toFloat()
            suppress = false
        }
        if (updateHex && !binding.hexInput.hasFocus()) {
            suppress = true
            binding.hexInput.setText(String.format(Locale.US, "%06X", color and 0xFFFFFF))
            suppress = false
        }
    }

    private fun promptSavePreset() {
        val editor = DialogEditTextBinding.inflate(layoutInflater)
        editor.editText.inputType = InputType.TYPE_CLASS_TEXT
        editor.editText.hint = getString(R.string.overlay_preset_name)
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.overlay_preset_save)
            .setView(editor.root)
            .setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                val name = editor.editText.text?.toString()?.trim().orEmpty()
                if (!isPresetNameValid(name)) {
                    Toast.makeText(this, R.string.invalid_profile_name, Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }
                val builtin = OverlayColors.matchingPalette(body, overrides, face)
                if (builtin != null) {
                    Toast.makeText(
                        this,
                        getString(R.string.overlay_preset_same_as_builtin, getString(builtin.labelRes)),
                        Toast.LENGTH_LONG
                    ).show()
                    return@setPositiveButton
                }
                if (OverlayPresetLibrary.isReservedName(name, reservedNameKeys())) {
                    Toast.makeText(this, R.string.overlay_preset_name_taken, Toast.LENGTH_LONG).show()
                    return@setPositiveButton
                }
                confirmOverwriteSavedPresetIfNeeded(name) {
                    savedPresets = OverlayPresetLibrary.save(name, body, face, overrides)
                    commitColors()
                    Snackbar.make(
                        binding.overlayColorRoot,
                        getString(R.string.overlay_preset_saved, name),
                        Snackbar.LENGTH_SHORT
                    ).show()
                    if (editingId == null) {
                        showTemplate()
                    }
                }
            }
            .show()
    }

    private fun confirmOverwriteSavedPresetIfNeeded(name: String, onSave: () -> Unit) {
        if (OverlayPresetLibrary.findSavedByExactName(name) == null) {
            onSave()
            return
        }
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.overlay_preset_overwrite)
            .setMessage(getString(R.string.overlay_preset_overwrite_message, name))
            .setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(R.string.overlay_preset_overwrite_confirm) { _, _ -> onSave() }
            .show()
    }

    private fun isPresetNameValid(name: String): Boolean {
        if (name.isEmpty() || name.length > 40) {
            return false
        }
        return name.none { it == '/' || it == '\\' || it == '\n' || it == '\r' }
    }

    private fun confirmDelete(name: String) {
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.overlay_preset_delete)
            .setMessage(getString(R.string.overlay_preset_delete_message, name))
            .setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(R.string.delete) { _, _ ->
                savedPresets = OverlayPresetLibrary.delete(name)
                showPresets()
            }
            .show()
    }

    private fun reloadColorsFromStorage() {
        body = OverlayColors.loadBody()
        overrides.clear()
        overrides.putAll(OverlayColors.loadOverrides())
        face = OverlayColors.loadFace()
        savedPresets = OverlayPresetLibrary.load()
        OverlayPresetThumbnailCache.evictAll()
        if (editingId != null) {
            showEditor(editingId!!)
            return
        }
        when (destination) {
            Destination.PRESETS -> showPresets()
            Destination.TEMPLATE -> showTemplate()
            Destination.HUB -> { /* unchanged */ }
        }
    }

    private fun dismissColorEditorIme() {
        binding.hexInput.clearFocus()
        binding.overlayColorRoot.requestFocus()
        val imm = getSystemService(InputMethodManager::class.java)
        imm?.hideSoftInputFromWindow(binding.overlayColorRoot.windowToken, 0)
    }

    private fun onNavigateBack() {
        dismissColorEditorIme()
        if (editingId != null) {
            showTemplate()
            return
        }
        when (destination) {
            Destination.PRESETS, Destination.TEMPLATE -> {
                if (entrySkipsHub) {
                    finish()
                } else {
                    showHub()
                }
            }
            Destination.HUB -> finish()
        }
    }

    private sealed class Row {
        data class Header(val titleRes: Int) : Row()
        data class PresetDesign(
            val title: String,
            val body: Int,
            val face: String,
            val controlOverrides: Map<String, Int>,
            val onClick: () -> Unit,
            val onLongClick: (() -> Unit)? = null
        ) : Row()
        data class Setting(
            val title: String,
            val description: String,
            val swatch: GradientDrawable? = null,
            val showChevron: Boolean = false,
            val onClick: () -> Unit,
            val onLongClick: (() -> Unit)? = null
        ) : Row()
        data class Control(
            val controlId: String,
            val title: String,
            val subtitle: String,
            val color: Int,
            val onClick: () -> Unit
        ) : Row()
    }

    private inner class ColorAdapter : RecyclerView.Adapter<RecyclerView.ViewHolder>() {
        private val rows = mutableListOf<Row>()
        /** 0 = hub/template (linear); 1–2 = presets grid width in landscape. */
        var presetGridColumns = 0

        val spanSizeLookup = object : GridLayoutManager.SpanSizeLookup() {
            override fun getSpanSize(position: Int): Int {
                val row = rows.getOrNull(position)
                if (presetGridColumns > 1) {
                    return when (row) {
                        is Row.PresetDesign -> 1
                        else -> presetGridColumns
                    }
                }
                return when (row) {
                    is Row.PresetDesign -> 1
                    else -> {
                        val grid = binding.listOverlayColor.layoutManager as? GridLayoutManager
                        grid?.spanCount ?: 1
                    }
                }
            }
        }

        fun currentRows(): List<Row> = rows.toList()

        fun submit(next: List<Row>) {
            rows.clear()
            rows.addAll(next)
            notifyDataSetChanged()
        }

        override fun getItemViewType(position: Int): Int = when (rows[position]) {
            is Row.Header -> 0
            is Row.PresetDesign -> 1
            is Row.Setting -> 2
            is Row.Control -> 3
        }

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RecyclerView.ViewHolder {
            val inflater = LayoutInflater.from(parent.context)
            return when (viewType) {
                0 -> HeaderHolder(ListItemSettingsHeaderBinding.inflate(inflater, parent, false))
                1 -> PresetDesignHolder(ListItemOverlayPresetBinding.inflate(inflater, parent, false))
                2 -> SettingHolder(ListItemSettingBinding.inflate(inflater, parent, false))
                else -> ControlHolder(ListItemSettingBinding.inflate(inflater, parent, false))
            }
        }

        override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
            when (val row = rows[position]) {
                is Row.Header -> (holder as HeaderHolder).bind(row)
                is Row.PresetDesign -> (holder as PresetDesignHolder).bind(row)
                is Row.Setting -> (holder as SettingHolder).bind(row)
                is Row.Control -> (holder as ControlHolder).bind(row)
            }
        }

        override fun getItemCount(): Int = rows.size
    }

    private class HeaderHolder(
        private val binding: ListItemSettingsHeaderBinding
    ) : RecyclerView.ViewHolder(binding.root) {
        fun bind(row: Row.Header) {
            binding.textHeaderName.setText(row.titleRes)
        }
    }

    private inner class PresetDesignHolder(
        private val binding: ListItemOverlayPresetBinding
    ) : RecyclerView.ViewHolder(binding.root) {
        fun bind(row: Row.PresetDesign) {
            binding.presetTitle.text = row.title
            binding.presetPreview.setPreset(row.body, row.face, row.controlOverrides)
            val apply = { row.onClick() }
            binding.root.setOnClickListener { apply() }
            binding.presetPreview.setOnClickListener { apply() }
            binding.root.setOnLongClickListener {
                row.onLongClick?.invoke()
                row.onLongClick != null
            }
            binding.presetPreview.setOnLongClickListener {
                row.onLongClick?.invoke()
                row.onLongClick != null
            }
        }
    }

    private inner class SettingHolder(
        private val binding: ListItemSettingBinding
    ) : RecyclerView.ViewHolder(binding.root) {
        fun bind(row: Row.Setting) {
            binding.textSettingName.text = row.title
            binding.textSettingDescription.text = row.description
            binding.textSettingDescription.setVisible(row.description.isNotEmpty())
            if (row.showChevron) {
                binding.textSettingValue.setVisible(true)
                binding.textSettingValue.text = ""
                binding.textSettingValue.setCompoundDrawablesRelativeWithIntrinsicBounds(
                    0,
                    0,
                    R.drawable.ic_arrow_forward,
                    0
                )
            } else {
                binding.textSettingValue.setVisible(false)
                binding.textSettingValue.setCompoundDrawablesRelativeWithIntrinsicBounds(0, 0, 0, 0)
            }
            binding.buttonClear.setVisible(false)
            if (row.swatch != null) {
                binding.icon.setVisible(true)
                binding.icon.background = row.swatch
            } else {
                binding.icon.setVisible(false)
            }
            binding.root.setOnClickListener { row.onClick() }
            binding.root.setOnLongClickListener {
                row.onLongClick?.invoke()
                row.onLongClick != null
            }
        }
    }

    private inner class ControlHolder(
        private val binding: ListItemSettingBinding
    ) : RecyclerView.ViewHolder(binding.root) {
        fun bind(row: Row.Control) {
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
                body,
                face,
                overrides,
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
                    (1f * resources.displayMetrics.density).toInt().coerceAtLeast(1),
                    0x33000000
                )
            }
    }

    companion object {
        private const val STATE_BODY = "overlay_color_body"
        private const val STATE_MAP = "overlay_color_map"
        private const val STATE_FACE = "overlay_color_face"
        private const val STATE_EDITING = "overlay_color_editing"
        private const val STATE_DESTINATION = "overlay_color_destination"
        private const val STATE_ENTRY_SKIPS_HUB = "overlay_color_entry_skips_hub"
        private const val EXTRA_DESTINATION = "overlay_color_start_destination"

        fun createIntent(context: Context): Intent = intentWithDestination(context, Destination.HUB)

        fun createTemplateIntent(context: Context): Intent =
            intentWithDestination(context, Destination.TEMPLATE)

        fun createPresetsIntent(context: Context): Intent =
            intentWithDestination(context, Destination.PRESETS)

        private fun intentWithDestination(context: Context, destination: Destination): Intent =
            Intent(context, OverlayColorActivity::class.java).apply {
                putExtra(EXTRA_DESTINATION, destination.ordinal)
            }
    }
}

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.overlay

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.graphics.Color
import android.net.Uri
import android.os.Bundle
import android.text.Editable
import android.text.InputType
import android.text.TextWatcher
import android.view.View
import android.view.inputmethod.InputMethodManager
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import androidx.transition.Transition
import androidx.transition.TransitionManager
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.slider.Slider
import com.google.android.material.snackbar.Snackbar
import com.google.android.material.transition.MaterialSharedAxis
import java.util.Locale
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.YuzuApplication
import org.yuzu.yuzu_emu.databinding.ActivityOverlayColorBinding
import org.yuzu.yuzu_emu.databinding.DialogEditTextBinding
import org.yuzu.yuzu_emu.utils.ThemeHelper
import org.yuzu.yuzu_emu.utils.ViewUtils.updateMargins

class OverlayColorActivity : AppCompatActivity() {
    private lateinit var binding: ActivityOverlayColorBinding
    private val overrides = linkedMapOf<String, Int>()
    private val adapter = OverlayColorListAdapter {
        OverlayColorState(body, face, overrides)
    }

    private var body = OverlayColors.ORIGINAL_COLOR
    private var face = OverlayColors.FACE_LETTERS
    private var destination = Destination.HUB
    private var editingId: String? = null
    private var suppress = false
    private var savedPresets = emptyList<OverlayPresetLibrary.Preset>()
    private lateinit var colorConfigTransfer: OverlayColorConfigTransfer
    private var lastPreviewLayoutSignature: String? = null
    /** Opened from settings straight to template/presets; back leaves the activity instead of the hub. */
    private var entrySkipsHub = false
    private var skipScreenTransition = false
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
        val layoutSignature = OverlayPresetThumbnailCache.layoutSignature()
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
        skipScreenTransition = savedInstanceState == null
        if (editingId != null) {
            showEditor(editingId!!)
        } else {
            navigateTo(destination)
        }
        skipScreenTransition = false
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putInt(STATE_BODY, body)
        outState.putString(STATE_MAP, OverlayColors.serialize(overrides))
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
        runSharedAxisTransition(backward = false) {
            destination = target
            when (target) {
                Destination.HUB -> showHub()
                Destination.PRESETS -> showPresets()
                Destination.TEMPLATE -> showTemplate()
            }
        }
    }

    /** Same axis/direction as [SettingsFragment] enter and return transitions. */
    private fun sharedAxisTransition(backward: Boolean): Transition =
        MaterialSharedAxis(MaterialSharedAxis.X, !backward).apply {
            addTarget(binding.appbarOverlayColor)
            addTarget(binding.listOverlayColor)
            addTarget(binding.editorScroll)
        }

    private fun runSharedAxisTransition(backward: Boolean, update: () -> Unit) {
        if (skipScreenTransition || isFinishing || isDestroyed) {
            update()
            return
        }
        fun applyTransition() {
            if (isFinishing || isDestroyed) {
                update()
                return
            }
            binding.listOverlayColor.suppressLayout(true)
            TransitionManager.beginDelayedTransition(
                binding.overlayColorRoot,
                sharedAxisTransition(backward)
            )
            update()
            binding.listOverlayColor.post {
                binding.listOverlayColor.suppressLayout(false)
            }
        }
        if (binding.overlayColorRoot.isLaidOut) {
            applyTransition()
        } else {
            binding.overlayColorRoot.post { applyTransition() }
        }
    }

    private fun showListScreen(titleRes: Int, rows: List<OverlayColorRow>) {
        dismissColorEditorIme()
        editingId = null
        binding.editorScroll.visibility = View.GONE
        binding.listOverlayColor.visibility = View.VISIBLE
        binding.toolbarOverlayColor.setTitle(titleRes)
        binding.listOverlayColor.layoutManager =
            LinearLayoutManager(this, LinearLayoutManager.VERTICAL, false)
        adapter.submit(rows)
    }

    private fun showHub() {
        destination = Destination.HUB
        showListScreen(
            R.string.overlay_color,
            listOf(
                OverlayColorRow.Header(R.string.overlay_color_section_create_use),
                OverlayColorRow.Setting(
                    title = getString(R.string.overlay_color_presets),
                    description = getString(R.string.overlay_color_presets_summary),
                    showChevron = true,
                    onClick = { navigateTo(Destination.PRESETS) }
                ),
                OverlayColorRow.Setting(
                    title = getString(R.string.overlay_color_create_template),
                    description = getString(R.string.overlay_color_create_template_summary),
                    showChevron = true,
                    onClick = { navigateTo(Destination.TEMPLATE) }
                ),
                OverlayColorRow.Header(R.string.overlay_color_section_manage_files),
                OverlayColorRow.Setting(
                    title = getString(R.string.overlay_import_color_config),
                    description = getString(R.string.overlay_import_color_config_summary),
                    iconRes = R.drawable.ic_import,
                    onClick = { colorConfigTransfer.launchImport { importLauncher.launch(it) } }
                ),
                OverlayColorRow.Setting(
                    title = getString(R.string.overlay_export_color_config),
                    description = getString(R.string.overlay_export_color_config_summary),
                    iconRes = R.drawable.ic_export,
                    onClick = { colorConfigTransfer.launchExport { exportLauncher.launch(it) } }
                )
            )
        )
    }

    private fun showPresets() {
        destination = Destination.PRESETS
        savedPresets = OverlayPresetLibrary.load()
        val rows = mutableListOf<OverlayColorRow>()
        rows += OverlayColorRow.Header(R.string.overlay_color_builtin_header)
        OverlayColors.palettes(this).forEach { palette ->
            val title = getString(palette.labelRes)
            rows += OverlayColorRow.PresetDesign(
                title = title,
                body = palette.body,
                face = palette.face,
                controlOverrides = palette.overrides,
                onClick = {
                    applyPalette(
                        palette.body,
                        palette.overrides,
                        palette.face,
                        getString(palette.labelRes),
                        followAppTheme = palette.key == OverlayColors.PRESET_APP_THEME
                    )
                }
            )
        }
        val savedRows = savedPresets.filter { preset ->
            OverlayColors.matchingPalette(preset.body, preset.overrides, preset.face) == null
        }
        if (savedRows.isNotEmpty()) {
            rows += OverlayColorRow.Header(R.string.overlay_preset_saved_header)
            savedRows.forEach { preset ->
                rows += OverlayColorRow.PresetDesign(
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
        val rows = mutableListOf<OverlayColorRow>()
        rows += OverlayColorRow.Setting(
            title = getString(R.string.overlay_preset_save),
            description = getString(R.string.overlay_color_save_preset_hint),
            showChevron = true,
            onClick = { promptSavePreset() }
        )
        rows += OverlayColorRow.Header(R.string.overlay_color_buttons)
        rows += controlRow(OverlayColors.BASE, R.string.overlay_color_base, body, usingBaseColor = false)
        OverlayColors.targets.forEach { target ->
            val custom = overrides.containsKey(target.id)
            val color = OverlayColors.resolved(body, overrides, target.id)
            rows += controlRow(target.id, OverlayColors.labelFor(target.id), color, !custom)
        }
        showListScreen(R.string.overlay_color_create_template, rows)
    }

    private fun showEditor(id: String) {
        val enteringEditor = editingId == null
        val run = {
            editingId = id
            binding.listOverlayColor.visibility = View.GONE
            binding.editorScroll.visibility = View.VISIBLE
            binding.toolbarOverlayColor.setTitle(
                if (id == OverlayColors.BASE) {
                    R.string.overlay_color_base
                } else {
                    OverlayColors.labelFor(id)
                }
            )
            renderEditor(pushSliders = true, updateHex = true)
        }
        if (enteringEditor) {
            runSharedAxisTransition(backward = false, run)
        } else {
            run()
        }
    }

    private fun controlRow(id: String, labelRes: Int, color: Int, usingBaseColor: Boolean): OverlayColorRow.Control {
        val subtitle = if (id == OverlayColors.BASE) {
            OverlayColors.hexString(color)
        } else if (usingBaseColor) {
            getString(R.string.overlay_color_using_base)
        } else {
            OverlayColors.hexString(color)
        }
        return OverlayColorRow.Control(
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
        appliedName: String,
        followAppTheme: Boolean = false
    ) {
        face = newFace
        if (followAppTheme) {
            OverlayColors.writeFollowAppTheme()
            body = OverlayColors.loadBody()
            overrides.clear()
            overrides.putAll(OverlayColors.loadOverrides())
        } else {
            body = newBody or 0xFF000000.toInt()
            overrides.clear()
            overrides.putAll(newOverrides)
            OverlayColors.write(body, overrides, face)
        }
        setResult(Activity.RESULT_OK)
        val message = getString(R.string.overlay_preset_applied, appliedName)
        Snackbar.make(binding.overlayColorRoot, message, Snackbar.LENGTH_SHORT).show()
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
                if (OverlayPresetLibrary.isReservedName(name, OverlayColors.reservedNameKeys(this))) {
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
            runSharedAxisTransition(backward = true) {
                showTemplate()
            }
            return
        }
        when (destination) {
            Destination.PRESETS, Destination.TEMPLATE -> {
                if (entrySkipsHub) {
                    finish()
                } else {
                    runSharedAxisTransition(backward = true) {
                        showHub()
                    }
                }
            }
            Destination.HUB -> finish()
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

        private fun intentWithDestination(context: Context, destination: Destination): Intent =
            Intent(context, OverlayColorActivity::class.java).apply {
                putExtra(EXTRA_DESTINATION, destination.ordinal)
            }
    }
}

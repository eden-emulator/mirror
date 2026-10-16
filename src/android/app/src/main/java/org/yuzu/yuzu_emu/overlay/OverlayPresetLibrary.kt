// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.overlay

import java.io.File
import java.util.Locale
import org.json.JSONArray
import org.json.JSONObject
import org.yuzu.yuzu_emu.YuzuApplication
import org.yuzu.yuzu_emu.overlay.model.OverlayControl
import org.yuzu.yuzu_emu.utils.DirectoryInitialization

/**
 * Named overlay color presets kept on device. Shareable `.edenovl` files (JSON) hold one design each;
 * Import accepts only files named `.edenovl` (JSON inside). On-device library is `config/presets.edenovl` with a `presets` array.
 */
object OverlayPresetLibrary {
    data class Preset(
        val name: String,
        val body: Int,
        val face: String,
        val overrides: Map<String, Int>
    )

    private const val STORAGE_FILE_NAME = "presets.edenovl"
    private const val LEGACY_JSON_FILE_NAME = "overlay_color_presets.json"
    private const val JSON_KEY_BASE_COLOR = "base_color"
    private const val JSON_KEY_SHELL_LEGACY = "shell"

    /**
     * Path under the user data root in export.zip archives.
     * Other Switch frontends only require [config/config.ini]; extra JSON here is ignored safely.
     */
    const val USER_DATA_RELATIVE_PATH = "config/$STORAGE_FILE_NAME"

    /** Exported shareable designs (JSON body, [EXPORT_FILE_EXTENSION] on disk). */
    const val EXPORT_FILE_EXTENSION = "edenovl"
    /** Not application/json — pickers append .json and produce `.edenovl.json` filenames. */
    const val EXPORT_MIME_TYPE = "application/octet-stream"
    const val DEFAULT_EXPORT_FILENAME = "overlay-preset.$EXPORT_FILE_EXTENSION"
    /** Basename for exporting unsaved on-screen colors (becomes `myoverlay.edenovl`). */
    const val LIVE_COLORS_EXPORT_BASENAME = "myoverlay"

    /** Open-document filter; import still validates the [.edenovl] extension. */
    val importMimeTypes: Array<String> = arrayOf("*/*")

    fun exportFilenameForPreset(displayName: String): String {
        var base = displayName.trim()
            .replace(Regex("[\\\\/:*?\"<>|\\n\\r]"), "_")
            .take(40)
        while (base.lowercase(Locale.US).endsWith(".json")) {
            base = base.dropLast(5)
        }
        if (base.lowercase(Locale.US).endsWith(".$EXPORT_FILE_EXTENSION")) {
            return base
        }
        return if (base.isEmpty()) {
            DEFAULT_EXPORT_FILENAME
        } else {
            "$base.$EXPORT_FILE_EXTENSION"
        }
    }

    fun isEdenovlUriName(name: String?): Boolean {
        if (name.isNullOrBlank()) {
            return false
        }
        val lower = name.lowercase(Locale.US)
        if (lower.endsWith(".json")) {
            return false
        }
        return lower.endsWith(".$EXPORT_FILE_EXTENSION")
    }

    private val NAME_ALIASES = mapOf(
        "gcn" to "gamecube",
        "ngc" to "gamecube",
        "gc" to "gamecube",
        "gamecube" to "gamecube",
        "snes" to "snes",
        "supernes" to "snes",
        "supernintendo" to "snes",
        "supernesusa" to "snes",
        "sfc" to "superfamicom",
        "superfamicom" to "superfamicom",
        "nes" to "nes",
        "nintendo" to "nes"
    )

    fun nameKey(name: String): String {
        val normalized = name
            .lowercase(Locale.US)
            .filter { it.isLetterOrDigit() }
        if (normalized.isEmpty()) {
            return ""
        }
        return NAME_ALIASES[normalized] ?: normalized
    }

    fun contentSignature(body: Int, face: String, overrides: Map<String, Int>): String {
        val baseRgb = body and 0xFFFFFF
        val buttons = overrides.entries
            .sortedBy { it.key }
            .joinToString(separator = ",") { (id, color) ->
                "$id=${color and 0xFFFFFF}"
            }
        return "$face|$baseRgb|$buttons"
    }

    fun contentSignature(preset: Preset): String =
        contentSignature(preset.body, preset.face, preset.overrides)

    /** Every preset entry in a shareable file (for the import picker only). */
    fun parseImportFile(raw: String): List<Preset> = parseDocument(raw)

    /** Call before zipping user data so presets live under [USER_DATA_RELATIVE_PATH]. */
    fun prepareForUserDataExport() {
        migrateLegacyStorageIfNeeded()
    }

    /** Call after user data zip import; picks up presets from standard or legacy zip layouts. */
    fun onUserDataImportRestored() {
        val userDir = runCatching {
            if (DirectoryInitialization.areDirectoriesReady) {
                DirectoryInitialization.userDirectory
            } else {
                null
            }
        }.getOrNull() ?: return
        val target = File(userDir, USER_DATA_RELATIVE_PATH)
        val legacyAtUserRoot = File(userDir, LEGACY_JSON_FILE_NAME)
        val legacyInConfig = File(userDir, "config/$LEGACY_JSON_FILE_NAME")
        if (!target.exists()) {
            target.parentFile?.mkdirs()
            when {
                legacyInConfig.isFile -> legacyInConfig.copyTo(target, overwrite = true)
                legacyAtUserRoot.isFile -> legacyAtUserRoot.copyTo(target, overwrite = true)
            }
        } else {
            val extra = mutableListOf<Preset>()
            if (legacyInConfig.isFile) {
                extra += loadFromFile(legacyInConfig)
            }
            if (legacyAtUserRoot.isFile) {
                extra += loadFromFile(legacyAtUserRoot)
            }
            if (extra.isNotEmpty()) {
                writePresetsToDisk(mergePresets(load(), extra))
            }
        }
        legacyInConfig.delete()
        legacyAtUserRoot.delete()
        migrateLegacyStorageIfNeeded()
    }

    fun load(): List<Preset> {
        migrateLegacyStorageIfNeeded()
        val file = storageFile()
        if (!file.exists()) {
            return emptyList()
        }
        return try {
            parseDocument(file.readText())
        } catch (_: Exception) {
            val backup = backupFile(file)
            if (backup.exists()) {
                try {
                    return parseDocument(backup.readText())
                } catch (_: Exception) {
                }
            }
            emptyList()
        }
    }

    fun save(name: String, body: Int, face: String, overrides: Map<String, Int>): List<Preset> {
        val trimmed = name.trim().take(40)
        require(trimmed.isNotEmpty())
        val signature = contentSignature(body, face, overrides)
        val next = load()
            .filterNot {
                namesMatchExactly(it.name, trimmed) || contentSignature(it) == signature
            } +
            Preset(trimmed, body or 0xFF000000.toInt(), OverlayColors.FACE_LETTERS, overrides.toMap())
        writeStorage(next)
        return next
    }

    fun findSavedByExactName(name: String): Preset? {
        val trimmed = name.trim()
        if (trimmed.isEmpty()) {
            return null
        }
        return load().firstOrNull { namesMatchExactly(it.name, trimmed) }
    }

    fun isReservedName(candidate: String, reservedKeys: Set<String>): Boolean {
        val key = nameKey(candidate)
        return key.isEmpty() || reservedKeys.contains(key)
    }

    fun delete(name: String): List<Preset> {
        val trimmed = name.trim()
        val next = load().filterNot { namesMatchExactly(it.name, trimmed) }
        writeStorage(next)
        return next
    }

    private fun namesMatchExactly(stored: String, candidate: String): Boolean =
        stored.equals(candidate.trim(), ignoreCase = true)

    /** One design per file for sharing (import/export picker). */
    fun exportJson(presets: List<Preset>): String {
        require(presets.size == 1) { "Export one design at a time" }
        return presetObject(presets.first()).let { obj ->
            JSONObject()
                .put("version", 1)
                .put("name", obj.getString("name"))
                .put("face", obj.getString("face"))
                .put(JSON_KEY_BASE_COLOR, obj.getString(JSON_KEY_BASE_COLOR))
                .put("buttons", obj.getJSONObject("buttons"))
                .toString(2)
        }
    }

    /** Adds or updates one saved preset after the user picks a single entry from an import file. */
    fun importSelected(preset: Preset): List<Preset> {
        val stored = preset.copy(
            name = preset.name.trim().take(40),
            body = preset.body or 0xFF000000.toInt()
        )
        return save(stored.name, stored.body, stored.face, stored.overrides)
    }

    private fun writeStorage(presets: List<Preset>) {
        migrateLegacyStorageIfNeeded()
        writePresetsToDisk(presets)
    }

    private fun writePresetsToDisk(presets: List<Preset>) {
        val target = storageFile()
        target.parentFile?.mkdirs()
        val payload = JSONObject()
            .put("version", 1)
            .put("presets", JSONArray().also { array ->
                presets.forEach { preset -> array.put(presetObject(preset)) }
            })
            .toString(2)
        val temp = File(target.parentFile, "${target.name}.tmp")
        temp.writeText(payload)
        if (target.exists()) {
            target.copyTo(backupFile(target), overwrite = true)
        }
        if (!temp.renameTo(target)) {
            target.writeText(payload)
            temp.delete()
        }
    }

    private fun presetObject(preset: Preset): JSONObject {
        val buttons = JSONObject()
        preset.overrides.forEach { (id, color) ->
            buttons.put(id, OverlayColors.hexString(color))
        }
        return JSONObject()
            .put("name", preset.name)
            .put("face", preset.face)
            .put(JSON_KEY_BASE_COLOR, OverlayColors.hexString(preset.body))
            .put("buttons", buttons)
    }

    private fun parseBaseColorHex(obj: JSONObject): String? {
        val base = obj.optString(JSON_KEY_BASE_COLOR).trim()
        if (base.isNotEmpty()) {
            return base
        }
        val legacy = obj.optString(JSON_KEY_SHELL_LEGACY).trim()
        return legacy.ifEmpty { null }
    }

    /** Same tree as [org.yuzu.yuzu_emu.features.settings.utils.SettingsFile] (survives app updates). */
    private fun storageFile(): File {
        val userDir = runCatching {
            if (DirectoryInitialization.areDirectoriesReady) {
                DirectoryInitialization.userDirectory
            } else {
                null
            }
        }.getOrNull()
        if (!userDir.isNullOrBlank()) {
            return File(userDir, USER_DATA_RELATIVE_PATH)
        }
        return legacyStorageFile()
    }

    private fun legacyStorageFile(): File =
        File(YuzuApplication.appContext.filesDir, LEGACY_JSON_FILE_NAME)

    private fun backupFile(primary: File): File =
        File(primary.parentFile, "${primary.name}.bak")

    private fun migrateLegacyStorageIfNeeded() {
        val target = storageFile()
        target.parentFile?.mkdirs()

        val legacyInternal = legacyStorageFile()
        if (legacyInternal.exists() && legacyInternal.absolutePath != target.absolutePath) {
            if (!target.exists()) {
                legacyInternal.copyTo(target, overwrite = true)
            } else {
                writePresetsToDisk(mergePresets(loadFromFile(target), loadFromFile(legacyInternal)))
            }
            legacyInternal.renameTo(File(legacyInternal.parent, "$LEGACY_JSON_FILE_NAME.legacy"))
        }

        val userDir = runCatching {
            if (DirectoryInitialization.areDirectoriesReady) {
                DirectoryInitialization.userDirectory
            } else {
                null
            }
        }.getOrNull()
        if (userDir.isNullOrBlank()) {
            return
        }
        val legacyConfig = File(userDir, "config/$LEGACY_JSON_FILE_NAME")
        if (!legacyConfig.exists()) {
            return
        }
        if (!target.exists()) {
            legacyConfig.copyTo(target, overwrite = true)
        } else {
            writePresetsToDisk(mergePresets(loadFromFile(target), loadFromFile(legacyConfig)))
        }
        legacyConfig.renameTo(File(legacyConfig.parent, "$LEGACY_JSON_FILE_NAME.legacy"))
    }

    private fun loadFromFile(file: File): List<Preset> {
        if (!file.exists()) {
            return emptyList()
        }
        return try {
            parseDocument(file.readText())
        } catch (_: Exception) {
            emptyList()
        }
    }

    private fun mergePresets(primary: List<Preset>, extra: List<Preset>): List<Preset> {
        if (extra.isEmpty()) {
            return primary
        }
        val merged = primary.toMutableList()
        for (preset in extra) {
            val duplicateName = merged.any { namesMatchExactly(it.name, preset.name) }
            val duplicateContent = merged.any { contentSignature(it) == contentSignature(preset) }
            if (!duplicateName && !duplicateContent) {
                merged += preset
            }
        }
        return merged
    }

    private fun parseDocument(raw: String): List<Preset> {
        val trimmed = raw.trim()
        if (trimmed.isEmpty()) {
            return emptyList()
        }
        return when {
            trimmed.startsWith("[") -> parseArray(JSONArray(trimmed))
            trimmed.startsWith("{") -> {
                val obj = JSONObject(trimmed)
                when {
                    obj.has("presets") -> parseArray(obj.getJSONArray("presets"))
                    obj.has(JSON_KEY_BASE_COLOR) ||
                        obj.has(JSON_KEY_SHELL_LEGACY) ||
                        obj.has("buttons") -> listOf(parsePreset(obj, "Imported"))
                    else -> emptyList()
                }
            }
            else -> emptyList()
        }
    }

    private fun parseArray(array: JSONArray): List<Preset> {
        val presets = mutableListOf<Preset>()
        for (index in 0 until array.length()) {
            val obj = array.optJSONObject(index) ?: continue
            presets += parsePreset(obj, "Imported ${index + 1}")
        }
        return presets
    }

    private fun parsePreset(obj: JSONObject, fallbackName: String): Preset {
        val name = obj.optString("name").trim().ifEmpty { fallbackName }.take(40)
        val body = OverlayColors.parseHex(parseBaseColorHex(obj).orEmpty())
            ?: OverlayColors.DEFAULT_COLOR
        val face = OverlayColors.FACE_LETTERS
        val overrides = linkedMapOf<String, Int>()
        val buttons = obj.optJSONObject("buttons")
        if (buttons != null) {
            val keys = buttons.keys()
            while (keys.hasNext()) {
                val id = keys.next()
                if (OverlayControl.from(id) == null) {
                    continue
                }
                val color = OverlayColors.parseHex(buttons.optString(id)) ?: continue
                overrides[id] = color
            }
        }
        return Preset(name, body, face, overrides)
    }

}

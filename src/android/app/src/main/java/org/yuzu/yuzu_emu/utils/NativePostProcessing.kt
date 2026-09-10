// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import org.json.JSONArray
import org.json.JSONObject

object NativePostProcessing {
    const val KIND_BOOL = 0
    const val KIND_INT = 1
    const val KIND_FLOAT = 2

    const val UI_HIDDEN = 0
    const val UI_SLIDER = 1
    const val UI_DRAG = 2
    const val UI_COMBO = 3
    const val UI_RADIO = 4
    const val UI_CHECKBOX = 5
    const val UI_COLOR = 6
    const val UI_INPUT_BOX = 7

    external fun getCatalogJson(): String

    external fun getChainJson(): String

    external fun append(file: String, technique: String)

    external fun replace(index: Int, file: String, technique: String)

    external fun remove(index: Int)

    external fun move(index: Int, delta: Int)

    external fun resetValues(index: Int)

    external fun getValue(index: Int, uniform: String, component: Int): Float

    external fun hasValue(index: Int, uniform: String): Boolean

    external fun setValue(index: Int, uniform: String, component: Int, value: Float)

    external fun store()

    external fun reload()

    external fun clearChain()

    external fun getPresetsJson(): String

    external fun getActivePreset(): String

    external fun isPresetModified(): Boolean

    external fun applyPreset(name: String): Boolean

    external fun savePreset(name: String, description: String): Boolean

    external fun deletePreset(name: String): Boolean

    external fun clearPreset()

    external fun isEnabled(): Boolean

    external fun setEnabled(enabled: Boolean)

    external fun getPresetDirectory(): String

    fun persist() {
        val perGame = NativeConfig.isPerGameConfigLoaded()
        store()
        if (perGame) {
            NativeConfig.savePerGameConfig()
        } else {
            NativeConfig.saveGlobalConfig()
        }
    }


    external fun getShaderDirectory(): String

    data class Uniform(
        val name: String,
        val label: String,
        val tooltip: String,
        val category: String,
        val kind: Int,
        val uiType: Int,
        val components: Int,
        val min: Float,
        val max: Float,
        val step: Float,
        val items: List<String>,
        val defaults: List<Float>
    ) {
        val steps: Int
            get() {
                val span = max - min
                if (step <= 0f) {
                    return 1
                }
                val count = Math.round(span / step)
                if (count < 1) {
                    return 1
                }
                return count
            }

        fun describe(position: Int): String {
            val value = min + position * step
            if (kind == NativePostProcessing.KIND_FLOAT) {
                return String.format("%.3f", value)
            }
            return Math.round(value).toString()
        }

        fun defaultAt(component: Int): Float {
            if (component < defaults.size) {
                return defaults[component]
            }
            return 0f
        }
    }

    data class Effect(
        val file: String,
        val name: String,
        val label: String,
        val description: String,
        val error: String,
        val techniques: List<String>,
        val uniforms: List<Uniform>
    ) {
        val valid: Boolean
            get() = error.isEmpty() && techniques.isNotEmpty()
    }

    data class ChainEntry(val file: String, val technique: String)

    data class Preset(
        val name: String,
        val description: String,
        val bundled: Boolean
    )

    fun presets(): List<Preset> {
        val out = mutableListOf<Preset>()
        val array = JSONArray(getPresetsJson())
        for (i in 0 until array.length()) {
            val obj = array.getJSONObject(i)
            out.add(
                Preset(
                    name = obj.optString("name"),
                    description = obj.optString("description"),
                    bundled = obj.optBoolean("bundled")
                )
            )
        }
        return out
    }

    fun catalog(): List<Effect> {
        val out = mutableListOf<Effect>()
        val array = JSONArray(getCatalogJson())
        for (i in 0 until array.length()) {
            val obj = array.getJSONObject(i)
            out.add(
                Effect(
                    file = obj.optString("file"),
                    name = obj.optString("name"),
                    label = obj.optString("label"),
                    description = obj.optString("description"),
                    error = obj.optString("error"),
                    techniques = obj.optJSONArray("techniques").toStringList(),
                    uniforms = obj.optJSONArray("uniforms").toUniformList()
                )
            )
        }
        return out
    }

    fun chain(): List<ChainEntry> {
        val out = mutableListOf<ChainEntry>()
        val array = JSONArray(getChainJson())
        for (i in 0 until array.length()) {
            val obj = array.getJSONObject(i)
            out.add(ChainEntry(obj.optString("file"), obj.optString("technique")))
        }
        return out
    }

    fun findEffect(file: String): Effect? = catalog().firstOrNull { it.file == file }

    private fun JSONArray?.toStringList(): List<String> {
        if (this == null) {
            return emptyList()
        }
        val out = mutableListOf<String>()
        for (i in 0 until length()) {
            out.add(optString(i))
        }
        return out
    }

    private fun JSONArray?.toFloatList(): List<Float> {
        if (this == null) {
            return emptyList()
        }
        val out = mutableListOf<Float>()
        for (i in 0 until length()) {
            out.add(optDouble(i, 0.0).toFloat())
        }
        return out
    }

    private fun JSONArray?.toUniformList(): List<Uniform> {
        if (this == null) {
            return emptyList()
        }
        val out = mutableListOf<Uniform>()
        for (i in 0 until length()) {
            val obj: JSONObject = optJSONObject(i) ?: continue
            out.add(
                Uniform(
                    name = obj.optString("name"),
                    label = obj.optString("label"),
                    tooltip = obj.optString("tooltip"),
                    category = obj.optString("category"),
                    kind = obj.optInt("kind", KIND_FLOAT),
                    uiType = obj.optInt("uiType", UI_HIDDEN),
                    components = obj.optInt("components", 1),
                    min = obj.optDouble("min", 0.0).toFloat(),
                    max = obj.optDouble("max", 1.0).toFloat(),
                    step = obj.optDouble("step", 0.01).toFloat(),
                    items = obj.optJSONArray("items").toStringList(),
                    defaults = obj.optJSONArray("defaults").toFloatList()
                )
            )
        }
        return out
    }
}

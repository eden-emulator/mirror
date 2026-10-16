// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.overlay

import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import java.util.Locale
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.features.settings.model.IntSetting
import org.yuzu.yuzu_emu.features.settings.model.StringSetting
import org.yuzu.yuzu_emu.overlay.model.OverlayControl
import org.yuzu.yuzu_emu.overlay.model.OverlayLayout
import org.yuzu.yuzu_emu.utils.NativeConfig

/**
 * Base color tint ([IntSetting.OVERLAY_COLOR]) plus optional per-button colors
 * stored as `id=AARRGGBB` pairs in [StringSetting.OVERLAY_CONTROL_COLORS].
 */
object OverlayColors {
    const val DEFAULT_COLOR = 0xFFFFFFFF.toInt()
    const val BASE = "base"
    const val FACE_LETTERS = "letters"

    private const val EDEN_PINK = 0xFFFF4D9A.toInt()
    private const val EDEN_PURPLE = 0xFF7B4DFF.toInt()
    private const val EDEN_BLUE = 0xFF9FD8F5.toInt()
    private const val JOY_RED = 0xFFFF3C28.toInt()
    private const val JOY_BLUE = 0xFF0AB9E6.toInt()

    /** Cool off-white with a blue cast, close to the Super Famicom shell. */
    private const val SFC_BODY = 0xFFE4E8F0.toInt()
    private const val RAL_3020 = 0xFFCC0605.toInt()
    private const val RAL_1003 = 0xFFF9A800.toInt()
    private const val RAL_6029 = 0xFF007243.toInt()
    private const val SFC_LOGO_BLUE = 0xFF2E45B5.toInt()

    /** US Super NES preset. */
    private const val SNES_BODY = 0xFF8F7FA8.toInt()
    private const val SNES_BTN_AB = 0xFF511D93.toInt()
    private const val SNES_BTN_XY = 0xFF7B4BB5.toInt()

    /** GameCube preset (matches bundled export). */
    private const val GC_BODY = 0xFF4A3D7A.toInt()
    private const val GC_GREEN = 0xFF6DBF48.toInt()
    private const val GC_RED = 0xFFE03C31.toInt()
    private const val GC_FACE_GRAY = 0xFFC0C0C8.toInt()
    private const val GC_STICK_CLICK = 0xFF2E2A32.toInt()
    private const val GC_C_STICK = 0xFFFFD200.toInt()

    private const val FAMICOM_BODY = 0xFFF3E2C4.toInt()
    private const val FAMICOM_RED = 0xFFE10600.toInt()
    private const val FAMICOM_DARK = 0xFFA10600.toInt()

    /** NES-style red shell with gray system controls. */
    private const val NES_BODY = 0xFFFF0000.toInt()
    private const val NES_GRAY = 0xFF999999.toInt()
    private const val NES_DARK = 0xFF555555.toInt()
    private const val NES_DPAD = 0xFFD10000.toInt()

    enum class PreviewKind {
        SOLID,
        GRADIENT,
        BANDS
    }

    data class Preview(
        val kind: PreviewKind,
        val colors: IntArray,
        val orientation: GradientDrawable.Orientation = GradientDrawable.Orientation.TOP_BOTTOM
    )

    data class Palette(
        val key: String,
        val labelRes: Int,
        val body: Int,
        val overrides: Map<String, Int>,
        val preview: Preview,
        val face: String = FACE_LETTERS
    )

    data class Target(val id: String, val labelRes: Int)

    val targets: List<Target> = listOf(
        Target(OverlayControl.BUTTON_A.id, R.string.button_a),
        Target(OverlayControl.BUTTON_B.id, R.string.button_b),
        Target(OverlayControl.BUTTON_X.id, R.string.button_x),
        Target(OverlayControl.BUTTON_Y.id, R.string.button_y),
        Target(OverlayControl.COMBINED_DPAD.id, R.string.dpad),
        Target(OverlayControl.BUTTON_L.id, R.string.button_l),
        Target(OverlayControl.BUTTON_R.id, R.string.button_r),
        Target(OverlayControl.BUTTON_ZL.id, R.string.button_zl),
        Target(OverlayControl.BUTTON_ZR.id, R.string.button_zr),
        Target(OverlayControl.STICK_L.id, R.string.overlay_color_left_stick),
        Target(OverlayControl.STICK_R.id, R.string.overlay_color_right_stick),
        Target(OverlayControl.BUTTON_STICK_L.id, R.string.overlay_stick_left_press),
        Target(OverlayControl.BUTTON_STICK_R.id, R.string.overlay_stick_right_press),
        Target(OverlayControl.BUTTON_MINUS.id, R.string.button_minus),
        Target(OverlayControl.BUTTON_PLUS.id, R.string.button_plus),
        Target(OverlayControl.BUTTON_CAPTURE.id, R.string.button_capture),
        Target(OverlayControl.BUTTON_HOME.id, R.string.button_home)
    )

    val palettes: List<Palette> by lazy {
        listOf(
            Palette(
                key = "original",
                labelRes = R.string.overlay_color_default,
                body = DEFAULT_COLOR,
                overrides = emptyMap(),
                preview = Preview(PreviewKind.SOLID, intArrayOf(DEFAULT_COLOR))
            ),
            Palette(
                key = "eden",
                labelRes = R.string.overlay_preset_eden,
                body = EDEN_PURPLE,
                overrides = byHeight(::edenColor),
                preview = Preview(
                    PreviewKind.GRADIENT,
                    intArrayOf(EDEN_PINK, EDEN_PURPLE, EDEN_BLUE)
                )
            ),
            Palette(
                key = "joycon",
                labelRes = R.string.overlay_preset_joycon,
                body = JOY_BLUE,
                overrides = sideSplit(JOY_RED, JOY_BLUE),
                preview = Preview(
                    PreviewKind.GRADIENT,
                    intArrayOf(JOY_RED, JOY_BLUE),
                    GradientDrawable.Orientation.LEFT_RIGHT
                )
            ),
            Palette(
                key = "super_famicom",
                labelRes = R.string.overlay_preset_super_famicom,
                body = SFC_BODY,
                overrides = face(SFC_BODY, RAL_3020, RAL_1003, RAL_6029, SFC_LOGO_BLUE),
                preview = Preview(
                    PreviewKind.BANDS,
                    intArrayOf(SFC_BODY, RAL_3020, RAL_1003, RAL_6029, SFC_LOGO_BLUE)
                )
            ),
            Palette(
                key = "snes",
                labelRes = R.string.overlay_preset_snes,
                body = SNES_BODY,
                overrides = mapOf(
                    OverlayControl.BUTTON_A.id to SNES_BTN_AB,
                    OverlayControl.BUTTON_B.id to SNES_BTN_AB,
                    OverlayControl.BUTTON_X.id to SNES_BTN_XY,
                    OverlayControl.BUTTON_Y.id to SNES_BTN_XY,
                    OverlayControl.COMBINED_DPAD.id to SNES_BTN_AB
                ),
                preview = Preview(
                    PreviewKind.BANDS,
                    intArrayOf(SNES_BODY, SNES_BTN_XY, SNES_BTN_AB)
                )
            ),
            Palette(
                key = "gamecube",
                labelRes = R.string.overlay_preset_gamecube,
                body = GC_BODY,
                overrides = mapOf(
                    OverlayControl.BUTTON_A.id to GC_GREEN,
                    OverlayControl.BUTTON_B.id to GC_RED,
                    OverlayControl.BUTTON_X.id to GC_FACE_GRAY,
                    OverlayControl.BUTTON_Y.id to GC_FACE_GRAY,
                    OverlayControl.BUTTON_STICK_L.id to GC_STICK_CLICK,
                    OverlayControl.BUTTON_STICK_R.id to GC_STICK_CLICK,
                    OverlayControl.STICK_L.id to GC_FACE_GRAY,
                    OverlayControl.STICK_R.id to GC_C_STICK,
                    OverlayControl.COMBINED_DPAD.id to GC_FACE_GRAY
                ),
                preview = Preview(
                    PreviewKind.BANDS,
                    intArrayOf(GC_BODY, GC_GREEN, GC_RED, GC_FACE_GRAY, GC_C_STICK)
                )
            ),
            Palette(
                key = "nes",
                labelRes = R.string.overlay_preset_nes,
                body = NES_BODY,
                overrides = mapOf(
                    OverlayControl.BUTTON_PLUS.id to NES_GRAY,
                    OverlayControl.BUTTON_MINUS.id to NES_GRAY,
                    OverlayControl.BUTTON_HOME.id to NES_GRAY,
                    OverlayControl.BUTTON_CAPTURE.id to NES_GRAY,
                    OverlayControl.BUTTON_L.id to NES_DARK,
                    OverlayControl.BUTTON_R.id to NES_DARK,
                    OverlayControl.BUTTON_ZL.id to NES_DARK,
                    OverlayControl.BUTTON_ZR.id to NES_DARK,
                    OverlayControl.STICK_L.id to NES_GRAY,
                    OverlayControl.STICK_R.id to NES_GRAY,
                    OverlayControl.COMBINED_DPAD.id to NES_DPAD
                ),
                preview = Preview(
                    PreviewKind.BANDS,
                    intArrayOf(NES_BODY, NES_DPAD, NES_GRAY, NES_DARK)
                )
            ),
            Palette(
                key = "famicom",
                labelRes = R.string.overlay_preset_famicom,
                body = FAMICOM_BODY,
                overrides = face(
                    FAMICOM_BODY,
                    FAMICOM_RED,
                    FAMICOM_RED,
                    FAMICOM_RED,
                    FAMICOM_RED,
                    mapOf(
                        OverlayControl.COMBINED_DPAD to FAMICOM_DARK,
                        OverlayControl.BUTTON_STICK_L to FAMICOM_DARK,
                        OverlayControl.BUTTON_STICK_R to FAMICOM_DARK
                    )
                ),
                preview = Preview(
                    PreviewKind.BANDS,
                    intArrayOf(FAMICOM_BODY, FAMICOM_RED, FAMICOM_DARK)
                )
            )
        )
    }

    private var cachedRaw: String? = null
    private var cachedBody: Int = DEFAULT_COLOR
    private var cachedMap: Map<String, Int> = emptyMap()

    fun hexString(color: Int): String =
        String.format(Locale.US, "#%06X", color and 0xFFFFFF)

    fun loadBody(): Int = IntSetting.OVERLAY_COLOR.getInt() or 0xFF000000.toInt()

    fun loadOverrides(): Map<String, Int> = parse(StringSetting.OVERLAY_CONTROL_COLORS.getString())

    fun loadFace(): String = FACE_LETTERS

    fun colorFor(controlId: String): Int {
        val body = loadBody()
        val raw = StringSetting.OVERLAY_CONTROL_COLORS.getString()
        if (raw != cachedRaw || body != cachedBody) {
            cachedRaw = raw
            cachedBody = body
            cachedMap = parse(raw)
        }
        return cachedMap[controlId] ?: body
    }

    fun write(body: Int, overrides: Map<String, Int>, face: String = FACE_LETTERS) {
        IntSetting.OVERLAY_COLOR.setInt(body or 0xFF000000.toInt())
        StringSetting.OVERLAY_CONTROL_COLORS.setString(serialize(overrides, face))
        cachedRaw = null
        NativeConfig.saveGlobalConfig()
    }

    fun resolved(body: Int, overrides: Map<String, Int>, controlId: String): Int =
        overrides[controlId] ?: body

    fun matchingPalette(body: Int, overrides: Map<String, Int>, face: String): Palette? =
        palettes.firstOrNull { palette -> matches(palette, body, overrides, face) }

    /** Built-in preset keys plus localized titles (blocks names like GCN / GameCube). */
    fun reservedPresetNameKeys(displayLabels: Collection<String>): Set<String> {
        val keys = mutableSetOf<String>()
        palettes.forEach { palette ->
            keys.add(OverlayPresetLibrary.nameKey(palette.key))
        }
        displayLabels.forEach { label ->
            keys.add(OverlayPresetLibrary.nameKey(label))
        }
        return keys
    }

    fun presetFromPalette(palette: Palette, displayName: String): OverlayPresetLibrary.Preset =
        OverlayPresetLibrary.Preset(
            displayName,
            palette.body,
            palette.face,
            palette.overrides
        )

    fun labelFor(controlId: String, face: String): Int =
        targets.firstOrNull { it.id == controlId }?.labelRes ?: R.string.overlay_color

    fun serialize(overrides: Map<String, Int>, face: String = FACE_LETTERS): String {
        val parts = mutableListOf<String>()
        OverlayControl.entries.forEach { control ->
            val color = overrides[control.id] ?: return@forEach
            parts += "${control.id}=${String.format(Locale.US, "%08X", color)}"
        }
        return parts.joinToString(",")
    }

    fun parseHex(text: String): Int? {
        val cleaned = text.trim().removePrefix("#")
        val value = cleaned.toLongOrNull(16) ?: return null
        return when (cleaned.length) {
            6 -> (value or 0xFF000000L).toInt()
            8 -> (value or 0xFF000000L).toInt()
            else -> null
        }
    }

    fun parse(raw: String): Map<String, Int> {
        if (raw.isBlank()) {
            return emptyMap()
        }
        val out = linkedMapOf<String, Int>()
        raw.split(',').forEach { token ->
            val parts = token.split('=')
            if (parts.size != 2 || OverlayControl.from(parts[0]) == null) {
                return@forEach
            }
            val color = parts[1].toLongOrNull(16) ?: return@forEach
            out[parts[0]] = (color or 0xFF000000L).toInt()
        }
        return out
    }

    private fun matches(
        palette: Palette,
        body: Int,
        overrides: Map<String, Int>,
        face: String
    ): Boolean {
        if (palette.face != face) {
            return false
        }
        if ((palette.body and 0xFFFFFF) != (body and 0xFFFFFF)) {
            return false
        }
        return OverlayControl.entries.all { control ->
            val want = palette.overrides[control.id] ?: palette.body
            val have = overrides[control.id] ?: body
            (want and 0xFFFFFF) == (have and 0xFFFFFF)
        }
    }

    private fun byHeight(colorAt: (Float) -> Int): Map<String, Int> =
        OverlayControl.entries.associate { control ->
            val y = control.getDefaultPositionForLayout(OverlayLayout.Landscape).second.toFloat()
            control.id to colorAt(y)
        }

    private fun sideSplit(left: Int, right: Int): Map<String, Int> =
        OverlayControl.entries.associate { control ->
            val x = control.getDefaultPositionForLayout(OverlayLayout.Landscape).first.toFloat()
            control.id to if (x < 0.5f) left else right
        }

    private fun face(
        body: Int,
        a: Int,
        b: Int,
        x: Int,
        y: Int,
        extra: Map<OverlayControl, Int> = emptyMap()
    ): Map<String, Int> {
        val map = linkedMapOf<String, Int>()
        fun put(control: OverlayControl, color: Int) {
            val opaque = color or 0xFF000000.toInt()
            if ((opaque and 0xFFFFFF) != (body and 0xFFFFFF)) {
                map[control.id] = opaque
            }
        }
        put(OverlayControl.BUTTON_A, a)
        put(OverlayControl.BUTTON_B, b)
        put(OverlayControl.BUTTON_X, x)
        put(OverlayControl.BUTTON_Y, y)
        extra.forEach { (control, color) -> put(control, color) }
        return map
    }

    private fun edenColor(y: Float): Int {
        val t = y.coerceIn(0f, 1f)
        return if (t <= 0.5f) {
            mix(EDEN_PINK, EDEN_PURPLE, t / 0.5f)
        } else {
            mix(EDEN_PURPLE, EDEN_BLUE, (t - 0.5f) / 0.5f)
        }
    }

    private fun mix(start: Int, end: Int, amount: Float): Int {
        val t = amount.coerceIn(0f, 1f)
        return Color.rgb(
            (Color.red(start) + (Color.red(end) - Color.red(start)) * t).toInt(),
            (Color.green(start) + (Color.green(end) - Color.green(start)) * t).toInt(),
            (Color.blue(start) + (Color.blue(end) - Color.blue(start)) * t).toInt()
        )
    }

    fun previewDrawable(preview: Preview): GradientDrawable {
        val strokePx = 1
        val strokeColor = 0x33000000
        return when (preview.kind) {
            PreviewKind.SOLID -> GradientDrawable().apply {
                shape = GradientDrawable.OVAL
                setColor(preview.colors.first())
                setStroke(strokePx, strokeColor)
            }
            PreviewKind.GRADIENT -> GradientDrawable(preview.orientation, preview.colors).apply {
                shape = GradientDrawable.OVAL
                setStroke(strokePx, strokeColor)
            }
            PreviewKind.BANDS -> GradientDrawable(
                GradientDrawable.Orientation.LEFT_RIGHT,
                preview.colors
            ).apply {
                shape = GradientDrawable.OVAL
                setStroke(strokePx, strokeColor)
            }
        }
    }
}

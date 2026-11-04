// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.overlay

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.ColorMatrix
import android.graphics.ColorMatrixColorFilter
import android.graphics.Paint
import android.graphics.drawable.BitmapDrawable
import android.graphics.drawable.Drawable
import android.graphics.drawable.VectorDrawable
import android.util.LruCache
import androidx.core.content.ContextCompat
import kotlin.math.max
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.overlay.model.OverlayControl

/**
 * Small tinted overlay control artwork for the custom design list (same vectors as in-game).
 */
object OverlayControlIcons {
    /** Matches [R.layout.list_item_setting] icon size (24dp), rendered oversampled for sharp downscale. */
    private const val LIST_ICON_DP = 24f
    /** Custom design list and color editor preview. */
    const val DESIGN_PREVIEW_ICON_DP = 24f
    const val EDITOR_PREVIEW_ICON_DP = 36f
    private const val RENDER_OVERSAMPLE = 2f
    /** Inner stick draw size vs outer ring; matches [InputOverlay.initializeOverlayJoystick]. */
    private const val STICK_INNER_TO_OUTER = 1.66f
    private val cache = LruCache<String, Bitmap>(48)

    /** Empty face circle (no label), same artwork tint path as face buttons. */
    fun tintedBaseColorIcon(
        context: Context,
        body: Int,
        iconSizeDp: Float = LIST_ICON_DP
    ): Drawable? = tintedArtwork(context, R.drawable.facebutton_empty, body, iconSizeDp, 0.11f)

    /** Wide base-color bar for the tint editor (same artwork tint path as face buttons). */
    fun tintedBaseColorEditorPreview(context: Context, body: Int): Drawable? {
        val tint = body or 0xFF000000.toInt()
        val key = "base|editor|${tint and 0xFFFFFF}"
        val bitmap = cache.get(key) ?: renderBaseColorEditorPreview(context, tint).also { cache.put(key, it) }
        return BitmapDrawable(context.resources, bitmap).apply {
            setAntiAlias(true)
            setFilterBitmap(false)
        }
    }

    fun tintedIcon(
        context: Context,
        controlId: String,
        body: Int,
        face: String,
        overrides: Map<String, Int>,
        iconSizeDp: Float = LIST_ICON_DP
    ): Drawable? {
        if (controlId == OverlayColors.BASE) {
            return tintedBaseColorIcon(context, body, iconSizeDp)
        }
        val tint = OverlayColors.resolved(body, overrides, controlId)
        val key = when (controlId) {
            OverlayControl.STICK_L.id,
            OverlayControl.STICK_R.id -> "stick|$controlId|$face|${tint and 0xFFFFFF}|$iconSizeDp"
            else -> {
                val drawableId = artworkDrawable(face, controlId) ?: return null
                "$controlId|$face|${tint and 0xFFFFFF}|$drawableId|$iconSizeDp"
            }
        }
        val bitmap = cache.get(key) ?: when (controlId) {
            OverlayControl.STICK_L.id,
            OverlayControl.STICK_R.id -> renderStick(context, tint, iconSizeDp)
            else -> {
                val drawableId = artworkDrawable(face, controlId) ?: return null
                render(context, drawableId, tint, iconSizeDp)
            }
        }.also { cache.put(key, it) }
        return BitmapDrawable(context.resources, bitmap).apply {
            setAntiAlias(true)
            setFilterBitmap(false)
        }
    }

    private fun targetMaxPixels(context: Context, iconSizeDp: Float): Int {
        val density = context.resources.displayMetrics.density
        return (iconSizeDp * density * RENDER_OVERSAMPLE).toInt().coerceAtLeast(48)
    }

    private fun artworkDrawable(face: String, controlId: String): Int? =
        when (controlId) {
            OverlayControl.BUTTON_A.id -> R.drawable.facebutton_a
            OverlayControl.BUTTON_B.id -> R.drawable.facebutton_b
            OverlayControl.BUTTON_X.id -> R.drawable.facebutton_x
            OverlayControl.BUTTON_Y.id -> R.drawable.facebutton_y
            OverlayControl.BUTTON_PLUS.id -> R.drawable.facebutton_plus
            OverlayControl.BUTTON_MINUS.id -> R.drawable.facebutton_minus
            OverlayControl.BUTTON_HOME.id -> R.drawable.facebutton_home
            OverlayControl.BUTTON_CAPTURE.id -> R.drawable.facebutton_screenshot
            OverlayControl.BUTTON_L.id -> R.drawable.l_shoulder
            OverlayControl.BUTTON_R.id -> R.drawable.r_shoulder
            OverlayControl.BUTTON_ZL.id -> R.drawable.zl_trigger
            OverlayControl.BUTTON_ZR.id -> R.drawable.zr_trigger
            OverlayControl.BUTTON_STICK_L.id -> R.drawable.button_l3
            OverlayControl.BUTTON_STICK_R.id -> R.drawable.button_r3
            OverlayControl.STICK_L.id,
            OverlayControl.STICK_R.id -> R.drawable.joystick
            OverlayControl.COMBINED_DPAD.id -> R.drawable.dpad_standard
            else -> null
        }

    private fun artworkScale(drawableId: Int): Float =
        when (drawableId) {
            R.drawable.facebutton_home,
            R.drawable.facebutton_screenshot,
            R.drawable.facebutton_plus,
            R.drawable.facebutton_minus -> 0.07f
            R.drawable.l_shoulder,
            R.drawable.r_shoulder,
            R.drawable.zl_trigger,
            R.drawable.zr_trigger -> 0.26f
            R.drawable.button_l3,
            R.drawable.button_r3 -> 0.155f
            R.drawable.dpad_standard -> 0.25f
            R.drawable.joystick_range,
            R.drawable.joystick -> 0.3f
            R.drawable.overlay_base_color_bar -> 0.11f
            else -> 0.11f
        }

    private fun render(
        context: Context,
        drawableId: Int,
        tint: Int,
        iconSizeDp: Float,
        artScale: Float = artworkScale(drawableId)
    ): Bitmap =
        renderDrawable(
            context,
            drawableId,
            artScale,
            targetMaxPixels(context, iconSizeDp),
            tint
        )

    private fun tintedArtwork(
        context: Context,
        drawableId: Int,
        tint: Int,
        iconSizeDp: Float,
        artScale: Float
    ): Drawable? {
        val key = "art|$drawableId|${tint and 0xFFFFFF}|$iconSizeDp|$artScale"
        val bitmap = cache.get(key) ?: render(context, drawableId, tint, iconSizeDp, artScale)
            .also { cache.put(key, it) }
        return BitmapDrawable(context.resources, bitmap).apply {
            setAntiAlias(true)
            setFilterBitmap(false)
        }
    }

    private fun renderBaseColorEditorPreview(context: Context, tint: Int): Bitmap {
        val density = context.resources.displayMetrics.density
        val targetMaxPx = (200f * density * RENDER_OVERSAMPLE).toInt().coerceAtLeast(48)
        return renderDrawable(
            context,
            R.drawable.overlay_base_color_bar,
            artworkScale(R.drawable.overlay_base_color_bar),
            targetMaxPx,
            tint
        )
    }

    private fun renderStick(context: Context, tint: Int, iconSizeDp: Float): Bitmap {
        val target = targetMaxPixels(context, iconSizeDp)
        val outer = renderDrawable(context, R.drawable.joystick_range, 0.3f, target, tint)
        val innerTarget =
            (max(outer.width, outer.height) / STICK_INNER_TO_OUTER).toInt().coerceAtLeast(1)
        val inner = renderDrawable(context, R.drawable.joystick, 1.0f, innerTarget, tint)
        val composite = Bitmap.createBitmap(outer.width, outer.height, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(composite)
        canvas.drawBitmap(outer, 0f, 0f, null)
        val left = (outer.width - inner.width) / 2f
        val top = (outer.height - inner.height) / 2f
        canvas.drawBitmap(inner, left, top, null)
        outer.recycle()
        inner.recycle()
        return composite
    }

    /** Same sizing rule as [InputOverlay.getBitmap], with [targetMaxPx] instead of screen size. */
    private fun renderDrawable(
        context: Context,
        drawableId: Int,
        artScale: Float,
        targetMaxPx: Int,
        tint: Int
    ): Bitmap {
        val vectorDrawable = ContextCompat.getDrawable(context, drawableId) as VectorDrawable
        val minScreenDimension = targetMaxPx / artScale

        val bitmap = Bitmap.createBitmap(
            (vectorDrawable.intrinsicWidth * artScale).toInt().coerceAtLeast(1),
            (vectorDrawable.intrinsicHeight * artScale).toInt().coerceAtLeast(1),
            Bitmap.Config.ARGB_8888
        )
        val maxBitmapDimension = max(bitmap.width, bitmap.height).coerceAtLeast(1)
        val bitmapScale = artScale * minScreenDimension / maxBitmapDimension
        val scaledBitmap = Bitmap.createScaledBitmap(
            bitmap,
            (bitmap.width * bitmapScale).toInt().coerceAtLeast(1),
            (bitmap.height * bitmapScale).toInt().coerceAtLeast(1),
            true
        )
        if (bitmap !== scaledBitmap) {
            bitmap.recycle()
        }
        val canvas = Canvas(scaledBitmap)
        vectorDrawable.setBounds(0, 0, canvas.width, canvas.height)
        vectorDrawable.draw(canvas)
        return applyOverlayTint(scaledBitmap, tint)
    }

    private fun applyOverlayTint(source: Bitmap, tint: Int): Bitmap {
        val opaque = tint or 0xFF000000.toInt()
        val red = Color.red(opaque) / 255f
        val green = Color.green(opaque) / 255f
        val blue = Color.blue(opaque) / 255f
        if (red >= 0.999f && green >= 0.999f && blue >= 0.999f) {
            return source
        }
        if (source.width <= 0 || source.height <= 0) {
            return source
        }
        val tinted = Bitmap.createBitmap(source.width, source.height, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(tinted)
        val paint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG).apply {
            colorFilter = ColorMatrixColorFilter(
                ColorMatrix(
                    floatArrayOf(
                        red, 0f, 0f, 0f, 0f,
                        0f, green, 0f, 0f, 0f,
                        0f, 0f, blue, 0f, 0f,
                        0f, 0f, 0f, 1f, 0f
                    )
                )
            )
        }
        canvas.drawBitmap(source, 0f, 0f, paint)
        source.recycle()
        return tinted
    }
}

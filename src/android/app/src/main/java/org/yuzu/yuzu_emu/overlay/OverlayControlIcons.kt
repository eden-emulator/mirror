// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.overlay

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.drawable.BitmapDrawable
import android.graphics.drawable.Drawable
import android.util.LruCache
import kotlin.math.max
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.overlay.model.OverlayControl

/**
 * Small tinted overlay control artwork for the custom design list (same vectors as in-game).
 */
object OverlayControlIcons {
    private const val LIST_ICON_DP = 24f
    const val DESIGN_PREVIEW_ICON_DP = LIST_ICON_DP
    const val EDITOR_PREVIEW_ICON_DP = 36f
    private const val RENDER_OVERSAMPLE = 2f
    private const val STICK_INNER_TO_OUTER = 1.66f
    private val cache = LruCache<String, Bitmap>(48)

    fun tintedBaseColorIcon(context: Context, body: Int, iconSizeDp: Float = LIST_ICON_DP): Drawable? {
        val tint = body or 0xFF000000.toInt()
        return tintedDrawable(context, R.drawable.facebutton_empty, 0.11f, tint, iconSizeDp)
    }

    fun tintedBaseColorEditorPreview(context: Context, body: Int): Drawable? {
        val tint = body or 0xFF000000.toInt()
        val key = "base|editor|${tint and 0xFFFFFF}"
        val density = context.resources.displayMetrics.density
        val targetMaxPx = (200f * density * RENDER_OVERSAMPLE).toInt().coerceAtLeast(48)
        val bitmap = cache.get(key) ?: OverlayVectorBitmap.render(
            context,
            R.drawable.overlay_base_color_bar,
            0.11f,
            targetMaxPx,
            tint
        ).also { cache.put(key, it) }
        return bitmapDrawable(context, bitmap)
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
                val drawableId = OverlayArtwork.drawableFor(controlId) ?: return null
                "$controlId|$face|${tint and 0xFFFFFF}|$drawableId|$iconSizeDp"
            }
        }
        val bitmap = cache.get(key) ?: when (controlId) {
            OverlayControl.STICK_L.id,
            OverlayControl.STICK_R.id -> renderStick(context, tint, iconSizeDp)
            else -> {
                val drawableId = OverlayArtwork.drawableFor(controlId) ?: return null
                renderIcon(context, drawableId, tint, iconSizeDp)
            }
        }.also { cache.put(key, it) }
        return bitmapDrawable(context, bitmap)
    }

    private fun targetMaxPixels(context: Context, iconSizeDp: Float): Int {
        val density = context.resources.displayMetrics.density
        return (iconSizeDp * density * RENDER_OVERSAMPLE).toInt().coerceAtLeast(48)
    }

    private fun renderIcon(context: Context, drawableId: Int, tint: Int, iconSizeDp: Float): Bitmap =
        OverlayVectorBitmap.render(
            context,
            drawableId,
            OverlayArtwork.scaleForDrawable(drawableId),
            targetMaxPixels(context, iconSizeDp),
            tint
        )

    private fun tintedDrawable(
        context: Context,
        drawableId: Int,
        artScale: Float,
        tint: Int,
        iconSizeDp: Float
    ): Drawable? {
        val key = "art|$drawableId|${tint and 0xFFFFFF}|$iconSizeDp|$artScale"
        val bitmap = cache.get(key) ?: renderIcon(context, drawableId, tint, iconSizeDp)
            .also { cache.put(key, it) }
        return bitmapDrawable(context, bitmap)
    }

    private fun renderStick(context: Context, tint: Int, iconSizeDp: Float): Bitmap {
        val target = targetMaxPixels(context, iconSizeDp)
        val outer = OverlayVectorBitmap.render(context, R.drawable.joystick_range, 0.3f, target, tint)
        val innerTarget =
            (max(outer.width, outer.height) / STICK_INNER_TO_OUTER).toInt().coerceAtLeast(1)
        val inner = OverlayVectorBitmap.render(context, R.drawable.joystick, 1.0f, innerTarget, tint)
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

    private fun bitmapDrawable(context: Context, bitmap: Bitmap): Drawable =
        BitmapDrawable(context.resources, bitmap).apply {
            setAntiAlias(true)
            setFilterBitmap(false)
        }
}

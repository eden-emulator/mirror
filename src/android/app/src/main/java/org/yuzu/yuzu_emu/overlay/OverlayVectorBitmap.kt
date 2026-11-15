// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.overlay

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.drawable.VectorDrawable
import androidx.core.content.ContextCompat
import kotlin.math.max

/** Scale a vector drawable to [targetMaxPx] along its longest edge, then apply overlay tint. */
object OverlayVectorBitmap {
    fun render(
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
        return OverlayBitmapTint.apply(scaledBitmap, tint)
    }
}

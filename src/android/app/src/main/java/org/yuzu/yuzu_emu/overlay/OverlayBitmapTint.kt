// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.overlay

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.ColorMatrix
import android.graphics.ColorMatrixColorFilter
import android.graphics.Paint

object OverlayBitmapTint {
    fun apply(source: Bitmap, tint: Int): Bitmap {
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

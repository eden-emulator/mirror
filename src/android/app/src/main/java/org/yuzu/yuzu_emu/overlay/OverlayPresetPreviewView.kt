// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.overlay

import android.content.Context
import android.content.res.Configuration
import android.graphics.drawable.GradientDrawable
import android.util.AttributeSet
import org.yuzu.yuzu_emu.R
import android.widget.FrameLayout
import android.widget.ImageView
import androidx.core.view.setPadding

/**
 * Cached bitmap thumbnail of the real overlay layout (see [OverlayPresetThumbnailCache]).
 */
class OverlayPresetPreviewView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : FrameLayout(context, attrs, defStyleAttr) {

    private val previewImage = ImageView(context)
    private var boundSignature: String? = null

    /** Landscape touch overlay: width / height (matches typical game/emulation layout). */
    private val landscapeAspect = 16f / 9f

    init {
        val pad = (4 * resources.displayMetrics.density).toInt()
        setPadding(pad)
        background = GradientDrawable().apply {
            cornerRadius = 8f * resources.displayMetrics.density
            setColor(0xFF1A1A1E.toInt())
        }
        previewImage.scaleType = ImageView.ScaleType.FIT_CENTER
        previewImage.adjustViewBounds = true
        previewImage.isClickable = false
        previewImage.isFocusable = false
        addView(
            previewImage,
            LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT)
        )
        isClickable = false
        isFocusable = false
    }

    fun setPreset(body: Int, face: String, controlOverrides: Map<String, Int>) {
        val signature = OverlayPresetThumbnailCache.signature(body, face, controlOverrides)
        if (signature == boundSignature && previewImage.drawable != null) {
            return
        }
        boundSignature = signature
        val cached = OverlayPresetThumbnailCache.get(signature)
        if (cached != null) {
            previewImage.setImageBitmap(cached)
            return
        }
        previewImage.setImageDrawable(null)
        OverlayPresetThumbnailCache.request(context, body, face, controlOverrides) { bitmap ->
            if (boundSignature == signature) {
                previewImage.setImageBitmap(bitmap)
            }
        }
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val widthMode = MeasureSpec.getMode(widthMeasureSpec)
        val widthSize = MeasureSpec.getSize(widthMeasureSpec)

        val horizontalPad = paddingLeft + paddingRight
        val verticalPad = paddingTop + paddingBottom

        var innerWidth = when (widthMode) {
            MeasureSpec.UNSPECIFIED -> widthSize
            else -> (widthSize - horizontalPad).coerceAtLeast(0)
        }
        var innerHeight = (innerWidth / landscapeAspect).toInt().coerceAtLeast(1)
        if (resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE) {
            val maxHeight = resources.getDimensionPixelSize(R.dimen.overlay_preset_preview_max_height_land)
            if (innerHeight > maxHeight) {
                innerHeight = maxHeight
                innerWidth = (innerHeight * landscapeAspect).toInt().coerceAtLeast(1)
            }
        }

        val childWidthSpec = MeasureSpec.makeMeasureSpec(innerWidth, MeasureSpec.EXACTLY)
        val childHeightSpec = MeasureSpec.makeMeasureSpec(innerHeight, MeasureSpec.EXACTLY)
        previewImage.measure(childWidthSpec, childHeightSpec)

        val totalWidth = when (widthMode) {
            MeasureSpec.EXACTLY -> widthSize
            else -> innerWidth + horizontalPad
        }
        val totalHeight = innerHeight + verticalPad
        setMeasuredDimension(totalWidth, totalHeight)
    }

    override fun onLayout(changed: Boolean, left: Int, top: Int, right: Int, bottom: Int) {
        val innerWidth = (right - left) - paddingLeft - paddingRight
        val innerHeight = (bottom - top) - paddingTop - paddingBottom
        val childLeft = paddingLeft
        val childTop = paddingTop
        previewImage.layout(
            childLeft,
            childTop,
            childLeft + innerWidth,
            childTop + innerHeight
        )
    }
}

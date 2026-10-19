// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.overlay

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.os.Handler
import android.os.Looper
import android.util.LruCache
import android.view.View.MeasureSpec

/**
 * Renders and caches static overlay preset thumbnails so list scrolling does not rebuild controls.
 */
object OverlayPresetThumbnailCache {
    private const val REFERENCE_WIDTH = 480
    private const val REFERENCE_HEIGHT = 270

    private val mainHandler = Handler(Looper.getMainLooper())
    private val cache = LruCache<String, Bitmap>(32)
    private val inflight = mutableSetOf<String>()
    private val pendingCallbacks = linkedMapOf<String, MutableList<(Bitmap) -> Unit>>()

    fun signature(body: Int, face: String, overrides: Map<String, Int>): String =
        OverlayPresetLibrary.contentSignature(body, face, overrides) +
            "|" + OverlayPreviewLayout.layoutSignature()

    fun evictAll() {
        cache.evictAll()
    }

    fun get(key: String): Bitmap? = cache.get(key)

    fun preload(context: Context, body: Int, face: String, overrides: Map<String, Int>) {
        val key = signature(body, face, overrides)
        if (cache.get(key) != null || inflight.contains(key)) {
            return
        }
        enqueueRender(context.applicationContext, key, body, face, overrides, null)
    }

    fun request(
        context: Context,
        body: Int,
        face: String,
        overrides: Map<String, Int>,
        onReady: (Bitmap) -> Unit
    ) {
        val key = signature(body, face, overrides)
        val cached = cache.get(key)
        if (cached != null) {
            onReady(cached)
            return
        }
        enqueueRender(context.applicationContext, key, body, face, overrides, onReady)
    }

    private fun enqueueRender(
        context: Context,
        key: String,
        body: Int,
        face: String,
        overrides: Map<String, Int>,
        onReady: ((Bitmap) -> Unit)?
    ) {
        if (onReady != null) {
            pendingCallbacks.getOrPut(key) { mutableListOf() }.add(onReady)
        }
        if (!inflight.add(key)) {
            return
        }
        mainHandler.post {
            try {
                if (cache.get(key) == null) {
                    cache.put(key, renderBitmap(context, body, face, overrides))
                }
                val bitmap = cache.get(key)!!
                pendingCallbacks.remove(key)?.forEach { callback -> callback(bitmap) }
            } finally {
                inflight.remove(key)
            }
        }
    }

    private fun renderSize(context: Context): Pair<Int, Int> {
        val density = context.resources.displayMetrics.density
        val width = (REFERENCE_WIDTH * density).toInt().coerceIn(REFERENCE_WIDTH, 1280)
        val height = (width * REFERENCE_HEIGHT / REFERENCE_WIDTH).coerceAtLeast(1)
        return width to height
    }

    private fun renderBitmap(
        context: Context,
        body: Int,
        face: String,
        overrides: Map<String, Int>
    ): Bitmap {
        val (width, height) = renderSize(context)
        val overlay = InputOverlay(context, null)
        overlay.applyStaticPreview(body, face, overrides)
        val widthSpec = MeasureSpec.makeMeasureSpec(width, MeasureSpec.EXACTLY)
        val heightSpec = MeasureSpec.makeMeasureSpec(height, MeasureSpec.EXACTLY)
        overlay.measure(widthSpec, heightSpec)
        overlay.layout(0, 0, width, height)

        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        canvas.drawColor(0xFF1A1A1E.toInt())
        overlay.draw(canvas)
        return bitmap
    }
}

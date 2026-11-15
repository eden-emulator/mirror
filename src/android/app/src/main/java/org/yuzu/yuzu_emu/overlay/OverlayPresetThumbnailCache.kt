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
import org.yuzu.yuzu_emu.features.settings.model.IntSetting
import org.yuzu.yuzu_emu.overlay.model.OverlayControl
import org.yuzu.yuzu_emu.overlay.model.OverlayControlData
import org.yuzu.yuzu_emu.utils.NativeConfig

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

    fun snapshotControlData(): List<OverlayControlData> {
        val stored = NativeConfig.getOverlayControlData()
        if (stored.isEmpty()) {
            return OverlayControl.entries.map { it.toOverlayControlData() }
        }
        val byId = stored.associateBy { it.id }.toMutableMap()
        OverlayControl.entries.forEach { entry ->
            if (!byId.containsKey(entry.id)) {
                byId[entry.id] = entry.toOverlayControlData()
            }
        }
        return byId.values.toList()
    }

    fun layoutSignature(): String {
        val scale = IntSetting.OVERLAY_SCALE.getInt()
        val opacity = IntSetting.OVERLAY_OPACITY.getInt()
        val controls = snapshotControlData().sortedBy { it.id }
        val layoutBody = controls.joinToString(";") { data ->
            buildString {
                append(data.id)
                append(':')
                append(if (data.enabled) 1 else 0)
                append(':')
                append(data.landscapePosition.first)
                append(',')
                append(data.landscapePosition.second)
                append(':')
                append(data.individualScale)
            }
        }
        return "L|$scale|$opacity|$layoutBody"
    }

    fun signature(body: Int, face: String, overrides: Map<String, Int>): String =
        OverlayPresetLibrary.contentSignature(body, face, overrides) + "|" + layoutSignature()

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
                val bitmap = cache.get(key) ?: return@post
                pendingCallbacks.remove(key)?.forEach { callback -> callback(bitmap) }
            } catch (_: RuntimeException) {
                pendingCallbacks.remove(key)
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
        val (previewBody, previewOverrides) = OverlayColors.resolveForPreview(context, body, overrides)
        val overlay = InputOverlay(context, null)
        overlay.applyStaticPreview(previewBody, face, previewOverrides)
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

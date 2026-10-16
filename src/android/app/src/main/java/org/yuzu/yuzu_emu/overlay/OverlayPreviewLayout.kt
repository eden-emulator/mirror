// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.overlay

import org.yuzu.yuzu_emu.features.settings.model.IntSetting
import org.yuzu.yuzu_emu.overlay.model.OverlayControl
import org.yuzu.yuzu_emu.overlay.model.OverlayControlData
import org.yuzu.yuzu_emu.utils.NativeConfig

/**
 * Reads the user's current overlay layout (positions, enabled controls, per-control scale)
 * for static color previews without modifying saved config.
 */
object OverlayPreviewLayout {
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
        return byId.values.map { data ->
            data.copy(
                landscapePosition = data.landscapePosition,
                portraitPosition = data.portraitPosition,
                foldablePosition = data.foldablePosition
            )
        }
    }

    /** Fingerprint for thumbnail cache; landscape layout matches in-game landscape overlay. */
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
}

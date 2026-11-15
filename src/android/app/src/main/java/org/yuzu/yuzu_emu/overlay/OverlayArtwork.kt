// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.overlay

import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.overlay.model.OverlayControl

/** Vector drawable ids and layout scales for overlay controls (in-game and list icons). */
object OverlayArtwork {
    fun drawableFor(controlId: String): Int? =
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

    fun scaleForDrawable(drawableId: Int): Float =
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

    fun scaleForControl(controlId: String): Float =
        when (controlId) {
            OverlayControl.BUTTON_HOME.id,
            OverlayControl.BUTTON_CAPTURE.id,
            OverlayControl.BUTTON_PLUS.id,
            OverlayControl.BUTTON_MINUS.id -> 0.07f
            OverlayControl.BUTTON_L.id,
            OverlayControl.BUTTON_R.id,
            OverlayControl.BUTTON_ZL.id,
            OverlayControl.BUTTON_ZR.id -> 0.26f
            OverlayControl.BUTTON_STICK_L.id,
            OverlayControl.BUTTON_STICK_R.id -> 0.155f
            OverlayControl.COMBINED_DPAD.id -> 0.25f
            OverlayControl.STICK_L.id,
            OverlayControl.STICK_R.id -> 0.3f
            else -> 0.11f
        }
}

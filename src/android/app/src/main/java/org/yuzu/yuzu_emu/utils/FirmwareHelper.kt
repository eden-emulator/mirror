// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import java.io.File
import org.yuzu.yuzu_emu.NativeLibrary

object FirmwareHelper {
    /**
     * Firmware files on NAND plus readable version string (requires prod keys).
     */
    fun isFirmwareReady(): Boolean {
        if (!NativeLibrary.areKeysPresent() || !NativeLibrary.isFirmwareAvailable()) {
            return false
        }
        val version = NativeLibrary.firmwareVersion()
        return version.isNotEmpty() && version != "N/A"
    }

    /**
     * Whether firmware NCAs were copied to NAND during setup, even if keys are not loaded yet.
     */
    fun isFirmwareInstalledOnNand(): Boolean {
        val registeredDir = File(NativeConfig.getNandDir() + "/system/Contents/registered/")
        if (!registeredDir.isDirectory) {
            return false
        }
        return registeredDir.walkTopDown().any { file ->
            file.isFile && file.name.endsWith(".nca", ignoreCase = true)
        }
    }

    fun isFirmwareSetupStepComplete(): Boolean {
        return isFirmwareInstalledOnNand() || NativeLibrary.isFirmwareAvailable()
    }

    /**
     * Matches Eden Android: every entry at the root of the extracted zip must be an .nca file.
     */
    fun isValidFirmwareExtract(dir: File): Boolean {
        if (!dir.isDirectory) {
            return false
        }
        val rootEntries = dir.list() ?: return false
        if (rootEntries.isEmpty()) {
            return false
        }
        return rootEntries.all { it.endsWith(".nca", ignoreCase = true) }
    }
}

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import android.content.SharedPreferences
import android.content.res.Configuration
import android.graphics.Color
import android.os.Build
import android.os.Handler
import android.os.Looper
import androidx.annotation.ColorInt
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.app.AppCompatDelegate
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsControllerCompat
import kotlin.math.roundToInt
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.features.settings.model.BooleanSetting
import org.yuzu.yuzu_emu.features.settings.model.IntSetting
import org.yuzu.yuzu_emu.ui.main.ThemeProvider
import androidx.preference.PreferenceManager
import org.yuzu.yuzu_emu.YuzuApplication
import org.yuzu.yuzu_emu.features.settings.model.Settings
import android.view.View
import com.google.android.material.color.MaterialColors
import java.util.Collections
import java.util.WeakHashMap

object ThemeHelper {
    const val SYSTEM_BAR_ALPHA = 0.9f

    @Volatile
    private var themeConfigurationGeneration = 0

    private val mainHandler = Handler(Looper.getMainLooper())
    private val trackedActivities =
        Collections.newSetFromMap(WeakHashMap<AppCompatActivity, Boolean>())
    private val lastAppliedThemeGeneration = WeakHashMap<AppCompatActivity, Int>()

    fun registerForThemeUpdates(activity: AppCompatActivity) {
        trackedActivities.add(activity)
        lastAppliedThemeGeneration.putIfAbsent(activity, themeConfigurationGeneration)
        activity.lifecycle.addObserver(
            object : DefaultLifecycleObserver {
                override fun onDestroy(owner: LifecycleOwner) {
                    trackedActivities.remove(activity)
                    lastAppliedThemeGeneration.remove(activity)
                }
            }
        )
    }

    fun markThemeConfigurationChanged() {
        themeConfigurationGeneration++
        dispatchThemeUpdates()
    }

    private fun dispatchThemeUpdates() {
        mainHandler.post {
            trackedActivities.toList().forEach { activity ->
                if (!activity.isFinishing &&
                    activity.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)
                ) {
                    applyThemeConfiguration(activity)
                }
            }
        }
    }

    private fun hasThemeConfigurationChanged(lastSeenGeneration: Int): Boolean =
        themeConfigurationGeneration != lastSeenGeneration

    // Listener that detects if the theme keys are being changed from the setting menu and recreates the activity
    private var listener: SharedPreferences.OnSharedPreferenceChangeListener? = null
    private val preferences = PreferenceManager.getDefaultSharedPreferences(
        YuzuApplication.appContext
    )

    fun setTheme(activity: AppCompatActivity) {
        setThemeMode(activity)
        when (Theme.from(IntSetting.THEME.getInt())) {
            Theme.Default -> activity.setTheme(getSelectedStaticThemeColor())
            Theme.MaterialYou -> {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    activity.setTheme(R.style.Theme_Yuzu_Main_MaterialYou)
                } else {
                    activity.setTheme(getSelectedStaticThemeColor())
                }
            }
        }

        // Using a specific night mode check because this could apply incorrectly when using the
        // light app mode, dark system mode, and black backgrounds. Launching the settings activity
        // will then show light mode colors/navigation bars but with black backgrounds.
        if (BooleanSetting.BLACK_BACKGROUNDS.getBoolean() && isNightMode(activity)) {
            activity.setTheme(R.style.ThemeOverlay_Yuzu_Dark)
        }
    }

    private fun getSelectedStaticThemeColor(): Int {
        val themeIndex = IntSetting.STATIC_THEME_COLOR.getInt(false)
        val themes = arrayOf(
            R.style.Theme_Eden_Main,
            R.style.Theme_Yuzu_Main_Violet,
            R.style.Theme_Yuzu_Main_Blue,
            R.style.Theme_Yuzu_Main_Cyan,
            R.style.Theme_Yuzu_Main_Red,
            R.style.Theme_Yuzu_Main_Green,
            R.style.Theme_Yuzu_Main_Yellow,
            R.style.Theme_Yuzu_Main_Orange,
            R.style.Theme_Yuzu_Main_Pink,
            R.style.Theme_Yuzu_Main_Gray
        )
        return themes[themeIndex]
    }

    @ColorInt
    fun getColorWithOpacity(@ColorInt color: Int, alphaFactor: Float): Int {
        return Color.argb(
            (alphaFactor * Color.alpha(color)).roundToInt(),
            Color.red(color),
            Color.green(color),
            Color.blue(color)
        )
    }

    @ColorInt
    fun getLegacyContentBackgroundColor(anchor: View): Int {
        return MaterialColors.getColor(anchor, com.google.android.material.R.attr.colorSurface)
    }

    /** Bottom navigation bar: slight contrast from the main content. */
    @ColorInt
    fun getLegacyChromeBackgroundColor(anchor: View): Int {
        return MaterialColors.layer(
            anchor,
            com.google.android.material.R.attr.colorSurface,
            com.google.android.material.R.attr.colorPrimary,
            0.12f
        )
    }

    /** Status bar scrim matches the fragment content ([getLegacyContentBackgroundColor]). */
    @ColorInt
    fun getLegacyStatusBarScrimColor(anchor: View): Int {
        return getColorWithOpacity(getLegacyContentBackgroundColor(anchor), SYSTEM_BAR_ALPHA)
    }

    /** Scrim behind the status bar (and bottom nav bar when shown). */
    @ColorInt
    fun getSystemBarScrimColor(anchor: View): Int {
        if (LegacyUi.isEnabled()) {
            return getLegacyStatusBarScrimColor(anchor)
        }
        return getColorWithOpacity(
            MaterialColors.getColor(anchor, com.google.android.material.R.attr.colorSurface),
            SYSTEM_BAR_ALPHA
        )
    }

    fun applySystemBarShadeColors(
        anchor: View,
        statusBarShade: View,
        navigationBarShade: View?,
        includeNavigationBarShade: Boolean,
    ) {
        val scrim = getSystemBarScrimColor(anchor)
        statusBarShade.setBackgroundColor(scrim)
        if (includeNavigationBarShade && navigationBarShade != null) {
            navigationBarShade.setBackgroundColor(scrim)
        }
    }

    fun safeRecreate(activity: AppCompatActivity) {
        if (activity.isFinishing) {
            return
        }
        if (activity.lifecycle.currentState.isAtLeast(Lifecycle.State.STARTED)) {
            activity.recreate()
        }
    }

    /**
     * Re-applies theme resources when settings changed in native config. Returns true if the
     * activity is being recreated and the caller should skip further resume work.
     */
    fun syncThemeConfiguration(activity: AppCompatActivity): Boolean {
        if (activity.isFinishing) {
            return false
        }

        val lastApplied = lastAppliedThemeGeneration[activity] ?: themeConfigurationGeneration
        val configurationChanged = hasThemeConfigurationChanged(lastApplied)

        val themeProvider = activity as? ThemeProvider
        val previousThemeId = themeProvider?.themeId ?: 0
        setTheme(activity)
        val themeStyleChanged =
            themeProvider != null && previousThemeId != themeProvider.themeId

        if (!configurationChanged && !themeStyleChanged) {
            return false
        }

        if (!activity.lifecycle.currentState.isAtLeast(Lifecycle.State.STARTED)) {
            return false
        }

        lastAppliedThemeGeneration[activity] = themeConfigurationGeneration
        safeRecreate(activity)
        return true
    }

    private fun applyThemeConfiguration(activity: AppCompatActivity) {
        syncThemeConfiguration(activity)
    }

    fun setThemeMode(activity: AppCompatActivity) {
        val themeMode = IntSetting.THEME_MODE.getInt()
        activity.delegate.localNightMode = themeMode
        val windowController = WindowCompat.getInsetsController(
            activity.window,
            activity.window.decorView
        )
        when (themeMode) {
            AppCompatDelegate.MODE_NIGHT_FOLLOW_SYSTEM -> when (isNightMode(activity)) {
                false -> setLightModeSystemBars(windowController)
                true -> setDarkModeSystemBars(windowController)
            }
            AppCompatDelegate.MODE_NIGHT_NO -> setLightModeSystemBars(windowController)
            AppCompatDelegate.MODE_NIGHT_YES -> setDarkModeSystemBars(windowController)
        }
    }

    private fun isNightMode(activity: AppCompatActivity): Boolean {
        return when (activity.resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK) {
            Configuration.UI_MODE_NIGHT_NO -> false
            Configuration.UI_MODE_NIGHT_YES -> true
            else -> false
        }
    }

    private fun setLightModeSystemBars(windowController: WindowInsetsControllerCompat) {
        windowController.isAppearanceLightStatusBars = true
        windowController.isAppearanceLightNavigationBars = true
    }

    private fun setDarkModeSystemBars(windowController: WindowInsetsControllerCompat) {
        windowController.isAppearanceLightStatusBars = false
        windowController.isAppearanceLightNavigationBars = false
    }

    fun ThemeChangeListener(activity: AppCompatActivity) {
        listener = SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
            val relevantKeys = listOf(
                Settings.PREF_STATIC_THEME_COLOR,
                Settings.PREF_THEME_MODE,
                Settings.PREF_BLACK_BACKGROUNDS
            )
            if (key in relevantKeys && !activity.isFinishing) {
                markThemeConfigurationChanged()
            }
        }
        preferences.registerOnSharedPreferenceChangeListener(listener)
    }

    fun unregisterThemeChangeListener() {
        listener?.let { preferences.unregisterOnSharedPreferenceChangeListener(it) }
        listener = null
    }
}

enum class Theme(val int: Int) {
    Default(0),
    MaterialYou(1);

    companion object {
        fun from(int: Int): Theme = entries.firstOrNull { it.int == int } ?: Default
    }
}

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import android.content.Context
import android.content.SharedPreferences
import android.content.res.Configuration
import android.graphics.Color
import android.os.Build
import android.util.TypedValue
import androidx.annotation.ColorInt
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.app.AppCompatDelegate
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.appcompat.view.ContextThemeWrapper
import com.google.android.material.R as MaterialR
import kotlin.math.roundToInt
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.YuzuApplication
import org.yuzu.yuzu_emu.features.settings.model.BooleanSetting
import org.yuzu.yuzu_emu.features.settings.model.IntSetting
import org.yuzu.yuzu_emu.features.settings.model.Settings
import org.yuzu.yuzu_emu.ui.main.ThemeProvider
import androidx.preference.PreferenceManager

object ThemeHelper {
    const val SYSTEM_BAR_ALPHA = 0.9f

    private var listener: SharedPreferences.OnSharedPreferenceChangeListener? = null
    private val preferences = PreferenceManager.getDefaultSharedPreferences(
        YuzuApplication.appContext
    )

    fun setTheme(activity: AppCompatActivity) {
        setThemeMode(activity)
        when (Theme.from(IntSetting.THEME.getInt())) {
            Theme.Default -> activity.setTheme(getSelectedStaticThemeStyle())
            Theme.MaterialYou -> {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    activity.setTheme(R.style.Theme_Yuzu_Main_MaterialYou)
                } else {
                    activity.setTheme(getSelectedStaticThemeStyle())
                }
            }
        }

        if (BooleanSetting.BLACK_BACKGROUNDS.getBoolean() && isNightMode(activity)) {
            activity.setTheme(R.style.ThemeOverlay_Yuzu_Dark)
        }
    }

    fun getSelectedStaticThemeStyle(): Int {
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
        return themes[themeIndex.coerceIn(0, themes.lastIndex)]
    }

    fun activeThemeStyleRes(): Int {
        return when (Theme.from(IntSetting.THEME.getInt())) {
            Theme.MaterialYou -> {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    R.style.Theme_Yuzu_Main_MaterialYou
                } else {
                    getSelectedStaticThemeStyle()
                }
            }
            Theme.Default -> getSelectedStaticThemeStyle()
        }
    }

    /**
     * Context with the same theme style and light/dark configuration as the main app UI
     * (so Material You dynamic colors and night resources resolve correctly).
     */
    fun themedContext(base: Context = YuzuApplication.appContext): Context {
        val style = activeThemeStyleRes()
        val configuration = Configuration(base.resources.configuration)
        when (IntSetting.THEME_MODE.getInt()) {
            AppCompatDelegate.MODE_NIGHT_NO ->
                configuration.uiMode = configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK.inv() or
                    Configuration.UI_MODE_NIGHT_NO
            AppCompatDelegate.MODE_NIGHT_YES ->
                configuration.uiMode = configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK.inv() or
                    Configuration.UI_MODE_NIGHT_YES
        }
        val configured = base.createConfigurationContext(configuration)
        return ContextThemeWrapper(configured, style)
    }

    @ColorInt
    fun resolveColorPrimary(context: Context = YuzuApplication.appContext): Int =
        resolveThemeColor(context, MaterialR.attr.colorPrimary)

    @ColorInt
    fun resolveColorSecondary(context: Context = YuzuApplication.appContext): Int =
        resolveThemeColor(context, MaterialR.attr.colorSecondary)

    @ColorInt
    private fun resolveThemeColor(context: Context, attr: Int): Int {
        val themed = themedContext(context)
        val typedValue = TypedValue()
        if (!themed.theme.resolveAttribute(attr, typedValue, true)) {
            return Color.WHITE
        }
        return typedValue.data
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

    fun setCorrectTheme(activity: AppCompatActivity) {
        val currentTheme = (activity as ThemeProvider).themeId
        setTheme(activity)
        if (currentTheme != (activity as ThemeProvider).themeId) {
            activity.recreate()
        }
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
                Settings.PREF_THEME,
                Settings.PREF_STATIC_THEME_COLOR,
                Settings.PREF_THEME_MODE,
                Settings.PREF_BLACK_BACKGROUNDS
            )
            if (key in relevantKeys) {
                activity.recreate()
            }
        }
        preferences.registerOnSharedPreferenceChangeListener(listener)
    }
}

enum class Theme(val int: Int) {
    Default(0),
    MaterialYou(1);

    companion object {
        fun from(int: Int): Theme = entries.firstOrNull { it.int == int } ?: Default
    }
}

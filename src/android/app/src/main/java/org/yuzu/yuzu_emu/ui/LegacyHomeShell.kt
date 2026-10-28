// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.ui

import android.os.Bundle
import android.view.View
import androidx.constraintlayout.widget.ConstraintLayout
import androidx.core.view.ViewCompat
import androidx.navigation.NavController
import androidx.navigation.ui.setupWithNavController
import com.google.android.material.navigation.NavigationBarView
import android.view.animation.PathInterpolator
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.databinding.ActivityMainBinding
import org.yuzu.yuzu_emu.model.GamesViewModel
import org.yuzu.yuzu_emu.model.HomeViewModel
import org.yuzu.yuzu_emu.ui.main.MainActivity
import org.yuzu.yuzu_emu.utils.LegacyUi
import org.yuzu.yuzu_emu.utils.ThemeHelper
import org.yuzu.yuzu_emu.utils.ViewUtils.setVisible
import org.yuzu.yuzu_emu.utils.collect

/**
 * Yuzu-style bottom navigation and home layout. No-op when [LegacyUi.isEnabled] is false.
 */
object LegacyHomeShell {
    private const val SAVED_LEGACY_SHELL = "SavedLegacyYuzuShell"

    fun applyChrome(binding: ActivityMainBinding, includeNavigationBarShade: Boolean) {
        if (!LegacyUi.isEnabled()) {
            return
        }
        val content = ThemeHelper.getLegacyContentBackgroundColor(binding.root)
        val navChrome = ThemeHelper.getLegacyChromeBackgroundColor(binding.root)
        binding.coordinatorMain.setBackgroundColor(content)
        binding.statusBarShade.setBackgroundColor(ThemeHelper.getLegacyStatusBarScrimColor(binding.root))
        (binding.navigationView as? NavigationBarView)?.setBackgroundColor(navChrome)
        if (includeNavigationBarShade) {
            binding.navigationBarShade.setBackgroundColor(content)
        }
    }

    fun onCreate(
        activity: MainActivity,
        binding: ActivityMainBinding,
        savedInstanceState: Bundle?,
        homeViewModel: HomeViewModel,
        gamesViewModel: GamesViewModel,
    ) {
        if (!LegacyUi.isEnabled()) {
            binding.navigationView?.visibility = View.GONE
            return
        }

        applyHomeShellLayout(binding)
        binding.navigationView?.visibility = View.VISIBLE

        val navigationBar = binding.navigationView as? NavigationBarView
        applyChrome(binding, includeNavigationBarShade = false)
        navigationBar?.setOnItemReselectedListener {
            when (it.itemId) {
                R.id.gamesFragment -> gamesViewModel.setShouldScrollToTop(true)
                R.id.searchFragment -> gamesViewModel.setSearchFocused(true)
                R.id.homeSettingsFragment -> { /* tab root */ }
            }
        }

        if (!homeViewModel.navigationVisible.value.first) {
            navigationBar?.setVisible(visible = false, gone = false)
            binding.statusBarShade.setVisible(visible = false, gone = false)
        }

        homeViewModel.navigationVisible.collect(activity) { (visible, animated) ->
            showNavigation(binding, visible, animated)
        }
    }

    fun onSaveInstanceState(outState: Bundle) {
        outState.putBoolean(SAVED_LEGACY_SHELL, LegacyUi.isEnabled())
    }

    fun onSetUpNavigation(binding: ActivityMainBinding, navController: NavController) {
        if (!LegacyUi.isEnabled()) {
            return
        }
        (binding.navigationView as? NavigationBarView)?.setupWithNavController(navController)
    }

    fun onFinishSetup(binding: ActivityMainBinding, navController: NavController) {
        if (!LegacyUi.isEnabled()) {
            return
        }
        showNavigation(binding, visible = true, animated = true)
    }


    fun onHomeSettingsViewCreated(homeViewModel: HomeViewModel, toolbar: com.google.android.material.appbar.MaterialToolbar) {
        if (!LegacyUi.isEnabled()) {
            return
        }
        homeViewModel.setNavigationVisibility(visible = true, animated = true)
        toolbar.navigationIcon = null
        toolbar.setNavigationOnClickListener(null)
    }

    private fun applyHomeShellLayout(binding: ActivityMainBinding) {
        if (!LegacyUi.isEnabled()) {
            return
        }
        if (binding.navigationView == null) {
            return
        }
        binding.navigationView?.visibility = View.VISIBLE
    }

    private fun showNavigation(binding: ActivityMainBinding, visible: Boolean, animated: Boolean) {
        if (!LegacyUi.isEnabled()) {
            return
        }
        val navigationView = binding.navigationView as? NavigationBarView ?: return
        if (!animated) {
            navigationView.setVisible(visible)
            return
        }

        val smallLayout = binding.root.resources.getBoolean(R.bool.small_layout)
        navigationView.animate().apply {
            if (visible) {
                navigationView.setVisible(true)
                duration = 300
                interpolator = PathInterpolator(0.05f, 0.7f, 0.1f, 1f)

                if (smallLayout) {
                    navigationView.translationY = navigationView.height.toFloat() * 2
                    translationY(0f)
                } else {
                    if (ViewCompat.getLayoutDirection(navigationView) == ViewCompat.LAYOUT_DIRECTION_LTR) {
                        navigationView.translationX = navigationView.width.toFloat() * -2
                        translationX(0f)
                    } else {
                        navigationView.translationX = navigationView.width.toFloat() * 2
                        translationX(0f)
                    }
                }
            } else {
                duration = 300
                interpolator = PathInterpolator(0.3f, 0f, 0.8f, 0.15f)

                if (smallLayout) {
                    translationY(navigationView.height.toFloat() * 2)
                } else {
                    if (ViewCompat.getLayoutDirection(navigationView) == ViewCompat.LAYOUT_DIRECTION_LTR) {
                        translationX(navigationView.width.toFloat() * -2)
                    } else {
                        translationX(navigationView.width.toFloat() * 2)
                    }
                }
            }
        }.withEndAction {
            if (!visible) {
                navigationView.setVisible(visible = false, gone = false)
            }
        }.start()
    }
}

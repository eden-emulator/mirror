// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.ui

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import androidx.recyclerview.widget.RecyclerView
import androidx.swiperefreshlayout.widget.SwipeRefreshLayout
import com.google.android.material.floatingactionbutton.ExtendedFloatingActionButton
import com.google.android.material.textview.MaterialTextView
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.utils.LegacyUi

class GamesFragmentViews private constructor(val root: View) {
    val legacyMode: Boolean = LegacyUi.isEnabled()

    val header: View? = root.findViewById(R.id.header)
    val title: MaterialTextView? = root.findViewById(R.id.title)
    val viewButton: View? = root.findViewById(R.id.view_button)
    val filterButton: View? = root.findViewById(R.id.filter_button)
    val settingsButton: View? = root.findViewById(R.id.settings_button)
    val searchBackground: View? = root.findViewById(R.id.search_background)
    val searchText: EditText? = root.findViewById(R.id.search_text)
    val clearButton: View? = root.findViewById(R.id.clear_button)
    val swipeRefresh: SwipeRefreshLayout = root.findViewById(R.id.swipe_refresh)
    val noticeText: MaterialTextView = root.findViewById(R.id.notice_text)
    val gridGames: RecyclerView = root.findViewById(R.id.grid_games)
    val addDirectory: ExtendedFloatingActionButton? = root.findViewById(R.id.add_directory)
    val launchQlaunch: ExtendedFloatingActionButton? = root.findViewById(R.id.launch_qlaunch)

    companion object {
        fun inflate(
            inflater: LayoutInflater,
            container: ViewGroup?,
            attachToRoot: Boolean = false
        ): GamesFragmentViews {
            val layoutId = if (LegacyUi.isEnabled()) {
                R.layout.fragment_games_legacy
            } else {
                R.layout.fragment_games
            }
            return GamesFragmentViews(
                inflater.inflate(layoutId, container, attachToRoot)
            )
        }
    }
}

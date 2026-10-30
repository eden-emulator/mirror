// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.ui

import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.content.res.Configuration
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.InputMethodManager
import android.widget.PopupMenu
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.doOnPreDraw
import androidx.core.view.updatePadding
import androidx.core.widget.doOnTextChanged
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.navigation.fragment.findNavController
import androidx.navigation.navOptions
import androidx.preference.PreferenceManager
import androidx.recyclerview.widget.RecyclerView
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.swiperefreshlayout.widget.SwipeRefreshLayout
import org.yuzu.yuzu_emu.HomeNavigationDirections
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.YuzuApplication
import org.yuzu.yuzu_emu.adapters.GameAdapter
import org.yuzu.yuzu_emu.layout.AutofitGridLayoutManager
import org.yuzu.yuzu_emu.utils.LegacyUi
import org.yuzu.yuzu_emu.utils.ViewUtils.updateMargins
import com.google.android.material.color.MaterialColors
import org.yuzu.yuzu_emu.features.settings.model.BooleanSetting
import org.yuzu.yuzu_emu.model.AppletInfo
import org.yuzu.yuzu_emu.model.Game
import org.yuzu.yuzu_emu.model.GamesViewModel
import org.yuzu.yuzu_emu.model.HomeViewModel
import org.yuzu.yuzu_emu.ui.main.MainActivity
import org.yuzu.yuzu_emu.utils.ViewUtils.setVisible
import org.yuzu.yuzu_emu.utils.collect
import info.debatty.java.stringsimilarity.Jaccard
import info.debatty.java.stringsimilarity.JaroWinkler
import java.util.Locale
import androidx.core.content.edit

class GamesFragment : Fragment() {
    private var _views: GamesFragmentViews? = null
    private val views get() = _views!!

    private var originalHeaderTopMargin: Int? = null
    private var originalHeaderBottomMargin: Int? = null
    private var originalHeaderRightMargin: Int? = null
    private var originalHeaderLeftMargin: Int? = null

    private var lastViewType: Int = GameAdapter.VIEW_TYPE_GRID
    private var pendingPostReloadListSettle = false
    private var pendingPostReloadListSettleGeneration = 0
    private var gameListSubmitGeneration = 0
    private var committedGameListSubmitGeneration = 0

    companion object {
        private const val SEARCH_TEXT = "SearchText"
        private const val PREF_SORT_TYPE = "GamesSortType"
        private const val SHELL_LEGACY_YUZU = "ShellLegacyYuzuUi"
    }

    private val gamesViewModel: GamesViewModel by activityViewModels()
    private val homeViewModel: HomeViewModel by activityViewModels()
    private lateinit var gameAdapter: GameAdapter

    private val preferences =
        PreferenceManager.getDefaultSharedPreferences(YuzuApplication.appContext)

    private lateinit var mainActivity: MainActivity
    private val getGamesDirectory =
        registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { result ->
            if (result != null) {
                mainActivity.processGamesDir(result, true)
            }
        }

    private fun getCurrentViewType(): Int {
        val isLandscape = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
        val key = if (isLandscape) CarouselRecyclerView.CAROUSEL_VIEW_TYPE_LANDSCAPE else CarouselRecyclerView.CAROUSEL_VIEW_TYPE_PORTRAIT
        val fallback = if (isLandscape && !LegacyUi.isEnabled()) {
            GameAdapter.VIEW_TYPE_CAROUSEL
        } else {
            GameAdapter.VIEW_TYPE_GRID
        }
        val stored = preferences.getInt(key, fallback)
        return if (LegacyUi.isEnabled() && stored == GameAdapter.VIEW_TYPE_CAROUSEL) {
            GameAdapter.VIEW_TYPE_GRID
        } else {
            stored
        }
    }

    private fun setCurrentViewType(type: Int) {
        val isLandscape = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
        val key = if (isLandscape) CarouselRecyclerView.CAROUSEL_VIEW_TYPE_LANDSCAPE else CarouselRecyclerView.CAROUSEL_VIEW_TYPE_PORTRAIT
        preferences.edit { putInt(key, type) }
    }
    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        val legacyShell = LegacyUi.isEnabled()
        val savedShell = savedInstanceState?.getBoolean(SHELL_LEGACY_YUZU, legacyShell) ?: legacyShell
        if (savedInstanceState != null && savedShell != legacyShell) {
            _views = null
        }
        _views = GamesFragmentViews.inflate(inflater, container, false)
        return views.root
    }

    @SuppressLint("NotifyDataSetChanged")
    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        homeViewModel.setStatusBarShadeVisibility(true)
        mainActivity = requireActivity() as MainActivity

        if (LegacyUi.isEnabled()) {
            setupLegacyGamesView(savedInstanceState)
            return
        }

        homeViewModel.setNavigationVisibility(visible = false, animated = false)

        if (savedInstanceState != null) {
            views.searchText?.setText(savedInstanceState.getString(SEARCH_TEXT))
        }

        gameAdapter = GameAdapter(
            requireActivity() as AppCompatActivity
        )

        applyGridGamesBinding()

        views.swipeRefresh.apply {
            (views.swipeRefresh as? SwipeRefreshLayout)?.setOnRefreshListener {
                gamesViewModel.reloadGames(false)
            }
            (views.swipeRefresh as? SwipeRefreshLayout)?.setProgressBackgroundColorSchemeColor(
                com.google.android.material.color.MaterialColors.getColor(
                    views.swipeRefresh,
                    com.google.android.material.R.attr.colorPrimary
                )
            )
            (views.swipeRefresh as? SwipeRefreshLayout)?.setColorSchemeColors(
                com.google.android.material.color.MaterialColors.getColor(
                    views.swipeRefresh,
                    com.google.android.material.R.attr.colorOnPrimary
                )
            )
            post {
                if (_views == null) {
                    return@post
                }
                (views.swipeRefresh as? SwipeRefreshLayout)?.isRefreshing = gamesViewModel.isReloading.value
            }
        }

        gamesViewModel.isReloading.collect(viewLifecycleOwner) {
            (views.swipeRefresh as? SwipeRefreshLayout)?.isRefreshing = it
            views.noticeText.setVisible(
                visible = gamesViewModel.games.value.isEmpty() && !it,
                gone = false
            )
        }
        gamesViewModel.games.collect(viewLifecycleOwner) {
            if (it.isNotEmpty()) {
                setAdapter(it)
            }
        }
        gamesViewModel.shouldSwapData.collect(
            viewLifecycleOwner,
            resetState = { gamesViewModel.setShouldSwapData(false) }
        ) {
            if (it) {
                setAdapter(gamesViewModel.games.value)
            }
        }
        gamesViewModel.shouldScrollToTop.collect(
            viewLifecycleOwner,
            resetState = { gamesViewModel.setShouldScrollToTop(false) }
        ) { if (it) scrollToTop() }

        gamesViewModel.shouldScrollAfterReload.collect(viewLifecycleOwner) { shouldScroll ->
            if (shouldScroll) {
                pendingPostReloadListSettle = true
                pendingPostReloadListSettleGeneration = gameListSubmitGeneration
                schedulePostReloadListSettle()
                gamesViewModel.setShouldScrollAfterReload(false)
            }
        }

        setupTopView()

        updateButtonsVisibility()

        views.addDirectory?.setOnClickListener {
            getGamesDirectory.launch(Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).data)
        }

        views.launchQlaunch?.setOnClickListener {
            launchQLaunch()
        }

        setInsets()
    }

    private fun setupLegacyGamesView(savedInstanceState: Bundle?) {
        homeViewModel.setNavigationVisibility(visible = true, animated = true)

        gameAdapter = GameAdapter(requireActivity() as AppCompatActivity)
        views.gridGames.apply {
            layoutManager = AutofitGridLayoutManager(
                requireContext(),
                requireContext().resources.getDimensionPixelSize(R.dimen.card_width)
            )
            adapter = gameAdapter
        }

        views.swipeRefresh.apply {
            setOnRefreshListener {
                gamesViewModel.reloadGames(false)
            }
            setProgressBackgroundColorSchemeColor(
                MaterialColors.getColor(
                    views.swipeRefresh,
                    com.google.android.material.R.attr.colorPrimary
                )
            )
            setColorSchemeColors(
                MaterialColors.getColor(
                    views.swipeRefresh,
                    com.google.android.material.R.attr.colorOnPrimary
                )
            )
            post {
                if (_views == null) {
                    return@post
                }
                views.swipeRefresh.isRefreshing = gamesViewModel.isReloading.value
            }
        }

        gamesViewModel.isReloading.collect(viewLifecycleOwner) {
            views.swipeRefresh.isRefreshing = it
            views.noticeText.setVisible(
                visible = gamesViewModel.games.value.isEmpty() && !it,
                gone = false
            )
        }
        gamesViewModel.games.collect(viewLifecycleOwner) {
            gameAdapter.submitList(it)
        }
        gamesViewModel.shouldSwapData.collect(
            viewLifecycleOwner,
            resetState = { gamesViewModel.setShouldSwapData(false) }
        ) {
            if (it) {
                gameAdapter.submitList(gamesViewModel.games.value)
            }
        }
        gamesViewModel.shouldScrollToTop.collect(
            viewLifecycleOwner,
            resetState = { gamesViewModel.setShouldScrollToTop(false) }
        ) { if (it) scrollToTop() }

        setLegacyInsets()
    }

    val applyGridGamesBinding = {
        views.gridGames.apply {
            val isLandscape = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
            val currentViewType = getCurrentViewType()
            val savedViewType = if (isLandscape || currentViewType != GameAdapter.VIEW_TYPE_CAROUSEL) currentViewType else GameAdapter.VIEW_TYPE_GRID

            //This prevents Grid/List views from reusing scaled or otherwise modified ViewHolders left over from the carousel.
            adapter = null
            recycledViewPool.clear()

            gameAdapter.setViewType(savedViewType)
            currentFilter = preferences.getInt(PREF_SORT_TYPE, View.NO_ID)

            // Set the correct layout manager
            layoutManager = when (savedViewType) {
                GameAdapter.VIEW_TYPE_GRID -> {
                    val columns = resources.getInteger(R.integer.game_columns_grid)
                    GridLayoutManager(context, columns)
                }
                GameAdapter.VIEW_TYPE_GRID_COMPACT -> {
                    val columns = resources.getInteger(R.integer.game_columns_grid)
                    GridLayoutManager(context, columns)
                }
                GameAdapter.VIEW_TYPE_LIST -> {
                    val columns = resources.getInteger(R.integer.game_columns_list)
                    GridLayoutManager(context, columns)
                }
                GameAdapter.VIEW_TYPE_CAROUSEL -> {
                    LinearLayoutManager(context, RecyclerView.HORIZONTAL, false)
                }
                else -> throw IllegalArgumentException("Invalid view type: $savedViewType")
            }
            if (savedViewType != GameAdapter.VIEW_TYPE_CAROUSEL) {
                (this as? CarouselRecyclerView)?.setupCarousel(false)
            }
            adapter = gameAdapter
            lastViewType = savedViewType
        }
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putBoolean(SHELL_LEGACY_YUZU, LegacyUi.isEnabled())
        _views?.searchText?.text?.toString()?.let { outState.putString(SEARCH_TEXT, it) }
    }

    override fun onPause() {
        super.onPause()
        if (_views == null) {
            return
        }
        if (getCurrentViewType() == GameAdapter.VIEW_TYPE_CAROUSEL) {
            gamesViewModel.lastScrollPosition = (views.gridGames as? CarouselRecyclerView)?.getClosestChildPosition() ?: 0
        }
    }

    override fun onResume() {
        super.onResume()
        if (_views == null) {
            return
        }
        if (LegacyUi.isEnabled()) {
            val destinationId = findNavController().currentDestination?.id
            val showNav = destinationId == R.id.gamesFragment ||
                destinationId == R.id.searchFragment ||
                destinationId == R.id.homeSettingsFragment
            homeViewModel.setNavigationVisibility(visible = showNav, animated = false)
        }
        if (getCurrentViewType() == GameAdapter.VIEW_TYPE_CAROUSEL) {
            (views.gridGames as? CarouselRecyclerView)?.setupCarousel(true)
            (views.gridGames as? CarouselRecyclerView)?.restoreScrollState(gamesViewModel.lastScrollPosition)
        }
    }

    private var lastSearchText: String = ""
    private var lastFilter: Int = preferences.getInt(PREF_SORT_TYPE, View.NO_ID)

    private fun setAdapter(games: List<Game>) {
        if (_views == null) return
        val currentSearchText = views.searchText?.text?.toString() ?: ""
        val currentFilter = views.filterButton?.id ?: View.NO_ID

        val searchChanged = currentSearchText != lastSearchText
        val filterChanged = currentFilter != lastFilter

        if (searchChanged || filterChanged) {
            filterAndSearch(games)
            lastSearchText = currentSearchText
            lastFilter = currentFilter
        } else {
            submitGameList(games)
            gamesViewModel.setFilteredGames(games)
        }
    }

    private fun submitGameList(games: List<Game>) {
        val adapter = (views.gridGames as? RecyclerView)?.adapter as? GameAdapter
        if (adapter == null) {
            schedulePostReloadListSettle()
            return
        }

        val submitGeneration = ++gameListSubmitGeneration
        adapter.submitList(games) {
            if (committedGameListSubmitGeneration < submitGeneration) {
                committedGameListSubmitGeneration = submitGeneration
            }
            schedulePostReloadListSettle()
        }
    }


    private fun schedulePostReloadListSettle() {
        if (!pendingPostReloadListSettle || _views == null) return

        views.gridGames.doOnPreDraw {
            if (!pendingPostReloadListSettle || _views == null) return@doOnPreDraw
            if (committedGameListSubmitGeneration < pendingPostReloadListSettleGeneration) {
                schedulePostReloadListSettle()
                return@doOnPreDraw
            }
            pendingPostReloadListSettle = false

            (views.gridGames as? CarouselRecyclerView)?.refreshView()
        }
    }
    private fun setupTopView() {
        val searchText = views.searchText ?: return
        val clearButton = views.clearButton ?: return
        views.searchBackground ?: return
        views.viewButton ?: return
        views.filterButton ?: return
        views.settingsButton ?: return

        searchText.doOnTextChanged { text: CharSequence?, _: Int, _: Int, _: Int ->
            if (text.toString().isNotEmpty()) {
                clearButton.visibility = View.VISIBLE
            } else {
                clearButton.visibility = View.INVISIBLE
            }
            filterAndSearch()
        }

        clearButton.setOnClickListener { searchText.setText("") }
        views.searchBackground?.setOnClickListener { focusSearch() }

        views.viewButton?.setOnClickListener { showViewMenu(it) }
        views.filterButton?.setOnClickListener { view -> showFilterMenu(view) }
        views.settingsButton?.setOnClickListener { navigateToSettings() }
    }

    private fun navigateToSettings() {
        val navController = findNavController()
        if (LegacyUi.isEnabled()) {
            navController.navigate(
                R.id.homeSettingsFragment,
                null,
                navOptions {
                    launchSingleTop = true
                    popUpTo(R.id.gamesFragment) {
                        saveState = true
                    }
                    restoreState = true
                }
            )
        } else {
            navController.navigate(R.id.action_gamesFragment_to_homeSettingsFragment)
        }
    }

    private fun showViewMenu(anchor: View) {
        val popup = PopupMenu(requireContext(), anchor)
        popup.menuInflater.inflate(R.menu.menu_game_views, popup.menu)
        val isLandscape = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
        if (!isLandscape || LegacyUi.isEnabled()) {
            popup.menu.findItem(R.id.view_carousel)?.isVisible = false
        }

        val currentViewType = getCurrentViewType()
        when (currentViewType) {
            GameAdapter.VIEW_TYPE_LIST -> popup.menu.findItem(R.id.view_list).isChecked = true
            GameAdapter.VIEW_TYPE_GRID_COMPACT -> popup.menu.findItem(R.id.view_grid_compact).isChecked = true
            GameAdapter.VIEW_TYPE_GRID -> popup.menu.findItem(R.id.view_grid).isChecked = true
            GameAdapter.VIEW_TYPE_CAROUSEL -> popup.menu.findItem(R.id.view_carousel).isChecked = true
        }

        popup.setOnMenuItemClickListener { item ->
            when (item.itemId) {
                R.id.view_grid -> {
                    if (getCurrentViewType() == GameAdapter.VIEW_TYPE_CAROUSEL) onPause()
                    setCurrentViewType(GameAdapter.VIEW_TYPE_GRID)
                    applyGridGamesBinding()
                    item.isChecked = true
                    true
                }

                R.id.view_grid_compact -> {
                    if (getCurrentViewType() == GameAdapter.VIEW_TYPE_CAROUSEL) onPause()
                    setCurrentViewType(GameAdapter.VIEW_TYPE_GRID_COMPACT)
                    applyGridGamesBinding()
                    item.isChecked = true
                    true
                }

                R.id.view_list -> {
                    if (getCurrentViewType() == GameAdapter.VIEW_TYPE_CAROUSEL) onPause()
                    setCurrentViewType(GameAdapter.VIEW_TYPE_LIST)
                    applyGridGamesBinding()
                    item.isChecked = true
                    true
                }

                R.id.view_carousel -> {
                    if (!item.isChecked || getCurrentViewType() != GameAdapter.VIEW_TYPE_CAROUSEL) {
                        setCurrentViewType(GameAdapter.VIEW_TYPE_CAROUSEL)
                        applyGridGamesBinding()
                        item.isChecked = true
                        onResume()
                    }
                    true
                }

                else -> false
            }
        }

        popup.show()
    }

    private fun showFilterMenu(anchor: View) {
        val popup = PopupMenu(requireContext(), anchor)
        popup.menuInflater.inflate(R.menu.menu_game_filters, popup.menu)

        // Set checked state based on current filter
        when (currentFilter) {
            R.id.alphabetical -> popup.menu.findItem(R.id.alphabetical).isChecked = true
            R.id.filter_recently_played -> popup.menu.findItem(R.id.filter_recently_played).isChecked =
                true

            R.id.filter_recently_added -> popup.menu.findItem(R.id.filter_recently_added).isChecked =
                true
        }

        popup.setOnMenuItemClickListener { item ->
            currentFilter = item.itemId
            preferences.edit { putInt(PREF_SORT_TYPE, currentFilter) }
            filterAndSearch()
            true
        }

        popup.show()
    }

    // Track current filter
    private var currentFilter = View.NO_ID

    private fun filterAndSearch(baseList: List<Game> = gamesViewModel.games.value) {
        val filteredList: List<Game> = when (currentFilter) {
            R.id.alphabetical -> baseList.sortedBy { it.title }
            R.id.filter_recently_played -> {
                baseList.filter {
                    val lastPlayedTime = preferences.getLong(it.keyLastPlayedTime, 0L)
                    lastPlayedTime > (System.currentTimeMillis() - 24 * 60 * 60 * 1000)
                }.sortedByDescending { preferences.getLong(it.keyLastPlayedTime, 0L) }
            }
            R.id.filter_recently_added -> {
                baseList.filter {
                    val addedTime = preferences.getLong(it.keyAddedToLibraryTime, 0L)
                    addedTime > (System.currentTimeMillis() - 24 * 60 * 60 * 1000)
                }.sortedByDescending { preferences.getLong(it.keyAddedToLibraryTime, 0L) }
            }
            else -> baseList
        }

        val searchTerm = views.searchText?.text?.toString()?.lowercase(Locale.getDefault()) ?: ""
        if (searchTerm.isEmpty()) {
            submitGameList(filteredList)
            gamesViewModel.setFilteredGames(filteredList)
            return
        }

        val searchAlgorithm = if (searchTerm.length > 1) Jaccard(2) else JaroWinkler()
        val sortedList = filteredList.mapNotNull { game ->
            val title = game.title.lowercase(Locale.getDefault())
            val score = searchAlgorithm.similarity(searchTerm, title)
            if (score > 0.03) {
                ScoredGame(score, game)
            } else {
                null
            }
        }.sortedByDescending { it.score }.map { it.item }

        submitGameList(sortedList)
        gamesViewModel.setFilteredGames(sortedList)
    }

    private inner class ScoredGame(val score: Double, val item: Game)

    private fun focusSearch() {
        val searchText = views.searchText ?: return
        searchText.requestFocus()
        val imm = requireActivity()
            .getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager?
        imm?.showSoftInput(searchText, InputMethodManager.SHOW_IMPLICIT)
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _views = null
    }

    private fun scrollToTop() {
        if (_views != null) {
            if (LegacyUi.isEnabled()) {
                views.gridGames.smoothScrollToPosition(0)
            } else {
                (views.gridGames as? CarouselRecyclerView)?.smoothScrollToPosition(0)
            }
        }
    }

    private fun launchQLaunch() {
        try {
            val appletPath = NativeLibrary.getAppletLaunchPath(AppletInfo.QLaunch.entryId)
            if (appletPath.isEmpty()) {
                Toast.makeText(
                    requireContext(),
                    R.string.applets_error_applet,
                    Toast.LENGTH_SHORT
                ).show()
                return
            }

            NativeLibrary.setCurrentAppletId(AppletInfo.QLaunch.appletId)

            val qlaunchGame = Game(
                title = getString(R.string.qlaunch_applet),
                path = appletPath
            )

            val action = HomeNavigationDirections.actionGlobalEmulationActivity(qlaunchGame)
            findNavController().navigate(action)
        } catch (e: Exception) {
            Toast.makeText(
                requireContext(),
                "Failed to launch QLaunch: ${e.message}",
                Toast.LENGTH_SHORT
            ).show()
        }
    }

    private fun updateButtonsVisibility() {
        val showQLaunch = BooleanSetting.ENABLE_QLAUNCH_BUTTON.getBoolean()
        val showFolder = BooleanSetting.ENABLE_FOLDER_BUTTON.getBoolean()
        val isFirmwareAvailable = NativeLibrary.isFirmwareAvailable()

        val shouldShowQLaunch = showQLaunch && isFirmwareAvailable
        views.launchQlaunch?.visibility = if (shouldShowQLaunch) View.VISIBLE else View.GONE

        views.addDirectory?.visibility = if (showFolder) View.VISIBLE else View.GONE
    }

    private fun setLegacyInsets() =
        ViewCompat.setOnApplyWindowInsetsListener(
            views.root
        ) { view: View, windowInsets: WindowInsetsCompat ->
            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())
            val extraListSpacing = resources.getDimensionPixelSize(R.dimen.spacing_large)
            val spacingNavigation = resources.getDimensionPixelSize(R.dimen.spacing_navigation)
            val spacingNavigationRail =
                resources.getDimensionPixelSize(R.dimen.spacing_navigation_rail)

            views.gridGames.updatePadding(
                top = barInsets.top + extraListSpacing,
                bottom = barInsets.bottom + spacingNavigation + extraListSpacing
            )

            views.swipeRefresh.setProgressViewEndTarget(
                false,
                barInsets.top + resources.getDimensionPixelSize(R.dimen.spacing_refresh_end)
            )

            val leftInsets = barInsets.left + cutoutInsets.left
            val rightInsets = barInsets.right + cutoutInsets.right
            val left: Int
            val right: Int
            if (ViewCompat.getLayoutDirection(view) == ViewCompat.LAYOUT_DIRECTION_LTR) {
                left = leftInsets + spacingNavigationRail
                right = rightInsets
            } else {
                left = leftInsets
                right = rightInsets + spacingNavigationRail
            }
            views.swipeRefresh.updateMargins(left = left, right = right)

            views.noticeText.updatePadding(bottom = spacingNavigation)

            windowInsets
        }

    private fun setInsets() =
        ViewCompat.setOnApplyWindowInsetsListener(
            views.root
        ) { _: View, windowInsets: WindowInsetsCompat ->
            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())
            val spacingNavigation = resources.getDimensionPixelSize(R.dimen.spacing_navigation)
            resources.getDimensionPixelSize(R.dimen.spacing_navigation_rail)

            (views.swipeRefresh as? SwipeRefreshLayout)?.setProgressViewEndTarget(
                false,
                barInsets.top + resources.getDimensionPixelSize(R.dimen.spacing_refresh_end)
            )

            val leftInset = barInsets.left + cutoutInsets.left
            val rightInset = barInsets.right + cutoutInsets.right
            val topInset = maxOf(barInsets.top, cutoutInsets.top)

            val mlpSwipe = views.swipeRefresh.layoutParams as ViewGroup.MarginLayoutParams
            mlpSwipe.leftMargin = leftInset
            mlpSwipe.rightMargin = rightInset
            views.swipeRefresh.layoutParams = mlpSwipe

            val header = views.header ?: return@setOnApplyWindowInsetsListener windowInsets
            val addDirectory = views.addDirectory ?: return@setOnApplyWindowInsetsListener windowInsets
            val mlpHeader = header.layoutParams as ViewGroup.MarginLayoutParams

            // Store original margins only once
            if (originalHeaderTopMargin == null) {
                originalHeaderTopMargin = mlpHeader.topMargin
                originalHeaderRightMargin = mlpHeader.rightMargin
                originalHeaderLeftMargin = mlpHeader.leftMargin
            }

            // Always set margin as original + insets
            mlpHeader.leftMargin = (originalHeaderLeftMargin ?: 0) + leftInset
            mlpHeader.rightMargin = (originalHeaderRightMargin ?: 0) + rightInset
            mlpHeader.topMargin = (originalHeaderTopMargin ?: 0) + topInset + resources.getDimensionPixelSize(
                R.dimen.spacing_med
            )
            header.layoutParams = mlpHeader

            views.noticeText.updatePadding(bottom = spacingNavigation)

            views.gridGames.updatePadding(
                top = resources.getDimensionPixelSize(R.dimen.spacing_med)
            )

            val mlpFab = addDirectory.layoutParams as ViewGroup.MarginLayoutParams
            val fabPadding = resources.getDimensionPixelSize(R.dimen.spacing_large)
            mlpFab.leftMargin = leftInset + fabPadding
            mlpFab.bottomMargin = barInsets.bottom + fabPadding
            mlpFab.rightMargin = rightInset + fabPadding
            addDirectory.layoutParams = mlpFab

            views.launchQlaunch?.let { qlaunchButton ->
                val mlpQLaunch = qlaunchButton.layoutParams as ViewGroup.MarginLayoutParams
                mlpQLaunch.leftMargin = leftInset + fabPadding
                mlpQLaunch.bottomMargin = barInsets.bottom + fabPadding
                qlaunchButton.layoutParams = mlpQLaunch
            }

            windowInsets
        }
}

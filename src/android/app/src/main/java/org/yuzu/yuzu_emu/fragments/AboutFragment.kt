// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.fragments

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.navigation.findNavController
import com.google.android.material.transition.MaterialSharedAxis
import org.yuzu.yuzu_emu.HomeNavigationDirections
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.databinding.FragmentAboutBinding
import org.yuzu.yuzu_emu.features.settings.ui.SettingsSubscreen
import org.yuzu.yuzu_emu.model.HomeViewModel
import org.yuzu.yuzu_emu.utils.ViewUtils.updateMargins
import org.yuzu.yuzu_emu.NativeLibrary
import androidx.viewbinding.ViewBinding
import android.widget.TextView
import com.google.android.material.appbar.MaterialToolbar
import org.yuzu.yuzu_emu.databinding.FragmentAboutLegacyBinding
import org.yuzu.yuzu_emu.utils.LegacyUi

class AboutFragment : Fragment() {
    private var _binding: ViewBinding? = null
    private val binding get() = _binding!!

    private fun <T : View> find(id: Int): T = binding.root.findViewById(id)

    private val homeViewModel: HomeViewModel by activityViewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enterTransition = MaterialSharedAxis(MaterialSharedAxis.X, true)
        returnTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
        reenterTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = if (LegacyUi.isEnabled()) {
            FragmentAboutLegacyBinding.inflate(layoutInflater)
        } else {
            FragmentAboutBinding.inflate(layoutInflater)
        }
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        homeViewModel.setStatusBarShadeVisibility(visible = false)
        find<MaterialToolbar>(R.id.toolbar_about).setNavigationOnClickListener {
            requireActivity().onBackPressedDispatcher.onBackPressed()
        }

        find<View>(R.id.image_logo).setOnLongClickListener {
            Toast.makeText(
                requireContext(),
                R.string.gaia_is_not_real,
                Toast.LENGTH_SHORT
            ).show()
            true
        }

        find<View>(R.id.button_contributors).setOnClickListener {
            openLink(
                getString(R.string.contributors_link)
            )
        }
        find<View>(R.id.button_licenses).setOnClickListener {
            val action = HomeNavigationDirections.actionGlobalSettingsSubscreenActivity(
                SettingsSubscreen.LICENSES,
                null
            )
            binding.root.findNavController().navigate(action)
        }

        val buildName = getString(R.string.app_name_suffixed)
        val buildVersion = NativeLibrary.getBuildVersion()
        val fullVersionText = "$buildName ($buildVersion)"

        find<TextView>(R.id.text_version_name).text = fullVersionText
        find<View>(R.id.button_version_name).setOnClickListener {
            val clipBoard =
                requireContext().getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
            val clip = ClipData.newPlainText(getString(R.string.build), fullVersionText)
            clipBoard.setPrimaryClip(clip)

            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
                Toast.makeText(
                    requireContext(),
                    R.string.copied_to_clipboard,
                    Toast.LENGTH_SHORT
                ).show()
            }
        }

        find<View>(R.id.button_discord).setOnClickListener { openLink(getString(R.string.discord_link)) }
        find<View>(R.id.button_stoat).setOnClickListener { openLink(getString(R.string.stoat_link)) }
        find<View>(R.id.button_x).setOnClickListener { openLink(getString(R.string.x_link)) }
        find<View>(R.id.button_website).setOnClickListener { openLink(getString(R.string.website_link)) }
        find<View>(R.id.button_github).setOnClickListener { openLink(getString(R.string.github_link)) }

        setInsets()
    }

    private fun openLink(link: String) {
        val intent = Intent(Intent.ACTION_VIEW, Uri.parse(link))
        startActivity(intent)
    }

    private fun setInsets() =
        ViewCompat.setOnApplyWindowInsetsListener(
            binding.root
        ) { _: View, windowInsets: WindowInsetsCompat ->
            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())

            val leftInsets = barInsets.left + cutoutInsets.left
            val rightInsets = barInsets.right + cutoutInsets.right

            find<View>(R.id.toolbar_about).updateMargins(left = leftInsets, right = rightInsets)
            find<View>(R.id.scroll_about).updateMargins(left = leftInsets, right = rightInsets)

            find<View>(R.id.content_about).updatePadding(bottom = barInsets.bottom)

            windowInsets
        }
}

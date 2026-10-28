// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.yuzu.yuzu_emu.adapters

import android.text.Html
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.res.ResourcesCompat
import com.google.android.material.button.MaterialButton
import org.yuzu.yuzu_emu.databinding.PageSetupBinding
import org.yuzu.yuzu_emu.model.PageState
import org.yuzu.yuzu_emu.model.SetupCallback
import org.yuzu.yuzu_emu.model.SetupPage
import org.yuzu.yuzu_emu.utils.ViewUtils
import org.yuzu.yuzu_emu.viewholder.AbstractViewHolder
import android.content.res.ColorStateList
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.model.ButtonState

class SetupAdapter(
    val activity: AppCompatActivity,
    pages: List<SetupPage>,
    private val setupCallback: SetupCallback,
) : AbstractListAdapter<SetupPage, SetupAdapter.SetupPageViewHolder>(pages) {
    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): SetupPageViewHolder {
        PageSetupBinding.inflate(LayoutInflater.from(parent.context), parent, false)
            .also { return SetupPageViewHolder(it) }
    }

    inner class SetupPageViewHolder(val binding: PageSetupBinding) :
        AbstractViewHolder<SetupPage>(binding) {
        override fun bind(model: SetupPage) {
            binding.pageButtonContainer.removeAllViews()
            resetBottomAreaLayout(model)

            if (model.pageSteps.invoke() == PageState.COMPLETE) {
                showPageCompleteUi(binding, animate = false)
            } else if (model.pageButtons != null) {
                model.pageButtons.forEachIndexed { _, pageButton ->
                    val pageButtonView = LayoutInflater.from(activity)
                        .inflate(
                            R.layout.page_button,
                            binding.pageButtonContainer,
                            false
                        ) as MaterialButton

                    pageButtonView.apply {
                        id = pageButton.titleId
                        icon = ResourcesCompat.getDrawable(
                            activity.resources,
                            pageButton.iconId,
                            activity.theme
                        )
                        text = activity.resources.getString(pageButton.titleId)
                    }

                    pageButtonView.setOnClickListener {
                        pageButton.buttonAction.invoke(setupCallback)
                    }

                    binding.pageButtonContainer.addView(pageButtonView)

                    if (pageButton.buttonState.invoke() == ButtonState.BUTTON_ACTION_COMPLETE) {
                        styleCompletedButton(pageButtonView)
                    }
                }
            }

            binding.icon.setImageDrawable(
                ResourcesCompat.getDrawable(
                    activity.resources,
                    model.iconId,
                    activity.theme
                )
            )
            binding.textTitle.text = activity.resources.getString(model.titleId)
            binding.textDescription.text =
                Html.fromHtml(activity.resources.getString(model.descriptionId), 0)
        }

        fun refreshStepUi(model: SetupPage, animateComplete: Boolean) {
            model.pageButtons?.forEachIndexed { index, pageButton ->
                if (pageButton.buttonState.invoke() != ButtonState.BUTTON_ACTION_COMPLETE) {
                    return@forEachIndexed
                }
                val button = binding.pageButtonContainer.getChildAt(index) as? MaterialButton
                if (button != null) {
                    styleCompletedButton(button)
                }
            }
            if (model.pageSteps.invoke() == PageState.COMPLETE) {
                showPageCompleteUi(binding, animate = animateComplete)
            }
        }

        fun applyStepCompletedUi(pageButtonId: Int, pageFullyCompleted: Boolean) {
            val button = binding.pageButtonContainer.findViewById<MaterialButton>(pageButtonId)

            if (pageFullyCompleted) {
                showPageCompleteUi(binding, animate = true)
            }

            if (button != null) {
                styleCompletedButton(button)
            }
        }

        private fun styleCompletedButton(button: MaterialButton) {
            button.isEnabled = false
            button.alpha = 0.38f
            val disabledColor = button.context.getColor(
                com.google.android.material.R.color.material_on_surface_disabled
            )
            button.setTextColor(disabledColor)
            val tint = ColorStateList.valueOf(disabledColor)
            button.iconTint = tint
            button.icon?.mutate()?.setTintList(tint)
        }

        private fun resetBottomAreaLayout(model: SetupPage) {
            binding.pageButtonContainer.animate().cancel()
            binding.textConfirmation.animate().cancel()

            if (model.pageSteps.invoke() == PageState.COMPLETE) {
                binding.pageButtonContainer.visibility = View.INVISIBLE
                binding.pageButtonContainer.alpha = 0f
                binding.pageButtonContainer.isClickable = false
                binding.textConfirmation.visibility = View.VISIBLE
                binding.textConfirmation.alpha = 1f
            } else {
                // Match page_setup.xml: confirmation is invisible but still participates in layout.
                binding.textConfirmation.visibility = View.INVISIBLE
                binding.textConfirmation.alpha = 0f
                binding.pageButtonContainer.visibility = View.VISIBLE
                binding.pageButtonContainer.alpha = 1f
                binding.pageButtonContainer.isClickable = true
            }
        }

        private fun showPageCompleteUi(binding: PageSetupBinding, animate: Boolean) {
            if (animate) {
                ViewUtils.hideView(binding.pageButtonContainer, 200)
                ViewUtils.showView(binding.textConfirmation, 200)
            } else {
                binding.pageButtonContainer.visibility = View.INVISIBLE
                binding.pageButtonContainer.alpha = 0f
                binding.pageButtonContainer.isClickable = false
                binding.textConfirmation.visibility = View.VISIBLE
                binding.textConfirmation.alpha = 1f
            }
        }
    }
}

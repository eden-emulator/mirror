// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.features.settings.model

import org.yuzu.yuzu_emu.utils.NativePostProcessing
import kotlin.math.roundToInt

abstract class FxUniformSetting(
    protected val index: Int,
    protected val uniform: NativePostProcessing.Uniform,
    protected val component: Int
) : AbstractSetting {
    override val key: String
        get() = "fx_${index}_${uniform.name}_$component"

    override val isRuntimeModifiable: Boolean
        get() = true

    override val pairedSettingKey: String
        get() = ""

    override val isSwitchable: Boolean
        get() = false

    override val isSaveable: Boolean
        get() = true

    override var global: Boolean
        get() = true
        set(_) {}

    protected fun currentValue(): Float {
        if (NativePostProcessing.hasValue(index, uniform.name)) {
            return NativePostProcessing.getValue(index, uniform.name, component)
        }
        return uniform.defaultAt(component)
    }

    protected fun commit(value: Float) {
        NativePostProcessing.setValue(index, uniform.name, component, value)
        NativePostProcessing.persist()
    }

    override fun reset() = commit(uniform.defaultAt(component))
}

class FxUniformSliderSetting(
    index: Int,
    uniform: NativePostProcessing.Uniform,
    component: Int
) : FxUniformSetting(index, uniform, component), AbstractIntSetting {
    override val defaultValue: Any
        get() = ((uniform.defaultAt(component) - uniform.min) / uniform.step).roundToInt()

    override fun getInt(needsGlobal: Boolean): Int =
        ((currentValue() - uniform.min) / uniform.step).roundToInt()

    override fun setInt(value: Int) = commit(uniform.min + value * uniform.step)

    override fun getValueAsString(needsGlobal: Boolean): String {
        if (uniform.kind == NativePostProcessing.KIND_FLOAT) {
            return String.format("%.3f", currentValue())
        }
        return currentValue().roundToInt().toString()
    }
}

class FxUniformChoiceSetting(
    index: Int,
    uniform: NativePostProcessing.Uniform,
    component: Int
) : FxUniformSetting(index, uniform, component), AbstractIntSetting {
    override val defaultValue: Any
        get() = uniform.defaultAt(component).roundToInt()

    override fun getInt(needsGlobal: Boolean): Int = currentValue().roundToInt()

    override fun setInt(value: Int) = commit(value.toFloat())

    override fun getValueAsString(needsGlobal: Boolean): String = getInt().toString()
}

class FxUniformBooleanSetting(
    index: Int,
    uniform: NativePostProcessing.Uniform,
    component: Int
) : FxUniformSetting(index, uniform, component), AbstractBooleanSetting {
    override val defaultValue: Any
        get() = uniform.defaultAt(component) != 0f

    override fun getBoolean(needsGlobal: Boolean): Boolean = currentValue() != 0f

    override fun setBoolean(value: Boolean) {
        if (value) {
            commit(1f)
            return
        }
        commit(0f)
    }

    override fun getValueAsString(needsGlobal: Boolean): String = getBoolean().toString()
}

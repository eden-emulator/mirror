// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#define HAS_RESHADE 1

#include <array>
#include <memory>
#include <string>

#include <jni.h>

#include "common/android/android_common.h"
#ifdef HAS_RESHADE
#include "android_config.h"
#include "video_core/post_processing/fx_chain.h"
#include "common/settings.h"
#include "video_core/post_processing/fx_effect.h"
#include "video_core/post_processing/fx_preset.h"

import jacinth;

extern std::unique_ptr<AndroidConfig> per_game_config;
#endif

namespace {

#ifdef HAS_RESHADE
bool EditingPerGame() {
    return per_game_config != nullptr;
}

void BeginFxEdit() {
    if (!EditingPerGame()) {
        return;
    }
    VideoCore::UsePerGameFxSettings();
}

std::array<f32, 4> DefaultValueOf(size_t index, const std::string& uniform) {
    const auto entries = VideoCore::FxChain::Instance().Entries();
    if (index >= entries.size()) {
        return {};
    }
    const VideoCore::FxEffectDesc* effect = VideoCore::FindFxEffect(entries[index].file);
    if (effect == nullptr) {
        return {};
    }
    const VideoCore::FxUniformDesc* desc = VideoCore::FindFxUniform(*effect, uniform);
    if (desc == nullptr) {
        return {};
    }
    return desc->default_value;
}
#endif

} // Anonymous namespace

extern "C" {

jstring Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_getCatalogJson(JNIEnv* env,
                                                                         jobject obj) {
    jacinth::json out;

#ifdef HAS_RESHADE
    VideoCore::FxChain::Instance().DropUnknownEntries();
    out = VideoCore::GetFxCatalog();
#endif

    return Common::Android::ToJString(env, out.dump());
}

jstring Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_getChainJson(JNIEnv* env, jobject obj) {
    jacinth::json out;

#ifdef HAS_RESHADE
    out = VideoCore::FxChain::Instance().Entries();
#endif

    return Common::Android::ToJString(env, out.dump());
}

void Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_append(JNIEnv* env, jobject obj,
                                                               jstring jfile, jstring jtechnique) {
#ifdef HAS_RESHADE
    VideoCore::FxChain::Instance().Append(Common::Android::GetJString(env, jfile),
                                          Common::Android::GetJString(env, jtechnique));
#endif
}

void Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_replace(JNIEnv* env, jobject obj,
                                                                jint index, jstring jfile,
                                                                jstring jtechnique) {
#ifdef HAS_RESHADE
    VideoCore::FxChain::Instance().Replace(static_cast<size_t>(index),
                                           Common::Android::GetJString(env, jfile),
                                           Common::Android::GetJString(env, jtechnique));
#endif
}

void Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_remove(JNIEnv* env, jobject obj,
                                                               jint index) {
#ifdef HAS_RESHADE
    VideoCore::FxChain::Instance().Remove(static_cast<size_t>(index));
#endif
}

void Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_move(JNIEnv* env, jobject obj, jint index,
                                                             jint delta) {
#ifdef HAS_RESHADE
    VideoCore::FxChain::Instance().Move(static_cast<size_t>(index), delta);
#endif
}

void Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_resetValues(JNIEnv* env, jobject obj,
                                                                    jint index) {
#ifdef HAS_RESHADE
    VideoCore::FxChain::Instance().ResetValues(static_cast<size_t>(index));
#endif
}

jfloat Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_getValue(JNIEnv* env, jobject obj,
                                                                   jint index, jstring juniform,
                                                                   jint component) {
#ifdef HAS_RESHADE
    if (component < 0 || component >= 4) {
        return 0.0f;
    }
    const auto value = VideoCore::FxChain::Instance().GetValue(
        static_cast<size_t>(index), Common::Android::GetJString(env, juniform));
    return value[static_cast<size_t>(component)];
#else
    return 0.0f;
#endif
}

jboolean Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_hasValue(JNIEnv* env, jobject obj,
                                                                     jint index,
                                                                     jstring juniform) {
#ifdef HAS_RESHADE
    return static_cast<jboolean>(VideoCore::FxChain::Instance().HasValue(
        static_cast<size_t>(index), Common::Android::GetJString(env, juniform)));
#else
    return static_cast<jboolean>(false);
#endif
}

void Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_setValue(JNIEnv* env, jobject obj,
                                                                  jint index, jstring juniform,
                                                                  jint component, jfloat value) {
#ifdef HAS_RESHADE
    if (component < 0 || component >= 4) {
        return;
    }
    const std::string uniform = Common::Android::GetJString(env, juniform);
    auto& chain = VideoCore::FxChain::Instance();
    const auto slot = static_cast<size_t>(index);

    auto current = chain.GetValue(slot, uniform);
    if (!chain.HasValue(slot, uniform)) {
        current = DefaultValueOf(slot, uniform);
    }

    current[static_cast<size_t>(component)] = value;
    chain.SetValue(slot, uniform, current);
#endif
}

void Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_store(JNIEnv* env, jobject obj) {
#ifdef HAS_RESHADE
    BeginFxEdit();
    VideoCore::FxChain::Instance().StoreToSettings();
#endif
}

void Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_reload(JNIEnv* env, jobject obj) {
#ifdef HAS_RESHADE
    if (!EditingPerGame()) {
        VideoCore::UseGlobalFxSettings();
    }
    VideoCore::FxChain::Instance().LoadFromSettings();
#endif
}

void Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_clearChain(JNIEnv* env, jobject obj) {
#ifdef HAS_RESHADE
    VideoCore::FxChain::Instance().Clear();
#endif
}

jstring Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_getShaderDirectory(JNIEnv* env,
                                                                              jobject obj) {
#ifdef HAS_RESHADE
    return Common::Android::ToJString(env, VideoCore::GetFxRootDirectory().string());
#else
    return Common::Android::ToJString(env, "");
#endif
}

jstring Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_getPresetsJson(JNIEnv* env,
                                                                          jobject obj) {
    jacinth::json out;
#ifdef HAS_RESHADE
    VideoCore::ReloadFxPresetCatalog();

    out = VideoCore::GetFxPresetCatalog();
#endif
    return Common::Android::ToJString(env, out.dump());
}

jstring Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_getActivePreset(JNIEnv* env,
                                                                           jobject obj) {
#ifdef HAS_RESHADE
    return Common::Android::ToJString(env, VideoCore::GetActiveFxPreset());
#else
    return Common::Android::ToJString(env, "");
#endif
}

jboolean Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_isPresetModified(JNIEnv* env,
                                                                             jobject obj) {
#ifdef HAS_RESHADE
    return static_cast<jboolean>(VideoCore::IsActiveFxPresetModified());
#else
    return static_cast<jboolean>(false);
#endif
}

jboolean Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_applyPreset(JNIEnv* env, jobject obj,
                                                                        jstring jname) {
#ifdef HAS_RESHADE
    BeginFxEdit();
    return static_cast<jboolean>(
        VideoCore::ApplyFxPreset(Common::Android::GetJString(env, jname)));
#else
    return static_cast<jboolean>(false);
#endif
}

jboolean Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_savePreset(JNIEnv* env, jobject obj,
                                                                       jstring jname,
                                                                       jstring jdescription) {
#ifdef HAS_RESHADE
    BeginFxEdit();
    return static_cast<jboolean>(
        VideoCore::SaveFxPreset(Common::Android::GetJString(env, jname),
                                Common::Android::GetJString(env, jdescription)));
#else
    return static_cast<jboolean>(false);
#endif
}

jboolean Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_deletePreset(JNIEnv* env, jobject obj,
                                                                         jstring jname) {
#ifdef HAS_RESHADE
    BeginFxEdit();
    return static_cast<jboolean>(
        VideoCore::DeleteFxPreset(Common::Android::GetJString(env, jname)));
#else
    return static_cast<jboolean>(false);
#endif
}

void Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_clearPreset(JNIEnv* env, jobject obj) {
#ifdef HAS_RESHADE
    BeginFxEdit();
    VideoCore::SetActiveFxPreset(std::string_view());
#endif
}

jboolean Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_isEnabled(JNIEnv* env, jobject obj) {
#ifdef HAS_RESHADE
    return static_cast<jboolean>(Settings::values.post_shader_enabled.GetValue());
#else
    return static_cast<jboolean>(false);
#endif
}

void Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_setEnabled(JNIEnv* env, jobject obj,
                                                                   jboolean enabled) {
#ifdef HAS_RESHADE
    BeginFxEdit();
    Settings::values.post_shader_enabled.SetValue(enabled != JNI_FALSE);
#endif
}

jstring Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_getPresetDirectory(JNIEnv* env,
                                                                              jobject obj) {
#ifdef HAS_RESHADE
    return Common::Android::ToJString(env, VideoCore::GetFxPresetDirectory().string());
#else
    return Common::Android::ToJString(env, "");
#endif
}

} // extern "C"

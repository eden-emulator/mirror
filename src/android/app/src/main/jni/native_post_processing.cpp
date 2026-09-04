// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <string>

#include <jni.h>
#include <nlohmann/json.hpp>

#include "common/android/android_common.h"
#ifdef HAS_RESHADE
#include "video_core/post_processing/fx_chain.h"
#include "video_core/post_processing/fx_effect.h"
#endif

namespace {

#ifdef HAS_RESHADE
nlohmann::json SerializeUniform(const VideoCore::FxUniformDesc& uniform) {
    nlohmann::json out;
    out["name"] = uniform.name;
    out["label"] = uniform.label;
    out["tooltip"] = uniform.tooltip;
    out["category"] = uniform.category;
    out["kind"] = static_cast<int>(uniform.kind);
    out["uiType"] = static_cast<int>(uniform.ui_type);
    out["components"] = uniform.components;
    out["min"] = uniform.ui_min;
    out["max"] = uniform.ui_max;
    out["step"] = uniform.ui_step;
    out["items"] = uniform.items;

    nlohmann::json defaults = nlohmann::json::array();
    for (u32 i = 0; i < uniform.components; ++i) {
        defaults.push_back(uniform.default_value[i]);
    }
    out["defaults"] = defaults;

    return out;
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
    nlohmann::json out = nlohmann::json::array();

#ifdef HAS_RESHADE
    for (const auto& effect : VideoCore::GetFxCatalog()) {
        nlohmann::json entry;
        entry["file"] = effect.file;
        entry["name"] = effect.name;
        entry["error"] = effect.error;
        entry["techniques"] = effect.techniques;

        nlohmann::json uniforms = nlohmann::json::array();
        for (const auto& uniform : effect.uniforms) {
            uniforms.push_back(SerializeUniform(uniform));
        }
        entry["uniforms"] = uniforms;

        out.push_back(entry);
    }
#endif

    return Common::Android::ToJString(env, out.dump());
}

jstring Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_getChainJson(JNIEnv* env, jobject obj) {
    nlohmann::json out = nlohmann::json::array();

#ifdef HAS_RESHADE
    for (const auto& entry : VideoCore::FxChain::Instance().Entries()) {
        nlohmann::json item;
        item["file"] = entry.file;
        item["technique"] = entry.technique;

        nlohmann::json values = nlohmann::json::object();
        for (const auto& [name, value] : entry.values) {
            values[name] = {value[0], value[1], value[2], value[3]};
        }
        item["values"] = values;

        out.push_back(item);
    }
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
    VideoCore::FxChain::Instance().StoreToSettings();
#endif
}

void Java_org_yuzu_yuzu_1emu_utils_NativePostProcessing_reload(JNIEnv* env, jobject obj) {
#ifdef HAS_RESHADE
    VideoCore::ReloadFxCatalog();
    VideoCore::FxChain::Instance().DropUnknownEntries();
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

} // extern "C"

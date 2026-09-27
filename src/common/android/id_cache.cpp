// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <jni.h>

#include "applets/software_keyboard.h"
#include "applets/web_browser.h"
#include "common/android/id_cache.h"
#include "common/assert.h"
#include "common/fs/fs_android.h"
#include "video_core/rasterizer_interface.h"
#include "common/android/multiplayer/multiplayer.h"
#include <network/network.h>

static struct {
    JavaVM *java_vm;
    jclass native_library_class;
    jclass disk_cache_progress_class;
    jclass load_callback_stage_class;
    jclass game_dir_class;
    jmethodID game_dir_constructor;
    jmethodID exit_emulation_activity;
    jmethodID disk_cache_load_progress;
    jmethodID on_emulation_started;
    jmethodID on_emulation_stopped;
    jmethodID on_program_changed;
    jmethodID copy_to_storage;
    jmethodID file_exists;
    jmethodID file_extension;

    jclass game_class;
    jmethodID game_constructor;
    jfieldID game_title_field;
    jfieldID game_path_field;
    jfieldID game_program_id_field;
    jfieldID game_developer_field;
    jfieldID game_version_field;
    jfieldID game_is_homebrew_field;

    jclass string_class;
    jclass pair_class;
    jmethodID pair_constructor;
    jfieldID pair_first_field;
    jfieldID pair_second_field;

    jclass overlay_control_data_class;
    jmethodID overlay_control_data_constructor;
    jfieldID overlay_control_data_id_field;
    jfieldID overlay_control_data_enabled_field;
    jfieldID overlay_control_data_individual_scale_field;
    jfieldID overlay_control_data_landscape_position_field;
    jfieldID overlay_control_data_portrait_position_field;
    jfieldID overlay_control_data_foldable_position_field;

    jclass patch_class;
    jmethodID patch_constructor;
    jfieldID patch_enabled_field;
    jfieldID patch_name_field;
    jfieldID patch_version_field;
    jfieldID patch_type_field;
    jfieldID patch_program_id_field;
    jfieldID patch_title_id_field;

    jclass double_class;
    jmethodID double_constructor;
    jmethodID double_value_method;

    jclass integer_class;
    jmethodID integer_constructor;
    jmethodID integer_value_method;

    jclass boolean_class;
    jmethodID boolean_constructor;
    jmethodID boolean_value_method;

    jclass player_input_class;
    jmethodID player_input_constructor;
    jfieldID player_input_connected_field;
    jfieldID player_input_buttons_field;
    jfieldID player_input_analogs_field;
    jfieldID player_input_motions_field;
    jfieldID player_input_vibration_enabled_field;
    jfieldID player_input_vibration_strength_field;
    jfieldID player_input_body_color_left_field;
    jfieldID player_input_body_color_right_field;
    jfieldID player_input_button_color_left_field;
    jfieldID player_input_button_color_right_field;
    jfieldID player_input_profile_name_field;
    jfieldID player_input_use_system_vibrator_field;

    jclass yuzu_input_device_interface;
    jmethodID yuzu_input_device_get_name;
    jmethodID yuzu_input_device_get_guid;
    jmethodID yuzu_input_device_get_port;
    jmethodID yuzu_input_device_get_supports_vibration;
    jmethodID yuzu_input_device_vibrate;
    jmethodID yuzu_input_device_get_axes;
    jmethodID yuzu_input_device_has_keys;

    jmethodID add_netplay_message;
    jmethodID clear_chat;
} state;

static constexpr jint JNI_VERSION = JNI_VERSION_1_6;

namespace Common::Android {
    JNIEnv *GetEnvForThread() {
        thread_local static struct OwnedEnv {
            OwnedEnv() {
                status = s_java_vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
                if (status == JNI_EDETACHED)
                    s_java_vm->AttachCurrentThread(&env, nullptr);
            }

            ~OwnedEnv() {
                if (status == JNI_EDETACHED)
                    s_java_vm->DetachCurrentThread();
            }

            int status;
            JNIEnv *env = nullptr;
        } owned;
        return owned.env;
    }

    jclass GetNativeLibraryClass() {
        return state.native_library_class;
    }

    jclass GetDiskCacheProgressClass() {
        return state.disk_cache_progress_class;
    }

    jclass GetDiskCacheLoadCallbackStageClass() {
        return state.load_callback_stage_class;
    }

    jclass GetGameDirClass() {
        return state.game_dir_class;
    }

    jmethodID GetGameDirConstructor() {
        return state.game_dir_constructor;
    }

    jmethodID GetExitEmulationActivity() {
        return state.exit_emulation_activity;
    }

    jmethodID GetDiskCacheLoadProgress() {
        return state.disk_cache_load_progress;
    }

    jmethodID GetCopyToStorage() {
        return state.copy_to_storage;
    }

    jmethodID GetFileExists() {
        return state.file_exists;
    }

    jmethodID GetFileExtension() {
        return state.file_extension;
    }

    jmethodID GetOnEmulationStarted() {
        return state.on_emulation_started;
    }

    jmethodID GetOnEmulationStopped() {
        return state.on_emulation_stopped;
    }

    jmethodID GetOnProgramChanged() {
        return state.on_program_changed;
    }

    jclass GetGameClass() {
        return state.game_class;
    }

    jmethodID GetGameConstructor() {
        return state.game_constructor;
    }

    jfieldID GetGameTitleField() {
        return state.game_title_field;
    }

    jfieldID GetGamePathField() {
        return state.game_path_field;
    }

    jfieldID GetGameProgramIdField() {
        return state.game_program_id_field;
    }

    jfieldID GetGameDeveloperField() {
        return state.game_developer_field;
    }

    jfieldID GetGameVersionField() {
        return state.game_version_field;
    }

    jfieldID GetGameIsHomebrewField() {
        return state.game_is_homebrew_field;
    }

    jclass GetStringClass() {
        return state.string_class;
    }

    jclass GetPairClass() {
        return state.pair_class;
    }

    jmethodID GetPairConstructor() {
        return state.pair_constructor;
    }

    jfieldID GetPairFirstField() {
        return state.pair_first_field;
    }

    jfieldID GetPairSecondField() {
        return state.pair_second_field;
    }

    jclass GetOverlayControlDataClass() {
        return state.overlay_control_data_class;
    }

    jmethodID GetOverlayControlDataConstructor() {
        return state.overlay_control_data_constructor;
    }

    jfieldID GetOverlayControlDataIdField() {
        return state.overlay_control_data_id_field;
    }

    jfieldID GetOverlayControlDataEnabledField() {
        return state.overlay_control_data_enabled_field;
    }

    jfieldID GetOverlayControlDataIndividualScaleField() {
        return state.overlay_control_data_individual_scale_field;
    }

    jfieldID GetOverlayControlDataLandscapePositionField() {
        return state.overlay_control_data_landscape_position_field;
    }

    jfieldID GetOverlayControlDataPortraitPositionField() {
        return state.overlay_control_data_portrait_position_field;
    }

    jfieldID GetOverlayControlDataFoldablePositionField() {
        return state.overlay_control_data_foldable_position_field;
    }

    jclass GetPatchClass() {
        return state.patch_class;
    }

    jmethodID GetPatchConstructor() {
        return state.patch_constructor;
    }

    jfieldID GetPatchEnabledField() {
        return state.patch_enabled_field;
    }

    jfieldID GetPatchNameField() {
        return state.patch_name_field;
    }

    jfieldID GetPatchVersionField() {
        return state.patch_version_field;
    }

    jfieldID GetPatchTypeField() {
        return state.patch_type_field;
    }

    jfieldID GetPatchProgramIdField() {
        return state.patch_program_id_field;
    }

    jfieldID GetPatchTitleIdField() {
        return state.patch_title_id_field;
    }

    jclass GetDoubleClass() {
        return state.double_class;
    }

    jmethodID GetDoubleConstructor() {
        return state.double_constructor;
    }

    jmethodID GetDoubleValueMethod() {
        return state.double_value_method;
    }

    jclass GetIntegerClass() {
        return state.integer_class;
    }

    jmethodID GetIntegerConstructor() {
        return state.integer_constructor;
    }

    jmethodID GetIntegerValueMethod() {
        return state.integer_value_method;
    }

    jclass GetBooleanClass() {
        return state.boolean_class;
    }

    jmethodID GetBooleanConstructor() {
        return state.boolean_constructor;
    }

    jmethodID GetBooleanValueMethod() {
        return state.boolean_value_method;
    }

    jclass GetPlayerInputClass() {
        return state.player_input_class;
    }

    jmethodID GetPlayerInputConstructor() {
        return state.player_input_constructor;
    }

    jfieldID GetPlayerInputConnectedField() {
        return state.player_input_connected_field;
    }

    jfieldID GetPlayerInputButtonsField() {
        return state.player_input_buttons_field;
    }

    jfieldID GetPlayerInputAnalogsField() {
        return state.player_input_analogs_field;
    }

    jfieldID GetPlayerInputMotionsField() {
        return state.player_input_motions_field;
    }

    jfieldID GetPlayerInputVibrationEnabledField() {
        return state.player_input_vibration_enabled_field;
    }

    jfieldID GetPlayerInputVibrationStrengthField() {
        return state.player_input_vibration_strength_field;
    }

    jfieldID GetPlayerInputBodyColorLeftField() {
        return state.player_input_body_color_left_field;
    }

    jfieldID GetPlayerInputBodyColorRightField() {
        return state.player_input_body_color_right_field;
    }

    jfieldID GetPlayerInputButtonColorLeftField() {
        return state.player_input_button_color_left_field;
    }

    jfieldID GetPlayerInputButtonColorRightField() {
        return state.player_input_button_color_right_field;
    }

    jfieldID GetPlayerInputProfileNameField() {
        return state.player_input_profile_name_field;
    }

    jfieldID GetPlayerInputUseSystemVibratorField() {
        return state.player_input_use_system_vibrator_field;
    }

    jclass GetYuzuInputDeviceInterface() {
        return state.yuzu_input_device_interface;
    }

    jmethodID GetYuzuDeviceGetName() {
        return state.yuzu_input_device_get_name;
    }

    jmethodID GetYuzuDeviceGetGUID() {
        return state.yuzu_input_device_get_guid;
    }

    jmethodID GetYuzuDeviceGetPort() {
        return state.yuzu_input_device_get_port;
    }

    jmethodID GetYuzuDeviceGetSupportsVibration() {
        return state.yuzu_input_device_get_supports_vibration;
    }

    jmethodID GetYuzuDeviceVibrate() {
        return state.yuzu_input_device_vibrate;
    }

    jmethodID GetYuzuDeviceGetAxes() {
        return state.yuzu_input_device_get_axes;
    }

    jmethodID GetYuzuDeviceHasKeys() {
        return state.yuzu_input_device_has_keys;
    }

    jmethodID GetAddNetPlayMessage() {
        return state.add_netplay_message;
    }

    jmethodID ClearChat() {
        return state.clear_chat;
    }

#ifdef __cplusplus
    extern "C" {
#endif

    jint InitFFmpegOnLoad(JavaVM *vm);

    void JNI_OnUnload(JavaVM *vm, void *reserved) {
        JNIEnv *env;
        if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION) != JNI_OK) {
            return;
        }

        // UnInitialize Android Storage
        Common::FS::Android::UnRegisterCallbacks();
        env->DeleteGlobalRef(state.native_library_class);
        env->DeleteGlobalRef(state.disk_cache_progress_class);
        env->DeleteGlobalRef(state.load_callback_stage_class);
        env->DeleteGlobalRef(state.game_dir_class);
        env->DeleteGlobalRef(state.game_class);
        env->DeleteGlobalRef(state.string_class);
        env->DeleteGlobalRef(state.pair_class);
        env->DeleteGlobalRef(state.overlay_control_data_class);
        env->DeleteGlobalRef(state.patch_class);
        env->DeleteGlobalRef(state.double_class);
        env->DeleteGlobalRef(state.integer_class);
        env->DeleteGlobalRef(state.boolean_class);
        env->DeleteGlobalRef(state.player_input_class);
        env->DeleteGlobalRef(state.yuzu_input_device_interface);

        // UnInitialize applets
        SoftwareKeyboard::CleanupJNI(env);
        WebBrowser::CleanupJNI(env);

        AndroidMultiplayer::NetworkShutdown();
    }

#ifdef __cplusplus
    }
#endif

void Initialize(JavaVM* vm, JNIEnv *env) {
    state.java_vm = vm;
    InitFFmpegOnLoad(vm);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    // Initialize Java classes
    const jclass native_library_class = env->FindClass("org/yuzu/yuzu_emu/NativeLibrary");
    state.native_library_class = reinterpret_cast<jclass>(env->NewGlobalRef(native_library_class));
    state.disk_cache_progress_class = reinterpret_cast<jclass>(env->NewGlobalRef(
        env->FindClass("org/yuzu/yuzu_emu/disk_shader_cache/DiskShaderCacheProgress")));
    state.load_callback_stage_class = reinterpret_cast<jclass>(env->NewGlobalRef(env->FindClass(
        "org/yuzu/yuzu_emu/disk_shader_cache/DiskShaderCacheProgress$LoadCallbackStage")));

    const jclass game_dir_class = env->FindClass("org/yuzu/yuzu_emu/model/GameDir");
    state.game_dir_class = reinterpret_cast<jclass>(env->NewGlobalRef(game_dir_class));
    state.game_dir_constructor = env->GetMethodID(game_dir_class, "<init>",
                                              "(Ljava/lang/String;Z)V");
    env->DeleteLocalRef(game_dir_class);

    // Initialize methods
    state.exit_emulation_activity =
        env->GetStaticMethodID(state.native_library_class, "exitEmulationActivity", "(I)V");
    state.disk_cache_load_progress =
        env->GetStaticMethodID(state.disk_cache_progress_class, "loadProgress", "(III)V");
    state.copy_to_storage = env->GetStaticMethodID(state.native_library_class, "copyFileToStorage",
                                               "(Ljava/lang/String;Ljava/lang/String;)Z");
    state.file_exists = env->GetStaticMethodID(state.native_library_class, "exists",
                                           "(Ljava/lang/String;)Z");
    state.file_extension = env->GetStaticMethodID(state.native_library_class, "getFileExtension",
                                              "(Ljava/lang/String;)Ljava/lang/String;");
    state.on_emulation_started =
        env->GetStaticMethodID(state.native_library_class, "onEmulationStarted", "()V");
    state.on_emulation_stopped =
        env->GetStaticMethodID(state.native_library_class, "onEmulationStopped", "(I)V");
    state.on_program_changed =
        env->GetStaticMethodID(state.native_library_class, "onProgramChanged", "(I)V");

    const jclass game_class = env->FindClass("org/yuzu/yuzu_emu/model/Game");
    state.game_class = reinterpret_cast<jclass>(env->NewGlobalRef(game_class));
    state.game_constructor = env->GetMethodID(game_class, "<init>",
                                          "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/"
                                          "String;Ljava/lang/String;Ljava/lang/String;Z)V");
    state.game_title_field = env->GetFieldID(game_class, "title", "Ljava/lang/String;");
    state.game_path_field = env->GetFieldID(game_class, "path", "Ljava/lang/String;");
    state.game_program_id_field = env->GetFieldID(game_class, "programId", "Ljava/lang/String;");
    state.game_developer_field = env->GetFieldID(game_class, "developer", "Ljava/lang/String;");
    state.game_version_field = env->GetFieldID(game_class, "version", "Ljava/lang/String;");
    state.game_is_homebrew_field = env->GetFieldID(game_class, "isHomebrew", "Z");
    env->DeleteLocalRef(game_class);

    const jclass string_class = env->FindClass("java/lang/String");
    state.string_class = reinterpret_cast<jclass>(env->NewGlobalRef(string_class));
    env->DeleteLocalRef(string_class);

    const jclass pair_class = env->FindClass("kotlin/Pair");
    state.pair_class = reinterpret_cast<jclass>(env->NewGlobalRef(pair_class));
    state.pair_constructor =
        env->GetMethodID(pair_class, "<init>", "(Ljava/lang/Object;Ljava/lang/Object;)V");
    state.pair_first_field = env->GetFieldID(pair_class, "first", "Ljava/lang/Object;");
    state.pair_second_field = env->GetFieldID(pair_class, "second", "Ljava/lang/Object;");
    env->DeleteLocalRef(pair_class);

    const jclass overlay_control_data_class =
        env->FindClass("org/yuzu/yuzu_emu/overlay/model/OverlayControlData");
    state.overlay_control_data_class =
        reinterpret_cast<jclass>(env->NewGlobalRef(overlay_control_data_class));
    state.overlay_control_data_constructor =
        env->GetMethodID(overlay_control_data_class, "<init>",
                         "(Ljava/lang/String;ZLkotlin/Pair;Lkotlin/Pair;Lkotlin/Pair;F)V");
    state.overlay_control_data_id_field =
        env->GetFieldID(overlay_control_data_class, "id", "Ljava/lang/String;");
    state.overlay_control_data_enabled_field =
        env->GetFieldID(overlay_control_data_class, "enabled", "Z");
    state.overlay_control_data_landscape_position_field =
        env->GetFieldID(overlay_control_data_class, "landscapePosition", "Lkotlin/Pair;");
    state.overlay_control_data_portrait_position_field =
        env->GetFieldID(overlay_control_data_class, "portraitPosition", "Lkotlin/Pair;");
    state.overlay_control_data_foldable_position_field =
        env->GetFieldID(overlay_control_data_class, "foldablePosition", "Lkotlin/Pair;");
    state.overlay_control_data_individual_scale_field =
        env->GetFieldID(overlay_control_data_class, "individualScale", "F");
    env->DeleteLocalRef(overlay_control_data_class);

    const jclass patch_class = env->FindClass("org/yuzu/yuzu_emu/model/Patch");
    state.patch_class = reinterpret_cast<jclass>(env->NewGlobalRef(patch_class));
    state.patch_constructor = env->GetMethodID(
        patch_class, "<init>",
        "(ZLjava/lang/String;Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;JI)V");
    state.patch_enabled_field = env->GetFieldID(patch_class, "enabled", "Z");
    state.patch_name_field = env->GetFieldID(patch_class, "name", "Ljava/lang/String;");
    state.patch_version_field = env->GetFieldID(patch_class, "version", "Ljava/lang/String;");
    state.patch_type_field = env->GetFieldID(patch_class, "type", "I");
    state.patch_program_id_field = env->GetFieldID(patch_class, "programId", "Ljava/lang/String;");
    state.patch_title_id_field = env->GetFieldID(patch_class, "titleId", "Ljava/lang/String;");
    env->DeleteLocalRef(patch_class);

    const jclass double_class = env->FindClass("java/lang/Double");
    state.double_class = reinterpret_cast<jclass>(env->NewGlobalRef(double_class));
    state.double_constructor = env->GetMethodID(double_class, "<init>", "(D)V");
    state.double_value_method = env->GetMethodID(double_class, "doubleValue", "()D");
    env->DeleteLocalRef(double_class);

    const jclass int_class = env->FindClass("java/lang/Integer");
    state.integer_class = reinterpret_cast<jclass>(env->NewGlobalRef(int_class));
    state.integer_constructor = env->GetMethodID(int_class, "<init>", "(I)V");
    state.integer_value_method = env->GetMethodID(int_class, "intValue", "()I");
    env->DeleteLocalRef(int_class);

    const jclass boolean_class = env->FindClass("java/lang/Boolean");
    state.boolean_class = reinterpret_cast<jclass>(env->NewGlobalRef(boolean_class));
    state.boolean_constructor = env->GetMethodID(boolean_class, "<init>", "(Z)V");
    state.boolean_value_method = env->GetMethodID(boolean_class, "booleanValue", "()Z");
    env->DeleteLocalRef(boolean_class);

    const jclass player_input_class =
        env->FindClass("org/yuzu/yuzu_emu/features/input/model/PlayerInput");
    state.player_input_class = reinterpret_cast<jclass>(env->NewGlobalRef(player_input_class));
    state.player_input_constructor = env->GetMethodID(
        player_input_class, "<init>",
        "(Z[Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;ZIJJJJLjava/lang/String;Z)V");
    state.player_input_connected_field = env->GetFieldID(player_input_class, "connected", "Z");
    state.player_input_buttons_field =
        env->GetFieldID(player_input_class, "buttons", "[Ljava/lang/String;");
    state.player_input_analogs_field =
        env->GetFieldID(player_input_class, "analogs", "[Ljava/lang/String;");
    state.player_input_motions_field =
        env->GetFieldID(player_input_class, "motions", "[Ljava/lang/String;");
    state.player_input_vibration_enabled_field =
        env->GetFieldID(player_input_class, "vibrationEnabled", "Z");
    state.player_input_vibration_strength_field =
        env->GetFieldID(player_input_class, "vibrationStrength", "I");
    state.player_input_body_color_left_field =
        env->GetFieldID(player_input_class, "bodyColorLeft", "J");
    state.player_input_body_color_right_field =
        env->GetFieldID(player_input_class, "bodyColorRight", "J");
    state.player_input_button_color_left_field =
        env->GetFieldID(player_input_class, "buttonColorLeft", "J");
    state.player_input_button_color_right_field =
        env->GetFieldID(player_input_class, "buttonColorRight", "J");
    state.player_input_profile_name_field =
        env->GetFieldID(player_input_class, "profileName", "Ljava/lang/String;");
    state.player_input_use_system_vibrator_field =
        env->GetFieldID(player_input_class, "useSystemVibrator", "Z");
    env->DeleteLocalRef(player_input_class);

    const jclass yuzu_input_device_interface =
        env->FindClass("org/yuzu/yuzu_emu/features/input/YuzuInputDevice");
    state.yuzu_input_device_interface =
        reinterpret_cast<jclass>(env->NewGlobalRef(yuzu_input_device_interface));
    state.yuzu_input_device_get_name =
        env->GetMethodID(yuzu_input_device_interface, "getName", "()Ljava/lang/String;");
    state.yuzu_input_device_get_guid =
        env->GetMethodID(yuzu_input_device_interface, "getGUID", "()Ljava/lang/String;");
    state.yuzu_input_device_get_port = env->GetMethodID(yuzu_input_device_interface, "getPort",
                                                    "()I");
    state.yuzu_input_device_get_supports_vibration =
        env->GetMethodID(yuzu_input_device_interface, "getSupportsVibration", "()Z");
    state.yuzu_input_device_vibrate = env->GetMethodID(yuzu_input_device_interface, "vibrate",
                                                   "(F)V");
    state.yuzu_input_device_get_axes =
        env->GetMethodID(yuzu_input_device_interface, "getAxes", "()[Ljava/lang/Integer;");
    state.yuzu_input_device_has_keys =
        env->GetMethodID(yuzu_input_device_interface, "hasKeys", "([I)[Z");
    env->DeleteLocalRef(yuzu_input_device_interface);
    state.add_netplay_message = env->GetStaticMethodID(state.native_library_class, "addNetPlayMessage",
                                                   "(ILjava/lang/String;)V");
    state.clear_chat = env->GetStaticMethodID(state.native_library_class, "clearChat", "()V");

    // Initialize Android Storage
    Common::FS::Android::RegisterCallbacks(env, state.native_library_class);

    // Initialize applets
    Common::Android::SoftwareKeyboard::InitJNI(env);
    Common::Android::WebBrowser::InitJNI(env);
}

} // namespace Common::Android

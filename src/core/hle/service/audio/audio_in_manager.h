// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "audio_core/audio_in_manager.h"
#include "audio_core/in/audio_in_system.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::Audio {

using AudioDeviceName = AudioCore::Renderer::AudioDevice::AudioDeviceName;
using Protocol = std::array<u32, 2>;

class IAudioIn;

class IAudioInManager final : public ServiceFramework<IAudioInManager> {
public:
    explicit IAudioInManager(Core::System& system_);
    ~IAudioInManager() override;

private:
    Result ListAudioIns(OutArray<AudioDeviceName, BufferAttr_HipcMapAlias> out_audio_ins,
                        Out<u32> out_count);
    Result OpenAudioIn(Out<AudioCore::AudioIn::AudioInParameterInternal> out_parameter_internal,
                       Out<SharedPointer<IAudioIn>> out_audio_in,
                       OutArray<AudioDeviceName, BufferAttr_HipcMapAlias> out_name,
                       InArray<AudioDeviceName, BufferAttr_HipcMapAlias> name,
                       AudioCore::AudioIn::AudioInParameter parameter,
                       InCopyHandle<Kernel::KProcess> process_handle,
                       ClientAppletResourceUserId aruid);

    Result ListAudioInsAuto(OutArray<AudioDeviceName, BufferAttr_HipcAutoSelect> out_audio_ins,
                            Out<u32> out_count);
    Result OpenAudioInAuto(Out<AudioCore::AudioIn::AudioInParameterInternal> out_parameter_internal,
                           Out<SharedPointer<IAudioIn>> out_audio_in,
                           OutArray<AudioDeviceName, BufferAttr_HipcAutoSelect> out_name,
                           InArray<AudioDeviceName, BufferAttr_HipcAutoSelect> name,
                           AudioCore::AudioIn::AudioInParameter parameter,
                           InCopyHandle<Kernel::KProcess> process_handle,
                           ClientAppletResourceUserId aruid);

    Result ListAudioInsAutoFiltered(
        OutArray<AudioDeviceName, BufferAttr_HipcAutoSelect> out_audio_ins, Out<u32> out_count);
    Result OpenAudioInProtocolSpecified(
        Out<AudioCore::AudioIn::AudioInParameterInternal> out_parameter_internal,
        Out<SharedPointer<IAudioIn>> out_audio_in,
        OutArray<AudioDeviceName, BufferAttr_HipcAutoSelect> out_name,
        InArray<AudioDeviceName, BufferAttr_HipcAutoSelect> name, Protocol protocol,
        AudioCore::AudioIn::AudioInParameter parameter,
        InCopyHandle<Kernel::KProcess> process_handle, ClientAppletResourceUserId aruid);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IAudioInManager::ListAudioIns>, "ListAudioIns"},
        FunctionInfo{1, D<&IAudioInManager::OpenAudioIn>, "OpenAudioIn"},
        FunctionInfo{2, D<&IAudioInManager::ListAudioIns>, "ListAudioInsAuto"},
        FunctionInfo{3, D<&IAudioInManager::OpenAudioIn>, "OpenAudioInAuto"},
        FunctionInfo{4, D<&IAudioInManager::ListAudioInsAutoFiltered>, "ListAudioInsAutoFiltered"},
        FunctionInfo{5, D<&IAudioInManager::OpenAudioInProtocolSpecified>, "OpenAudioInProtocolSpecified"}
    );
    std::unique_ptr<AudioCore::AudioIn::Manager> impl;
};

} // namespace Service::Audio

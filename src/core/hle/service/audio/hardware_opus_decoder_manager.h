// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "audio_core/opus/decoder_manager.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::Audio {

class IHardwareOpusDecoder;

using AudioCore::OpusDecoder::OpusMultiStreamParameters;
using AudioCore::OpusDecoder::OpusMultiStreamParametersEx;
using AudioCore::OpusDecoder::OpusParameters;
using AudioCore::OpusDecoder::OpusParametersEx;

class IHardwareOpusDecoderManager final : public ServiceFramework<IHardwareOpusDecoderManager> {
public:
    explicit IHardwareOpusDecoderManager(Core::System& system_);
    ~IHardwareOpusDecoderManager() override;

private:
    Result OpenHardwareOpusDecoder(Out<SharedPointer<IHardwareOpusDecoder>> out_decoder,
                                   OpusParameters params, u32 tmem_size,
                                   InCopyHandle<Kernel::KTransferMemory> tmem_handle);
    Result GetWorkBufferSize(Out<u32> out_size, OpusParameters params);
    Result OpenHardwareOpusDecoderForMultiStream(
        Out<SharedPointer<IHardwareOpusDecoder>> out_decoder,
        InLargeData<OpusMultiStreamParameters, BufferAttr_HipcPointer> params, u32 tmem_size,
        InCopyHandle<Kernel::KTransferMemory> tmem_handle);
    Result GetWorkBufferSizeForMultiStream(
        Out<u32> out_size, InLargeData<OpusMultiStreamParameters, BufferAttr_HipcPointer> params);
    Result OpenHardwareOpusDecoderEx(Out<SharedPointer<IHardwareOpusDecoder>> out_decoder,
                                     OpusParametersEx params, u32 tmem_size,
                                     InCopyHandle<Kernel::KTransferMemory> tmem_handle);
    Result GetWorkBufferSizeEx(Out<u32> out_size, OpusParametersEx params);
    Result OpenHardwareOpusDecoderForMultiStreamEx(
        Out<SharedPointer<IHardwareOpusDecoder>> out_decoder,
        InLargeData<OpusMultiStreamParametersEx, BufferAttr_HipcPointer> params, u32 tmem_size,
        InCopyHandle<Kernel::KTransferMemory> tmem_handle);
    Result GetWorkBufferSizeForMultiStreamEx(
        Out<u32> out_size, InLargeData<OpusMultiStreamParametersEx, BufferAttr_HipcPointer> params);
    Result GetWorkBufferSizeExEx(Out<u32> out_size, OpusParametersEx params);
    Result GetWorkBufferSizeForMultiStreamExEx(
        Out<u32> out_size, InLargeData<OpusMultiStreamParametersEx, BufferAttr_HipcPointer> params);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IHardwareOpusDecoderManager::OpenHardwareOpusDecoder>, "OpenHardwareOpusDecoder"},
        FunctionInfo{1, D<&IHardwareOpusDecoderManager::GetWorkBufferSize>, "GetWorkBufferSize"},
        FunctionInfo{2, D<&IHardwareOpusDecoderManager::OpenHardwareOpusDecoderForMultiStream>, "OpenOpusDecoderForMultiStream"},
        FunctionInfo{3, D<&IHardwareOpusDecoderManager::GetWorkBufferSizeForMultiStream>, "GetWorkBufferSizeForMultiStream"},
        FunctionInfo{4, D<&IHardwareOpusDecoderManager::OpenHardwareOpusDecoderEx>, "OpenHardwareOpusDecoderEx"},
        FunctionInfo{5, D<&IHardwareOpusDecoderManager::GetWorkBufferSizeEx>, "GetWorkBufferSizeEx"},
        FunctionInfo{6, D<&IHardwareOpusDecoderManager::OpenHardwareOpusDecoderForMultiStreamEx>, "OpenHardwareOpusDecoderForMultiStreamEx"},
        FunctionInfo{7, D<&IHardwareOpusDecoderManager::GetWorkBufferSizeForMultiStreamEx>, "GetWorkBufferSizeForMultiStreamEx"},
        FunctionInfo{8, D<&IHardwareOpusDecoderManager::GetWorkBufferSizeExEx>, "GetWorkBufferSizeExEx"},
        FunctionInfo{9, D<&IHardwareOpusDecoderManager::GetWorkBufferSizeForMultiStreamExEx>, "GetWorkBufferSizeForMultiStreamExEx"}
    );
    Core::System& system;
    AudioCore::OpusDecoder::OpusDecoderManager impl;
};

} // namespace Service::Audio

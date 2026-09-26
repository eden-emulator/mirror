// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/audio/hardware_opus_decoder.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::Audio {

using namespace AudioCore::OpusDecoder;

    ServiceFrameworkBase::FunctionInfoBase const* IHardwareOpusDecoder::FindRequest(u32 key) {
        static constexpr auto functions = CreateStaticMap(
            FunctionInfo{0, D<&IHardwareOpusDecoder::DecodeInterleavedOld>, "DecodeInterleavedOld"},
            FunctionInfo{1, D<&IHardwareOpusDecoder::SetContext>, "SetContext"},
            FunctionInfo{2, D<&IHardwareOpusDecoder::DecodeInterleavedForMultiStreamOld>, "DecodeInterleavedForMultiStreamOld"},
            FunctionInfo{3, D<&IHardwareOpusDecoder::SetContextForMultiStream>, "SetContextForMultiStream"},
            FunctionInfo{4, D<&IHardwareOpusDecoder::DecodeInterleavedWithPerfOld>, "DecodeInterleavedWithPerfOld"},
            FunctionInfo{5, D<&IHardwareOpusDecoder::DecodeInterleavedForMultiStreamWithPerfOld>, "DecodeInterleavedForMultiStreamWithPerfOld"},
            FunctionInfo{6, D<&IHardwareOpusDecoder::DecodeInterleavedWithPerfAndResetOld>, "DecodeInterleavedWithPerfAndResetOld"},
            FunctionInfo{7, D<&IHardwareOpusDecoder::DecodeInterleavedForMultiStreamWithPerfAndResetOld>, "DecodeInterleavedForMultiStreamWithPerfAndResetOld"},
            FunctionInfo{8, D<&IHardwareOpusDecoder::DecodeInterleaved>, "DecodeInterleaved"},
            FunctionInfo{9, D<&IHardwareOpusDecoder::DecodeInterleavedForMultiStream>, "DecodeInterleavedForMultiStream"}
        );
        return HandlerTableGenerateWithFind(key, functions);
    }

IHardwareOpusDecoder::IHardwareOpusDecoder(Core::System& system_, HardwareOpus& hardware_opus)
    : ServiceFramework{system_, "IHardwareOpusDecoder"},
      impl{std::make_unique<AudioCore::OpusDecoder::OpusDecoder>(system_, hardware_opus)} {
}

IHardwareOpusDecoder::~IHardwareOpusDecoder() = default;

Result IHardwareOpusDecoder::Initialize(const OpusParametersEx& params,
                                        Kernel::KTransferMemory* transfer_memory,
                                        u64 transfer_memory_size) {
    return impl->Initialize(params, transfer_memory, transfer_memory_size);
}

Result IHardwareOpusDecoder::Initialize(const OpusMultiStreamParametersEx& params,
                                        Kernel::KTransferMemory* transfer_memory,
                                        u64 transfer_memory_size) {
    return impl->Initialize(params, transfer_memory, transfer_memory_size);
}

Result IHardwareOpusDecoder::DecodeInterleavedOld(OutBuffer<BufferAttr_HipcMapAlias> out_pcm_data,
                                                  Out<u32> out_data_size, Out<u32> out_sample_count,
                                                  InBuffer<BufferAttr_HipcMapAlias> opus_data) {
    R_TRY(impl->DecodeInterleaved(out_data_size, nullptr, out_sample_count, opus_data, out_pcm_data,
                                  false));
    LOG_DEBUG(Service_Audio, "bytes read {:#x} samples generated {}", *out_data_size,
              *out_sample_count);
    R_SUCCEED();
}

Result IHardwareOpusDecoder::SetContext(InBuffer<BufferAttr_HipcMapAlias> decoder_context) {
    LOG_DEBUG(Service_Audio, "called");
    R_RETURN(impl->SetContext(decoder_context));
}

Result IHardwareOpusDecoder::DecodeInterleavedForMultiStreamOld(
    OutBuffer<BufferAttr_HipcMapAlias> out_pcm_data, Out<u32> out_data_size,
    Out<u32> out_sample_count, InBuffer<BufferAttr_HipcMapAlias> opus_data) {
    R_TRY(impl->DecodeInterleavedForMultiStream(out_data_size, nullptr, out_sample_count, opus_data,
                                                out_pcm_data, false));
    LOG_DEBUG(Service_Audio, "bytes read {:#x} samples generated {}", *out_data_size,
              *out_sample_count);
    R_SUCCEED();
}

Result IHardwareOpusDecoder::SetContextForMultiStream(
    InBuffer<BufferAttr_HipcMapAlias> decoder_context) {
    LOG_DEBUG(Service_Audio, "called");
    R_RETURN(impl->SetContext(decoder_context));
}

Result IHardwareOpusDecoder::DecodeInterleavedWithPerfOld(
    OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_pcm_data,
    Out<u32> out_data_size, Out<u32> out_sample_count, Out<u64> out_time_taken,
    InBuffer<BufferAttr_HipcMapAlias> opus_data) {
    R_TRY(impl->DecodeInterleaved(out_data_size, out_time_taken, out_sample_count, opus_data,
                                  out_pcm_data, false));
    LOG_DEBUG(Service_Audio, "bytes read {:#x} samples generated {} time taken {}", *out_data_size,
              *out_sample_count, *out_time_taken);
    R_SUCCEED();
}

Result IHardwareOpusDecoder::DecodeInterleavedForMultiStreamWithPerfOld(
    OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_pcm_data,
    Out<u32> out_data_size, Out<u32> out_sample_count, Out<u64> out_time_taken,
    InBuffer<BufferAttr_HipcMapAlias> opus_data) {
    R_TRY(impl->DecodeInterleavedForMultiStream(out_data_size, out_time_taken, out_sample_count,
                                                opus_data, out_pcm_data, false));
    LOG_DEBUG(Service_Audio, "bytes read {:#x} samples generated {} time taken {}", *out_data_size,
              *out_sample_count, *out_time_taken);
    R_SUCCEED();
}

Result IHardwareOpusDecoder::DecodeInterleavedWithPerfAndResetOld(
    OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_pcm_data,
    Out<u32> out_data_size, Out<u32> out_sample_count, Out<u64> out_time_taken,
    InBuffer<BufferAttr_HipcMapAlias> opus_data, bool reset) {
    R_TRY(impl->DecodeInterleaved(out_data_size, out_time_taken, out_sample_count, opus_data,
                                  out_pcm_data, reset));
    LOG_DEBUG(Service_Audio, "reset {} bytes read {:#x} samples generated {} time taken {}", reset,
              *out_data_size, *out_sample_count, *out_time_taken);
    R_SUCCEED();
}

Result IHardwareOpusDecoder::DecodeInterleavedForMultiStreamWithPerfAndResetOld(
    OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_pcm_data,
    Out<u32> out_data_size, Out<u32> out_sample_count, Out<u64> out_time_taken,
    InBuffer<BufferAttr_HipcMapAlias> opus_data, bool reset) {
    R_TRY(impl->DecodeInterleavedForMultiStream(out_data_size, out_time_taken, out_sample_count,
                                                opus_data, out_pcm_data, reset));
    LOG_DEBUG(Service_Audio, "reset {} bytes read {:#x} samples generated {} time taken {}", reset,
              *out_data_size, *out_sample_count, *out_time_taken);
    R_SUCCEED();
}

Result IHardwareOpusDecoder::DecodeInterleaved(
    OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_pcm_data,
    Out<u32> out_data_size, Out<u32> out_sample_count, Out<u64> out_time_taken,
    InBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> opus_data,
    bool reset) {
    R_TRY(impl->DecodeInterleaved(out_data_size, out_time_taken, out_sample_count, opus_data,
                                  out_pcm_data, reset));
    LOG_DEBUG(Service_Audio, "reset {} bytes read {:#x} samples generated {} time taken {}", reset,
              *out_data_size, *out_sample_count, *out_time_taken);
    R_SUCCEED();
}

Result IHardwareOpusDecoder::DecodeInterleavedForMultiStream(
    OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_pcm_data,
    Out<u32> out_data_size, Out<u32> out_sample_count, Out<u64> out_time_taken,
    InBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> opus_data,
    bool reset) {
    R_TRY(impl->DecodeInterleavedForMultiStream(out_data_size, out_time_taken, out_sample_count,
                                                opus_data, out_pcm_data, reset));
    LOG_DEBUG(Service_Audio, "reset {} bytes read {:#x} samples generated {} time taken {}", reset,
              *out_data_size, *out_sample_count, *out_time_taken);
    R_SUCCEED();
}

} // namespace Service::Audio

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <bitset>
#include <fstream>
#include "common/assert.h"
#include "common/logging.h"
#include "shader_recompiler/exception.h"
#include "core/core.h"
#include "video_core/engines/kepler_compute.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/memory_manager.h"
#include "video_core/native_query_trace.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/textures/decoders.h"

namespace Tegra::Engines {

KeplerCompute::KeplerCompute(MemoryManager& memory_manager_)
    : memory_manager{memory_manager_}
    , upload_state{memory_manager, regs.upload}
{
    execution_mask.reset();
    execution_mask[KEPLER_COMPUTE_REG_INDEX(exec_upload)] = true;
    execution_mask[KEPLER_COMPUTE_REG_INDEX(data_upload)] = true;
    execution_mask[KEPLER_COMPUTE_REG_INDEX(launch)] = true;
}

KeplerCompute::~KeplerCompute() = default;

void KeplerCompute::BindRasterizer(VideoCore::RasterizerInterface* rasterizer_) {
    rasterizer = rasterizer_;
    upload_state.BindRasterizer(rasterizer);
}

void KeplerCompute::ConsumeSinkImpl(Core::System& system) {
    for (auto [method, value] : method_sink) {
        regs.reg_array[method] = value;
    }
    method_sink.clear();
}

bool KeplerCompute::ProcessNativeMethod(Core::System& system, u32 method, u32 argument, bool is_last_call) {
    if (native_context) {
        if (method == 0x45 || method == 0x47) native_compute_uploaded = true;
        native_macro = native_compute_uploaded ? &native_context->compute_engine : &native_context->engine;
        native_macro_positions = native_compute_uploaded ? std::span<u32>{native_context->compute_positions} : std::span<u32>{native_context->positions};
    }
    if (method >= native_registers.size()) {
        LOG_ERROR(HW_GPU, "Native compute method out of range: {:#x}", method);
        return true;
    }
    if (method >= 0xe00) {
        if (!native_executing_macro) {
            if (method & 1) { LOG_ERROR(HW_GPU, "Native compute macro starts on argument method"); return true; }
            native_executing_macro = method;
        }
        native_parameters.push_back(argument);
        if (!is_last_call) return true;
        const u32 entry = ((*native_executing_macro - 0xe00) / 2) % native_macro_positions.size();
        native_executing_macro.reset();
        const auto result = native_macro->native_interpreter.Execute(native_macro->native_code,
            native_macro_positions[entry], native_parameters,
            [&](u32 address) { return address < native_registers.size() ? native_registers[address] : 0U; },
            [&](u32 address, u32 value) {
                static u32 diagnostic_writes{};
                if (diagnostic_writes++ < 4096) LOG_DEBUG(HW_GPU, "Native compute MME emit method={:#x}, value={:#x}", address, value);
                if (address >= 0xe00) return false;
                CallMethod(system, address, value, true);
                return true;
            });
        if (!result.success) {
            native_macro->native_execution_failed = true;
            std::ofstream dump("native-compute-mme.bin", std::ios::binary);
            dump.write(reinterpret_cast<const char*>(native_macro->native_code.data()), sizeof(native_macro->native_code));
            LOG_ERROR(HW_GPU, "Native MME failed: compute start={:#x}, pc={:#x}, consumed={}/{}, error={}, parameters={:08x}",
                native_macro_positions[entry], result.pc, result.consumed, native_parameters.size(), result.error,
                fmt::join(native_parameters, ","));
        }
        native_parameters.clear();
        return true;
    }
    native_registers[method] = argument;
    if (method == 0x5a || (method >= 0xc6 && method <= 0x108)) {
        VideoCommon::TraceNativeQuery(12, memory_manager.GetID(), method, 0, 0, argument, 0);
    }
    if (method == 0x107) {
        const GPUVAddr address = ((GPUVAddr{native_registers[0xc6]} << 32) |
            native_registers[0xc7]) << 8;
        const GPUVAddr program = (GPUVAddr{native_registers[0xc8 + 49] & 0x1ffff} << 32) |
            native_registers[0xc8 + 48];
        const u32 control = native_registers[0xa7];
        LOG_WARNING(HW_GPU, "Native inline QMD: address={:#x}, program={:#x}, control={:#x}, grid={}/{}/{}",
            address, program, control, native_registers[0xc8 + 12],
            native_registers[0xc8 + 13] & 0xffff, native_registers[0xc8 + 14] & 0xffff);
        if (std::getenv("EDEN_NATIVE_DRAW_DIAGNOSTICS")) {
            std::ofstream dump("native-inline-qmd.bin", std::ios::binary);
            dump.write(reinterpret_cast<const char*>(native_registers.data() + 0xc8), 256);
            std::ofstream registers("native-inline-compute-registers.bin", std::ios::binary);
            registers.write(reinterpret_cast<const char*>(native_registers.data()), sizeof(native_registers));
            if (memory_manager.IsFullyMappedRange(program, 0x10000)) {
                std::vector<u8> code(0x10000);
                memory_manager.ReadBlockUnsafe(program, code.data(), code.size());
                std::ofstream shader("native-inline-compute-shader.bin", std::ios::binary);
                shader.write(reinterpret_cast<const char*>(code.data()), code.size());
            }
        }
        if (!memory_manager.IsFullyMappedRange(address, 256)) {
            native_macro->native_execution_failed = true;
            LOG_ERROR(HW_GPU, "Unmapped native inline QMD destination");
            return true;
        }
        rasterizer->AccelerateInlineToMemory(address, 256,
            std::span<const u8>{reinterpret_cast<const u8*>(native_registers.data() + 0xc8), 256});
        if ((control & (1U << 24)) || !(native_registers[0xcc] & (1U << 12))) {
            const u32 previous = native_registers[0xad];
            native_registers[0xad] = static_cast<u32>(address >> 8);
            ProcessNativeMethod(system, 0xb0, 3, true);
            native_registers[0xad] = previous;
        }
        return true;
    }
    switch (method) {
    case 0x45: native_macro->ClearCode(argument); return true;
    case 0x46: native_macro->AddCode(0, argument); return true;
    case 0x47: native_bind_pointer = argument; return true;
    case 0x48:
        if (native_bind_pointer < native_macro_positions.size()) native_macro_positions[native_bind_pointer++] = argument;
        else LOG_ERROR(HW_GPU, "Native compute macro bind out of bounds");
        return true;
    case 0xae: case 0xaf: case 0xb0: {
        const GPUVAddr qmd_address = GPUVAddr{native_registers[0xad]} << 8;
        VideoCommon::TraceNativeQuery(9, memory_manager.GetID(), qmd_address,
            memory_manager.GpuToCpuAddress(qmd_address).value_or(0), method, argument, 0);
        if (method == 0xae || (method == 0xaf && !(argument & 2)) ||
            (method == 0xb0 && ((argument & 15) == 0 || (argument & 15) == 1))) return true;
        if ((argument & 15) == 12 && std::getenv("EDEN_NATIVE_DRAW_DIAGNOSTICS")) {
            static u32 captured{};
            if (captured++ == 0 && memory_manager.IsFullyMappedRange(qmd_address, 256)) {
                std::array<u32, 64> descriptor{};
                memory_manager.ReadBlockUnsafe(qmd_address, descriptor.data(), sizeof(descriptor));
                std::ofstream dump("native-increment-qmd.bin", std::ios::binary);
                dump.write(reinterpret_cast<const char*>(descriptor.data()), sizeof(descriptor));
                LOG_WARNING(HW_GPU, "Native increment dependency descriptor: address={:#x}, words={:08x}",
                    qmd_address, fmt::join(descriptor, ","));
            }
        }
        const auto error = native_qmd.Signal(qmd_address, argument,
            [&](u64 address, NativeQmdScheduler::Descriptor& descriptor) {
                if (!memory_manager.IsFullyMappedRange(address, sizeof(descriptor))) return false;
                memory_manager.ReadBlockUnsafe(address, descriptor.data(), sizeof(descriptor));
                for (u32 slot = 0; slot < 2; ++slot) {
                    const u32 flags = descriptor[25 + slot * 4];
                    if (!(flags & (1U << 23))) continue;
                    const GPUVAddr destination = (GPUVAddr{flags & 0xff} << 32) | descriptor[24 + slot * 4];
                    VideoCommon::TraceNativeQuery(7 + slot, memory_manager.GetID(), destination,
                        memory_manager.GpuToCpuAddress(destination).value_or(0),
                        descriptor[4], descriptor[26 + slot * 4], flags);
                }
                return true;
            },
            [&](u64 address, u32 payload, u32 flags) {
                if (!memory_manager.IsFullyMappedRange(address, sizeof(payload))) return false;
                if (flags & (1U << 26)) {
                    rasterizer->SignalFence([this, address, payload] {
                        const u32 value = memory_manager.Read<u32>(address) + payload;
                        memory_manager.Write<u32>(address, value);
                        VideoCommon::TraceNativeQuery(1, memory_manager.GetID(), address,
                            memory_manager.GpuToCpuAddress(address).value_or(0), 0, value, 1U << 26);
                    });
                } else {
                    rasterizer->Query(address, VideoCommon::QueryType::Payload,
                        VideoCommon::QueryPropertiesFlags::IsAFence, payload, 0);
                }
                return true;
            }, [&](const auto& descriptor) { return DispatchNativeCompute(descriptor); });
        if (error.empty()) {
            LOG_DEBUG(HW_GPU, "Native QMD control action={:#x}, QMD={:#x}", argument, qmd_address);
            return true;
        }
        native_macro->native_execution_failed = true;
        LOG_ERROR(HW_GPU, "Native QMD failed: {}", error);
        std::array<u32, 128> qmd{};
        if (memory_manager.IsFullyMappedRange(qmd_address, sizeof(qmd))) {
            memory_manager.ReadBlockUnsafe(qmd_address, qmd.data(), sizeof(qmd));
            std::ofstream dump("native-qmd.bin", std::ios::binary);
            dump.write(reinterpret_cast<const char*>(qmd.data()), sizeof(qmd));
            std::ofstream macros("native-dispatch-mme.bin", std::ios::binary);
            macros.write(reinterpret_cast<const char*>(native_macro->native_code.data()), sizeof(native_macro->native_code));
            std::ofstream registers("native-dispatch-registers.bin", std::ios::binary);
            registers.write(reinterpret_cast<const char*>(native_registers.data()), sizeof(native_registers));
        }
        LOG_ERROR(HW_GPU, "Native compute dispatch unsupported: method={:#x}, value={:#x}, QMD={:#x}, words={:08x}", method, argument,
                  qmd_address, fmt::join(qmd, ","));
        return true;
    }
    default:
        // The inline upload register block is shared with the older class.
        if (method < Regs::NUM_REGS) regs.reg_array[method] = argument;
        return method != KEPLER_COMPUTE_REG_INDEX(exec_upload) && method != KEPLER_COMPUTE_REG_INDEX(data_upload);
    }
}

void KeplerCompute::CallMethod(Core::System& system, u32 method, u32 method_argument, bool is_last_call) {
    if (native_mode && ProcessNativeMethod(system, method, method_argument, is_last_call)) return;
    if (method >= Regs::NUM_REGS) {
        LOG_ERROR(HW_GPU, "Compute register out of range: {:#x}", method);
        return;
    }

    regs.reg_array[method] = method_argument;

    switch (method) {
    case KEPLER_COMPUTE_REG_INDEX(exec_upload): {
        UploadInfo info{.upload_address = upload_address,
                        .exec_address = upload_state.ExecTargetAddress(),
                        .copy_size = upload_state.GetUploadSize(),
                        .was_dirty = upload_dirty};
        uploads.push_back(info);
        upload_state.ProcessExec(regs.exec_upload.linear != 0);
        break;
    }
    case KEPLER_COMPUTE_REG_INDEX(data_upload): {
        upload_address = current_dma_segment;
        upload_dirty = current_dirty;
        current_dirty = false;
        upload_state.ProcessData(method_argument, is_last_call);
        break;
    }
    case KEPLER_COMPUTE_REG_INDEX(launch): {
        const GPUVAddr launch_desc_loc = regs.launch_desc_loc.Address();

        for (auto& data : uploads) {
            const GPUVAddr offset = data.exec_address - launch_desc_loc;
            if (offset / sizeof(u32) == LAUNCH_REG_INDEX(grid_dim_x)) {
                const bool source_dirty = memory_manager.IsMemoryDirty(data.upload_address, data.copy_size);
                if (data.was_dirty || source_dirty) {
                    indirect_compute = {data.upload_address};
                }
            }
        }
        uploads.clear();
        ProcessLaunch();
        indirect_compute = std::nullopt;
        break;
    }
    default:
        break;
    }
}

void KeplerCompute::CallMultiMethod(Core::System& system, u32 method, const u32* base_start, u32 amount, u32 methods_pending) {
    if (native_mode) {
        for (u32 i = 0; i < amount; ++i) CallMethod(system, method, base_start[i], methods_pending - i <= 1);
        return;
    }
    switch (method) {
    case KEPLER_COMPUTE_REG_INDEX(data_upload):
        upload_address = current_dma_segment;
        upload_dirty = current_dirty;
        current_dirty = false;
        upload_state.ProcessData(base_start, amount);
        return;
    default:
        for (u32 i = 0; i < amount; i++) {
            CallMethod(system, method, base_start[i], methods_pending - i <= 1);
        }
        break;
    }
}

bool KeplerCompute::DispatchNativeCompute(const NativeQmdScheduler::Descriptor& qmd) {
    launch_description = {};
    launch_description.grid_dim_x.Assign(qmd[12]);
    launch_description.grid_dim_y.Assign(qmd[13] & 0xffff);
    launch_description.grid_dim_z.Assign(qmd[14] & 0xffff);
    launch_description.shared_alloc.Assign(qmd[17] & 0x3ffff);
    launch_description.block_dim_x.Assign(qmd[18] >> 16);
    launch_description.block_dim_y.Assign(qmd[19] & 0xffff);
    launch_description.block_dim_z.Assign(qmd[19] >> 16);
    launch_description.const_buffer_enable_mask.Assign(qmd[20] & 0xff);
    launch_description.local_pos_alloc.Assign(qmd[23] & 0xffffff);
    launch_description.local_crs_alloc.Assign(qmd[50] & 0xffffff);
    regs.code_loc.address_low = qmd[48];
    regs.code_loc.address_high = qmd[49] & 0x1ffff;
    for (u32 slot = 0; slot < NumConstBuffers; ++slot) {
        const u32 high = qmd[33 + slot * 2];
        if (high & 0x1ff00) {
            LOG_ERROR(HW_GPU, "Native compute constant address exceeds supported GPU width");
            return false;
        }
        auto& buffer = launch_description.const_buffer_config[slot];
        buffer.address_low = qmd[32 + slot * 2];
        buffer.address_high.Assign(high & 0xff);
        buffer.size.Assign(((high >> 19) & 0x1fff) << 4);
    }
    if (!launch_description.block_dim_x || !launch_description.block_dim_y || !launch_description.block_dim_z)
        return false;
    try {
        rasterizer->DispatchCompute();
    } catch (const Shader::Exception& error) {
        LOG_ERROR(HW_GPU, "Native compute shader rejected: {}", error.what());
        return false;
    }
    return true;
}

void KeplerCompute::ProcessLaunch() {
    const GPUVAddr launch_desc_loc = regs.launch_desc_loc.Address();
    memory_manager.ReadBlockUnsafe(launch_desc_loc, &launch_description,
                                   LaunchParams::NUM_LAUNCH_PARAMETERS * sizeof(u32));
    rasterizer->DispatchCompute();
}

Texture::TICEntry KeplerCompute::GetTICEntry(u32 tic_index) const {
    const GPUVAddr tic_address_gpu{regs.tic.Address() + tic_index * sizeof(Texture::TICEntry)};

    Texture::TICEntry tic_entry;
    memory_manager.ReadBlockUnsafe(tic_address_gpu, &tic_entry, sizeof(Texture::TICEntry));
    return tic_entry;
}

Texture::TSCEntry KeplerCompute::GetTSCEntry(u32 tsc_index) const {
    const GPUVAddr tsc_address_gpu{regs.tsc.Address() + tsc_index * sizeof(Texture::TSCEntry)};

    Texture::TSCEntry tsc_entry;
    memory_manager.ReadBlockUnsafe(tsc_address_gpu, &tsc_entry, sizeof(Texture::TSCEntry));
    return tsc_entry;
}

} // namespace Tegra::Engines

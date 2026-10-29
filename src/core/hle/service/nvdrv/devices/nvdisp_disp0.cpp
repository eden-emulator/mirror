// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <boost/container/small_vector.hpp>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include "core/frontend/framebuffer_layout.h"
#include "video_core/renderer_base.h"

#include "common/adpf.h"
#include "common/assert.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/core_timing.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/core/nvmap.h"
#include "core/hle/service/nvdrv/devices/nvdisp_disp0.h"
#include "core/perf_stats.h"
#include "video_core/gpu.h"
#include "video_core/host1x/host1x.h"

namespace Service::Nvidia::Devices {

namespace {

Tegra::BlendMode ConvertBlending(Service::Nvnflinger::LayerBlending blending) {
    switch (blending) {
    case Service::Nvnflinger::LayerBlending::None:
    default:
        return Tegra::BlendMode::Opaque;
    case Service::Nvnflinger::LayerBlending::Premultiplied:
        return Tegra::BlendMode::Premultiplied;
    case Service::Nvnflinger::LayerBlending::Coverage:
        return Tegra::BlendMode::Coverage;
    }
}

} // namespace

nvdisp_disp0::nvdisp_disp0(Core::System& system_, NvCore::Container& core)
    : nvdevice{system_}, container{core}, nvmap{core.GetNvMapFile()} {}
nvdisp_disp0::~nvdisp_disp0() = default;

NvResult nvdisp_disp0::Ioctl1(DeviceFD fd, Ioctl command, std::span<const u8> input,
                              std::span<u8> output) {
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

NvResult nvdisp_disp0::Ioctl2(DeviceFD fd, Ioctl command, std::span<const u8> input,
                              std::span<const u8> inline_input, std::span<u8> output) {
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

NvResult nvdisp_disp0::Ioctl3(DeviceFD fd, Ioctl command, std::span<const u8> input,
                              std::span<u8> output, std::span<u8> inline_output) {
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

void nvdisp_disp0::OnOpen(NvCore::SessionId session_id, DeviceFD fd) {}
void nvdisp_disp0::OnClose(DeviceFD fd) {}

void nvdisp_disp0::Composite(std::span<const Nvnflinger::HwcLayer> sorted_layers) {
    std::vector<Tegra::FramebufferConfig> output_layers;
    std::vector<Service::Nvidia::NvFence> output_fences;
    output_layers.reserve(sorted_layers.size());
    output_fences.reserve(sorted_layers.size());

    for (auto& layer : sorted_layers) {
        const auto handle = nvmap.GetHandle(layer.buffer_handle);
        const auto* session = handle ? container.GetSession(handle->session_id) : nullptr;
        const bool native_tiling = session && session->process->UsesPAuthReturns();
        output_layers.emplace_back(Tegra::FramebufferConfig{
            .address = nvmap.GetHandleAddress(layer.buffer_handle),
            .offset = layer.offset,
            .width = layer.width,
            .height = layer.height,
            .stride = layer.stride,
            .pixel_format = layer.format,
            .transform_flags = layer.transform,
            .crop_rect = layer.crop_rect,
            .blending = ConvertBlending(layer.blending),
            .native_tiling = native_tiling,
            .layer_stack_mask = layer.layer_stack_mask,
        });

        for (size_t i = 0; i < layer.acquire_fence.num_fences; i++) {
            if (layer.acquire_fence.fences[i].id >= 0) {
                output_fences.push_back(layer.acquire_fence.fences[i]);
            }
        }
    }

    const char* capture = std::getenv("EDEN_NATIVE_FRAME_CAPTURE");
    const bool sequence = std::getenv("EDEN_NATIVE_FRAME_SEQUENCE") != nullptr;
    const bool on_demand = std::getenv("EDEN_NATIVE_FRAME_ON_DEMAND") != nullptr;
    const bool requested = on_demand && !output_layers.empty() &&
        !diagnostic_frame_pending->load() && std::filesystem::exists("native-frame.request");
    if (requested) std::filesystem::remove("native-frame.request");
    const bool sample_frame = requested || (capture && std::string_view{capture} == "1" && !output_layers.empty() &&
        diagnostic_frame_count < (sequence ? 36000U : 3840U) && ++diagnostic_frame_count != 0 &&
        (sequence ? (diagnostic_frame_count == 1 || diagnostic_frame_count % 120 == 0)
                  : !diagnostic_visible_frame->load()));
    if (sample_frame && !diagnostic_frame_pending->exchange(true)) {
        for (const auto& fence : output_fences) {
            LOG_INFO(Service_NVDRV, "Native composite waits for syncpoint={}/{}, current={}", fence.id, fence.value,
                     system.Host1x().GetSyncpointManager().GetGuestSyncpointValue(fence.id));
        }
        LOG_INFO(Service_NVDRV, "Native composite submitted: layers={}, address={:#x}", output_layers.size(), output_layers.front().address);
        const auto layout = Layout::DefaultFrameLayout(1920, 1080);
        auto pixels = std::make_shared<std::vector<u8>>(layout.width * layout.height * 4);
        const bool first = diagnostic_frame_count == 1;
        auto pending = diagnostic_frame_pending;
        auto visible = diagnostic_visible_frame;
        system.GPU().Renderer().RequestScreenshot(pixels->data(), [pixels, layout, first, pending, visible, sequence, on_demand](bool invert_y) {
            std::array<u8, 18> header{};
            header[2] = 2;
            header[12] = static_cast<u8>(layout.width & 255);
            header[13] = static_cast<u8>(layout.width >> 8);
            header[14] = static_cast<u8>(layout.height & 255);
            header[15] = static_cast<u8>(layout.height >> 8);
            header[16] = 32;
            header[17] = invert_y ? 8 : 0x28;
            bool nonblack{};
            for (size_t i = 0; i < pixels->size(); i += 4) {
                if ((*pixels)[i] || (*pixels)[i + 1] || (*pixels)[i + 2]) {
                    nonblack = true;
                    break;
                }
            }
            const auto save = [&](const char* path) {
                std::ofstream out(path, std::ios::binary);
                out.write(reinterpret_cast<const char*>(header.data()), header.size());
                out.write(reinterpret_cast<const char*>(pixels->data()), pixels->size());
                return out.good();
            };
            if (first) save("native-first-frame.tga");
            const bool saved = (first || nonblack || sequence || on_demand) ? save("native-latest-frame.tga") : true;
            if (nonblack && !visible->load() && save("native-visible-frame.tga")) visible->store(true);
            LOG_INFO(Service_NVDRV, "Captured composited frame: {}x{}, saved={}, nonblack={}", layout.width, layout.height, saved, nonblack);
            pending->store(false);
        }, layout);
    }
    system.GPU().RequestComposite(std::move(output_layers), std::move(output_fences));
    Common::ADPF::ReportFrameInterval();
    system.SpeedLimiter().DoSpeedLimiting(system.CoreTiming().GetGlobalTimeUs());
    system.GetPerfStats().EndSystemFrame();
    system.GetPerfStats().BeginSystemFrame();
}

void nvdisp_disp0::WaitForComposite() {
    system.GPU().WaitForComposite();
}

Kernel::KEvent* nvdisp_disp0::QueryEvent(u32 event_id) {
    LOG_CRITICAL(Service_NVDRV, "Unknown DISP Event {}", event_id);
    return nullptr;
}

} // namespace Service::Nvidia::Devices

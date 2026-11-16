// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <format>
#include <algorithm>
#include <array>

#include <TargetConditionals.h>

#include "common/literals.h"
#include <filesystem>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include <atomic>
#include <cstdlib>

#include "common/settings.h"
#include "video_core/metal_common/metal_device.h"

#if TARGET_OS_IPHONE
#define METAL_IS_MOBILE 1
#else
#define METAL_IS_MOBILE 0
#endif

namespace Metal {

using namespace Common::Literals;

namespace {

constexpr u32 MakeMslVersion(u32 major, u32 minor) {
    return major * 10000 + minor * 100;
}

DeviceClass ClassifyDevice(MTL::Device* device) {
    if (device->supportsFamily(MTL::GPUFamilyApple7)) {
        return DeviceClass::AppleSilicon;
    }
    if (device->supportsFamily(MTL::GPUFamilyApple1)) {
        return DeviceClass::AppleMobile;
    }
    if (device->supportsFamily(MTL::GPUFamilyMac2)) {
        return DeviceClass::MacDiscrete;
    }
    return DeviceClass::Unknown;
}

u32 HighestAppleFamily(MTL::Device* device) {
    static constexpr std::array families{
        MTL::GPUFamilyApple9, MTL::GPUFamilyApple8, MTL::GPUFamilyApple7,
        MTL::GPUFamilyApple6, MTL::GPUFamilyApple5, MTL::GPUFamilyApple4,
        MTL::GPUFamilyApple3, MTL::GPUFamilyApple2, MTL::GPUFamilyApple1,
    };
    for (size_t i = 0; i < families.size(); ++i) {
        if (device->supportsFamily(families[i])) {
            return static_cast<u32>(families.size() - i);
        }
    }
    return 0;
}

void ApplyValidationSetting() {
    if (!Settings::values.metal_validation.GetValue()) {
        return;
    }
    setenv("MTL_DEBUG_LAYER", "1", 1);
    setenv("MTL_SHADER_VALIDATION", "1", 1);
    static std::atomic<bool> announced{false};
    if (!announced.exchange(true)) {
        LOG_WARNING(Render_Metal,
                    "Metal validation requested, but Metal has already initialised - this has NO "
                    "effect. Relaunch with MTL_SHADER_VALIDATION=1 MTL_DEBUG_LAYER=1 in the "
                    "environment instead. Expect roughly 2-3x slower rendering when it is on");
    }
}

Owned<MTL::Device> CreateDefaultDevice() {
    ApplyValidationSetting();
    MTL::Device* raw = MTL::CreateSystemDefaultDevice();
    if (raw == nullptr) {
        throw Exception("No Metal device is available on this system");
    }
    return Adopt(raw);
}

Owned<MTL::Device> CreateDeviceByIndex(u32 device_index) {
    ApplyValidationSetting();
#if METAL_IS_MOBILE
    // iOS exposes exactly one GPU and has no MTLCopyAllDevices.
    if (device_index != 0) {
        LOG_WARNING(Render_Metal, "Ignoring device index {}, iOS exposes a single GPU",
                    device_index);
    }
    return CreateDefaultDevice();
#else
    Owned<NS::Array> devices = Adopt(MTL::CopyAllDevices());
    if (!devices || devices->count() == 0) {
        LOG_WARNING(Render_Metal, "MTLCopyAllDevices returned nothing, using the default device");
        return CreateDefaultDevice();
    }

    if (device_index >= devices->count()) {
        LOG_WARNING(Render_Metal, "Device index {} out of range ({} present), using device 0",
                    device_index, devices->count());
        device_index = 0;
    }

    auto* selected = devices->object<MTL::Device>(device_index);
    if (selected == nullptr) {
        return CreateDefaultDevice();
    }
    // The array owns its elements, so retain to outlive it.
    return Retain(selected);
#endif
}

} // Anonymous namespace

std::vector<DeviceRecord> EnumerateDevices() {
    ApplyValidationSetting();
    ScopedAutoreleasePool pool;
    std::vector<DeviceRecord> records;

#if METAL_IS_MOBILE
    Owned<MTL::Device> device = Adopt(MTL::CreateSystemDefaultDevice());
    if (device) {
        records.push_back(DeviceRecord{
            .name = FromNSString(device->name()),
            .registry_id = device->registryID(),
            .low_power = false,
            .headless = false,
        });
    }
#else
    Owned<NS::Array> devices = Adopt(MTL::CopyAllDevices());
    if (!devices) {
        return records;
    }

    records.reserve(devices->count());
    for (NS::UInteger i = 0; i < devices->count(); ++i) {
        auto* device = devices->object<MTL::Device>(i);
        if (device == nullptr) {
            continue;
        }
        records.push_back(DeviceRecord{
            .name = FromNSString(device->name()),
            .registry_id = device->registryID(),
            .low_power = device->isLowPower(),
            .headless = device->isHeadless(),
        });
    }
#endif

    return records;
}

Device::Device(u32 device_index) : device{CreateDeviceByIndex(device_index)} {
    ScopedAutoreleasePool pool;

    device_name = FromNSString(device->name());

    command_queue = Adopt(device->newCommandQueue());
    if (!command_queue) {
        throw Exception("Failed to create the Metal command queue");
    }
    command_queue->setLabel(ToNSString("eden"));

    DetectCapabilities();
    LogDeviceInfo();

    // Every resource binds through a hand-written argument buffer of raw handles
    // (MTLBuffer::gpuAddress, MTLTexture::gpuResourceID), with no fallback.
    if (!argument_buffers_tier2) {
        throw Exception("This GPU only supports Tier 1 argument buffers, which the Metal "
                        "renderer cannot use");
    }
    if (!metal3) {
        throw Exception("This GPU does not support Metal 3, which the Metal renderer requires");
    }
    if (!metal4) {
        throw Exception("This GPU does not support Metal 4, which the Metal renderer requires "
                        "(needs macOS 26 or later on supported hardware)");
    }

    CreateMetal4Objects();
}

void Device::LoadMetal4Archive(u64 title_id) const {
    if (!Settings::values.metal_pipeline_archive.GetValue()) {
        return;
    }
    ScopedAutoreleasePool pool;

    const auto dir = Common::FS::GetEdenPath(Common::FS::EdenPath::ShaderDir) / "metal4" /
                     std::format("{:016x}", title_id);
    if (!Common::FS::CreateDirs(dir)) {
        LOG_WARNING(Render_Metal, "Could not create the Metal 4 archive directory");
        return;
    }
    mtl4_archive_path =
        Common::FS::PathToUTF8String(std::filesystem::path{dir} / "pipelines.metallib");

    if (!Common::FS::Exists(mtl4_archive_path)) {
        LOG_INFO(Render_Metal, "No Metal 4 pipeline archive yet; this run will create one");
        return;
    }

    NS::Error* error = nullptr;
    Owned<MTL4::Archive> archive = Adopt(
        device->newArchive(NS::URL::fileURLWithPath(ToNSString(mtl4_archive_path)), &error));
    if (!archive) {
        LOG_WARNING(Render_Metal, "Could not load the Metal 4 pipeline archive: {}",
                    DescribeError(error));
        return;
    }

    const MTL4::Archive* archives[] = {archive.get()};
    mtl4_lookup_options = Adopt(MTL4::CompilerTaskOptions::alloc()->init());
    mtl4_lookup_options->setLookupArchives(
        NS::Array::array(reinterpret_cast<const NS::Object* const*>(archives), 1));
    // The options hold the array, which holds the archive.
    archive.reset();
    LOG_INFO(Render_Metal, "Loaded the Metal 4 pipeline archive from {}", mtl4_archive_path);
}

void Device::SaveMetal4Archive() const {
    if (!mtl4_serializer || mtl4_archive_path.empty()) {
        return;
    }
    ScopedAutoreleasePool pool;
    NS::Error* error = nullptr;

    // Renamed into place, never modified where it lies. Metal keeps the loaded
    // archive mapped for the compiler's lookup options, so overwriting it under a
    // live mapping segfaults inside the compiler queue (_MTLBinaryArchive
    // newArchiverIdWithBinaryKey -> dispatch_data_create_map).
    const std::string temporary_path = mtl4_archive_path + ".tmp";
    if (!mtl4_serializer->serializeAsArchiveAndFlushToURL(
            NS::URL::fileURLWithPath(ToNSString(temporary_path)), &error)) {
        // No error means nothing was captured, the serializer is created with capture off.
        if (error != nullptr) {
            LOG_WARNING(Render_Metal, "Could not write the Metal 4 pipeline archive: {}",
                        DescribeError(error));
        }
        std::error_code remove_error;
        std::filesystem::remove(std::filesystem::path{temporary_path}, remove_error);
        return;
    }
    std::error_code rename_error;
    std::filesystem::rename(std::filesystem::path{temporary_path},
                            std::filesystem::path{mtl4_archive_path}, rename_error);
    if (rename_error) {
        LOG_WARNING(Render_Metal, "Could not replace the Metal 4 pipeline archive: {}",
                    rename_error.message());
        std::error_code remove_error;
        std::filesystem::remove(std::filesystem::path{temporary_path}, remove_error);
        return;
    }
    LOG_INFO(Render_Metal, "Wrote the Metal 4 pipeline archive to {}", mtl4_archive_path);
}

void Device::AddResidentAllocation(const MTL::Allocation* allocation) const {
    if (!metal4 || !mtl4_residency_set || allocation == nullptr) {
        return;
    }
    std::scoped_lock lock{residency_mutex};
    mtl4_residency_set->addAllocation(allocation);
    residency_dirty = true;
    residency_needs_request = true;
}

/// Keep false. Leaks residency entries, which are wired and cannot be evicted.
/// A probe for GPU page faults: faults survived it, clearing every removal path.
static constexpr bool RESIDENCYPROBE_NEVER_REMOVE = false;

void Device::RemoveResidentAllocation(const MTL::Allocation* allocation) const {
    if (!metal4 || !mtl4_residency_set || allocation == nullptr ||
        RESIDENCYPROBE_NEVER_REMOVE) {
        return;
    }
    std::scoped_lock lock{residency_mutex};
    mtl4_residency_set->removeAllocation(allocation);
    residency_dirty = true;
}

void Device::CommitResidency() const {
    if (!metal4 || !mtl4_residency_set) {
        return;
    }
    std::scoped_lock lock{residency_mutex};
    if (!residency_dirty) {
        return;
    }
    mtl4_residency_set->commit();
    residency_dirty = false;

    // NEVER rate limit this. requestResidency() is what actually makes the set's
    // allocations resident, commit() only publishes its contents. It used to fire
    // once every 64 commits, and a GPU touch of non-resident memory does not fault
    // and fix up the way a CPU touch does, it comes back as
    // kIOGPUCommandBufferCallbackErrorPageFault and fails that submission plus
    // every one behind it. That was the exploded meshes in Legends Z-A.
    if (!residency_needs_request) {
        return;
    }
    residency_needs_request = false;
    mtl4_residency_set->requestResidency();
}

bool Device::RecreateMTL4Queue() const {
    ScopedAutoreleasePool pool;
    NS::Error* error = nullptr;

    MTL4::CommandQueueDescriptor* queue_descriptor = MTL4::CommandQueueDescriptor::alloc()->init();
    queue_descriptor->setLabel(ToNSString("eden mtl4 (recovered)"));
    Owned<MTL4::CommandQueue> replacement =
        Adopt(device->newMTL4CommandQueue(queue_descriptor, &error));
    queue_descriptor->release();
    if (!replacement) {
        LOG_ERROR(Render_Metal, "Could not rebuild the Metal 4 command queue: {}",
                  DescribeError(error));
        return false;
    }

    // A queue makes nothing resident by itself, so attach the set again or every
    // allocation is unmapped and the first submission on the new queue faults.
    if (mtl4_residency_set) {
        replacement->addResidencySet(mtl4_residency_set.get());
    }

    mtl4_queue = std::move(replacement);
    LOG_WARNING(Render_Metal,
                "Rebuilt the Metal 4 command queue after it stopped executing submissions");
    // A drawable left waiting on a dead queue never returns to the layer's pool.
    if (on_queue_rebuilt) {
        on_queue_rebuilt();
    }
    return true;
}

void Device::CreateMetal4Objects() {
    NS::Error* error = nullptr;

    // MTL4::CommandQueue has no setLabel, so the label goes on the descriptor.
    MTL4::CommandQueueDescriptor* queue_descriptor = MTL4::CommandQueueDescriptor::alloc()->init();
    queue_descriptor->setLabel(ToNSString("eden mtl4"));
    mtl4_queue = Adopt(device->newMTL4CommandQueue(queue_descriptor, &error));
    queue_descriptor->release();
    if (!mtl4_queue) {
        throw Exception("Failed to create the Metal 4 command queue: " + DescribeError(error));
    }

    // The serializer has to be attached at compiler creation, never later.
    MTL4::PipelineDataSetSerializerDescriptor* serializer_descriptor =
        MTL4::PipelineDataSetSerializerDescriptor::alloc()->init();
    // The configuration is an options mask defaulting to zero, capture nothing, so
    // no archive is produced or loaded. CaptureBinaries does work - it wrote a
    // 13.7 MB archive - but the result is not sound: generation reported "missing
    // 'vertex' stage in pipeline no. 0" because graphics and compute pipelines
    // share one serializer, and loading it segfaulted Metal's own compiler queue.
    // Re-enable only with a separate serializer for compute.
    mtl4_serializer = Adopt(device->newPipelineDataSetSerializer(serializer_descriptor));
    serializer_descriptor->release();

    MTL4::CompilerDescriptor* compiler_descriptor = MTL4::CompilerDescriptor::alloc()->init();
    compiler_descriptor->setLabel(ToNSString("eden mtl4 compiler"));
    if (mtl4_serializer) {
        compiler_descriptor->setPipelineDataSetSerializer(mtl4_serializer.get());
    }
    mtl4_compiler = Adopt(device->newCompiler(compiler_descriptor, &error));
    compiler_descriptor->release();
    if (!mtl4_compiler) {
        throw Exception("Failed to create the Metal 4 compiler: " + DescribeError(error));
    }

    // Nothing is implicitly resident on Metal 4. Every buffer and texture the GPU
    // may touch must be added here, or it reads unmapped memory with no error.
    MTL::ResidencySetDescriptor* residency_descriptor =
        MTL::ResidencySetDescriptor::alloc()->init();
    residency_descriptor->setLabel(ToNSString("eden mtl4 residency"));
    residency_descriptor->setInitialCapacity(4096);
    mtl4_residency_set = Adopt(device->newResidencySet(residency_descriptor, &error));
    residency_descriptor->release();
    if (!mtl4_residency_set) {
        throw Exception("Failed to create the Metal 4 residency set: " + DescribeError(error));
    }
    mtl4_queue->addResidencySet(mtl4_residency_set.get());

    LOG_INFO(Render_Metal, "Metal 4 available: queue, compiler and residency set created");
}

Device::~Device() = default;

void Device::DetectCapabilities() {
    device_class = ClassifyDevice(device.get());
    mobile_platform = METAL_IS_MOBILE != 0;

    unified_memory = device->hasUnifiedMemory();
    metal3 = device->supportsFamily(MTL::GPUFamilyMetal3);
    metal4 = device->supportsFamily(MTL::GPUFamilyMetal4);
    argument_buffers_tier2 = device->argumentBuffersSupport() == MTL::ArgumentBuffersTier2;
    float32_filtering = device->supports32BitFloatFiltering();
    raster_order_groups = device->areRasterOrderGroupsSupported();

    // Off until there is a way to ask. Metal publishes no query for min/max
    // sampler reduction and this machine (M1, Apple7) rejects it: "Use of minimum
    // or maximum reductionMode requires device support". With validation off
    // newSamplerState just returns nothing.
    sampler_reduction = false;
    stage_boundary_counters =
        device->supportsCounterSampling(MTL::CounterSamplingPointAtStageBoundary);

    max_buffer_length = device->maxBufferLength();
    max_threadgroup_memory = device->maxThreadgroupMemoryLength();
    recommended_working_set = device->recommendedMaxWorkingSetSize();

    // BC (DXT/BCn) is native on every Mac GPU but only on Apple7 and newer
    // mobile parts. Where it is missing the texture cache transcodes on the CPU.
    bc_texture_compression = device->supportsBCTextureCompression();

#if METAL_IS_MOBILE
    // D24S8 has never existed on Apple mobile GPUs, the cache promotes to D32S8.
    depth24_stencil8 = false;
#else
    depth24_stencil8 = device->isDepth24Stencil8PixelFormatSupported();
#endif

    // Inferred rather than queried, the real answer needs a compiled compute
    // pipeline and there is none this early. Apple GPUs are 32 wide, AMD 64.
    subgroup_size = device_class == DeviceClass::MacDiscrete ? 64 : 32;

    max_sample_count = 1;
    for (const u32 count : {8u, 4u, 2u}) {
        if (device->supportsTextureSampleCount(count)) {
            max_sample_count = count;
            break;
        }
    }

    // The 2.4 branch is unreachable, construction refuses a device without Metal 3.
    // 3.1 has native 2D texture atomics. Below it SPIRV-Cross emulates them through a
    // buffer slot beside the image that nothing here fills, and the atomics fault the GPU.
    // Metal 4 implies an OS new enough for 3.1.
    msl_version = metal4 ? MakeMslVersion(3, 1)
                         : (metal3 ? MakeMslVersion(3, 0) : MakeMslVersion(2, 4));

    if (recommended_working_set == 0) {
        // Some devices report zero, and the caches divide by this.
        recommended_working_set = 2_GiB;
    }
}

std::string Device::GetVendorName() const {
    switch (device_class) {
    case DeviceClass::AppleSilicon:
    case DeviceClass::AppleMobile:
        return "Apple";
    case DeviceClass::MacDiscrete:
        if (device_name.find("AMD") != std::string::npos ||
            device_name.find("Radeon") != std::string::npos) {
            return "AMD";
        }
        if (device_name.find("Intel") != std::string::npos) {
            return "Intel";
        }
        return "Unknown";
    case DeviceClass::Unknown:
    default:
        return "Unknown";
    }
}

void Device::LogDeviceInfo() const {
    static constexpr std::array class_names{"Apple Silicon", "Apple Mobile", "Mac Discrete",
                                            "Unknown"};

    LOG_INFO(Render_Metal, "Metal device: {}", device_name);
    LOG_INFO(Render_Metal, "Device class: {} (Apple family {})",
             class_names[static_cast<size_t>(device_class)], HighestAppleFamily(device.get()));
    LOG_INFO(Render_Metal, "Metal 3: {} | MSL {}.{}", metal3, msl_version / 10000,
             (msl_version % 10000) / 100);
    LOG_INFO(Render_Metal, "Unified memory: {} | Argument buffers: Tier {}", unified_memory,
             argument_buffers_tier2 ? 2 : 1);
    LOG_INFO(Render_Metal, "Max buffer: {} MiB | Max threadgroup memory: {} KiB",
             max_buffer_length / 1_MiB, max_threadgroup_memory / 1_KiB);
    LOG_INFO(Render_Metal, "Recommended working set: {} MiB", recommended_working_set / 1_MiB);
    LOG_INFO(Render_Metal, "BC compression: {} | D24S8: {} | Max MSAA: {}x",
             bc_texture_compression, depth24_stencil8, max_sample_count);
    LOG_INFO(Render_Metal, "Stage boundary counters: {} | Raster order groups: {}",
             stage_boundary_counters, raster_order_groups);
}

} // namespace Metal

// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <array>
#include <span>
#include "common/common_types.h"
#include "core/hle/result.h"

namespace Core { class System; }
namespace Kernel { class KProcess; }
namespace Service { class SessionRequestHandler; }
namespace Service::Nvidia { class Module; }
namespace Service::Nvidia::Devices { class nvhost_as_gpu; }
namespace Service::AM {
enum class NativeVideoKind { Decoder, Encoder };
struct NativeVideoState;

class NativeVideoService {
public:
    NativeVideoService(Core::System& system, std::shared_ptr<Nvidia::Module> module,
                       Kernel::KProcess* process, std::shared_ptr<void> memory_owner);
    std::shared_ptr<SessionRequestHandler> CreateContext(NativeVideoKind kind);
    Result MapGpuImage(std::shared_ptr<Nvidia::Devices::nvhost_as_gpu> address_space,
                       u32 image_id, u64& address, u64& size);
    Result UnmapGpuImage(const std::shared_ptr<Nvidia::Devices::nvhost_as_gpu>& address_space,
                         u32 image_id, u64 address, u64 end);
    Result QueryGpuImage(u32 image_id, std::array<u64, 16>& descriptor);
    Result QueryVideoImage(u32 image_id, std::array<u64, 16>& descriptor);
    Result DestroyImagePool(u32 first_image_id);
    Result CreateImagePool(std::span<const u32> preference, u32 count, u32 memory_id,
                           u32 first_image_id);
private:
    std::shared_ptr<NativeVideoState> state;
};
} // namespace Service::AM

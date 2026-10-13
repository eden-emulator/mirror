// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/psc/ovln/ovln_types.h"
#include "core/hle/service/psc/ovln/sender_service.h"

namespace Service::PSC {

class ISender final : public ServiceFramework<ISender> {
public:
    explicit ISender(Core::System& system_) : ServiceFramework{system_, "ISender"} {}
    ~ISender() override = default;

    Result Send(const OverlayNotification& notification, MessageFlags flags) {
        std::string data;
        for (const auto m : notification) {
            data += fmt::format("{:016X} ", m);
        }

        LOG_WARNING(Service_PSC, "(STUBBED) called, flags={} notification={}", flags.raw, data);
        R_SUCCEED();
    }

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&ISender::Send>, "Send"},
        FunctionInfo{1, nullptr, "GetUnreceivedMessageCount"}
    );
};

std::optional<ServiceFrameworkBase::FunctionInfoBase> ISenderService::FindRequest(u32 key) {
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&ISenderService::OpenSender>, "OpenSender"}
    );
    return HandlerTableGenerateWithFind(key, functions);
}

ISenderService::ISenderService(Core::System& system_) : ServiceFramework{system_, "ovln:snd"} {
}

ISenderService::~ISenderService() = default;

Result ISenderService::OpenSender(Out<SharedPointer<ISender>> out_sender, u32 sender_id,
                                  std::array<u64, 2> data) {
    LOG_WARNING(Service_PSC, "(STUBBED) called, sender_id={}, data={:016X} {:016X}", sender_id,
                data[0], data[1]);
    *out_sender = std::make_shared<ISender>(system);
    R_SUCCEED();
}

} // namespace Service::PSC

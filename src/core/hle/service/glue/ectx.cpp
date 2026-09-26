// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/glue/ectx.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::Glue {

// This is nn::err::context::IContextRegistrar
class IContextRegistrar : public ServiceFramework<IContextRegistrar> {
public:
    IContextRegistrar(Core::System& system_) : ServiceFramework{system_, "IContextRegistrar"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

    ~IContextRegistrar() override = default;

private:
    void Complete(HLERequestContext& ctx) {
        struct InputParameters {
            u32 unk;
        };
        struct OutputParameters {
            u32 unk;
        };

        IPC::RequestParser rp{ctx};
        [[maybe_unused]] auto input = rp.PopRaw<InputParameters>();
        [[maybe_unused]] auto value = ctx.ReadBuffer();

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(0);
    }

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &IContextRegistrar::Complete, "Complete"}
    );
};

ECTX_W::ECTX_W(Core::System& system_) : ServiceFramework{system_, "ectx:w"} {
}
ECTX_W::~ECTX_W() = default;

ECTX_R::ECTX_R(Core::System& system_) : ServiceFramework{system_, "ectx:r"} {
}
ECTX_R::~ECTX_R() = default;

ECTX_AW::ECTX_AW(Core::System& system_) : ServiceFramework{system_, "ectx:aw"} {
}

ECTX_AW::~ECTX_AW() = default;

void ECTX_AW::CreateContextRegistrar(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IContextRegistrar>(ctx, system);
}

} // namespace Service::Glue

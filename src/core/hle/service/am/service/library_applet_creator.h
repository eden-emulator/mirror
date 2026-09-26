// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/am/am_types.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {

struct Applet;
class ILibraryAppletAccessor;
class IStorage;
class WindowSystem;

class ILibraryAppletCreator final : public ServiceFramework<ILibraryAppletCreator> {
public:
    explicit ILibraryAppletCreator(Core::System& system_, std::shared_ptr<Applet> applet,
                                   WindowSystem& window_system);
    ~ILibraryAppletCreator() override;

private:
    Result CreateLibraryApplet(
        Out<SharedPointer<ILibraryAppletAccessor>> out_library_applet_accessor, AppletId applet_id,
        LibraryAppletMode library_applet_mode);
    Result CreateLibraryAppletEx(
        Out<SharedPointer<ILibraryAppletAccessor>> out_library_applet_accessor, AppletId applet_id,
        LibraryAppletMode library_applet_mode, u64 thread_id);
    Result CreateStorage(Out<SharedPointer<IStorage>> out_storage, s64 size);
    Result CreateTransferMemoryStorage(
        Out<SharedPointer<IStorage>> out_storage, bool is_writable, s64 size,
        InCopyHandle<Kernel::KTransferMemory> transfer_memory_handle);
    Result CreateHandleStorage(Out<SharedPointer<IStorage>> out_storage, s64 size,
        InCopyHandle<Kernel::KTransferMemory> transfer_memory_handle);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&ILibraryAppletCreator::CreateLibraryApplet>, "CreateLibraryApplet"},
        FunctionInfo{1, nullptr, "TerminateAllLibraryApplets"},
        FunctionInfo{2, nullptr, "AreAnyLibraryAppletsLeft"},
        FunctionInfo{3, D<&ILibraryAppletCreator::CreateLibraryAppletEx>, "CreateLibraryAppletEx"},
        FunctionInfo{10, D<&ILibraryAppletCreator::CreateStorage>, "CreateStorage"},
        FunctionInfo{11, D<&ILibraryAppletCreator::CreateTransferMemoryStorage>, "CreateTransferMemoryStorage"},
        FunctionInfo{12, D<&ILibraryAppletCreator::CreateHandleStorage>, "CreateHandleStorage"}
    );
    WindowSystem& m_window_system;
    const std::shared_ptr<Applet> m_applet;
};

} // namespace Service::AM

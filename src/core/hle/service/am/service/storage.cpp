// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/kernel/k_transfer_memory.h"
#include "core/hle/service/am/am_results.h"
#include "core/hle/service/am/library_applet_storage.h"
#include "core/hle/service/am/service/storage.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

class IStorageAccessor final : public ServiceFramework<IStorageAccessor> {
public:
    explicit IStorageAccessor(Core::System& system_, std::shared_ptr<LibraryAppletStorage> impl) : ServiceFramework{system_, "IStorageAccessor"}, m_impl{std::move(impl)} {}
    ~IStorageAccessor() override = default;

    Result GetSize(Out<s64> out_size) {
        LOG_DEBUG(Service_AM, "called");
        *out_size = m_impl->GetSize();
        R_SUCCEED();
    }

    Result Write(InBuffer<BufferAttr_HipcAutoSelect> buffer, s64 offset) {
        LOG_DEBUG(Service_AM, "called, offset={} size={}", offset, buffer.size());
        R_RETURN(m_impl->Write(offset, buffer.data(), buffer.size()));
    }

    Result Read(OutBuffer<BufferAttr_HipcAutoSelect> out_buffer, s64 offset) {
        LOG_DEBUG(Service_AM, "called, offset={} size={}", offset, out_buffer.size());
        R_RETURN(m_impl->Read(offset, out_buffer.data(), out_buffer.size()));
    }

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IStorageAccessor::GetSize>, "GetSize"},
        FunctionInfo{10, D<&IStorageAccessor::Write>, "Write"},
        FunctionInfo{11, D<&IStorageAccessor::Read>, "Read"}
    );

    const std::shared_ptr<LibraryAppletStorage> m_impl;
};

class ITransferStorageAccessor final : public ServiceFramework<ITransferStorageAccessor> {
public:
    explicit ITransferStorageAccessor(Core::System& system_, std::shared_ptr<LibraryAppletStorage> impl) : ServiceFramework{system_, "ITransferStorageAccessor"}, m_impl{std::move(impl)} {}
    ~ITransferStorageAccessor() override = default;

    Result GetSize(Out<s64> out_size) {
        LOG_DEBUG(Service_AM, "called");
        *out_size = m_impl->GetSize();
        R_SUCCEED();
    }

    Result GetHandle(Out<s64> out_size, OutCopyHandle<Kernel::KTransferMemory> out_handle) {
        LOG_INFO(Service_AM, "called");
        *out_size = m_impl->GetSize();
        *out_handle = m_impl->GetHandle();
        R_SUCCEED();
    }

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&ITransferStorageAccessor::GetSize>, "GetSize"},
        FunctionInfo{1, D<&ITransferStorageAccessor::GetHandle>, "GetHandle"}
    );

    const std::shared_ptr<LibraryAppletStorage> m_impl;
};

std::optional<ServiceFrameworkBase::FunctionInfoBase> IStorage::FindRequest(u32 key) {
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IStorage::Open>, "Open"},
        FunctionInfo{1, D<&IStorage::OpenTransferStorage>, "OpenTransferStorage"}
    );
    return HandlerTableGenerateWithFind(key, functions);
}

IStorage::IStorage(Core::System& system_, std::shared_ptr<LibraryAppletStorage> impl)
    : ServiceFramework{system_, "IStorage"}, m_impl{std::move(impl)} {
}

IStorage::IStorage(Core::System& system_, std::vector<u8>&& data)
    : IStorage(system_, CreateStorage(std::move(data))) {}

IStorage::~IStorage() = default;

Result IStorage::Open(Out<SharedPointer<IStorageAccessor>> out_storage_accessor) {
    LOG_DEBUG(Service_AM, "called");
    R_UNLESS(m_impl->GetHandle() == nullptr, AM::ResultInvalidStorageType);
    *out_storage_accessor = std::make_shared<IStorageAccessor>(system, m_impl);
    R_SUCCEED();
}

Result IStorage::OpenTransferStorage(Out<SharedPointer<ITransferStorageAccessor>> out_transfer_storage_accessor) {
    LOG_DEBUG(Service_AM, "called");
    R_UNLESS(m_impl->GetHandle() != nullptr, AM::ResultInvalidStorageType);
    *out_transfer_storage_accessor = std::make_shared<ITransferStorageAccessor>(system, m_impl);
    R_SUCCEED();
}

std::vector<u8> IStorage::GetData() const {
    return m_impl->GetData();
}

} // namespace Service::AM

// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"
#include "core/hle/service/ns/ns_types.h"

namespace Service::NS {

class IReadOnlyApplicationRecordInterface final
    : public ServiceFramework<IReadOnlyApplicationRecordInterface> {
public:
    explicit IReadOnlyApplicationRecordInterface(Core::System& system_);
    ~IReadOnlyApplicationRecordInterface() override;

private:
    Result HasApplicationRecord(Out<bool> out_has_application_record, u64 program_id);
    Result IsDataCorruptedResult(Out<bool> out_is_data_corrupted_result, Result result);
    Result ListApplicationRecord(
        OutArray<ApplicationRecord, BufferAttr_HipcMapAlias> out_records, Out<s32> out_count,
        s32 entry_offset);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IReadOnlyApplicationRecordInterface::HasApplicationRecord>, "HasApplicationRecord"},
        FunctionInfo{1, nullptr, "NotifyApplicationFailure"},
        FunctionInfo{2, D<&IReadOnlyApplicationRecordInterface::IsDataCorruptedResult>, "IsDataCorruptedResult"},
        FunctionInfo{3, D<&IReadOnlyApplicationRecordInterface::ListApplicationRecord>, "ListApplicationRecord"}
    );
};

} // namespace Service::NS

// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

#include "core/hle/service/bcat/news/news_storage.h"

namespace Core {
class System;
}

namespace Service::News {

class INewsDatabaseService final : public ServiceFramework<INewsDatabaseService> {
public:
    explicit INewsDatabaseService(Core::System& system_);
    ~INewsDatabaseService() override;

private:
    Result Count(Out<s32> out_count, InBuffer<BufferAttr_HipcPointer> buffer_data);

    Result CountWithKey(Out<s32> out_count, InBuffer<BufferAttr_HipcPointer> key,
                        InBuffer<BufferAttr_HipcPointer> where);

    Result UpdateIntegerValue(u32 value, InBuffer<BufferAttr_HipcPointer> key,
                              InBuffer<BufferAttr_HipcPointer> where);

    Result UpdateIntegerValueWithAddition(u32 value, InBuffer<BufferAttr_HipcPointer> buffer_data_1,
                                          InBuffer<BufferAttr_HipcPointer> buffer_data_2);

    Result UpdateStringValue(InBuffer<BufferAttr_HipcPointer> key,
                             InBuffer<BufferAttr_HipcPointer> value,
                             InBuffer<BufferAttr_HipcPointer> where);

    Result GetListV1(Out<s32> out_count,
                     OutBuffer<BufferAttr_HipcMapAlias> out_buffer_data,
                     InBuffer<BufferAttr_HipcPointer> where_phrase,
                     InBuffer<BufferAttr_HipcPointer> order_by_phrase,
                     s32 offset);

    Result GetList(Out<s32> out_count,
                   OutBuffer<BufferAttr_HipcMapAlias> out_buffer_data,
                   InBuffer<BufferAttr_HipcPointer> where_phrase,
                   InBuffer<BufferAttr_HipcPointer> order_by_phrase,
                   s32 offset);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&INewsDatabaseService::GetListV1>, "GetListV1"},
        FunctionInfo{1, D<&INewsDatabaseService::Count>, "Count"},
        FunctionInfo{2, D<&INewsDatabaseService::CountWithKey>, "CountWithKey"},
        FunctionInfo{3, D<&INewsDatabaseService::UpdateIntegerValue>, "UpdateIntegerValue"},
        FunctionInfo{4, D<&INewsDatabaseService::UpdateIntegerValueWithAddition>, "UpdateIntegerValueWithAddition"},
        FunctionInfo{5, D<&INewsDatabaseService::UpdateStringValue>, "UpdateStringValue"},
        FunctionInfo{1000, D<&INewsDatabaseService::GetList>, "GetList"}
    );
};

} // namespace Service::News

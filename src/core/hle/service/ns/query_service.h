// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/uuid.h"
#include "core/hle/service/am/am_types.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/ns/ns_types.h"
#include "core/hle/service/service.h"

#include <memory>

namespace PlayTime {
class PlayTimeManager;
}

namespace Service::NS {

struct PlayStatistics {
    u64 application_id{};
    u32 first_entry_index{};
    u32 first_timestamp_user{};
    u32 first_timestamp_network{};
    u32 last_entry_index{};
    u32 last_timestamp_user{};
    u32 last_timestamp_network{};
    u32 play_time_in_minutes{};
    u32 total_launches{};
};
static_assert(sizeof(PlayStatistics) == 0x28, "PlayStatistics is an invalid size");

struct LastPlayTime {};

struct ApplicationPlayStatistics {
    u64 application_id{};
    u64 play_time_ns{};
    u64 launch_count{};
};
static_assert(sizeof(ApplicationPlayStatistics) == 0x18, "ApplicationPlayStatistics is an invalid size");

class IQueryService final : public ServiceFramework<IQueryService> {
public:
    explicit IQueryService(Core::System& system_);
    ~IQueryService() override;

private:
    Result QueryPlayStatisticsByApplicationIdAndUserAccountId(
        Out<PlayStatistics> out_play_statistics, bool unknown, u64 application_id, Uid account_id);
    Result QueryLastPlayTime(Out<s32> out_entries, u8 unknown,
                             OutArray<LastPlayTime, BufferAttr_HipcMapAlias> out_last_play_times,
                             InArray<s32, BufferAttr_HipcMapAlias> application_ids);
    Result QueryApplicationPlayStatisticsForSystem(
        Out<s32> out_entries, u8 flag,
        OutArray<ApplicationPlayStatistics, BufferAttr_HipcMapAlias> out_stats,
        InArray<u64, BufferAttr_HipcMapAlias> application_ids);
    Result QueryApplicationPlayStatisticsByUserAccountIdForSystem(
        Out<s32> out_entries, u8 flag, Common::UUID user_id,
        OutArray<ApplicationPlayStatistics, BufferAttr_HipcMapAlias> out_stats,
        InArray<u64, BufferAttr_HipcMapAlias> application_ids);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "QueryAppletEvent"},
        FunctionInfo{1, nullptr, "QueryPlayStatistics"},
        FunctionInfo{2, nullptr, "QueryPlayStatisticsByUserAccountId"},
        FunctionInfo{3, nullptr, "QueryPlayStatisticsByNetworkServiceAccountId"},
        FunctionInfo{4, nullptr, "QueryPlayStatisticsByApplicationId"},
        FunctionInfo{5, D<&IQueryService::QueryPlayStatisticsByApplicationIdAndUserAccountId>, "QueryPlayStatisticsByApplicationIdAndUserAccountId"},
        FunctionInfo{6, nullptr, "QueryPlayStatisticsByApplicationIdAndNetworkServiceAccountId"},
        FunctionInfo{7, nullptr, "QueryLastPlayTimeV0"},
        FunctionInfo{8, nullptr, "QueryPlayEvent"},
        FunctionInfo{9, nullptr, "GetAvailablePlayEventRange"},
        FunctionInfo{10, nullptr, "QueryAccountEvent"},
        FunctionInfo{11, nullptr, "QueryAccountPlayEvent"},
        FunctionInfo{12, nullptr, "GetAvailableAccountPlayEventRange"},
        FunctionInfo{13, nullptr, "QueryApplicationPlayStatisticsForSystemV0"},
        FunctionInfo{14, nullptr, "QueryRecentlyPlayedApplication"},
        FunctionInfo{15, nullptr, "GetRecentlyPlayedApplicationUpdateEvent"},
        FunctionInfo{16, nullptr, "QueryApplicationPlayStatisticsByUserAccountIdForSystemV0"},
        FunctionInfo{17, D<&IQueryService::QueryLastPlayTime>, "QueryLastPlayTime"},
        FunctionInfo{18, D<&IQueryService::QueryApplicationPlayStatisticsForSystem>, "QueryApplicationPlayStatisticsForSystem"},
        FunctionInfo{19, D<&IQueryService::QueryApplicationPlayStatisticsByUserAccountIdForSystem>, "QueryApplicationPlayStatisticsByUserAccountIdForSystem"},
        FunctionInfo{30, nullptr, "Cmd30" },
        FunctionInfo{31, nullptr, "Cmd31" },
        FunctionInfo{100, nullptr, "Cmd100" },
        FunctionInfo{110, nullptr, "Cmd110" },
        FunctionInfo{118, nullptr, "Cmd118" },
        FunctionInfo{119, nullptr, "Cmd119" },
    );
    std::unique_ptr<PlayTime::PlayTimeManager> play_time_manager;
};

} // namespace Service::NS

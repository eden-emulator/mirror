// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"
#include "core/hle/service/set/settings_types.h"

namespace Core {
class System;
}

namespace Service::Set {
using KeyCodeMap = std::array<u8, 0x1000>;

LanguageCode GetLanguageCodeFromIndex(std::size_t idx);

class ISettingsServer final : public ServiceFramework<ISettingsServer> {
public:
    explicit ISettingsServer(Core::System& system_);
    ~ISettingsServer() override;

private:
    Result GetLanguageCode(Out<LanguageCode> out_language_code);
    Result GetAvailableLanguageCodes(Out<s32> out_count, OutArray<LanguageCode, BufferAttr_HipcPointer> language_codes);
    Result MakeLanguageCode(Out<LanguageCode> out_language_code, Language language);
    Result GetAvailableLanguageCodeCount(Out<s32> out_count);
    Result GetRegionCode(Out<SystemRegionCode> out_region_code);
    Result GetAvailableLanguageCodes2(Out<s32> out_count, OutArray<LanguageCode, BufferAttr_HipcMapAlias> language_codes);
    Result GetAvailableLanguageCodeCount2(Out<s32> out_count);
    Result GetKeyCodeMap(OutLargeData<KeyCodeMap, BufferAttr_HipcMapAlias> out_key_code_map);
    Result GetQuestFlag(Out<bool> out_quest_flag);
    Result GetKeyCodeMap2(OutLargeData<KeyCodeMap, BufferAttr_HipcMapAlias> out_key_code_map);
    Result GetDeviceNickName(OutLargeData<std::array<u8, 0x80>, BufferAttr_HipcMapAlias> out_device_name);
    Result GetKeyCodeMapByPort(OutLargeData<KeyCodeMap, BufferAttr_HipcMapAlias> out_key_code_map, u32 port);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, C<&ISettingsServer::GetLanguageCode>, "GetLanguageCode"},
        FunctionInfo{1, C<&ISettingsServer::GetAvailableLanguageCodes>, "GetAvailableLanguageCodes"},
        FunctionInfo{2, C<&ISettingsServer::MakeLanguageCode>, "MakeLanguageCode"},
        FunctionInfo{3, C<&ISettingsServer::GetAvailableLanguageCodeCount>, "GetAvailableLanguageCodeCount"},
        FunctionInfo{4, C<&ISettingsServer::GetRegionCode>, "GetRegionCode"},
        FunctionInfo{5, C<&ISettingsServer::GetAvailableLanguageCodes2>, "GetAvailableLanguageCodes2"},
        FunctionInfo{6, C<&ISettingsServer::GetAvailableLanguageCodeCount2>, "GetAvailableLanguageCodeCount2"},
        FunctionInfo{7, C<&ISettingsServer::GetKeyCodeMap>, "GetKeyCodeMap"},
        FunctionInfo{8, C<&ISettingsServer::GetQuestFlag>, "GetQuestFlag"},
        FunctionInfo{9, C<&ISettingsServer::GetKeyCodeMap2>, "GetKeyCodeMap2"},
        FunctionInfo{10, nullptr, "GetFirmwareVersionForDebug"},
        FunctionInfo{11, C<&ISettingsServer::GetDeviceNickName>, "GetDeviceNickName"},
        FunctionInfo{12, C<&ISettingsServer::GetKeyCodeMapByPort>, "GetKeyCodeMapByPort"}
    );
};

} // namespace Service::Set

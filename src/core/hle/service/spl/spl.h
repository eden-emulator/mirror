// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/spl/spl_module.h"

namespace Core {
class System;
}

namespace Service::SPL {

class SPL final : public Module::Interface {
public:
    explicit SPL(Core::System& system_, std::shared_ptr<Module> module_);
    ~SPL() override;

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &SPL::GetConfig, "GetConfig"},
        FunctionInfo{1, &SPL::ModularExponentiate, "ModularExponentiate"},
        FunctionInfo{5, &SPL::SetConfig, "SetConfig"},
        FunctionInfo{7, &SPL::GenerateRandomBytes, "GenerateRandomBytes"},
        FunctionInfo{11, &SPL::IsDevelopment, "IsDevelopment"},
        FunctionInfo{24, &SPL::SetBootReason, "SetBootReason"},
        FunctionInfo{25, &SPL::GetBootReason, "GetBootReason"}
    );
};

class SPL_MIG final : public Module::Interface {
public:
    explicit SPL_MIG(Core::System& system_, std::shared_ptr<Module> module_);
    ~SPL_MIG() override;

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &SPL::GetConfig, "GetConfig"},
        FunctionInfo{1, &SPL::ModularExponentiate, "ModularExponentiate"},
        FunctionInfo{2, &SPL::GenerateAesKek, "GenerateAesKek"},
        FunctionInfo{3, nullptr, "LoadAesKey"},
        FunctionInfo{4, &SPL::GenerateAesKey, "GenerateAesKey"},
        FunctionInfo{5, &SPL::SetConfig, "SetConfig"},
        FunctionInfo{7, &SPL::GenerateRandomBytes, "GenerateRandomBytes"},
        FunctionInfo{11, &SPL::IsDevelopment, "IsDevelopment"},
        FunctionInfo{14, nullptr, "DecryptAesKey"},
        FunctionInfo{15, nullptr, "CryptAesCtr"},
        FunctionInfo{16, nullptr, "ComputeCmac"},
        FunctionInfo{21, nullptr, "AllocateAesKeyslot"},
        FunctionInfo{22, nullptr, "DeallocateAesKeySlot"},
        FunctionInfo{23, nullptr, "GetAesKeyslotAvailableEvent"},
        FunctionInfo{24, &SPL::SetBootReason, "SetBootReason"},
        FunctionInfo{25, &SPL::GetBootReason, "GetBootReason"}
    );
};

class SPL_FS final : public Module::Interface {
public:
    explicit SPL_FS(Core::System& system_, std::shared_ptr<Module> module_);
    ~SPL_FS() override;

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &SPL::GetConfig, "GetConfig"},
        FunctionInfo{1, &SPL::ModularExponentiate, "ModularExponentiate"},
        FunctionInfo{2, nullptr, "GenerateAesKek"},
        FunctionInfo{3, nullptr, "LoadAesKey"},
        FunctionInfo{4, nullptr, "GenerateAesKey"},
        FunctionInfo{5, &SPL::SetConfig, "SetConfig"},
        FunctionInfo{7, &SPL::GenerateRandomBytes, "GenerateRandomBytes"},
        FunctionInfo{9, nullptr, "ImportLotusKey"},
        FunctionInfo{10, nullptr, "DecryptLotusMessage"},
        FunctionInfo{11, &SPL::IsDevelopment, "IsDevelopment"},
        FunctionInfo{12, nullptr, "GenerateSpecificAesKey"},
        FunctionInfo{14, nullptr, "DecryptAesKey"},
        FunctionInfo{15, nullptr, "CryptAesCtr"},
        FunctionInfo{16, nullptr, "ComputeCmac"},
        FunctionInfo{19, nullptr, "LoadTitleKey"},
        FunctionInfo{21, nullptr, "AllocateAesKeyslot"},
        FunctionInfo{22, nullptr, "DeallocateAesKeySlot"},
        FunctionInfo{23, nullptr, "GetAesKeyslotAvailableEvent"},
        FunctionInfo{24, &SPL::SetBootReason, "SetBootReason"},
        FunctionInfo{25, &SPL::GetBootReason, "GetBootReason"},
        FunctionInfo{31, nullptr, "GetPackage2Hash"}
    );
};

class SPL_SSL final : public Module::Interface {
public:
    explicit SPL_SSL(Core::System& system_, std::shared_ptr<Module> module_);
    ~SPL_SSL() override;

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &SPL::GetConfig, "GetConfig"},
        FunctionInfo{1, &SPL::ModularExponentiate, "ModularExponentiate"},
        FunctionInfo{2, nullptr, "GenerateAesKek"},
        FunctionInfo{3, nullptr, "LoadAesKey"},
        FunctionInfo{4, nullptr, "GenerateAesKey"},
        FunctionInfo{5, &SPL::SetConfig, "SetConfig"},
        FunctionInfo{7, &SPL::GenerateRandomBytes, "GenerateRandomBytes"},
        FunctionInfo{11, &SPL::IsDevelopment, "IsDevelopment"},
        FunctionInfo{13, nullptr, "DecryptDeviceUniqueData"},
        FunctionInfo{14, nullptr, "DecryptAesKey"},
        FunctionInfo{15, nullptr, "CryptAesCtr"},
        FunctionInfo{16, nullptr, "ComputeCmac"},
        FunctionInfo{21, nullptr, "AllocateAesKeyslot"},
        FunctionInfo{22, nullptr, "DeallocateAesKeySlot"},
        FunctionInfo{23, nullptr, "GetAesKeyslotAvailableEvent"},
        FunctionInfo{24, &SPL::SetBootReason, "SetBootReason"},
        FunctionInfo{25, &SPL::GetBootReason, "GetBootReason"},
        FunctionInfo{26, nullptr, "DecryptAndStoreSslClientCertKey"},
        FunctionInfo{27, nullptr, "ModularExponentiateWithSslClientCertKey"}
    );
};

class SPL_ES final : public Module::Interface {
public:
    explicit SPL_ES(Core::System& system_, std::shared_ptr<Module> module_);
    ~SPL_ES() override;

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &SPL::GetConfig, "GetConfig"},
        FunctionInfo{1, &SPL::ModularExponentiate, "ModularExponentiate"},
        FunctionInfo{2, nullptr, "GenerateAesKek"},
        FunctionInfo{3, nullptr, "LoadAesKey"},
        FunctionInfo{4, nullptr, "GenerateAesKey"},
        FunctionInfo{5, &SPL::SetConfig, "SetConfig"},
        FunctionInfo{7, &SPL::GenerateRandomBytes, "GenerateRandomBytes"},
        FunctionInfo{11, &SPL::IsDevelopment, "IsDevelopment"},
        FunctionInfo{13, nullptr, "DecryptDeviceUniqueData"},
        FunctionInfo{14, nullptr, "DecryptAesKey"},
        FunctionInfo{15, nullptr, "CryptAesCtr"},
        FunctionInfo{16, nullptr, "ComputeCmac"},
        FunctionInfo{17, nullptr, "ImportEsKey"},
        FunctionInfo{18, nullptr, "UnwrapTitleKey"},
        FunctionInfo{20, nullptr, "PrepareEsCommonKey"},
        FunctionInfo{21, nullptr, "AllocateAesKeyslot"},
        FunctionInfo{22, nullptr, "DeallocateAesKeySlot"},
        FunctionInfo{23, nullptr, "GetAesKeyslotAvailableEvent"},
        FunctionInfo{24, &SPL::SetBootReason, "SetBootReason"},
        FunctionInfo{25, &SPL::GetBootReason, "GetBootReason"},
        FunctionInfo{28, nullptr, "DecryptAndStoreDrmDeviceCertKey"},
        FunctionInfo{29, nullptr, "ModularExponentiateWithDrmDeviceCertKey"},
        FunctionInfo{31, nullptr, "PrepareEsArchiveKey"},
        FunctionInfo{32, nullptr, "LoadPreparedAesKey"}
    );
};

class SPL_MANU final : public Module::Interface {
public:
    explicit SPL_MANU(Core::System& system_, std::shared_ptr<Module> module_);
    ~SPL_MANU() override;

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &SPL::GetConfig, "GetConfig"},
        FunctionInfo{1, &SPL::ModularExponentiate, "ModularExponentiate"},
        FunctionInfo{2, nullptr, "GenerateAesKek"},
        FunctionInfo{3, nullptr, "LoadAesKey"},
        FunctionInfo{4, nullptr, "GenerateAesKey"},
        FunctionInfo{5, &SPL::SetConfig, "SetConfig"},
        FunctionInfo{7, &SPL::GenerateRandomBytes, "GenerateRandomBytes"},
        FunctionInfo{11, &SPL::IsDevelopment, "IsDevelopment"},
        FunctionInfo{13, nullptr, "DecryptDeviceUniqueData"},
        FunctionInfo{14, nullptr, "DecryptAesKey"},
        FunctionInfo{15, nullptr, "CryptAesCtr"},
        FunctionInfo{16, nullptr, "ComputeCmac"},
        FunctionInfo{21, nullptr, "AllocateAesKeyslot"},
        FunctionInfo{22, nullptr, "DeallocateAesKeySlot"},
        FunctionInfo{23, nullptr, "GetAesKeyslotAvailableEvent"},
        FunctionInfo{24, &SPL::SetBootReason, "SetBootReason"},
        FunctionInfo{25, &SPL::GetBootReason, "GetBootReason"},
        FunctionInfo{30, nullptr, "ReencryptDeviceUniqueData"}
    );
};

} // namespace Service::SPL

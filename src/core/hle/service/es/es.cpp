// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/crypto/key_manager.h"
#include "core/hle/service/es/es.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "frontend_common/firmware_manager.h"

namespace Service::ES {

constexpr Result ERROR_INVALID_ARGUMENT{ErrorModule::ETicket, 2};
constexpr Result ERROR_INVALID_RIGHTS_ID{ErrorModule::ETicket, 3};

class ETicket final : public ServiceFramework<ETicket> {
public:
    explicit ETicket(Core::System& system_) : ServiceFramework{system_, "es"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            FunctionInfo{1, &ETicket::ImportTicket, "ImportTicket"},
            FunctionInfo{2, nullptr, "ImportTicketCertificateSet"},
            FunctionInfo{3, nullptr, "DeleteTicket"},
            FunctionInfo{4, nullptr, "DeletePersonalizedTicket"},
            FunctionInfo{5, nullptr, "DeleteAllCommonTicket"},
            FunctionInfo{6, nullptr, "DeleteAllPersonalizedTicket"},
            FunctionInfo{7, nullptr, "DeleteAllPersonalizedTicketEx"},
            FunctionInfo{8, &ETicket::GetTitleKey, "GetTitleKey"},
            FunctionInfo{9, &ETicket::CountCommonTicket, "CountCommonTicket"},
            FunctionInfo{10, &ETicket::CountPersonalizedTicket, "CountPersonalizedTicket"},
            FunctionInfo{11, &ETicket::ListCommonTicketRightsIds, "ListCommonTicketRightsIds"},
            FunctionInfo{12, &ETicket::ListPersonalizedTicketRightsIds, "ListPersonalizedTicketRightsIds"},
            FunctionInfo{13, nullptr, "ListMissingPersonalizedTicket"},
            FunctionInfo{14, &ETicket::GetCommonTicketSize, "GetCommonTicketSize"},
            FunctionInfo{15, &ETicket::GetPersonalizedTicketSize, "GetPersonalizedTicketSize"},
            FunctionInfo{16, &ETicket::GetCommonTicketData, "GetCommonTicketData"},
            FunctionInfo{17, &ETicket::GetPersonalizedTicketData, "GetPersonalizedTicketData"},
            FunctionInfo{18, nullptr, "OwnTicket"},
            FunctionInfo{19, nullptr, "GetTicketInfo"},
            FunctionInfo{20, nullptr, "ListLightTicketInfo"},
            FunctionInfo{21, nullptr, "SignData"},
            FunctionInfo{22, nullptr, "GetCommonTicketAndCertificateSize"},
            FunctionInfo{23, nullptr, "GetCommonTicketAndCertificateData"},
            FunctionInfo{24, nullptr, "ImportPrepurchaseRecord"},
            FunctionInfo{25, nullptr, "DeletePrepurchaseRecord"},
            FunctionInfo{26, nullptr, "DeleteAllPrepurchaseRecord"},
            FunctionInfo{27, nullptr, "CountPrepurchaseRecord"},
            FunctionInfo{28, nullptr, "ListPrepurchaseRecordRightsIds"},
            FunctionInfo{29, nullptr, "ListPrepurchaseRecordInfo"},
            FunctionInfo{30, nullptr, "CountTicket"},
            FunctionInfo{31, nullptr, "ListTicketRightsIds"},
            FunctionInfo{32, nullptr, "CountPrepurchaseRecordEx"},
            FunctionInfo{33, nullptr, "ListPrepurchaseRecordRightsIdsEx"},
            FunctionInfo{34, nullptr, "GetEncryptedTicketSize"},
            FunctionInfo{35, nullptr, "GetEncryptedTicketData"},
            FunctionInfo{36, nullptr, "DeleteAllInactiveELicenseRequiredPersonalizedTicket"},
            FunctionInfo{37, nullptr, "OwnTicket2"},
            FunctionInfo{38, nullptr, "OwnTicket3"},
            FunctionInfo{39, nullptr, "DeleteAllInactivePersonalizedTicket"},
            FunctionInfo{40, nullptr, "DeletePrepurchaseRecordByNintendoAccountId"},
            FunctionInfo{101, nullptr, "Unknown101"}, //18.0.0+
            FunctionInfo{102, nullptr, "Unknown102"}, //18.0.0+
            FunctionInfo{103, nullptr, "Unknown103"}, //18.0.0+
            FunctionInfo{104, nullptr, "Unknown104"}, //18.0.0+
            FunctionInfo{105, nullptr, "Unknown105"}, //20.0.0+
            FunctionInfo{201, nullptr, "Unknown201"}, //18.0.0+
            FunctionInfo{202, nullptr, "Unknown202"}, //18.0.0+
            FunctionInfo{203, nullptr, "Unknown203"}, //18.0.0+
            FunctionInfo{204, nullptr, "Unknown204"}, //18.0.0+
            FunctionInfo{205, nullptr, "Unknown205"}, //18.0.0+
            FunctionInfo{501, nullptr, "Unknown501"},
            FunctionInfo{502, nullptr, "Unknown502"},
            FunctionInfo{503, nullptr, "GetTitleKey"},
            FunctionInfo{504, nullptr, "Unknown504"},
            FunctionInfo{508, nullptr, "Unknown508"},
            FunctionInfo{509, nullptr, "Unknown509"},
            FunctionInfo{510, nullptr, "Unknown510"},
            FunctionInfo{511, nullptr, "Unknown511"},
            FunctionInfo{1001, nullptr, "Unknown1001"},
            FunctionInfo{1002, nullptr, "Unknown1001"},
            FunctionInfo{1003, nullptr, "Unknown1003"},
            FunctionInfo{1004, nullptr, "Unknown1004"},
            FunctionInfo{1005, nullptr, "Unknown1005"},
            FunctionInfo{1006, nullptr, "Unknown1006"},
            FunctionInfo{1007, nullptr, "Unknown1007"},
            FunctionInfo{1009, nullptr, "Unknown1009"},
            FunctionInfo{1010, nullptr, "Unknown1010"},
            FunctionInfo{1011, nullptr, "Unknown1011"},
            FunctionInfo{1012, nullptr, "Unknown1012"},
            FunctionInfo{1013, nullptr, "Unknown1013"},
            FunctionInfo{1014, nullptr, "Unknown1014"},
            FunctionInfo{1015, nullptr, "Unknown1015"},
            FunctionInfo{1016, nullptr, "Unknown1016"},
            FunctionInfo{1017, nullptr, "Unknown1017"},
            FunctionInfo{1018, nullptr, "Unknown1018"},
            FunctionInfo{1019, nullptr, "Unknown1019"},
            FunctionInfo{1020, nullptr, "Unknown1020"},
            FunctionInfo{1021, nullptr, "Unknown1021"},
            FunctionInfo{1501, nullptr, "Unknown1501"},
            FunctionInfo{1502, nullptr, "Unknown1502"},
            FunctionInfo{1503, nullptr, "Unknown1503"},
            FunctionInfo{1504, nullptr, "Unknown1504"},
            FunctionInfo{1505, nullptr, "Unknown1505"},
            FunctionInfo{1506, nullptr, "Unknown1506"},
            FunctionInfo{2000, nullptr, "Unknown2000"},
            FunctionInfo{2001, nullptr, "Unknown2001"},
            FunctionInfo{2002, nullptr, "Unknown2002"},
            FunctionInfo{2003, nullptr, "Unknown2003"},
            FunctionInfo{2100, nullptr, "Unknown2100"},
            FunctionInfo{2501, nullptr, "Unknown2501"},
            FunctionInfo{2502, nullptr, "Unknown2502"},
            FunctionInfo{2601, nullptr, "Unknown2601"},
            FunctionInfo{3001, nullptr, "Unknown3001"},
            FunctionInfo{3002, nullptr, "Unknown3002"}
        };
        // clang-format on
        RegisterHandlers(functions);

        keys.PopulateTickets();
        keys.SynthesizeTickets();
    }

private:
    bool CheckRightsId(HLERequestContext& ctx, const u128& rights_id) {
        if (rights_id == u128{}) {
            LOG_ERROR(Service_ETicket, "The rights ID was invalid!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_RIGHTS_ID);
            return false;
        }

        return true;
    }

    void ImportTicket(HLERequestContext& ctx) {
        const auto raw_ticket = ctx.ReadBuffer();
        [[maybe_unused]] const auto cert = ctx.ReadBuffer(1);

        if (raw_ticket.size() < sizeof(Core::Crypto::Ticket)) {
            LOG_ERROR(Service_ETicket, "The input buffer is not large enough!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_ARGUMENT);
            return;
        }

        Core::Crypto::Ticket ticket = Core::Crypto::Ticket::Read(raw_ticket);
        if (!keys.AddTicket(ticket)) {
            LOG_ERROR(Service_ETicket, "The ticket could not be imported!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_ARGUMENT);
            return;
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetTitleKey(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto rights_id = rp.PopRaw<u128>();

        LOG_DEBUG(Service_ETicket, "called, rights_id={:016X}{:016X}", rights_id[1], rights_id[0]);

        if (!CheckRightsId(ctx, rights_id))
            return;

        const auto key =
            keys.GetKey(Core::Crypto::S128KeyType::Titlekey, rights_id[1], rights_id[0]);

        if (key == Core::Crypto::Key128{}) {
            LOG_ERROR(Service_ETicket,
                      "The titlekey doesn't exist in the KeyManager or the rights ID was invalid!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_RIGHTS_ID);
            return;
        }

        ctx.WriteBuffer(key);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void CountCommonTicket(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ETicket, "called");

        const u32 count = static_cast<u32>(keys.GetCommonTickets().size());

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(count);
    }

    void CountPersonalizedTicket(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ETicket, "called");

        const u32 count = static_cast<u32>(keys.GetPersonalizedTickets().size());

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(count);
    }

    void ListCommonTicketRightsIds(HLERequestContext& ctx) {
        size_t out_entries = 0;
        if (!keys.GetCommonTickets().empty()) {
            out_entries = ctx.GetWriteBufferNumElements<u128>();
        }
        LOG_DEBUG(Service_ETicket, "called, entries={:016X}", out_entries);

        keys.PopulateTickets();
        const auto tickets = keys.GetCommonTickets();
        std::vector<u128> ids;
        std::transform(tickets.begin(), tickets.end(), std::back_inserter(ids),
                       [](const auto& pair) { return pair.first; });

        out_entries = (std::min)(ids.size(), out_entries);
        ctx.WriteBuffer(ids.data(), out_entries * sizeof(u128));

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(static_cast<u32>(out_entries));
    }

    void ListPersonalizedTicketRightsIds(HLERequestContext& ctx) {
        size_t out_entries = 0;
        if (!keys.GetPersonalizedTickets().empty()) {
            out_entries = ctx.GetWriteBufferNumElements<u128>();
        }

        LOG_DEBUG(Service_ETicket, "called, entries={:016X}", out_entries);

        keys.PopulateTickets();
        const auto tickets = keys.GetPersonalizedTickets();
        std::vector<u128> ids;
        std::transform(tickets.begin(), tickets.end(), std::back_inserter(ids),
                       [](const auto& pair) { return pair.first; });

        out_entries = (std::min)(ids.size(), out_entries);
        ctx.WriteBuffer(ids.data(), out_entries * sizeof(u128));

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(static_cast<u32>(out_entries));
    }

    void GetCommonTicketSize(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto rights_id = rp.PopRaw<u128>();

        LOG_DEBUG(Service_ETicket, "called, rights_id={:016X}{:016X}", rights_id[1], rights_id[0]);

        if (!CheckRightsId(ctx, rights_id))
            return;

        const auto ticket = keys.GetCommonTickets().at(rights_id);

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push<u64>(ticket.GetSize());
    }

    void GetPersonalizedTicketSize(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto rights_id = rp.PopRaw<u128>();

        LOG_DEBUG(Service_ETicket, "called, rights_id={:016X}{:016X}", rights_id[1], rights_id[0]);

        if (!CheckRightsId(ctx, rights_id))
            return;

        const auto ticket = keys.GetPersonalizedTickets().at(rights_id);

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push<u64>(ticket.GetSize());
    }

    void GetCommonTicketData(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto rights_id = rp.PopRaw<u128>();

        LOG_DEBUG(Service_ETicket, "called, rights_id={:016X}{:016X}", rights_id[1], rights_id[0]);

        if (!CheckRightsId(ctx, rights_id))
            return;

        const auto ticket = keys.GetCommonTickets().at(rights_id);

        const auto write_size = std::min<u64>(ticket.GetSize(), ctx.GetWriteBufferSize());
        ctx.WriteBuffer(&ticket, write_size);

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push<u64>(write_size);
    }

    void GetPersonalizedTicketData(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto rights_id = rp.PopRaw<u128>();

        LOG_DEBUG(Service_ETicket, "called, rights_id={:016X}{:016X}", rights_id[1], rights_id[0]);

        if (!CheckRightsId(ctx, rights_id))
            return;

        const auto ticket = keys.GetPersonalizedTickets().at(rights_id);

        const auto write_size = std::min<u64>(ticket.GetSize(), ctx.GetWriteBufferSize());
        ctx.WriteBuffer(&ticket, write_size);

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push<u64>(write_size);
    }

    Core::Crypto::KeyManager& keys = Core::Crypto::KeyManager::Instance();
};

class NDRM_LU final : public ServiceFramework<NDRM_LU> {
public:
    explicit NDRM_LU(Core::System& system_)
        : ServiceFramework{system_, "ndrm:lu"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            FunctionInfo{1, nullptr, "Cmd1"},
            FunctionInfo{2, nullptr, "Cmd2"},
            FunctionInfo{3, nullptr, "Cmd3"},
            FunctionInfo{1000, nullptr, "Cmd1000"},
            FunctionInfo{8000, nullptr, "Cmd8000"}
        };
        // clang-format on
        RegisterHandlers(functions);
    }
};

class NDRM_LA final : public ServiceFramework<NDRM_LA> {
public:
    explicit NDRM_LA(Core::System& system_)
        : ServiceFramework{system_, "ndrm:la"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            FunctionInfo{1, nullptr, "Cmd1"},
            FunctionInfo{2, nullptr, "Cmd2"},
            FunctionInfo{3, nullptr, "Cmd3"},
            FunctionInfo{4, nullptr, "Cmd4"},
            FunctionInfo{5, nullptr, "Cmd5"},
            FunctionInfo{6, nullptr, "Cmd6"},
            FunctionInfo{7, nullptr, "Cmd7"},
            FunctionInfo{8, nullptr, "Cmd8"},
            FunctionInfo{9, nullptr, "Cmd9"},
            FunctionInfo{10, nullptr, "Cmd10"},
            FunctionInfo{11, nullptr, "Cmd11"},
            FunctionInfo{12, nullptr, "Cmd12"},
            FunctionInfo{13, nullptr, "Cmd13"},
            FunctionInfo{14, nullptr, "Cmd14"},
            FunctionInfo{15, nullptr, "Cmd15"},
            FunctionInfo{16, nullptr, "Cmd16"},
            FunctionInfo{17, nullptr, "Cmd17"},
            FunctionInfo{18, nullptr, "Cmd18"},
            FunctionInfo{19, nullptr, "Cmd19"},
            FunctionInfo{20, nullptr, "Cmd20"},
            FunctionInfo{21, nullptr, "Cmd21"},
            FunctionInfo{22, nullptr, "Cmd22"},
            FunctionInfo{23, nullptr, "Cmd23"},
            FunctionInfo{24, nullptr, "Cmd24"},
            FunctionInfo{25, nullptr, "Cmd25"},
            FunctionInfo{26, nullptr, "Cmd26"},
            FunctionInfo{27, nullptr, "Cmd27"},
            FunctionInfo{28, nullptr, "Cmd28"},
            FunctionInfo{29, nullptr, "Cmd29"},
            FunctionInfo{30, nullptr, "Cmd30"},
            FunctionInfo{31, nullptr, "Cmd31"},
            FunctionInfo{32, nullptr, "Cmd32"},
            FunctionInfo{33, nullptr, "Cmd33"},
            FunctionInfo{34, nullptr, "Cmd34"},
            FunctionInfo{35, nullptr, "Cmd35"},
            FunctionInfo{36, nullptr, "Cmd36"},
            FunctionInfo{37, nullptr, "Cmd37"},
            FunctionInfo{38, nullptr, "Cmd38"},
            FunctionInfo{39, nullptr, "Cmd39"},
            FunctionInfo{40, nullptr, "Cmd40"},
            FunctionInfo{42, nullptr, "Cmd42"},
            FunctionInfo{43, nullptr, "Cmd43"},
            FunctionInfo{44, nullptr, "Cmd44"},
            FunctionInfo{45, nullptr, "Cmd45"},
            FunctionInfo{46, nullptr, "Cmd46"},
            FunctionInfo{47, nullptr, "Cmd47"},
            FunctionInfo{48, nullptr, "Cmd48"},
            FunctionInfo{49, nullptr, "Cmd49"},
            FunctionInfo{50, nullptr, "Cmd50"},
            FunctionInfo{51, nullptr, "Cmd51"},
            FunctionInfo{8000, nullptr, "Cmd8000"},
            FunctionInfo{8001, nullptr, "Cmd8001"},
            FunctionInfo{8002, nullptr, "Cmd8002"},
            FunctionInfo{8003, nullptr, "Cmd8003"}
        };
        // clang-format on
        RegisterHandlers(functions);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("es", std::make_shared<ETicket>(system));
    // +13.0.0
    if (FirmwareManager::GetFirmwareVersion(system).first.major >= 13) {
        server_manager->RegisterNamedService("ndrm:lu", std::make_shared<NDRM_LU>(system));
        server_manager->RegisterNamedService("ndrm:la", std::make_shared<NDRM_LA>(system));
    }
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::ES

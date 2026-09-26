// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/string_util.h"

#include "core/core.h"
#include "core/hle/result.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/sm.h"
#include "core/hle/service/sockets/bsd.h"
#include "core/hle/service/ssl/cert_store.h"
#include "core/hle/service/ssl/ssl.h"
#include "core/hle/service/ssl/ssl_backend.h"
#include "core/internal_network/network.h"
#include "core/internal_network/sockets.h"

namespace Service::SSL {

// This is nn::ssl::sf::CertificateFormat
enum class CertificateFormat : u32 {
    Pem = 1,
    Der = 2,
};

// This is nn::ssl::sf::ContextOption
enum class ContextOption : u32 {
    None = 0,
    CrlImportDateCheckEnable = 1,
};

// This is nn::ssl::Connection::IoMode
enum class IoMode : u32 {
    Blocking = 1,
    NonBlocking = 2,
};

// This is nn::ssl::sf::OptionType
enum class OptionType : u32 {
    DoNotCloseSocket = 0,
    GetServerCertChain = 1,
    SkipDefaultVerify = 2,
    EnableAlpn = 3,
};

// This is nn::ssl::sf::RenegotiationMode
enum RenegotiationMode : u32 {
    None = 0, ///< None
    Secure = 1, ///< Secure
};

// This is nn::ssl::sf::SslVersion
struct SslVersion {
    union {
        u32 raw{};

        BitField<0, 1, u32> tls_auto;
        BitField<3, 1, u32> tls_v10;
        BitField<4, 1, u32> tls_v11;
        BitField<5, 1, u32> tls_v12;
        BitField<6, 1, u32> tls_v13;
        BitField<24, 7, u32> api_version;
    };
};

struct SslContextSharedData {
    u32 connection_count = 0;
};

class ISslConnection final : public ServiceFramework<ISslConnection> {
public:
    explicit ISslConnection(Core::System& system_in, SslVersion ssl_version_in, std::shared_ptr<SslContextSharedData>& shared_data_in, std::unique_ptr<SSLConnectionBackend>&& backend_in)
        : ServiceFramework{system_in, "ISslConnection"}
        , ssl_version{ssl_version_in}
        , shared_data{shared_data_in}
        , backend{std::move(backend_in)} {
        backend->SetVerifyOption(verify_option);
        shared_data->connection_count++;
    }

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

    ~ISslConnection() {
        shared_data->connection_count--;
        if (fd_to_close.has_value()) {
            const s32 fd = *fd_to_close;
            if (!do_not_close_socket) {
                LOG_ERROR(Service_SSL,
                          "do_not_close_socket was changed after setting socket; is this right?");
            } else {
                auto bsd = system.ServiceManager().GetService<Service::Sockets::BSD_USA>("bsd:u");
                if (bsd) {
                    auto err = bsd->CloseImpl(fd);
                    if (err != Service::Sockets::Errno::SUCCESS) {
                        LOG_ERROR(Service_SSL, "Failed to close duplicated socket: {}", err);
                    }
                }
            }
        }
    }

private:
    Result DoHandshakeImpl() {
        ASSERT_OR_EXECUTE(!did_handshake && socket, { return ResultNoSocket; });
        Result res = backend->DoHandshake();
        did_handshake = res.IsSuccess();
        return res;
    }

    std::vector<u8> SerializeServerCerts(const std::vector<std::vector<u8>>& certs) {
        struct Header {
            u64 magic;
            u32 count;
            u32 pad;
        };
        struct EntryHeader {
            u32 size;
            u32 offset;
        };
        if (!get_server_cert_chain) {
            // Just return the first one, unencoded.
            ASSERT_OR_EXECUTE_MSG(!certs.empty(), { return {}; }, "Should be at least one server cert");
            return certs[0];
        }
        std::vector<u8> ret;
        Header header{0x4E4D684374726543, u32(certs.size()), 0};
        ret.insert(ret.end(), reinterpret_cast<u8*>(&header), reinterpret_cast<u8*>(&header + 1));
        size_t data_offset = sizeof(Header) + certs.size() * sizeof(EntryHeader);
        for (auto& cert : certs) {
            EntryHeader entry_header{u32(cert.size()), u32(data_offset)};
            data_offset += cert.size();
            ret.insert(ret.end(), reinterpret_cast<u8*>(&entry_header), reinterpret_cast<u8*>(&entry_header + 1));
        }
        for (auto& cert : certs) {
            ret.insert(ret.end(), cert.begin(), cert.end());
        }
        return ret;
    }

    Result SetSocketDescriptor(s32 in_fd, Out<s32> out_fd) {
        LOG_DEBUG(Service_SSL, "called, fd={}", in_fd);
        ASSERT(!did_handshake);
        auto bsd = system.ServiceManager().GetService<Service::Sockets::BSD_USA>("bsd:u");
        ASSERT_OR_EXECUTE(bsd, { return ResultInternalError; });

        auto const res_v = bsd->DuplicateSocketImpl(in_fd);
        if (auto *res = std::get_if<s32>(&res_v)) {
            const s32 dup_fd = *res;
            *out_fd = do_not_close_socket ? dup_fd : -1;
            if (!do_not_close_socket)
                fd_to_close = dup_fd;
            auto const sock = bsd->GetSocket(dup_fd);
            if (!sock.has_value()) {
                LOG_ERROR(Service_SSL, "invalid socket fd {} after duplication", dup_fd);
                return ResultInvalidSocket;
            }
            socket = std::move(*sock);
            backend->SetSocket(std::move(socket));
            return ResultSuccess;
        }
        LOG_ERROR(Service_SSL, "Failed to duplicate socket with fd {}", in_fd);
        return ResultInvalidSocket;
    }

    Result SetHostName(InBuffer<BufferAttr_HipcMapAlias> buf) {
        auto const hostname = Common::StringFromBuffer(buf);
        LOG_DEBUG(Service_SSL, "called. hostname={}", hostname);
        ASSERT(!did_handshake);
        return backend->SetHostName(hostname.c_str());
    }

    Result SetVerifyOption(u32 option) {
        LOG_DEBUG(Service_SSL, "called. option={} (forcing 0)", option);
        ASSERT(!did_handshake);
        verify_option = 0;
        backend->SetVerifyOption(0);
        R_SUCCEED();
    }

    Result SetIoMode(u32 input_mode) {
        auto mode = IoMode(input_mode);
        ASSERT(mode == IoMode::Blocking || mode == IoMode::NonBlocking);
        R_UNLESS(socket, ResultNoSocket);
        const bool non_block = mode == IoMode::NonBlocking;
        const Network::Errno error = socket->SetNonBlock(non_block);
        if (error != Network::Errno::SUCCESS) {
            LOG_ERROR(Service_SSL, "Failed to set native socket non-block flag to {}", non_block);
        }
        R_SUCCEED();
    }

    Result GetSocketDescriptor(Out<u32> out_fd) {
        LOG_WARNING(Service_SSL, "(STUBBED)");
        *out_fd = uint32_t(socket->GetFD());
        R_SUCCEED();
    }

    Result GetHostName(OutBuffer<BufferAttr_HipcMapAlias> data, Out<u32> out_size) {
        LOG_WARNING(Service_SSL, "(STUBBED)");
        ASSERT(!did_handshake);
        return backend->GetHostName(std::span<u8>{data.begin(), data.end()}, out_size);
    }

    Result GetIoMode(Out<u32> out_mode) {
        LOG_WARNING(Service_SSL, "(STUBBED)");
        R_SUCCEED();
    }

    Result DoHandshake() {
        return DoHandshakeImpl();
    }

    void DoHandshakeGetServerCert(HLERequestContext& ctx) {
        struct OutputParameters {
            u32 certs_size;
            u32 certs_count;
        };
        static_assert(sizeof(OutputParameters) == 0x8);

        Result res = DoHandshakeImpl();
        OutputParameters out{};
        if (res == ResultSuccess) {
            std::vector<std::vector<u8>> certs;
            res = backend->GetServerCerts(&certs);
            if (res == ResultSuccess) {
                const std::vector<u8> certs_buf = SerializeServerCerts(certs);
                if (ctx.CanWriteBuffer()) {
                    const size_t buffer_size = ctx.GetWriteBufferSize();
                    if (certs_buf.size() <= buffer_size) {
                        ctx.WriteBuffer(certs_buf);
                    } else {
                        LOG_WARNING(Service_SSL, "Certificate buffer too small: {} bytes needed, {} bytes available",
                                    certs_buf.size(), buffer_size);
                        ctx.WriteBuffer(std::span<const u8>(certs_buf.data(), buffer_size));
                    }
                } else {
                    LOG_DEBUG(Service_SSL, "No output buffer provided for certificates ({} bytes)", certs_buf.size());
                }

                out.certs_count = static_cast<u32>(certs.size());
                out.certs_size = static_cast<u32>(certs_buf.size());
            }
        }
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(res);
        rb.PushRaw(out);
    }

    Result Read(OutBuffer<BufferAttr_HipcMapAlias> data, Out<u32> out_size) {
        R_UNLESS(did_handshake, ResultInternalError);
        size_t tmp{};
        auto const res = backend->Read(&tmp, data);
        *out_size = u32(tmp);
        return res;
    }

    Result Write(InBuffer<BufferAttr_HipcMapAlias> data, Out<u32> out_size) {
        R_UNLESS(did_handshake, ResultInternalError);
        size_t tmp{};
        auto const res = backend->Write(&tmp, data);
        *out_size = u32(tmp);
        return res;
    }

    Result Pending(Out<s32> out_pending_size) {
        LOG_WARNING(Service_SSL, "(STUBBED)");
        *out_pending_size = s32(backend->Pending());
        R_SUCCEED();
    }

    Result Peek(OutBuffer<BufferAttr_HipcMapAlias> data, Out<u32> out_size) {
        LOG_WARNING(Service_SSL, "(STUBBED)");
        size_t tmp{};
        auto const res = backend->Peek(&tmp, data);
        *out_size = u32(tmp);
        return res;
    }

    Result Poll(u32 in_pollevent, u32 timer, Out<u32> out_pollevent) {
        LOG_WARNING(Service_SSL, "(STUBBED)");
        R_SUCCEED();
    }

    Result GetVerifyCertError() {
        LOG_WARNING(Service_SSL, "(STUBBED)");
        R_SUCCEED();
    }

    Result GetNeededServerCertBufferSize(Out<u32> out_needed_buffer_size) {
        LOG_WARNING(Service_SSL, "(STUBBED)");
        R_SUCCEED();
    }

    Result SetSessionCacheMode(u32 mode) {
        LOG_WARNING(Service_SSL, "(STUBBED) called. value={}", mode);
        R_UNLESS(!did_handshake, ResultInternalError);
        R_SUCCEED();
    }

    Result GetSessionCacheMode(Out<u32> mode) {
        LOG_WARNING(Service_SSL, "(STUBBED)");
        R_UNLESS(!did_handshake, ResultInternalError);
        R_SUCCEED();
    }

    Result FlushSessionCache() {
        LOG_WARNING(Service_SSL, "(STUBBED)");
        R_UNLESS(!did_handshake, ResultInternalError);
        R_SUCCEED();
    }

    Result SetRenegotiationMode(RenegotiationMode mode) {
        LOG_WARNING(Service_SSL, "(STUBBED)");
        backend->SetRenegotiationMode(u32(mode));
        R_SUCCEED();
    }

    Result GetRenegotiationMode(Out<RenegotiationMode> mode) {
        LOG_WARNING(Service_SSL, "(STUBBED)");
        u32 tmp{};
        auto const res = backend->GetRenegotiationMode(&tmp);
        *mode = RenegotiationMode(tmp);
        return res;
    }

    Result SetOption(OptionType option, s32 value) {
        switch (option) {
        case OptionType::DoNotCloseSocket:
            do_not_close_socket = bool(value);
            break;
        case OptionType::GetServerCertChain:
            get_server_cert_chain = bool(value);
            break;
        case OptionType::SkipDefaultVerify:
            skip_default_verify = bool(value);
            break;
        case OptionType::EnableAlpn:
            enable_alpn = bool(value);
            break;
        default:
            LOG_WARNING(Service_SSL, "Unknown option={}, value={}", option, value);
        }
        R_SUCCEED();
    }

    Result GetOption(OptionType option, Out<u8> value) {
        switch (option) {
        case OptionType::DoNotCloseSocket:
            *value = u8(do_not_close_socket);
            break;
        case OptionType::GetServerCertChain:
            *value = u8(get_server_cert_chain);
            break;
        case OptionType::SkipDefaultVerify:
            *value = u8(skip_default_verify);
            break;
        case OptionType::EnableAlpn:
            *value = u8(enable_alpn);
            break;
        default:
            LOG_WARNING(Service_SSL, "Unknown option={}", u32(option));
            *value = 0;
            break;
        }
        LOG_DEBUG(Service_SSL, "GetOption called, option={}, ret value={}", u32(option), *value);
        R_SUCCEED();
    }

    Result SetNextAlpnProto(InBuffer<BufferAttr_HipcMapAlias> data) {
        auto const to_write = u32((std::min)(next_alpn_proto.size(), data.size()));
        next_alpn_proto.assign(data.begin(), data.begin() + to_write);
        LOG_DEBUG(Service_SSL, "SetNextAlpnProto called, size={}", next_alpn_proto.size());
        R_SUCCEED();
    }

    Result GetNextAlpnProto(OutBuffer<BufferAttr_HipcMapAlias> data, Out<u32> to_write) {
        *to_write = u32((std::min)(next_alpn_proto.size(), data.size()));
        next_alpn_proto.assign(data.begin(), data.begin() + *to_write);
        LOG_DEBUG(Service_SSL, "GetNextAlpnProto called, size={}", *to_write);
        R_SUCCEED();
    }

    Result GetVerifyCertErrors(OutBuffer<BufferAttr_HipcMapAlias> unk0, Out<u32> unk1, Out<u32> unk2) {
        LOG_WARNING(Service_SSL, "(STUBBED)");
        R_SUCCEED();
    }

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&ISslConnection::SetSocketDescriptor>, "SetSocketDescriptor"},
        FunctionInfo{1, D<&ISslConnection::SetHostName>, "SetHostName"},
        FunctionInfo{2, D<&ISslConnection::SetVerifyOption>, "SetVerifyOption"},
        FunctionInfo{3, D<&ISslConnection::SetIoMode>, "SetIoMode"},
        FunctionInfo{4, D<&ISslConnection::GetSocketDescriptor>, "GetSocketDescriptor"},
        FunctionInfo{5, D<&ISslConnection::GetHostName>, "GetHostName"},
        FunctionInfo{6, nullptr, "GetVerifyOption"},
        FunctionInfo{7, D<&ISslConnection::GetIoMode>, "GetIoMode"},
        FunctionInfo{8, D<&ISslConnection::DoHandshake>, "DoHandshake"},
        FunctionInfo{9, &ISslConnection::DoHandshakeGetServerCert, "DoHandshakeGetServerCert"},
        FunctionInfo{10, D<&ISslConnection::Read>, "Read"},
        FunctionInfo{11, D<&ISslConnection::Write>, "Write"},
        FunctionInfo{12, D<&ISslConnection::Pending>, "Pending"},
        FunctionInfo{13, D<&ISslConnection::Peek>, "Peek"},
        FunctionInfo{14, D<&ISslConnection::Poll>, "Poll"},
        FunctionInfo{15, D<&ISslConnection::GetVerifyCertError>, "GetVerifyCertError"},
        FunctionInfo{16, D<&ISslConnection::GetNeededServerCertBufferSize>, "GetNeededServerCertBufferSize"},
        FunctionInfo{17, D<&ISslConnection::SetSessionCacheMode>, "SetSessionCacheMode"},
        FunctionInfo{18, D<&ISslConnection::GetSessionCacheMode>, "GetSessionCacheMode"},
        FunctionInfo{19, D<&ISslConnection::FlushSessionCache>, "FlushSessionCache"},
        FunctionInfo{20, D<&ISslConnection::SetRenegotiationMode>, "SetRenegotiationMode"},
        FunctionInfo{21, D<&ISslConnection::GetRenegotiationMode>, "GetRenegotiationMode"},
        FunctionInfo{22, D<&ISslConnection::SetOption>, "SetOption"},
        FunctionInfo{23, D<&ISslConnection::GetOption>, "GetOption"},
        FunctionInfo{24, nullptr, "GetVerifyCertErrors"},
        FunctionInfo{25, nullptr, "GetCipherInfo"},
        FunctionInfo{26, D<&ISslConnection::SetNextAlpnProto>, "SetNextAlpnProto"},
        FunctionInfo{27, D<&ISslConnection::GetNextAlpnProto>, "GetNextAlpnProto"},
        FunctionInfo{28, nullptr, "SetDtlsSocketDescriptor"},
        FunctionInfo{29, nullptr, "GetDtlsHandshakeTimeout"},
        FunctionInfo{30, nullptr, "SetPrivateOption"},
        FunctionInfo{31, nullptr, "SetSrtpCiphers"},
        FunctionInfo{32, nullptr, "GetSrtpCipher"},
        FunctionInfo{33, nullptr, "ExportKeyingMaterial"},
        FunctionInfo{34, nullptr, "SetIoTimeout"},
        FunctionInfo{35, nullptr, "GetIoTimeout"}
    );
    SslVersion ssl_version;
    std::shared_ptr<SslContextSharedData> shared_data;
    std::unique_ptr<SSLConnectionBackend> backend;
    std::optional<int> fd_to_close;
    std::shared_ptr<Network::SocketBase> socket;
    std::vector<u8> next_alpn_proto;
    u32 verify_option = 0;

    bool do_not_close_socket = false;
    bool get_server_cert_chain = false;
    bool skip_default_verify = false;
    bool enable_alpn = false;
    bool did_handshake = false;
};

class ISslContext final : public ServiceFramework<ISslContext> {
public:
    explicit ISslContext(Core::System& system_, SslVersion version)
        : ServiceFramework{system_, "ISslContext"}, ssl_version{version},
          shared_data{std::make_shared<SslContextSharedData>()} {
        static const FunctionInfo functions[] = {
            FunctionInfo{0, &ISslContext::SetOption, "SetOption"},
            FunctionInfo{1, &ISslContext::GetOption, "GetOption"},
            FunctionInfo{2, &ISslContext::CreateConnection, "CreateConnection"},
            FunctionInfo{3, &ISslContext::GetConnectionCount, "GetConnectionCount"},
            FunctionInfo{4, &ISslContext::ImportServerPki, "ImportServerPki"},
            FunctionInfo{5, &ISslContext::ImportClientPki, "ImportClientPki"},
            FunctionInfo{6, nullptr, "RemoveServerPki"},
            FunctionInfo{7, nullptr, "RemoveClientPki"},
            FunctionInfo{8, D<&ISslContext::RegisterInternalPki>, "RegisterInternalPki"},
            FunctionInfo{9, nullptr, "AddPolicyOid"},
            FunctionInfo{10, nullptr, "ImportCrl"},
            FunctionInfo{11, nullptr, "RemoveCrl"},
            FunctionInfo{12, nullptr, "ImportClientCertKeyPki"},
            FunctionInfo{13, nullptr, "GeneratePrivateKeyAndCert"}
        };
        RegisterHandlers(functions);
    }

private:
    SslVersion ssl_version;
    std::shared_ptr<SslContextSharedData> shared_data;

    void SetOption(HLERequestContext& ctx) {
        struct Parameters {
            ContextOption option;
            s32 value;
        };
        static_assert(sizeof(Parameters) == 0x8, "Parameters is an invalid size");

        IPC::RequestParser rp{ctx};
        const auto parameters = rp.PopRaw<Parameters>();

        LOG_WARNING(Service_SSL, "(STUBBED) called. option={}, value={}", parameters.option,
                    parameters.value);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);

    }

    void GetOption(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto parameters = rp.PopRaw<OptionType>();

        LOG_WARNING(Service_SSL, "(STUBBED) called. option={}", parameters);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void CreateConnection(HLERequestContext& ctx) {
        LOG_WARNING(Service_SSL, "called");

        std::unique_ptr<SSLConnectionBackend> backend;
        const Result res = CreateSSLConnectionBackend(&backend);

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(res);
        if (res == ResultSuccess) {
            rb.PushIpcInterface<ISslConnection>(ctx, system, ssl_version, shared_data, std::move(backend));
        }
    }

    void GetConnectionCount(HLERequestContext& ctx) {
        LOG_DEBUG(Service_SSL, "connection_count={}", shared_data->connection_count);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(shared_data->connection_count);
    }

    void ImportServerPki(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto certificate_format = rp.PopEnum<CertificateFormat>();
        [[maybe_unused]] const auto pkcs_12_certificates = ctx.ReadBuffer(0);

        constexpr u64 server_id = 0;

        LOG_WARNING(Service_SSL, "(STUBBED) called, certificate_format={}", certificate_format);

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push(server_id);
    }

    void ImportClientPki(HLERequestContext& ctx) {
        [[maybe_unused]] const auto pkcs_12_certificate = ctx.ReadBuffer(0);
        [[maybe_unused]] const auto ascii_password = [&ctx] {
            if (ctx.CanReadBuffer(1)) {
                return ctx.ReadBuffer(1);
            }

            return std::span<const u8>{};
        }();

        constexpr u64 client_id = 0;

        LOG_WARNING(Service_SSL, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push(client_id);
    }

    Result RegisterInternalPki() {
        LOG_WARNING(Service_SSL, "(STUBBED) called");
        R_SUCCEED();
    }
};

class ISslService final : public ServiceFramework<ISslService> {
public:
    explicit ISslService(Core::System& system_) : ServiceFramework{system_, "ssl"}, cert_store{system} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

private:
    void CreateContext(HLERequestContext& ctx) {
        struct Parameters {
            SslVersion ssl_version;
            INSERT_PADDING_BYTES(0x4);
            u64 pid_placeholder;
        };
        static_assert(sizeof(Parameters) == 0x10, "Parameters is an invalid size");

        IPC::RequestParser rp{ctx};
        const auto parameters = rp.PopRaw<Parameters>();

        LOG_WARNING(Service_SSL, "(STUBBED) called, api_version={}, pid_placeholder={}", parameters.ssl_version.api_version, parameters.pid_placeholder);

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<ISslContext>(ctx, system, parameters.ssl_version);
    }

    void SetInterfaceVersion(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        u32 ssl_version = rp.Pop<u32>();

        LOG_DEBUG(Service_SSL, "called, ssl_version={}", ssl_version);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    Result GetCertificateBufSize(
        Out<u32> out_size, InArray<CaCertificateId, BufferAttr_HipcMapAlias> certificate_ids) {
        LOG_INFO(Service_SSL, "called");
        u32 num_entries;
        R_RETURN(cert_store.GetCertificateBufSize(out_size, &num_entries, certificate_ids));
    }

    Result GetCertificates(Out<u32> out_num_entries, OutBuffer<BufferAttr_HipcMapAlias> out_buffer,
                           InArray<CaCertificateId, BufferAttr_HipcMapAlias> certificate_ids) {
        LOG_INFO(Service_SSL, "called");
        R_RETURN(cert_store.GetCertificates(out_num_entries, out_buffer, certificate_ids));
    }

private:
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &ISslService::CreateContext, "CreateContext"},
        FunctionInfo{1, nullptr, "GetContextCount"},
        FunctionInfo{2, D<&ISslService::GetCertificates>, "GetCertificates"},
        FunctionInfo{3, D<&ISslService::GetCertificateBufSize>, "GetCertificateBufSize"},
        FunctionInfo{4, nullptr, "DebugIoctl"},
        FunctionInfo{5, &ISslService::SetInterfaceVersion, "SetInterfaceVersion"},
        FunctionInfo{6, nullptr, "FlushSessionCache"},
        FunctionInfo{7, nullptr, "SetDebugOption"},
        FunctionInfo{8, nullptr, "GetDebugOption"},
        FunctionInfo{8, nullptr, "ClearTls12FallbackFlag"}
    );
    CertStore cert_store;
};

class ISslServiceForSystem final : public ServiceFramework<ISslServiceForSystem> {
    public:
        explicit ISslServiceForSystem(Core::System& system_) : ServiceFramework{system_, "ssl:s"} {
            // clang-format off
            FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
                FunctionInfo{0, D<&ISslServiceForSystem::CreateContext>, "CreateContext"},
                FunctionInfo{1, D<&ISslServiceForSystem::GetContextCount>, "GetContextCount"},
                FunctionInfo{2, D<&ISslServiceForSystem::GetCertificates>, "GetCertificates"},
                FunctionInfo{3, D<&ISslServiceForSystem::GetCertificateBufSize>, "GetCertificateBufSize"},
                FunctionInfo{4, D<&ISslServiceForSystem::DebugIoctl>, "DebugIoctl"},
                FunctionInfo{5, D<&ISslServiceForSystem::SetInterfaceVersion>, "SetInterfaceVersion"},
                FunctionInfo{6, D<&ISslServiceForSystem::FlushSessionCache>, "FlushSessionCache"},
                FunctionInfo{7, D<&ISslServiceForSystem::SetDebugOption>, "SetDebugOption"},
                FunctionInfo{8, D<&ISslServiceForSystem::GetDebugOption>, "GetDebugOption"},
                FunctionInfo{9, D<&ISslServiceForSystem::ClearTls12FallbackFlag>, "ClearTls12FallbackFlag"},
                FunctionInfo{100, D<&ISslServiceForSystem::CreateContextForSystem>, "CreateContextForSystem"},
                FunctionInfo{101, D<&ISslServiceForSystem::SetThreadCoreMask>, "SetThreadCoreMask"},
                FunctionInfo{102, D<&ISslServiceForSystem::GetThreadCoreMask>, "GetThreadCoreMask"},
                FunctionInfo{103, D<&ISslServiceForSystem::VerifySignature>, "VerifySignature"}
            );
        };

        Result CreateContext() {
            LOG_DEBUG(Service_SSL, "(STUBBED) called.");

            // TODO (jarrodnorwell)

            return ResultSuccess;
        };

        Result GetContextCount() {
            LOG_DEBUG(Service_SSL, "(STUBBED) called.");

            // TODO (jarrodnorwell)

            return ResultSuccess;
        };

        Result GetCertificates() {
            LOG_DEBUG(Service_SSL, "(STUBBED) called.");

            // TODO (jarrodnorwell)

            return ResultSuccess;
        };

        Result GetCertificateBufSize() {
            LOG_DEBUG(Service_SSL, "(STUBBED) called.");

            // TODO (jarrodnorwell)

            return ResultSuccess;
        };

        Result DebugIoctl() {
            LOG_DEBUG(Service_SSL, "(STUBBED) called.");

            // TODO (jarrodnorwell)

            return ResultSuccess;
        };

        Result SetInterfaceVersion() {
            LOG_DEBUG(Service_SSL, "(STUBBED) called.");

            // TODO (jarrodnorwell)

            return ResultSuccess;
        };

        Result FlushSessionCache() {
            LOG_DEBUG(Service_SSL, "(STUBBED) called.");

            // TODO (jarrodnorwell)

            return ResultSuccess;
        };

        Result SetDebugOption() {
            LOG_DEBUG(Service_SSL, "(STUBBED) called.");

            // TODO (jarrodnorwell)

            return ResultSuccess;
        };

        Result GetDebugOption() {
            LOG_DEBUG(Service_SSL, "(STUBBED) called.");

            // TODO (jarrodnorwell)

            return ResultSuccess;
        };

        Result ClearTls12FallbackFlag() {
            LOG_DEBUG(Service_SSL, "(STUBBED) called.");

            // TODO (jarrodnorwell)

            return ResultSuccess;
        };

        Result CreateContextForSystem() {
            LOG_DEBUG(Service_SSL, "(STUBBED) called.");

            // TODO (jarrodnorwell)

            return ResultSuccess;
        };

        Result SetThreadCoreMask() {
            LOG_DEBUG(Service_SSL, "(STUBBED) called.");

            // TODO (jarrodnorwell)

            return ResultSuccess;
        };

        Result GetThreadCoreMask() {
            LOG_DEBUG(Service_SSL, "(STUBBED) called.");

            // TODO (jarrodnorwell)

            return ResultSuccess;
        };

        Result VerifySignature() {
            LOG_DEBUG(Service_SSL, "(STUBBED) called.");

            // TODO (jarrodnorwell)

            return ResultSuccess;
        };
    };

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("ssl", std::make_shared<ISslService>(system));
    server_manager->RegisterNamedService("ssl:s", std::make_shared<ISslServiceForSystem>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::SSL

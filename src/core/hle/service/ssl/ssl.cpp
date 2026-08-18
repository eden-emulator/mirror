// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <mutex>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#ifdef YUZU_BUNDLED_OPENSSL
#include <openssl/cert.h>
#endif

#include "common/fs/file.h"
#include "common/hex_util.h"
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

namespace {

std::once_flag one_time_init_flag;
bool one_time_init_success = false;
SSL_CTX* ssl_ctx = nullptr;
BIO_METHOD* bio_meth = nullptr;
Common::FS::IOFile key_log_file; // only open if SSLKEYLOGFILE set in environment

Result CheckOpenSSLErrors();
void OneTimeInit();
void OneTimeInitLogFile();
bool OneTimeInitBIO();

#ifdef YUZU_BUNDLED_OPENSSL
// This is ported from httplib
struct scope_exit {
  explicit scope_exit(std::function<void(void)> &&f)
      : exit_function(std::move(f)), execute_on_destruction{true} {}

  scope_exit(scope_exit &&rhs) noexcept
      : exit_function(std::move(rhs.exit_function)),
        execute_on_destruction{rhs.execute_on_destruction} {
    rhs.release();
  }

  ~scope_exit() {
    if (execute_on_destruction) { this->exit_function(); }
  }

  void release() { this->execute_on_destruction = false; }

private:
  scope_exit(const scope_exit &) = delete;
  void operator=(const scope_exit &) = delete;
  scope_exit &operator=(scope_exit &&) = delete;

  std::function<void(void)> exit_function;
  bool execute_on_destruction;
};

inline X509_STORE *CreateCaCertStore(const char *ca_cert,
                                                    std::size_t size) {
    auto mem = BIO_new_mem_buf(ca_cert, static_cast<int>(size));
    auto se = scope_exit([&] { BIO_free_all(mem); });
    if (!mem) { return nullptr; }

    auto inf = PEM_X509_INFO_read_bio(mem, nullptr, nullptr, nullptr);
    if (!inf) { return nullptr; }

    auto cts = X509_STORE_new();
    if (cts) {
        for (auto i = 0; i < static_cast<int>(sk_X509_INFO_num(inf)); i++) {
            auto itmp = sk_X509_INFO_value(inf, i);
            if (!itmp) { continue; }

            if (itmp->x509) { X509_STORE_add_cert(cts, itmp->x509); }
            if (itmp->crl) { X509_STORE_add_crl(cts, itmp->crl); }
        }
    }

    sk_X509_INFO_pop_free(inf, X509_INFO_free);
    return cts;
}

inline void SetCaCertStore(SSL_CTX *ctx, X509_STORE *ca_cert_store) {
    if (ca_cert_store) {
        if (ctx) {
            if (SSL_CTX_get_cert_store(ctx) != ca_cert_store) {
                // Free memory allocated for old cert and use new store `ca_cert_store`
                SSL_CTX_set_cert_store(ctx, ca_cert_store);
            }
        } else {
            X509_STORE_free(ca_cert_store);
        }
    }
}

inline void LoadCaCertStore(SSL_CTX* ctx, const char* ca_cert, std::size_t size)
{
    SetCaCertStore(ctx, CreateCaCertStore(ca_cert, size));
}
#endif

} // namespace

class SSLConnectionBackend final {
public:
    Result Init() {
        // on bundled OpenSSL, load ca cert store
#ifdef YUZU_BUNDLED_OPENSSL
        LoadCaCertStore(ssl_ctx, kCert, sizeof(kCert));
#endif
        std::call_once(one_time_init_flag, OneTimeInit);

        if (!one_time_init_success) {
            LOG_ERROR(Service_SSL, "Can't create SSL connection because OpenSSL one-time initialization failed");
            return ResultInternalError;
        }

        ssl = SSL_new(ssl_ctx);
        if (!ssl) {
            LOG_ERROR(Service_SSL, "SSL_new failed");
            return CheckOpenSSLErrors();
        }
        SSL_set_connect_state(ssl);
        bio = BIO_new(bio_meth);
        if (!bio) {
            LOG_ERROR(Service_SSL, "BIO_new failed");
            return CheckOpenSSLErrors();
        }
        BIO_set_data(bio, this);
        BIO_set_init(bio, 1);
        SSL_set_bio(ssl, bio, bio);
        return ResultSuccess;
    }

    Result SetHostName(const std::string& hostname) {
        if (!skip_cert_verification) {
            if (!SSL_set1_host(ssl, hostname.c_str())) {
                LOG_ERROR(Service_SSL, "SSL_set1_host({}) failed", hostname);
                return CheckOpenSSLErrors();
            }
        }
        if (!SSL_set_tlsext_host_name(ssl, hostname.c_str())) { // hostname for SNI
            LOG_ERROR(Service_SSL, "SSL_set_tlsext_host_name({}) failed", hostname);
            return CheckOpenSSLErrors();
        }
        return ResultSuccess;
    }

    void SetVerifyOption(u32 option) {
        skip_cert_verification = (option == 0);
        LOG_WARNING(Service_SSL, "option={} skip_verification={}", option,
                    skip_cert_verification);
        if (skip_cert_verification) {
            SSL_set_verify(ssl, SSL_VERIFY_NONE, nullptr);
            SSL_set1_host(ssl, nullptr);
            SSL_set_hostflags(ssl, 0);
        } else {
            SSL_set_verify(ssl, SSL_VERIFY_PEER, nullptr);
        }
    }

    Result DoHandshake() {
        SSL_set_verify_result(ssl, X509_V_OK);
        const int ret = SSL_do_handshake(ssl);

        if (!skip_cert_verification) {
            const long verify_result = SSL_get_verify_result(ssl);
            if (verify_result != X509_V_OK) {
                LOG_ERROR(Service_SSL, "SSL cert verification failed because: {}",
                          X509_verify_cert_error_string(verify_result));
                return CheckOpenSSLErrors();
            }
        }

        if (ret <= 0) {
            const int ssl_err = SSL_get_error(ssl, ret);
            if (ssl_err == SSL_ERROR_ZERO_RETURN ||
                (ssl_err == SSL_ERROR_SYSCALL && got_read_eof)) {
                LOG_ERROR(Service_SSL, "SSL handshake failed because server hung up");
                return ResultInternalError;
            }
        }
        return HandleReturn("SSL_do_handshake", 0, ret);
    }

    Result HandleReturn(const char* what, size_t* actual, int ret) {
        const int ssl_err = SSL_get_error(ssl, ret);
        CheckOpenSSLErrors();
        switch (ssl_err) {
        case SSL_ERROR_NONE:
            return ResultSuccess;
        case SSL_ERROR_ZERO_RETURN:
            LOG_DEBUG(Service_SSL, "{} => SSL_ERROR_ZERO_RETURN", what);
            // DoHandshake special-cases this, but for Read and Write:
            *actual = 0;
            return ResultSuccess;
        case SSL_ERROR_WANT_READ:
            LOG_DEBUG(Service_SSL, "{} => SSL_ERROR_WANT_READ", what);
            return ResultWouldBlock;
        case SSL_ERROR_WANT_WRITE:
            LOG_DEBUG(Service_SSL, "{} => SSL_ERROR_WANT_WRITE", what);
            return ResultWouldBlock;
        default:
            if (ssl_err == SSL_ERROR_SYSCALL && got_read_eof) {
                LOG_DEBUG(Service_SSL, "{} => SSL_ERROR_SYSCALL because server hung up", what);
                *actual = 0;
                return ResultSuccess;
            }
            LOG_ERROR(Service_SSL, "{} => other SSL_get_error return value {}", what, ssl_err);
            return ResultInternalError;
        }
    }

    ~SSLConnectionBackend() {
        // this is null-tolerant:
        SSL_free(ssl);
    }

    static void KeyLogCallback(const ::SSL* ssl, const char* line) {
        std::string str(line);
        str.push_back('\n');
        // Do this in a single WriteString for atomicity if multiple instances
        // are running on different threads (though that can't currently
        // happen).
        if (key_log_file.WriteString(str) != str.size() || !key_log_file.Flush()) {
            LOG_CRITICAL(Service_SSL, "Failed to write to SSLKEYLOGFILE");
        }
        LOG_DEBUG(Service_SSL, "Wrote to SSLKEYLOGFILE: {}", line);
    }

    static int WriteCallback(BIO* bio, const char* buf, size_t len, size_t* actual_p) {
        auto self = static_cast<SSLConnectionBackend*>(BIO_get_data(bio));
        ASSERT_OR_EXECUTE_MSG(
            self->socket, { return 0; }, "OpenSSL asked to send but we have no socket");
        BIO_clear_retry_flags(bio);
        auto [actual, err] = self->socket->Send({reinterpret_cast<const u8*>(buf), len}, 0);
        switch (err) {
        case Network::Errno::SUCCESS:
            *actual_p = actual;
            return 1;
        case Network::Errno::AGAIN:
            BIO_set_flags(bio, BIO_FLAGS_WRITE | BIO_FLAGS_SHOULD_RETRY);
            return 0;
        default:
            LOG_ERROR(Service_SSL, "Socket send returned Network::Errno {}", err);
            return -1;
        }
    }

    static int ReadCallback(BIO* bio, char* buf, size_t len, size_t* actual_p) {
        auto self = static_cast<SSLConnectionBackend*>(BIO_get_data(bio));
        ASSERT_OR_EXECUTE_MSG(
            self->socket, { return 0; }, "OpenSSL asked to recv but we have no socket");
        BIO_clear_retry_flags(bio);
        auto [actual, err] = self->socket->Recv(0, {reinterpret_cast<u8*>(buf), len});
        switch (err) {
        case Network::Errno::SUCCESS:
            *actual_p = actual;
            if (actual == 0) {
                self->got_read_eof = true;
            }
            return actual ? 1 : 0;
        case Network::Errno::AGAIN:
            BIO_set_flags(bio, BIO_FLAGS_READ | BIO_FLAGS_SHOULD_RETRY);
            return 0;
        default:
            LOG_ERROR(Service_SSL, "Socket recv returned Network::Errno {}", err);
            return -1;
        }
    }

    static long CtrlCallback(BIO* bio, int cmd, long l_arg, void* p_arg) {
        switch (cmd) {
        case BIO_CTRL_FLUSH:
            // Nothing to flush.
            return 1;
        case BIO_CTRL_PUSH:
        case BIO_CTRL_POP:
#ifdef BIO_CTRL_GET_KTLS_SEND
        case BIO_CTRL_GET_KTLS_SEND:
        case BIO_CTRL_GET_KTLS_RECV:
#endif
            // We don't support these operations, but don't bother logging them
            // as they're nothing unusual.
            return 0;
        default:
            LOG_DEBUG(Service_SSL, "OpenSSL BIO got ctrl({}, {}, {})", cmd, l_arg, p_arg);
            return 0;
        }
    }

    ::SSL* ssl = nullptr;
    BIO* bio = nullptr;
    bool got_read_eof = false;
    bool skip_cert_verification = false;
    std::shared_ptr<Network::SocketBase> socket;
};

Result CreateSSLConnectionBackend(std::unique_ptr<SSLConnectionBackend>* out_backend) {
    auto conn = std::make_unique<SSLConnectionBackend>();
    R_TRY(conn->Init());
    *out_backend = std::move(conn);
    return ResultSuccess;
}

namespace {

Result CheckOpenSSLErrors() {
    unsigned long rc;
    const char* file;
    int line;
    const char* func;
    const char* data;
    int flags;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    while ((rc = ERR_get_error_all(&file, &line, &func, &data, &flags)))
#else
    // Can't get function names from OpenSSL on this version, so use mine:
    func = __func__;
    while ((rc = ERR_get_error_line_data(&file, &line, &data, &flags)))
#endif
    {
        std::string msg;
        msg.resize(1024, '\0');
        ERR_error_string_n(rc, msg.data(), msg.size());
        msg.resize(strlen(msg.data()), '\0');
        if (flags & ERR_TXT_STRING) {
            msg.append(" | ");
            msg.append(data);
        }
        Common::Log::FmtLogMessage(Common::Log::Class::Service_SSL, Common::Log::Level::Error,
                                   file, line, func, "OpenSSL: {}",
                                   msg);
    }
    return ResultInternalError;
}

void OneTimeInit() {
    ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!ssl_ctx) {
        LOG_ERROR(Service_SSL, "SSL_CTX_new failed");
        CheckOpenSSLErrors();
        return;
    }

    SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, nullptr);

    if (!SSL_CTX_set_default_verify_paths(ssl_ctx)) {
        LOG_ERROR(Service_SSL, "SSL_CTX_set_default_verify_paths failed");
        CheckOpenSSLErrors();
        return;
    }

    OneTimeInitLogFile();

    if (!OneTimeInitBIO()) {
        return;
    }

    one_time_init_success = true;
}

void OneTimeInitLogFile() {
    const char* logfile = getenv("SSLKEYLOGFILE");
    if (logfile) {
        key_log_file.Open(logfile, Common::FS::FileAccessMode::Append, Common::FS::FileType::TextFile, Common::FS::FileShareFlag::ShareWriteOnly);
        if (key_log_file.IsOpen()) {
            SSL_CTX_set_keylog_callback(ssl_ctx, &SSLConnectionBackend::KeyLogCallback);
        } else {
            LOG_CRITICAL(Service_SSL, "SSLKEYLOGFILE was set but file could not be opened; not logging keys!");
        }
    }
}

bool OneTimeInitBIO() {
    bio_meth =
        BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK, "SSLConnectionBackend");
    if (!bio_meth ||
        !BIO_meth_set_write_ex(bio_meth, &SSLConnectionBackend::WriteCallback) ||
        !BIO_meth_set_read_ex(bio_meth, &SSLConnectionBackend::ReadCallback) ||
        !BIO_meth_set_ctrl(bio_meth, &SSLConnectionBackend::CtrlCallback)) {
        LOG_ERROR(Service_SSL, "Failed to create BIO_METHOD");
        return false;
    }
    return true;
}

} // namespace

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
    explicit ISslConnection(Core::System& system_in, SslVersion ssl_version_in,
                            std::shared_ptr<SslContextSharedData>& shared_data_in,
                            std::unique_ptr<SSLConnectionBackend>&& backend_in)
        : ServiceFramework{system_in, "ISslConnection"}, ssl_version{ssl_version_in},
          shared_data{shared_data_in}, backend{std::move(backend_in)} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &ISslConnection::SetSocketDescriptor, "SetSocketDescriptor"},
            {1, &ISslConnection::SetHostName, "SetHostName"},
            {2, &ISslConnection::SetVerifyOption, "SetVerifyOption"},
            {3, &ISslConnection::SetIoMode, "SetIoMode"},
            {4, nullptr, "GetSocketDescriptor"},
            {5, nullptr, "GetHostName"},
            {6, nullptr, "GetVerifyOption"},
            {7, nullptr, "GetIoMode"},
            {8, &ISslConnection::DoHandshake, "DoHandshake"},
            {9, &ISslConnection::DoHandshakeGetServerCert, "DoHandshakeGetServerCert"},
            {10, &ISslConnection::Read, "Read"},
            {11, &ISslConnection::Write, "Write"},
            {12, &ISslConnection::Pending, "Pending"},
            {13, nullptr, "Peek"},
            {14, nullptr, "Poll"},
            {15, nullptr, "GetVerifyCertError"},
            {16, nullptr, "GetNeededServerCertBufferSize"},
            {17, &ISslConnection::SetSessionCacheMode, "SetSessionCacheMode"},
            {18, nullptr, "GetSessionCacheMode"},
            {19, nullptr, "FlushSessionCache"},
            {20, nullptr, "SetRenegotiationMode"},
            {21, nullptr, "GetRenegotiationMode"},
            {22, &ISslConnection::SetOption, "SetOption"},
            {23, &ISslConnection::GetOption, "GetOption"},
            {24, nullptr, "GetVerifyCertErrors"},
            {25, nullptr, "GetCipherInfo"},
            {26, &ISslConnection::SetNextAlpnProto, "SetNextAlpnProto"},
            {27, &ISslConnection::GetNextAlpnProto, "GetNextAlpnProto"},
            {28, nullptr, "SetDtlsSocketDescriptor"},
            {29, nullptr, "GetDtlsHandshakeTimeout"},
            {30, nullptr, "SetPrivateOption"},
            {31, nullptr, "SetSrtpCiphers"},
            {32, nullptr, "GetSrtpCipher"},
            {33, nullptr, "ExportKeyingMaterial"},
            {34, nullptr, "SetIoTimeout"},
            {35, nullptr, "GetIoTimeout"},
        };
        // clang-format on

        RegisterHandlers(functions);

        backend->SetVerifyOption(verify_option);

        shared_data->connection_count++;
    }

    ~ISslConnection() {
        shared_data->connection_count--;
        if (fd_to_close.has_value()) {
            const s32 fd = *fd_to_close;
            if (!do_not_close_socket) {
                LOG_ERROR(Service_SSL,
                          "do_not_close_socket was changed after setting socket; is this right?");
            } else {
                auto bsd = system.ServiceManager().GetService<Service::Sockets::BSD>("bsd:u");
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
    SslVersion ssl_version;
    std::shared_ptr<SslContextSharedData> shared_data;
    std::unique_ptr<SSLConnectionBackend> backend;
    std::optional<int> fd_to_close;
    bool do_not_close_socket = false;
    bool get_server_cert_chain = false;
    bool skip_default_verify = false;
    bool enable_alpn = false;
    std::shared_ptr<Network::SocketBase> socket;
    std::vector<u8> next_alpn_proto;
    bool did_handshake = false;
    u32 verify_option = 0;

    Result SetSocketDescriptorImpl(s32* out_fd, s32 fd) {
        LOG_DEBUG(Service_SSL, "called, fd={}", fd);
        ASSERT(!did_handshake);
        auto bsd = system.ServiceManager().GetService<Service::Sockets::BSD>("bsd:u");
        ASSERT_OR_EXECUTE(bsd, { return ResultInternalError; });

        auto const res_v = bsd->DuplicateSocketImpl(fd);
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
            backend->socket = std::move(socket);
            return ResultSuccess;
        }
        LOG_ERROR(Service_SSL, "Failed to duplicate socket with fd {}", fd);
        return ResultInvalidSocket;
    }

    Result SetHostNameImpl(const std::string& hostname) {
        LOG_DEBUG(Service_SSL, "called. hostname={}", hostname);
        ASSERT(!did_handshake);
        return backend->SetHostName(hostname);
    }

    Result SetVerifyOptionImpl(u32 option) {
        LOG_DEBUG(Service_SSL, "called. option={} (forcing 0)", option);
        ASSERT(!did_handshake);
        verify_option = 0;
        backend->SetVerifyOption(0);
        R_SUCCEED();
    }

    Result SetIoModeImpl(u32 input_mode) {
        auto mode = static_cast<IoMode>(input_mode);
        ASSERT(mode == IoMode::Blocking || mode == IoMode::NonBlocking);
        ASSERT_OR_EXECUTE(socket, { return ResultNoSocket; });

        const bool non_block = mode == IoMode::NonBlocking;
        const Network::Errno error = socket->SetNonBlock(non_block);
        if (error != Network::Errno::SUCCESS) {
            LOG_ERROR(Service_SSL, "Failed to set native socket non-block flag to {}", non_block);
        }
        R_SUCCEED();
    }

    Result SetSessionCacheModeImpl(u32 mode) {
        ASSERT(!did_handshake);
        LOG_WARNING(Service_SSL, "(STUBBED) called. value={}", mode);
        R_SUCCEED();
    }

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

    Result ReadImpl(std::vector<u8>* out_data) {
        ASSERT_OR_EXECUTE(did_handshake, { return ResultInternalError; });
        size_t actual_size{};
        const int ret = SSL_read_ex(backend->ssl, out_data->data(), out_data->size(), &actual_size);
        Result res = backend->HandleReturn("SSL_read_ex", &actual_size, ret);
        if (res != ResultSuccess) {
            return res;
        }
        out_data->resize(actual_size);
        return res;
    }

    Result WriteImpl(size_t* out_size, std::span<const u8> data) {
        ASSERT_OR_EXECUTE(did_handshake, { return ResultInternalError; });
        const int ret = SSL_write_ex(backend->ssl, data.data(), data.size(), out_size);
        return backend->HandleReturn("SSL_write_ex", out_size, ret);
    }

    Result PendingImpl(s32* out_pending) {
        LOG_WARNING(Service_SSL, "(STUBBED) called.");
        *out_pending = SSL_pending(backend->ssl);
        return ResultSuccess;
    }

    void SetSocketDescriptor(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const s32 in_fd = rp.Pop<s32>();
        s32 out_fd{-1};
        const Result res = SetSocketDescriptorImpl(&out_fd, in_fd);
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(res);
        rb.Push<s32>(out_fd);
    }

    void SetHostName(HLERequestContext& ctx) {
        const std::string hostname = Common::StringFromBuffer(ctx.ReadBuffer());
        const Result res = SetHostNameImpl(hostname);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(res);
    }

    void SetVerifyOption(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u32 option = rp.Pop<u32>();
        const Result res = SetVerifyOptionImpl(option);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(res);
    }

    void SetIoMode(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u32 mode = rp.Pop<u32>();
        const Result res = SetIoModeImpl(mode);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(res);
    }

    void DoHandshake(HLERequestContext& ctx) {
        const Result res = DoHandshakeImpl();
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(res);
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
            STACK_OF(X509)* chain = SSL_get_peer_cert_chain(backend->ssl);
            if (chain) {
                int count = sk_X509_num(chain);
                ASSERT(count >= 0);
                for (int i = 0; i < count; i++) {
                    X509* x509 = sk_X509_value(chain, i);
                    ASSERT_OR_EXECUTE(x509 != nullptr, { continue; });
                    unsigned char* buf = nullptr;
                    int len = i2d_X509(x509, &buf);
                    ASSERT_OR_EXECUTE(len >= 0 && buf, { continue; });
                    certs.emplace_back(buf, buf + len);
                    OPENSSL_free(buf);
                }

                // succeed!
                const std::vector<u8> certs_buf = SerializeServerCerts(certs);
                if (ctx.CanWriteBuffer()) {
                    const size_t buffer_size = ctx.GetWriteBufferSize();
                    if (certs_buf.size() <= buffer_size) {
                        ctx.WriteBuffer(certs_buf);
                    } else {
                        LOG_WARNING(Service_SSL, "Certificate buffer too small: {} bytes needed, {} bytes available", certs_buf.size(), buffer_size);
                        ctx.WriteBuffer(std::span<const u8>(certs_buf.data(), buffer_size));
                    }
                } else {
                    LOG_DEBUG(Service_SSL, "No output buffer provided for certificates ({} bytes)", certs_buf.size());
                }

                out.certs_count = u32(certs.size());
                out.certs_size = u32(certs_buf.size());
            } else {
                LOG_ERROR(Service_SSL, "SSL_get_peer_cert_chain returned nullptr");
                res = ResultInternalError;
            }
        }
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(res);
        rb.PushRaw(out);
    }

    void Read(HLERequestContext& ctx) {
        std::vector<u8> output_bytes(ctx.GetWriteBufferSize());
        const Result res = ReadImpl(&output_bytes);
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(res);
        if (res == ResultSuccess) {
            rb.Push(static_cast<u32>(output_bytes.size()));
            ctx.WriteBuffer(output_bytes);
        } else {
            rb.Push(static_cast<u32>(0));
        }
    }

    void Write(HLERequestContext& ctx) {
        size_t write_size{0};
        const Result res = WriteImpl(&write_size, ctx.ReadBuffer());
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(res);
        rb.Push(static_cast<u32>(write_size));
    }

    void Pending(HLERequestContext& ctx) {
        s32 pending_size{0};
        const Result res = PendingImpl(&pending_size);
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(res);
        rb.Push<s32>(pending_size);
    }

    void SetSessionCacheMode(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u32 mode = rp.Pop<u32>();
        const Result res = SetSessionCacheModeImpl(mode);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(res);
    }

    void SetOption(HLERequestContext& ctx) {
        struct Parameters {
            OptionType option;
            s32 value;
        };
        static_assert(sizeof(Parameters) == 0x8, "Parameters is an invalid size");

        IPC::RequestParser rp{ctx};
        const auto parameters = rp.PopRaw<Parameters>();

        switch (parameters.option) {
        case OptionType::DoNotCloseSocket:
            do_not_close_socket = static_cast<bool>(parameters.value);
            break;
        case OptionType::GetServerCertChain:
            get_server_cert_chain = static_cast<bool>(parameters.value);
            break;
        case OptionType::SkipDefaultVerify:
            skip_default_verify = static_cast<bool>(parameters.value);
            break;
        case OptionType::EnableAlpn:
            enable_alpn = static_cast<bool>(parameters.value);
            break;
        default:
            LOG_WARNING(Service_SSL, "Unknown option={}, value={}", parameters.option,
                        parameters.value);
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetOption(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto option = rp.PopRaw<OptionType>();

        u8 value = 0;

        switch (option) {
        case OptionType::DoNotCloseSocket:
            value = static_cast<u8>(do_not_close_socket);
            break;
        case OptionType::GetServerCertChain:
            value = static_cast<u8>(get_server_cert_chain);
            break;
        case OptionType::SkipDefaultVerify:
            value = static_cast<u8>(skip_default_verify);
            break;
        case OptionType::EnableAlpn:
            value = static_cast<u8>(enable_alpn);
            break;
        default:
            LOG_WARNING(Service_SSL, "Unknown option={}", option);
            value = 0;
            break;
        }

        LOG_DEBUG(Service_SSL, "GetOption called, option={}, ret value={}", option, value);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u8>(value);
    }

    void SetNextAlpnProto(HLERequestContext& ctx) {
        const auto data = ctx.ReadBuffer(0);
        next_alpn_proto.assign(data.begin(), data.end());

        LOG_DEBUG(Service_SSL, "SetNextAlpnProto called, size={}", next_alpn_proto.size());

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetNextAlpnProto(HLERequestContext& ctx) {
        const size_t writable = ctx.GetWriteBufferSize();
        const size_t to_write = (std::min)(next_alpn_proto.size(), writable);

        if (to_write != 0) {
            ctx.WriteBuffer(std::span<const u8>(next_alpn_proto.data(), to_write));
        }

        LOG_DEBUG(Service_SSL, "GetNextAlpnProto called, size={}", to_write);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(static_cast<u32>(to_write));
    }
};

class ISslContext final : public ServiceFramework<ISslContext> {
public:
    explicit ISslContext(Core::System& system_, SslVersion version)
        : ServiceFramework{system_, "ISslContext"}, ssl_version{version},
          shared_data{std::make_shared<SslContextSharedData>()} {
        static const FunctionInfo functions[] = {
            {0, &ISslContext::SetOption, "SetOption"},
            {1, &ISslContext::GetOption, "GetOption"},
            {2, &ISslContext::CreateConnection, "CreateConnection"},
            {3, &ISslContext::GetConnectionCount, "GetConnectionCount"},
            {4, &ISslContext::ImportServerPki, "ImportServerPki"},
            {5, &ISslContext::ImportClientPki, "ImportClientPki"},
            {6, nullptr, "RemoveServerPki"},
            {7, nullptr, "RemoveClientPki"},
            {8, nullptr, "RegisterInternalPki"},
            {9, nullptr, "AddPolicyOid"},
            {10, nullptr, "ImportCrl"},
            {11, nullptr, "RemoveCrl"},
            {12, nullptr, "ImportClientCertKeyPki"},
            {13, nullptr, "GeneratePrivateKeyAndCert"},
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
};

class ISslService final : public ServiceFramework<ISslService> {
public:
    explicit ISslService(Core::System& system_)
        : ServiceFramework{system_, "ssl"}, cert_store{system} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &ISslService::CreateContext, "CreateContext"},
            {1, nullptr, "GetContextCount"},
            {2, D<&ISslService::GetCertificates>, "GetCertificates"},
            {3, D<&ISslService::GetCertificateBufSize>, "GetCertificateBufSize"},
            {4, nullptr, "DebugIoctl"},
            {5, &ISslService::SetInterfaceVersion, "SetInterfaceVersion"},
            {6, nullptr, "FlushSessionCache"},
            {7, nullptr, "SetDebugOption"},
            {8, nullptr, "GetDebugOption"},
            {8, nullptr, "ClearTls12FallbackFlag"},
        };
        // clang-format on

        RegisterHandlers(functions);
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
    CertStore cert_store;
};

class ISslServiceForSystem final : public ServiceFramework<ISslServiceForSystem> {
    public:
        explicit ISslServiceForSystem(Core::System& system_) : ServiceFramework{system_, "ssl:s"} {
            // clang-format off
            static const FunctionInfo functions[] = {
                {0, D<&ISslServiceForSystem::CreateContext>, "CreateContext"},
                {1, D<&ISslServiceForSystem::GetContextCount>, "GetContextCount"},
                {2, D<&ISslServiceForSystem::GetCertificates>, "GetCertificates"},
                {3, D<&ISslServiceForSystem::GetCertificateBufSize>, "GetCertificateBufSize"},
                {4, D<&ISslServiceForSystem::DebugIoctl>, "DebugIoctl"},
                {5, D<&ISslServiceForSystem::SetInterfaceVersion>, "SetInterfaceVersion"},
                {6, D<&ISslServiceForSystem::FlushSessionCache>, "FlushSessionCache"},
                {7, D<&ISslServiceForSystem::SetDebugOption>, "SetDebugOption"},
                {8, D<&ISslServiceForSystem::GetDebugOption>, "GetDebugOption"},
                {9, D<&ISslServiceForSystem::ClearTls12FallbackFlag>, "ClearTls12FallbackFlag"},
                {100, D<&ISslServiceForSystem::CreateContextForSystem>, "CreateContextForSystem"},
                {101, D<&ISslServiceForSystem::SetThreadCoreMask>, "SetThreadCoreMask"},
                {102, D<&ISslServiceForSystem::GetThreadCoreMask>, "GetThreadCoreMask"},
                {103, D<&ISslServiceForSystem::VerifySignature>, "VerifySignature"}
            };
            // clang-format on

            RegisterHandlers(functions);
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

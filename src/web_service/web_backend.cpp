// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2017 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <chrono>
#include <format>
#include <mutex>
#include <string>
#include <utility>

#ifdef YUZU_BUNDLED_OPENSSL
#include <openssl/cert.h>
#endif

#include "common/logging.h"
#include "web_service/web_backend.h"
#include "web_service/web_result.h"

import hydrocarbon;

namespace WebService {

constexpr std::array<const char, 1> API_VERSION{'1'};

struct Client::Impl {
    Impl(std::string host_, std::string username_, std::string token_)
        : host{std::move(host_)}, username{std::move(username_)}, token{std::move(token_)} {
        std::scoped_lock lock{jwt_cache.mutex};
        if (this->username == jwt_cache.username && this->token == jwt_cache.token) {
            jwt = jwt_cache.jwt;
        }

        // Normalize host expression
        if (!this->host.empty() && this->host.back() == '/') {
            static_cast<void>(this->host.pop_back());
        }
    }

    /// A generic function handles POST, GET and DELETE request together
    WebResult GenericRequest(const std::string& method, const std::string& path,
                             const std::string& data, bool allow_anonymous,
                             const std::string& accept) {
        if (jwt.empty()) {
            UpdateJWT();
        }

        if (jwt.empty() && !allow_anonymous) {
            LOG_ERROR(WebService, "Credentials must be provided for authenticated requests");
            return WebResult{WebResult::Code::CredentialsMissing, "Credentials needed", ""};
        }

        auto result = GenericRequest(method, path, data, accept, jwt);
        if (result.result_string == "401") {
            // Try again with new JWT
            UpdateJWT();
            result = GenericRequest(method, path, data, accept, jwt);
        }

        return result;
    }

    /**
     * A generic function with explicit authentication method specified
     * JWT is used if the jwt parameter is not empty
     * username + token is used if jwt is empty but username and token are
     * not empty anonymous if all of jwt, username and token are empty
     */
    WebResult GenericRequest(const std::string& method, const std::string& path,
                             const std::string& data, const std::string& accept,
                             const std::string& jwt_ = "", const std::string& username_ = "",
                             const std::string& token_ = "") {
        hydro::RequestOptions opts;
#ifdef YUZU_BUNDLED_OPENSSL
        opts.connect.cert = std::string{kCert};
#endif

        opts.connect.connectionTimeout = std::chrono::milliseconds{10000};

        if (!jwt_.empty()) {
            opts.headers.add("Authorization", std::format("Bearer {}", jwt_));
        } else if (!username_.empty()) {
            opts.headers.add("x-username", username_);
            opts.headers.add("x-token", token_);
        }

        opts.headers.add("api-version", std::string(API_VERSION.begin(), API_VERSION.end()));
        if (method != "GET") {
            opts.headers.add("Content-Type", "application/json");
        }

        opts.body = data;

        auto res = hydro::post(std::format("{}{}", host, path), opts);

        if (!res) {
            LOG_ERROR(WebService, "{} to {} returned error {}", method, host + path, res.error());
            return WebResult{WebResult::Code::LibError, "Null response", ""};
        }

        if (res->status_code >= hydro::Status::BadRequest) {
            LOG_ERROR(WebService, "{} to {} returned error status code: {}", method, host + path,
                      res->status_code);
            return WebResult{WebResult::Code::HttpError, std::format("{}", res->status_code), ""};
        }

        auto content_type = res->headers.get("content-type");

        if (!content_type) {
            LOG_ERROR(WebService, "{} to {} returned no content", method, host + path);
            return WebResult{WebResult::Code::WrongContent, "", ""};
        }

        if (content_type->find(accept) == std::string::npos) {
            LOG_ERROR(WebService, "{} to {} returned wrong content: {}", method, host + path,
                      content_type.value());
            return WebResult{WebResult::Code::WrongContent, "Wrong content", ""};
        }
        return WebResult{WebResult::Code::Success, "", res->body};
    }

    // Retrieve a new JWT from given username and token
    void UpdateJWT() {
        if (username.empty() || token.empty()) {
            return;
        }

        auto result = GenericRequest("POST", "/jwt/internal", "", "text/html", "", username, token);
        if (result.result_code != WebResult::Code::Success) {
            LOG_ERROR(WebService, "UpdateJWT failed");
        } else {
            std::scoped_lock lock{jwt_cache.mutex};
            jwt_cache.username = username;
            jwt_cache.token = token;
            jwt_cache.jwt = jwt = result.returned_data;
        }
    }

    std::string host;
    std::string username;
    std::string token;
    std::string jwt;

    struct JWTCache {
        std::mutex mutex;
        std::string username;
        std::string token;
        std::string jwt;
    };
    static inline JWTCache jwt_cache;
};

Client::Client(std::string host, std::string username, std::string token)
    : impl{std::make_unique<Impl>(std::move(host), std::move(username), std::move(token))} {}

Client::~Client() = default;

WebResult Client::PostJson(const std::string& path, const std::string& data, bool allow_anonymous) {
    return impl->GenericRequest("POST", path, data, allow_anonymous, "application/json");
}

WebResult Client::GetJson(const std::string& path, bool allow_anonymous) {
    return impl->GenericRequest("GET", path, "", allow_anonymous, "application/json");
}

WebResult Client::DeleteJson(const std::string& path, const std::string& data,
                             bool allow_anonymous) {
    return impl->GenericRequest("DELETE", path, data, allow_anonymous, "application/json");
}

WebResult Client::GetPlain(const std::string& path, bool allow_anonymous) {
    return impl->GenericRequest("GET", path, "", allow_anonymous, "text/plain");
}

WebResult Client::GetImage(const std::string& path, bool allow_anonymous) {
    return impl->GenericRequest("GET", path, "", allow_anonymous, "image/png");
}

WebResult Client::GetExternalJWT(const std::string& audience) {
    return impl->GenericRequest("POST", std::format("/jwt/external/{}", audience), "", false,
                                "text/html");
}

} // namespace WebService

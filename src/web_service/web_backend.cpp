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
    Impl(std::string host_, std::string username_)
        : host{std::move(host_)}, username{std::move(username_)} {

        // Normalize host expression
        if (!this->host.empty() && this->host.back() == '/') {
            static_cast<void>(this->host.pop_back());
        }
    }

    /**
     * A generic function with explicit authentication method specified
     */
    WebResult GenericRequest(const std::string& method, const std::string& path,
                             const std::string& data, const std::string& accept,
                             const std::string& username_ = "") {
        hydro::RequestOptions opts;
#ifdef YUZU_BUNDLED_OPENSSL
        opts.connect.cert = std::string{kCert};
#endif

        opts.connect.connectionTimeout = std::chrono::milliseconds{10000};

        if (!username_.empty()) {
            opts.headers.add("x-username", username_);
        }

        opts.headers.add("api-version", std::string(API_VERSION.begin(), API_VERSION.end()));
        if (method != "GET") {
            opts.headers.add("Content-Type", "application/json");
        }

        opts.body = data;

        auto res = hydro::perform(method, std::format("{}{}", host, path), opts);

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

    std::string host;
    std::string username;
};

Client::Client(std::string host, std::string username)
    : impl{std::make_unique<Impl>(std::move(host), std::move(username))} {}

Client::~Client() = default;

WebResult Client::PostJson(const std::string& path, const std::string& data) {
    return impl->GenericRequest("POST", path, data, "application/json");
}

WebResult Client::GetJson(const std::string& path) {
    return impl->GenericRequest("GET", path, "", "application/json");
}

WebResult Client::DeleteJson(const std::string& path, const std::string& data) {
    return impl->GenericRequest("DELETE", path, data, "application/json");
}

WebResult Client::GetPlain(const std::string& path) {
    return impl->GenericRequest("GET", path, "", "text/plain");
}

WebResult Client::GetImage(const std::string& path) {
    return impl->GenericRequest("GET", path, "", "image/png");
}

} // namespace WebService

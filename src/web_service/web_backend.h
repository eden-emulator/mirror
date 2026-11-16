// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2017 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <string>

namespace WebService {

struct WebResult;

class Client {
public:
    Client(std::string host, std::string username);
    ~Client();

    /**
     * Posts JSON to the specified path.
     * @param path the URL segment after the host address.
     * @param data String of JSON data to use for the body of the POST request.
     * @param allow_anonymous If true, allow anonymous unauthenticated requests.
     * @return the result of the request.
     */
    WebResult PostJson(const std::string& path, const std::string& data);

    /**
     * Gets JSON from the specified path.
     * @param path the URL segment after the host address.
     * @param allow_anonymous If true, allow anonymous unauthenticated requests.
     * @return the result of the request.
     */
    WebResult GetJson(const std::string& path);

    /**
     * Deletes JSON to the specified path.
     * @param path the URL segment after the host address.
     * @param data String of JSON data to use for the body of the DELETE request.
     * @param allow_anonymous If true, allow anonymous unauthenticated requests.
     * @return the result of the request.
     */
    WebResult DeleteJson(const std::string& path, const std::string& data);

    /**
     * Gets a plain string from the specified path.
     * @param path the URL segment after the host address.
     * @param allow_anonymous If true, allow anonymous unauthenticated requests.
     * @return the result of the request.
     */
    WebResult GetPlain(const std::string& path);

    /**
     * Gets an PNG image from the specified path.
     * @param path the URL segment after the host address.
     * @param allow_anonymous If true, allow anonymous unauthenticated requests.
     * @return the result of the request.
     */
    WebResult GetImage(const std::string& path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace WebService

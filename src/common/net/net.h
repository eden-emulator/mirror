// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace Common::Net {

struct Asset {
    std::string name;
    std::size_t size;
    std::optional<std::string> digest;
    std::string created_at;
    std::string browser_download_url;
};

struct NamedAsset {
    std::string name;
    Asset asset;
};

struct Release {
    std::string tag_name;
    std::string name;
    std::string body;
    std::string html_url;
    std::optional<std::string> published_at;

    std::vector<Asset> assets;
};

std::vector<Release> GetReleasesFromJson(const std::string& body);

// Get the relevant list of assets for the current platform.
std::vector<NamedAsset> GetPlatformAssets(const Release &r);

// Make a request, and return the response body if applicable.
std::optional<std::string> MakeRequest(const std::string &url);
std::optional<std::string> MakeRequest(const std::string_view url);

bool HeadRequest(const std::string &url);

// Get all of the latest stable releases.
std::vector<Release> GetReleases();

// Get all of the latest stable releases as text.
std::optional<std::string> GetReleasesBody();

// Get the latest release of the current channel.
std::optional<Release> GetLatestRelease();

}

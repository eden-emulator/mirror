// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <deque>
#include <memory>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "common/slot_vector.h"
#include "video_core/texture_cache/pending_download.h"

namespace {
struct DownloadImage {
    DownloadImage(u32 value, std::shared_ptr<std::vector<u32>> destroyed_)
        : handle{std::make_unique<u32>(value)}, destroyed{std::move(destroyed_)} {}
    DownloadImage(DownloadImage&&) noexcept = default;
    DownloadImage& operator=(DownloadImage&&) noexcept = default;
    ~DownloadImage() { if (handle) destroyed->push_back(*handle); }
    std::unique_ptr<u32> handle;
    std::shared_ptr<std::vector<u32>> destroyed;
};
using Download = VideoCommon::PendingImageDownload<DownloadImage>;
}

TEST_CASE("Queued texture downloads survive deletion and reuse of their cache slot", "[texture-download]") {
    auto destroyed = std::make_shared<std::vector<u32>>();
    Common::SlotVector<DownloadImage> images;
    const auto old_id = images.insert(42U, destroyed);
    std::vector<Download> pending{{true, 0, old_id, {}}, {false, 0, old_id, {}}};
    std::deque<std::vector<Download>> submitted{{{true, 0, old_id, {}}}};
    REQUIRE(VideoCommon::RetainPendingImageDownloads(old_id, images[old_id], pending, submitted));
    REQUIRE_FALSE(images[old_id].handle);
    REQUIRE(pending[0].retired_image == submitted[0][0].retired_image);
    REQUIRE_FALSE(pending[1].retired_image); // Buffer-download slot IDs are a separate namespace.
    images.erase(old_id);
    const auto replacement_id = images.insert(99U, destroyed);
    REQUIRE(replacement_id == old_id);
    REQUIRE(*pending[0].GetImage(images).handle == 42);
    REQUIRE(*submitted[0][0].GetImage(images).handle == 42);
    REQUIRE(*images[replacement_id].handle == 99);
    // Retiring the replacement must not redirect an earlier generation's downloads.
    pending.push_back({true, 1, replacement_id, {}});
    REQUIRE(VideoCommon::RetainPendingImageDownloads(replacement_id, images[replacement_id], pending, submitted));
    images.erase(replacement_id);
    REQUIRE(*pending[0].GetImage(images).handle == 42);
    REQUIRE(*pending[2].GetImage(images).handle == 99);
    REQUIRE(destroyed->empty());
    pending.clear();
    REQUIRE(*destroyed == std::vector<u32>{99});
    REQUIRE(*submitted[0][0].GetImage(images).handle == 42);
    submitted.pop_front();
    REQUIRE(*destroyed == std::vector<u32>{99, 42});
}

TEST_CASE("Textures without queued downloads retain ordinary cache retirement", "[texture-download]") {
    auto destroyed = std::make_shared<std::vector<u32>>();
    Common::SlotVector<DownloadImage> images;
    const auto id = images.insert(7U, destroyed);
    std::vector<Download> pending{{false, 0, id, {}}};
    std::deque<std::vector<Download>> submitted;
    REQUIRE_FALSE(VideoCommon::RetainPendingImageDownloads(id, images[id], pending, submitted));
    REQUIRE(*images[id].handle == 7);
    images.erase(id);
    REQUIRE(*destroyed == std::vector<u32>{7});
}

TEST_CASE("Repeated whole-image downloads keep first occurrence and buffer requests", "[texture-download]") {
    auto destroyed = std::make_shared<std::vector<u32>>();
    Common::SlotVector<DownloadImage> images;
    const auto a = images.insert(42U, destroyed);
    const auto b = images.insert(99U, destroyed);
    std::vector<Download> pending{{true, 0, a, {}}, {true, 0, b, {}},
                                  {false, 3, Common::SlotId{10000}, {}}, {true, 0, a, {}}};
    const auto writeback = [&](const auto& downloads) {
        u32 value = 0;
        for (auto it = downloads.rbegin(); it != downloads.rend(); ++it)
            if (it->is_swizzle) value = *it->GetImage(images).handle;
        return value;
    };
    const u32 expected = writeback(pending);
    REQUIRE(VideoCommon::DeduplicatePendingImageDownloads(pending, images) == 1);
    REQUIRE(pending.size() == 3);
    REQUIRE(pending[0].object_id == a);
    REQUIRE(pending[1].object_id == b);
    REQUIRE_FALSE(pending[2].is_swizzle);
    REQUIRE(pending[2].async_buffer_id == 3);
    REQUIRE(writeback(pending) == expected);
    REQUIRE(expected == 42);
}

TEST_CASE("Readback deduplication distinguishes recycled image generations", "[texture-download]") {
    auto destroyed = std::make_shared<std::vector<u32>>();
    Common::SlotVector<DownloadImage> images;
    const auto id = images.insert(42U, destroyed);
    std::vector<Download> pending{{true, 0, id, {}}};
    std::deque<std::vector<Download>> submitted;
    REQUIRE(VideoCommon::RetainPendingImageDownloads(id, images[id], pending, submitted));
    auto old = pending.front();
    images.erase(id);
    const auto replacement = images.insert(99U, destroyed);
    REQUIRE(replacement == id);
    pending.push_back({true, 0, replacement, {}});
    for (size_t i = 0; i < 1000; ++i) pending.push_back(old);
    REQUIRE(VideoCommon::DeduplicatePendingImageDownloads(pending, images) == 1000);
    REQUIRE(pending.size() == 2);
    REQUIRE(*pending[0].GetImage(images).handle == 42);
    REQUIRE(*pending[1].GetImage(images).handle == 99);
    REQUIRE(destroyed->empty());
    pending.clear();
    REQUIRE(destroyed->empty());
    old.retired_image.reset();
    REQUIRE(*destroyed == std::vector<u32>{42});
}

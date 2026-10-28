// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <memory>
#include <unordered_set>
#include <cstddef>
#include <utility>
#include "common/slot_vector.h"

namespace VideoCommon {

template <class Image>
struct PendingImageDownload {
    bool is_swizzle;
    size_t async_buffer_id;
    Common::SlotId object_id;
    std::shared_ptr<Image> retired_image{};

    Image& GetImage(Common::SlotVector<Image>& images) const {
        return retired_image ? *retired_image : images[object_id];
    }
};

// Whole-image copies are recorded together at commit time. Repeated requests
// for the same live image therefore observe the same contents. Keep the first
// occurrence so reverse CPU writeback order remains unchanged for aliases.
template <class Image, class Downloads>
size_t DeduplicatePendingImageDownloads(Downloads& downloads, Common::SlotVector<Image>& images) {
    std::unordered_set<const Image*> seen;
    const size_t previous_size = downloads.size();
    const auto end = std::remove_if(downloads.begin(), downloads.end(), [&](const auto& download) {
        return download.is_swizzle && !seen.insert(&download.GetImage(images)).second;
    });
    downloads.erase(end, downloads.end());
    return previous_size - downloads.size();
}

// Cache slots may be recycled before queued GPU downloads are submitted or
// consumed. Keep the actual image, including its metadata, until those finish.
template <class Image, class Downloads, class Batches>
bool RetainPendingImageDownloads(Common::SlotId id, Image& image, Downloads& pending,
                                 Batches& submitted) {
    std::shared_ptr<Image> retained;
    const auto retain = [&](auto& downloads) {
        for (auto& download : downloads) {
            if (!download.is_swizzle || download.object_id != id || download.retired_image) continue;
            if (!retained) retained = std::make_shared<Image>(std::move(image));
            download.retired_image = retained;
        }
    };
    retain(pending);
    for (auto& downloads : submitted) retain(downloads);
    return retained != nullptr;
}

} // namespace VideoCommon

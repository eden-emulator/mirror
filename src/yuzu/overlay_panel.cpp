// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>
#include <mutex>

#include <QCursor>
#include <QImage>
#include <QPainter>
#include <QRectF>

#include "yuzu/bootmanager.h"
#include "yuzu/overlay_panel.h"

namespace GameOverlay {

void Publish(Common::OverlayPanel& panel, const QImage* image) {
    std::scoped_lock lock{panel.mutex};
    if (!image) {
        if (panel.enabled) {
            panel.enabled = false;
            ++panel.version;
        }
        return;
    }
    panel.width = static_cast<u32>(image->width());
    panel.height = static_cast<u32>(image->height());
    panel.pixels.resize(static_cast<std::size_t>(image->sizeInBytes()));
    std::memcpy(panel.pixels.data(), image->constBits(),
                static_cast<std::size_t>(image->sizeInBytes()));
    panel.enabled = true;
    ++panel.version;
}

bool Shown(Common::OverlayPanel& panel) {
    std::scoped_lock lock{panel.mutex};
    return panel.enabled;
}

Cursor CursorOn(Common::OverlayPanel& panel, const GRenderWindow& window) {
    const auto& layout = window.GetFramebufferLayout();
    Common::OverlayRect place{};
    {
        std::scoped_lock lock{panel.mutex};
        place = Common::PlaceOverlayPanel(
            panel, static_cast<float>(layout.screen.left), static_cast<float>(layout.screen.top),
            static_cast<float>(layout.screen.right), static_cast<float>(layout.screen.bottom));
    }
    // The panel is drawn at its own size, in the framebuffer's pixels
    const qreal ratio = window.devicePixelRatioF();
    const QPointF logical = window.mapFromGlobal(QCursor::pos());
    const QPoint at(static_cast<int>(logical.x() * ratio) - static_cast<int>(place.x),
                    static_cast<int>(logical.y() * ratio) - static_cast<int>(place.y));
    const bool inside = at.x() >= 0 && at.y() >= 0 && at.x() < static_cast<int>(place.width) &&
                        at.y() < static_cast<int>(place.height);
    return {at, inside};
}

namespace Style {

QFont TextFont() {
    QFont font;
    font.setPixelSize(13);
    return font;
}

QFont LabelFont() {
    QFont font;
    font.setPixelSize(12);
    font.setBold(true);
    return font;
}

QFont SmallFont() {
    QFont font;
    font.setPixelSize(11);
    font.setBold(true);
    return font;
}

void Pill(QPainter& painter, const QRectF& rect, const QColor& fill) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawRoundedRect(rect, rect.height() / 2.0, rect.height() / 2.0);
}

void Switch(QPainter& painter, const QRectF& rect, bool on, bool locked) {
    QColor track = on ? QColor(45, 160, 80, 230) : QColor(255, 255, 255, 45);
    if (locked) {
        track.setAlpha(110);
    }
    Pill(painter, rect, track);
    const qreal knob = rect.height() - 4;
    const qreal x = on ? rect.right() - 2 - knob : rect.left() + 2;
    painter.setBrush(locked ? QColor(230, 230, 230, 150) : QColor(245, 245, 245));
    painter.drawEllipse(QRectF(x, rect.top() + 2, knob, knob));
}

} // namespace Style

} // namespace GameOverlay

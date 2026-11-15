// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QFont>
#include <QPoint>

#include "common/overlay_panels.h"

class GRenderWindow;
class QImage;
class QPainter;
class QRectF;
class QString;

/// The frontend's side of the in-game overlay panels (common/overlay_panels.h)
namespace GameOverlay {

/// Shows an image as the panel (copied), or hides the panel (nullptr)
void Publish(Common::OverlayPanel& panel, const QImage* image);

/// Whether the panel is drawn now
[[nodiscard]] bool Shown(Common::OverlayPanel& panel);

/// The cursor in a panel's image, and whether it is over the panel (which must be shown)
struct Cursor {
    QPoint at;
    bool inside = false;
};
[[nodiscard]] Cursor CursorOn(Common::OverlayPanel& panel, const GRenderWindow& window);

/// The overlay's look, shared by its panels
namespace Style {

/// A panel's dark, see-through fill
inline const QColor Fill{14, 14, 18, 175};
/// A held, hovered or active item
inline const QColor Active{45, 95, 200, 200};
/// Something that joined or turned on
inline const QColor Good{45, 160, 80, 200};
/// Something that left or failed
inline const QColor Bad{190, 50, 50, 200};
inline const QColor Text{240, 240, 240};
inline const QColor DimText{255, 255, 255, 120};

/// The fonts: names and lines, labels (bold), small labels (bold)
[[nodiscard]] QFont TextFont();
[[nodiscard]] QFont LabelFont();
[[nodiscard]] QFont SmallFont();

/// A pill: a rounded rectangle with fully round ends
void Pill(QPainter& painter, const QRectF& rect, const QColor& fill);

/// A switch: on (green), off (grey) or locked (on, dimmed)
void Switch(QPainter& painter, const QRectF& rect, bool on, bool locked);

} // namespace Style

} // namespace GameOverlay

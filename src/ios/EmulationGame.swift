// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2024 Pomelo, Stossy11
// SPDX-License-Identifier: GPL-3.0-or-later

import Foundation

struct EmulationGame : Comparable, Hashable, Identifiable {
    var id = UUID()

    let developer: String
    let fileURL: URL
    let imageData: Data
    let title: String

    func hash(into hasher: inout Hasher) {
        hasher.combine(id)
        hasher.combine(developer)
        hasher.combine(fileURL)
        hasher.combine(imageData)
        hasher.combine(title)
    }

    static func < (lhs: EmulationGame, rhs: EmulationGame) -> Bool {
        lhs.title < rhs.title
    }

    static func == (lhs: EmulationGame, rhs: EmulationGame) -> Bool {
        lhs.title == rhs.title
    }
}

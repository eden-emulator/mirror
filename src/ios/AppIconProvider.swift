// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2024 Pomelo, Stossy11
// SPDX-License-Identifier: GPL-3.0-or-later

import Foundation

enum AppIconProvider {
    static func appIcon(in bundle: Bundle = .main) -> String {
        guard let icons = bundle.object(forInfoDictionaryKey: "CFBundleIcons") as? [String: Any],
        let primaryIcon = icons["CFBundlePrimaryIcon"] as? [String: Any],
        let iconFiles = primaryIcon["CFBundleIconFiles"] as? [String],
        let iconFileName = iconFiles.last else {
            print("Could not find icons in bundle")
            return ""
        }
        return iconFileName
    }
}

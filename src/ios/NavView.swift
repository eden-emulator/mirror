// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2024 Pomelo, Stossy11
// SPDX-License-Identifier: GPL-3.0-or-later

import SwiftUI
import AppUI

struct NavView: View {
    @Binding var core: Core
    @State private var selectedTab = 0
    var body: some View {
        TabView(selection: $selectedTab) {
            LibraryView(core: $core)
                .tabItem { Label("Library", systemImage: "rectangle.on.rectangle") }
                .tag(0)
            BootOSView(core: $core, currentnavigarion: $selectedTab)
                .toolbar(.hidden, for: .tabBar)
                .tabItem { Label("Boot OS", systemImage: "house") }
                .tag(1)
            SettingsView(core: core)
                .tabItem { Label("Settings", systemImage: "gear") }
                .tag(2)
        }
    }
}

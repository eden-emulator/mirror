// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2024 Yuzu, Stossy11
// SPDX-License-Identifier: GPL-3.0-or-later

import SwiftUI

struct InfoView: View {
    @AppStorage("entitlementNotExists") private var entitlementNotExists: Bool = false
    @AppStorage("increaseddebugmem") private var increaseddebugmem: Bool = false
    @AppStorage("extended-virtual-addressing") private var extended: Bool = false
    let infoDictionary = Bundle.main.infoDictionary

    var body: some View {
        ScrollView {
            VStack {
                Text("Welcome").font(.largeTitle)
                Divider()
                Text("Entitlements:").font(.title).font(Font.headline.weight(.bold))
                Spacer().frame(height: 10)
                Group {
                    Text("Required:").font(.title2).font(Font.headline.weight(.bold))
                    Spacer().frame(height: 10)
                    Text("Limit: \(String(describing: !entitlementNotExists))")
                    Spacer().frame(height: 10)
                }
                Group {
                    Spacer().frame(height: 10)
                    Text("Reccomended:").font(.title2).font(Font.headline.weight(.bold))
                    Spacer().frame(height: 10)
                    Text("Limit: \(String(describing: increaseddebugmem))").padding()
                    Text("Extended: \(String(describing: extended))")
                }

            }
            .padding()
            Text("Version: \(getAppVersion())").foregroundColor(.gray)
        }
    }
    func getAppVersion() -> String {
        guard let s = infoDictionary?["CFBundleShortVersionString"] as? String else {
            return "Unknown"
        }
        return s
    }
}

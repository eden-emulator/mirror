// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2024 Pomelo, Stossy11
// SPDX-License-Identifier: GPL-3.0-or-later

import SwiftUI
import UniformTypeIdentifiers

struct AdvancedSettingsView: View {
    @AppStorage("exitgame") var exitgame: Bool = false
    @AppStorage("ClearBackingRegion") var kpagetable: Bool = false
    @AppStorage("WaitingforJIT") var waitingJIT: Bool = false
    @AppStorage("cangetfullpath") var canGetFullPath: Bool = false
    @AppStorage("onscreenhandheld") var onscreenjoy: Bool = false
    var body: some View {
        ScrollView {
            Rectangle()
                .fill(Color(uiColor: UIColor.secondarySystemBackground))
                .cornerRadius(10)
                .frame(width: .infinity, height: 50)
                .overlay() {
                    HStack {
                        Toggle("Exit Game Button", isOn: $exitgame)
                            .padding()
                    }
                }
            Text("This is very unstable and can lead to game freezing and overall bad preformance after you exit a game")
                .padding(.bottom)
                .font(.footnote)
                .foregroundColor(.gray)
            Rectangle()
                .fill(Color(uiColor: UIColor.secondarySystemBackground))
                .cornerRadius(10)
                .frame(width: .infinity, height: 50)
                .overlay() {
                    HStack {
                        Toggle("Memory Usage Increase", isOn: $kpagetable)
                            .padding()
                    }
                }
            Text("This makes games way more stable but a lot of games will crash as you will run out of Memory way quicker. (Don't Enable this on devices with less then 8GB of memory as most games will crash)")
                .padding(.bottom)
                .font(.footnote)
                .foregroundColor(.gray)

            Rectangle()
                .fill(Color(uiColor: UIColor.secondarySystemBackground))
                .cornerRadius(10)
                .frame(width: .infinity, height: 50)
                .overlay() {
                    HStack {
                        Toggle("Check for Booting OS", isOn: $canGetFullPath)
                            .padding()
                    }
                }
            Text("If you do not have the neccesary files for Booting the Switch OS, it will just crash almost instantly.")
                .padding(.bottom)
                .font(.footnote)
                .foregroundColor(.gray)

            Rectangle()
                .fill(Color(uiColor: UIColor.secondarySystemBackground))
                .cornerRadius(10)
                .frame(width: .infinity, height: 50)
                .overlay() {
                    HStack {
                        Toggle("Set OnScreen Controls to Handheld", isOn: $onscreenjoy)
                            .padding()
                    }
                }
            Text("You need in Core Settings to set \"use_docked_mode = 0\"")
                .padding(.bottom)
                .font(.footnote)
                .foregroundColor(.gray)
        }
    }
}

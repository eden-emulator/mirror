// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2024 Pomelo, Stossy11
// SPDX-License-Identifier: GPL-3.0-or-later

import SwiftUI

infix operator --: LogicalDisjunctionPrecedence

func --(lhs: Bool, rhs: Bool) -> Bool {
    return lhs || rhs
}

struct ContentView: View {
@State var core = Core(games: [], root: FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0])
    var body: some View {
        HomeView(core: core).onAppear() {
        }
    }
}

@main
struct PomeloApp: App {
    var body: some Scene {
        WindowGroup { ContentView() }
    }
}

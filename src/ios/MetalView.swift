// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2024 Pomelo, Stossy11
// SPDX-License-Identifier: GPL-3.0-or-later

import SwiftUI
import Metal


struct MetalView: UIViewRepresentable {
    let device: MTLDevice?
    let configure: (UIView) -> Void

    func makeUIView(context: Context) -> EmulationScreenView {
        let view = EmulationScreenView()
        configure(view.primaryScreen)
        return view
    }

    func updateUIView(_ uiView: EmulationScreenView, context: Context) {
        //
    }
}

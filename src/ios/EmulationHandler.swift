// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2024 Pomelo, Stossy11
// SPDX-License-Identifier: GPL-3.0-or-later

import SwiftUI
import AppUI
import Metal
import Foundation

class EmulationViewModel: ObservableObject {
    @Published var isShowingCustomButton = true
    @State var should = false
    var device: MTLDevice?
    @State var mtkView: MTKView = MTKView()
    var CaLayer: CAMetalLayer?
    private var sudachiGame: EmulationGame?
    private let appui = AppUI.shared
    private var thread: Thread!
    private var isRunning = false
    var doesneedresources = false
    @State var iscustom: Bool = false

    init(game: EmulationGame?) {
        self.device = MTLCreateSystemDefaultDevice()
        self.sudachiGame = game
    }

    func configureAppUI(with mtkView: MTKView) {
        self.mtkView = mtkView
        device = self.mtkView.device
        guard !isRunning else { return }
        isRunning = true
        appui.configure(layer: mtkView.layer as! CAMetalLayer, with: mtkView.frame.size)

        iscustom = ((sudachiGame?.fileURL.startAccessingSecurityScopedResource()) != nil)

        DispatchQueue.global(qos: .userInitiated).async { [self] in
            if let sudachiGame = self.sudachiGame {
                self.appui.insert(game: sudachiGame.fileURL)
            } else {
                self.appui.bootOS()
            }
        }

        thread = .init(block: self.step)
        thread.name = "Yuzu"
        thread.qualityOfService = .userInteractive
        thread.threadPriority = 0.9
        thread.start()
    }

    private func step() {
        while true {
            appui.step()
        }
    }

    func customButtonTapped() {
        stopEmulation()
    }

    private func stopEmulation() {
        if isRunning {
            isRunning = false
            appui.exit()
            thread.cancel()
            if iscustom {
                sudachiGame?.fileURL.stopAccessingSecurityScopedResource()
            }
        }
    }

    func handleOrientationChange() {
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            let interfaceOrientation = self.getInterfaceOrientation(from: UIDevice.current.orientation)
            self.appui.orientationChanged(orientation: interfaceOrientation, with: self.mtkView.layer as! CAMetalLayer, size: mtkView.frame.size)
        }
    }

    private func getInterfaceOrientation(from deviceOrientation: UIDeviceOrientation) -> UIInterfaceOrientation {
        switch deviceOrientation {
        case .portrait:
            return .portrait
        case .portraitUpsideDown:
            return .portraitUpsideDown
        case .landscapeLeft:
            return .landscapeRight
        case .landscapeRight:
            return .landscapeLeft
        default:
            return .unknown
        }
    }
}

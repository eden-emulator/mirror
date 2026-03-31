// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2024 Pomelo, Stossy11
// SPDX-License-Identifier: GPL-3.0-or-later

import SwiftUI

import Foundation
import GameController
import UIKit
import SwiftUIIntrospect

struct EmulationView: View {
    @StateObject private var viewModel: EmulationViewModel
    @State var controllerconnected = false
    @State var appui = AppUI.shared
    var device: MTLDevice? = MTLCreateSystemDefaultDevice()
    @State var CaLayer: CAMetalLayer?
    @State var ShowPopup: Bool = false
    @State var mtkview: MTKView?
    @State private var thread: Thread!
    @State var uiTabBarController: UITabBarController?
    @State private var isFirstFrameShown = false
    @State private var timer: Timer?
    @Environment(\.scenePhase) var scenePhase

    init(game: EmulationGame?) {
        _viewModel = StateObject(wrappedValue: EmulationViewModel(game: game))
    }

    var body: some View {
        ZStack {
            MetalView(device: device) { view in
                DispatchQueue.main.async {
                    if let metalView = view as? MTKView {
                        mtkview = metalView
                        viewModel.configureAppUI(with: metalView)
                    } else {
                        print("Error: view is not of type MTKView")
                    }
                }
            }
            .onRotate { size in
                viewModel.handleOrientationChange()
            }
            ControllerView()
        }
        .overlay(
            // Loading screen overlay on top of MetalView
            Group {
                if !isFirstFrameShown {
                    LoadingView()
                }
            }
            .transition(.opacity)
        )
        .onAppear {
            UIApplication.shared.isIdleTimerDisabled = true
            startPollingFirstFrameShowed()
        }
        .onDisappear {
            stopPollingFirstFrameShowed()
            uiTabBarController?.tabBar.isHidden = false
            viewModel.customButtonTapped()
        }
        .navigationBarBackButtonHidden(true)
        .introspect(.tabView, on: .iOS(.v13, .v14, .v15, .v16, .v17)) { (tabBarController) in
            tabBarController.tabBar.isHidden = true
            uiTabBarController = tabBarController
        }
    }

    private func startPollingFirstFrameShowed() {
        timer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { _ in
            if appui.FirstFrameShowed() {
                withAnimation {
                    isFirstFrameShown = true
                }
                stopPollingFirstFrameShowed()
            }
        }
    }

    private func stopPollingFirstFrameShowed() {
        timer?.invalidate()
        timer = nil
        print("Timer Invalidated")
    }
}

struct LoadingView: View {
    var body: some View {
        VStack {
            ProgressView("Loading...")
                // .font(.system(size: 90))
                .progressViewStyle(CircularProgressViewStyle())
                .padding()
            Text("Please wait while the game loads.")
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color.black.opacity(0.8))
        .foregroundColor(.white)
    }
}

extension View {
    func onRotate(perform action: @escaping (CGSize) -> Void) -> some View {
        self.modifier(DeviceRotationModifier(action: action))
    }
}

struct DeviceRotationModifier: ViewModifier {
    let action: (CGSize) -> Void
    @State var startedfirst: Bool = false

    func body(content: Content) -> some View { content
        .background(GeometryReader { geometry in
            Color.clear
                .preference(key: SizePreferenceKey.self, value: geometry.size)
        })
        .onPreferenceChange(SizePreferenceKey.self) { newSize in
            if startedfirst {
                action(newSize)
            } else {
                startedfirst = true
            }
        }
    }
}

struct SizePreferenceKey: PreferenceKey {
    static var defaultValue: CGSize = .zero

    static func reduce(value: inout CGSize, nextValue: () -> CGSize) {
        value = nextValue()
    }
}

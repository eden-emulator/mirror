// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2024 Pomelo, Stossy11
// SPDX-License-Identifier: GPL-3.0-or-later

import SwiftUI

import Foundation
import GameController
import UIKit
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

struct ControllerView: View {
    let appui = AppUI.shared
    @State var isPressed = false
    @State var controllerconnected = false
    @State private var x: CGFloat = 0.0
    @State private var y: CGFloat = 0.0
    @Environment(\.presentationMode) var presentationMode

    var body: some View {
        GeometryReader { geometry in
            ZStack {
                if !controllerconnected {
                    OnScreenController(geometry: geometry) // i did this to clean it up as it was quite long lmfao
                }
            }
        }
        .onAppear {
            print("checking for controller:")
            controllerconnected = false
            DispatchQueue.main.async {
                setupControllers() // i dont know what half of this shit does
            }
        }
    }

    // Add a dictionary to track controller IDs
    @State var controllerIDs: [GCController: Int] = [:]

    private func setupControllers() {
        NotificationCenter.default.addObserver(forName: .GCControllerDidConnect, object: nil, queue: .main) { notification in
            if let controller = notification.object as? GCController {
                print("wow controller onstart") // yippeeee
                self.setupController(controller)
                self.controllerconnected = true
            } else {
                print("not GCController :((((((") // wahhhhhhh
            }
        }


        NotificationCenter.default.addObserver(forName: .GCControllerDidDisconnect, object: nil, queue: .main) { notification in
            if let controller = notification.object as? GCController {
                print("wow controller gone")
                if self.controllerIDs.isEmpty {
                    controllerconnected = false
                }
                self.controllerIDs.removeValue(forKey: controller) // Remove the controller ID
            }
        }

        GCController.controllers().forEach { controller in
            print("wow controller")
            self.controllerconnected = true
            self.setupController(controller)
        }
    }

    private func setupController(_ controller: GCController) {
        // Assign a unique ID to the controller, max 5 controllers
        if controllerIDs.count < 6, controllerIDs[controller] == nil {
            controllerIDs[controller] = controllerIDs.count
        }
        guard let controllerId = controllerIDs[controller] else { return }
        if let extendedGamepad = controller.extendedGamepad {
            // Handle extended gamepad
            extendedGamepad.dpad.up.pressedChangedHandler = { button, value, pressed in
                pressed ? self.touchDown(.directionalPadUp, controllerId: controllerId) : self.touchUpInside(.directionalPadUp, controllerId: controllerId)
            }
            extendedGamepad.dpad.down.pressedChangedHandler = { button, value, pressed in
                pressed ? self.touchDown(.directionalPadDown, controllerId: controllerId) : self.touchUpInside(.directionalPadDown, controllerId: controllerId)
            }
            extendedGamepad.dpad.left.pressedChangedHandler = { button, value, pressed in
                pressed ? self.touchDown(.directionalPadLeft, controllerId: controllerId) : self.touchUpInside(.directionalPadLeft, controllerId: controllerId)
            }
            extendedGamepad.dpad.right.pressedChangedHandler = { button, value, pressed in
                pressed ? self.touchDown(.directionalPadRight, controllerId: controllerId) : self.touchUpInside(.directionalPadRight, controllerId: controllerId)
            }
            extendedGamepad.buttonOptions?.pressedChangedHandler = { button, value, pressed in
                pressed ? self.touchDown(.minus, controllerId: controllerId) : self.touchUpInside(.minus, controllerId: controllerId)
            }
            extendedGamepad.buttonMenu.pressedChangedHandler = { button, value, pressed in
                pressed ? self.touchDown(.plus, controllerId: controllerId) : self.touchUpInside(.plus, controllerId: controllerId)
            }
            extendedGamepad.buttonA.pressedChangedHandler = { button, value, pressed in
                pressed ? self.touchDown(.A, controllerId: controllerId) : self.touchUpInside(.A, controllerId: controllerId)
            }
            extendedGamepad.buttonB.pressedChangedHandler = { button, value, pressed in
                pressed ? self.touchDown(.B, controllerId: controllerId) : self.touchUpInside(.B, controllerId: controllerId)
            }
            extendedGamepad.buttonX.pressedChangedHandler = { button, value, pressed in
                pressed ? self.touchDown(.X, controllerId: controllerId) : self.touchUpInside(.X, controllerId: controllerId)
            }
            extendedGamepad.buttonY.pressedChangedHandler = { button, value, pressed in
                pressed ? self.touchDown(.Y, controllerId: controllerId) : self.touchUpInside(.Y, controllerId: controllerId)
            }
            extendedGamepad.leftShoulder.pressedChangedHandler = { button, value, pressed in
                pressed ? self.touchDown(.triggerL, controllerId: controllerId) : self.touchUpInside(.L, controllerId: controllerId)
            }
            extendedGamepad.leftTrigger.pressedChangedHandler = { button, value, pressed in
                pressed ? self.touchDown(.triggerZL, controllerId: controllerId) : self.touchUpInside(.triggerZL, controllerId: controllerId)
            }
            extendedGamepad.rightShoulder.pressedChangedHandler = { button, value, pressed in
                pressed ? self.touchDown(.triggerR, controllerId: controllerId) : self.touchUpInside(.triggerR, controllerId: controllerId)
            }
            extendedGamepad.leftThumbstickButton?.pressedChangedHandler = { button, value, pressed in
                pressed ? self.touchDown(.L, controllerId: controllerId) : self.touchUpInside(.triggerR, controllerId: controllerId)
            }
            extendedGamepad.rightThumbstickButton?.pressedChangedHandler = { button, value, pressed in
                pressed ? self.touchDown(.R, controllerId: controllerId) : self.touchUpInside(.triggerR, controllerId: controllerId)
            }
            extendedGamepad.rightTrigger.pressedChangedHandler = { button, value, pressed in
                pressed ? self.touchDown(.triggerZR, controllerId: controllerId) : self.touchUpInside(.triggerZR, controllerId: controllerId)
            }
            extendedGamepad.buttonHome?.pressedChangedHandler = { button, value, pressed in
                if pressed {
                    appui.exit()
                    presentationMode.wrappedValue.dismiss()
                }
            }
            extendedGamepad.leftThumbstick.valueChangedHandler = { dpad, x, y in
                self.appui.thumbstickMoved(analog: .left, x: x, y: y, controllerid: controllerId)
            }
            extendedGamepad.rightThumbstick.valueChangedHandler = { dpad, x, y in
                self.appui.thumbstickMoved(analog: .right, x: x, y: y, controllerid: controllerId)
            }
            if let motion = controller.motion {
                var lastTimestamp = Date().timeIntervalSince1970 // Initialize timestamp when motion starts
                motion.valueChangedHandler = { motion in
                    // Get current time
                    let currentTimestamp = Date().timeIntervalSince1970

                    // Update last timestamp
                    lastTimestamp = currentTimestamp

                    // Get gyroscope data
                    let gyroX = motion.rotationRate.x
                    let gyroY = motion.rotationRate.y
                    let gyroZ = motion.rotationRate.z

                    // Get accelerometer data
                    let accelX = motion.gravity.x + motion.userAcceleration.x
                    let accelY = motion.gravity.y + motion.userAcceleration.y
                    let accelZ = motion.gravity.z + motion.userAcceleration.z

                    print("\(gyroX), \(gyroY), \(gyroZ), \(accelX), \(accelY), \(accelZ)")

                    // Call your gyroMoved function with the motion data
                    appui.gyroMoved(x: Float(gyroX), y: Float(gyroY), z: Float(gyroZ), accelX: Float(accelX), accelY: Float(accelY), accelZ: Float(accelZ), controllerId: Int32(controllerId), deltaTimestamp: Int32(lastTimestamp))
                }
            }
        } else if let microGamepad = controller.microGamepad {
            // Handle micro gamepad
            microGamepad.dpad.up.pressedChangedHandler = { button, value, pressed in
                pressed ? self.touchDown(.directionalPadUp, controllerId: controllerId) : self.touchUpInside(.directionalPadUp, controllerId: controllerId)
            }
            microGamepad.dpad.down.pressedChangedHandler = { button, value, pressed in
                pressed ? self.touchDown(.directionalPadDown, controllerId: controllerId) : self.touchUpInside(.directionalPadDown, controllerId: controllerId)
            }
            microGamepad.dpad.left.pressedChangedHandler = { button, value, pressed in
                pressed ? self.touchDown(.directionalPadLeft, controllerId: controllerId) : self.touchUpInside(.directionalPadLeft, controllerId: controllerId)
            }
            microGamepad.dpad.right.pressedChangedHandler = { button, value, pressed in
                pressed ? self.touchDown(.directionalPadRight, controllerId: controllerId) : self.touchUpInside(.directionalPadRight, controllerId: controllerId)
            }
            microGamepad.buttonA.pressedChangedHandler = { button, value, pressed in
                pressed ? self.touchDown(.A, controllerId: controllerId) : self.touchUpInside(.A, controllerId: controllerId)
            }
            microGamepad.buttonX.pressedChangedHandler = { button, value, pressed in
                pressed ? self.touchDown(.X, controllerId: controllerId) : self.touchUpInside(.X, controllerId: controllerId)
            }
        }
    }

    private func touchDown(_ button: VirtualControllerButtonType, controllerId: Int) {
        appui.virtualControllerButtonDown(button: button, controllerid: controllerId)    }

    private func touchUpInside(_ button: VirtualControllerButtonType, controllerId: Int) {
        appui.virtualControllerButtonUp(button: button, controllerid: controllerId)
    }
}

struct OnScreenController: View {
    @State var geometry: GeometryProxy
    var body: some View {
        if geometry.size.height > geometry.size.width && UIDevice.current.userInterfaceIdiom != .pad {
            // portrait
            VStack {
                Spacer()
                VStack {
                    HStack {
                        VStack {
                            ShoulderButtonsViewLeft()
                            ZStack {
                                Joystick()
                                DPadView()
                            }
                        }
                        .padding()
                        VStack {
                            ShoulderButtonsViewRight()
                            ZStack {
                                Joystick(iscool: true) // hope this works
                                ABXYView()
                            }
                        }
                        .padding()
                    }
                    HStack {
                        ButtonView(button: .plus).padding(.horizontal, 40)
                        ButtonView(button: .minus).padding(.horizontal, 40)
                    }
                }
                .padding(.bottom, geometry.size.height / 3.2) // very broken
            }
        } else {
            // could be landscape
            VStack {
                HStack {
                    Spacer()
                    ButtonView(button: .home)
                        .padding(.horizontal)
                }
                Spacer()
                VStack {
                    HStack {

                        // gotta fuckin add + and - now
                        VStack {
                            ShoulderButtonsViewLeft()
                            ZStack {
                                Joystick()
                                DPadView()
                            }
                        }
                        HStack {
                            Spacer()
                            VStack {
                                Spacer()
                                ButtonView(button: .plus) // Adding the + button
                            }
                            VStack {
                                Spacer()
                                ButtonView(button: .minus) // Adding the - button
                            }
                            Spacer()
                        }
                        VStack {
                            ShoulderButtonsViewRight()
                            ZStack {
                                Joystick(iscool: true) // hope this work s
                                ABXYView()
                            }
                        }
                    }
                }
                .padding(.bottom, geometry.size.height / 11) // also extremally broken (
            }
        }
    }
}

struct ShoulderButtonsViewLeft: View {
    var body: some View {
        HStack {
            ButtonView(button: .triggerZL).padding(.horizontal)
            ButtonView(button: .triggerL).padding(.horizontal)
        }
        .frame(width: 160, height: 20)
    }
}

struct ShoulderButtonsViewRight: View {
    var body: some View {
        HStack {
            ButtonView(button: .triggerR).padding(.horizontal)
            ButtonView(button: .triggerZR).padding(.horizontal)
        }
        .frame(width: 160, height: 20)

    }
}

struct DPadView: View {
    var body: some View {
        VStack {
            ButtonView(button: .directionalPadUp)
            HStack {
                ButtonView(button: .directionalPadLeft)
                Spacer(minLength: 20)
                ButtonView(button: .directionalPadRight)
            }
            ButtonView(button: .directionalPadDown).padding(.horizontal)
        }
        .frame(width: 145, height: 145)
    }
}

struct ABXYView: View {
    var body: some View {
        VStack {
            ButtonView(button: .X)
            HStack {
                ButtonView(button: .Y)
                Spacer(minLength: 20)
                ButtonView(button: .A)
            }
            ButtonView(button: .B).padding(.horizontal)
        }
        .frame(width: 145, height: 145)
    }
}

class Haptics {
    static let shared = Haptics()
    private init() { }
    func play(_ feedbackStyle: UIImpactFeedbackGenerator.FeedbackStyle) {
        print("haptics")
        UIImpactFeedbackGenerator(style: feedbackStyle).impactOccurred()
    }
    func notify(_ feedbackType: UINotificationFeedbackGenerator.FeedbackType) {
        UINotificationFeedbackGenerator().notificationOccurred(feedbackType)
    }
}

struct ButtonView: View {
    var button: VirtualControllerButtonType
    @StateObject private var viewModel: EmulationViewModel = EmulationViewModel(game: nil)
    let appui = AppUI.shared
    @State var mtkView: MTKView?
    @State var width: CGFloat = 45
    @State var height: CGFloat = 45
    @State var isPressed = false
    var id: Int {
        if onscreenjoy {
            return 8
        }
        return 0
    }
    @AppStorage("onscreenhandheld") var onscreenjoy: Bool = false
    @Environment(\.colorScheme) var colorScheme
    @Environment(\.presentationMode) var presentationMode

    var body: some View {
        Image(systemName: buttonText)
            .resizable()
            .frame(width: width, height: height)
            .foregroundColor(colorScheme == .dark ? Color.gray : Color.gray)
            .opacity(isPressed ? 0.5 : 1)
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { _ in
                        if !self.isPressed {
                            self.isPressed = true
                            DispatchQueue.main.async {
                                if button == .home {
                                    presentationMode.wrappedValue.dismiss()
                                    appui.exit()
                                } else {
                                    appui.virtualControllerButtonDown(button: button, controllerid: id)
                                    Haptics.shared.play(.heavy)
                                }
                            }
                        }
                    }
                    .onEnded { _ in
                        self.isPressed = false
                        DispatchQueue.main.async {
                            if button != .home {
                                appui.virtualControllerButtonUp(button: button, controllerid: id)
                            }
                        }
                    }
            )
            .onAppear() {
                if button == .triggerL || button == .triggerZL || button == .triggerZR || button == .triggerR {
                    width = 65
                }


                if button == .minus || button == .plus || button == .home {
                    width = 35
                    height = 35
                }
            }
    }

    private var buttonText: String {
        switch button {
        case .A: return "a.circle.fill"
        case .B: return "b.circle.fill"
        case .X: return "x.circle.fill"
        case .Y: return "y.circle.fill"
        case .directionalPadUp: return "arrowtriangle.up.circle.fill"
        case .directionalPadDown: return "arrowtriangle.down.circle.fill"
        case .directionalPadLeft: return "arrowtriangle.left.circle.fill"
        case .directionalPadRight: return "arrowtriangle.right.circle.fill"
        case .triggerZL: return"zl.rectangle.roundedtop.fill"
        case .triggerZR: return "zr.rectangle.roundedtop.fill"
        case .triggerL: return "l.rectangle.roundedbottom.fill"
        case .triggerR: return "r.rectangle.roundedbottom.fill"
        case .plus: return "plus.circle.fill"
        case .minus: return "minus.circle.fill"
        case .home: return "house.circle.fill"
        default: return ""
        }
    }
}

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
        // .introspect(.tabView, on: .iOS(.v13, .v14, .v15, .v16, .v17)) { (tabBarController) in
        //     tabBarController.tabBar.isHidden = true
        //     uiTabBarController = tabBarController
        // }
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

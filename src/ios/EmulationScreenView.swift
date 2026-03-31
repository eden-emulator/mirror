// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2024 Pomelo, Stossy11
// SPDX-License-Identifier: GPL-3.0-or-later

import SwiftUI

import MetalKit

class EmulationScreenView: UIView {
    var primaryScreen: UIView!
    var portraitconstraints = [NSLayoutConstraint]()
    var landscapeconstraints = [NSLayoutConstraint]()
    var fullscreenconstraints = [NSLayoutConstraint]()
    let appui = AppUI.shared
    let userDefaults = UserDefaults.standard

    override init(frame: CGRect) {
        super.init(frame: frame)
        if UIDevice.current.userInterfaceIdiom == .pad {
            setupAppUIScreenforiPad()
        } else {
            setupAppUIScreen()
        }
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        if UIDevice.current.userInterfaceIdiom == .pad {
            setupAppUIScreenforiPad()
        } else {
            setupAppUIScreen()
        }

    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        super.touchesBegan(touches, with: event)
        guard let touch = touches.first else {
            return
        }

        print("Location: \(touch.location(in: primaryScreen))")
        appui.touchBegan(at: touch.location(in: primaryScreen), for: 0)
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        super.touchesEnded(touches, with: event)
        print("Touch Ended")
        appui.touchEnded(for: 0)
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        super.touchesMoved(touches, with: event)
        guard let touch = touches.first else {
            return
        }
        let location = touch.location(in: primaryScreen)
        print("Location Moved: \(location)")
        appui.touchMoved(at: location, for: 0)
    }

    func setupAppUIScreenforiPad() {
        primaryScreen = MTKView(frame: .zero, device: MTLCreateSystemDefaultDevice())
        primaryScreen.translatesAutoresizingMaskIntoConstraints = false
        primaryScreen.clipsToBounds = true
        primaryScreen.layer.borderColor = UIColor.secondarySystemBackground.cgColor
        primaryScreen.layer.borderWidth = 3
        primaryScreen.layer.cornerCurve = .continuous
        primaryScreen.layer.cornerRadius = 10
        addSubview(primaryScreen)


        portraitconstraints = [
            primaryScreen.topAnchor.constraint(equalTo: safeAreaLayoutGuide.topAnchor, constant: 10),
            primaryScreen.leadingAnchor.constraint(equalTo: safeAreaLayoutGuide.leadingAnchor, constant: 10),
            primaryScreen.trailingAnchor.constraint(equalTo: safeAreaLayoutGuide.trailingAnchor, constant: -10),
            primaryScreen.heightAnchor.constraint(equalTo: primaryScreen.widthAnchor, multiplier: 9 / 16),
        ]

        landscapeconstraints = [
            primaryScreen.topAnchor.constraint(equalTo: safeAreaLayoutGuide.topAnchor, constant: 50),
            primaryScreen.bottomAnchor.constraint(equalTo: safeAreaLayoutGuide.bottomAnchor, constant: -100),
            primaryScreen.widthAnchor.constraint(equalTo: primaryScreen.heightAnchor, multiplier: 16 / 9),
            primaryScreen.centerXAnchor.constraint(equalTo: safeAreaLayoutGuide.centerXAnchor),
        ]


        updateConstraintsForOrientation()
    }



    func setupAppUIScreen() {
        primaryScreen = MTKView(frame: .zero, device: MTLCreateSystemDefaultDevice())
        primaryScreen.translatesAutoresizingMaskIntoConstraints = false
        primaryScreen.clipsToBounds = true
        primaryScreen.layer.borderColor = UIColor.secondarySystemBackground.cgColor
        primaryScreen.layer.borderWidth = 3
        primaryScreen.layer.cornerCurve = .continuous
        primaryScreen.layer.cornerRadius = 10
        addSubview(primaryScreen)


        portraitconstraints = [
            primaryScreen.topAnchor.constraint(equalTo: safeAreaLayoutGuide.topAnchor, constant: 10),
            primaryScreen.leadingAnchor.constraint(equalTo: safeAreaLayoutGuide.leadingAnchor, constant: 10),
            primaryScreen.trailingAnchor.constraint(equalTo: safeAreaLayoutGuide.trailingAnchor, constant: -10),
            primaryScreen.heightAnchor.constraint(equalTo: primaryScreen.widthAnchor, multiplier: 9 / 16),
        ]

        landscapeconstraints = [
            primaryScreen.topAnchor.constraint(equalTo: safeAreaLayoutGuide.topAnchor, constant: 10),
            primaryScreen.bottomAnchor.constraint(equalTo: safeAreaLayoutGuide.bottomAnchor, constant: -10),
            primaryScreen.widthAnchor.constraint(equalTo: primaryScreen.heightAnchor, multiplier: 16 / 9),
            primaryScreen.centerXAnchor.constraint(equalTo: safeAreaLayoutGuide.centerXAnchor),
        ]

        updateConstraintsForOrientation()
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        updateConstraintsForOrientation()
    }

    private func updateConstraintsForOrientation() {
        removeConstraints(portraitconstraints)
        removeConstraints(landscapeconstraints)
        let isPortrait = UIApplication.shared.statusBarOrientation.isPortrait
        addConstraints(isPortrait ? portraitconstraints : landscapeconstraints)
    }
}

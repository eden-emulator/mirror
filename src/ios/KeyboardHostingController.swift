// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2024 Pomelo, Stossy11
// SPDX-License-Identifier: GPL-3.0-or-later

import SwiftUI
import UIKit

class KeyboardHostingController<Content: View>: UIHostingController<Content> {
    override var canBecomeFirstResponder: Bool {
        return true
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        becomeFirstResponder() // Make sure the view can become the first responder
    }

    override var keyCommands: [UIKeyCommand]? {
        return [
            UIKeyCommand(input: UIKeyInputUpArrow, modifierFlags: [], action: #selector(handleKeyCommand)),
            UIKeyCommand(input: UIKeyInputDownArrow, modifierFlags: [], action: #selector(handleKeyCommand)),
            UIKeyCommand(input: UIKeyInputLeftArrow, modifierFlags: [], action: #selector(handleKeyCommand)),
            UIKeyCommand(input: UIKeyInputRightArrow, modifierFlags: [], action: #selector(handleKeyCommand)),
            UIKeyCommand(input: "w", modifierFlags: [], action: #selector(handleKeyCommand)),
            UIKeyCommand(input: "s", modifierFlags: [], action: #selector(handleKeyCommand)),
            UIKeyCommand(input: "a", modifierFlags: [], action: #selector(handleKeyCommand)),
            UIKeyCommand(input: "d", modifierFlags: [], action: #selector(handleKeyCommand))
        ]
    }

    @objc func handleKeyCommand(_ sender: UIKeyCommand) {
        if let input = sender.input {
            switch input {
            case UIKeyInputUpArrow:
                print("Up Arrow Pressed")
            case UIKeyInputDownArrow:
                print("Down Arrow Pressed")
            case UIKeyInputLeftArrow:
                print("Left Arrow Pressed")
            case UIKeyInputRightArrow:
                print("Right Arrow Pressed")
            case "w":
                print("W Key Pressed")
            case "s":
                print("S Key Pressed")
            case "a":
                print("A Key Pressed")
            case "d":
                print("D Key Pressed")
            default:
                break
            }
        }
    }
}

struct KeyboardSupportView: UIViewControllerRepresentable {
    let content: Text

    func makeUIViewController(context: Context) -> KeyboardHostingController<Text> {
        return KeyboardHostingController(rootView: content)
    }

    func updateUIViewController(_ uiViewController: KeyboardHostingController<Text>, context: Context) {
        // Handle any updates needed
    }
}

struct KeyboardView: View {
    var body: some View {
        KeyboardSupportView(content: Text(""))
    }
}

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2024 Pomelo, Stossy11
// SPDX-License-Identifier: GPL-3.0-or-later

import SwiftUI
import Foundation
import UIKit

infix operator --: LogicalDisjunctionPrecedence

func --(lhs: Bool, rhs: Bool) -> Bool {
    return lhs || rhs
}

class YuzuFileManager {
    static var shared = YuzuFileManager()
    func directories() -> [String : [String : String]] {
        [
            "themes" : [:],
            "amiibo" : [:],
            "cache" : [:],
            "config" : [:],
            "crash_dumps" : [:],
            "dump" : [:],
            "keys" : [:],
            "load" : [:],
            "log" : [:],
            "nand" : [:],
            "play_time" : [:],
            "roms" : [:],
            "screenshots" : [:],
            "sdmc" : [:],
            "shader" : [:],
            "tas" : [:],
            "icons" : [:]
        ]
    }

    func createdirectories() throws {
        let documentdir = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        try directories().forEach() { directory, filename in
            let directoryURL = documentdir.appendingPathComponent(directory)

            if !FileManager.default.fileExists(atPath: directoryURL.path) {
                print("creating dir at \(directoryURL.path)") // yippee
                try FileManager.default.createDirectory(at: directoryURL, withIntermediateDirectories: false, attributes: nil)
            }
        }
    }
}

struct ContentView: View {
@State var core = Core(games: [], root: FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0])
    var body: some View {
        HomeView(core: core).onAppear() {
            do {
                try YuzuFileManager.shared.createdirectories() // this took a while to create the proper directories
                do {
                    core = try LibraryManager.shared.library() // this shit is like you tried to throw a egg into a blender with no lid on
                } catch {
                    print("Failed to fetch library: \(error)") // aaaaaaaaa
                }
            } catch {
                print("Failed to create directories: \(error)") // i wonder why hmmmmmmm
                return
            }
        }
    }
}

@main
struct PomeloApp: App {
    var body: some Scene {
        WindowGroup { ContentView() }
    }
}

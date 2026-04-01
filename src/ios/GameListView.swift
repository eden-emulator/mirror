// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2024 Pomelo, Stossy11
// SPDX-License-Identifier: GPL-3.0-or-later

import SwiftUI
import Foundation
import UIKit
import UniformTypeIdentifiers


struct GameButtonListView: View {
    var game: EmulationGame
    @Environment(\.colorScheme) var colorScheme

    var body: some View {
        HStack(spacing: 15) {
            if let image = UIImage(data: game.imageData) {
                Image(uiImage: image)
                    .resizable()
                    .frame(width: 60, height: 60)
                    .cornerRadius(8)
            } else {
                Image(systemName: "photo")
                    .resizable()
                    .frame(width: 60, height: 60)
                    .cornerRadius(8)
            }

            VStack(alignment: .leading, spacing: 4) {
                Text(game.title)
                    .font(.headline)
                    .foregroundColor(colorScheme == .dark ? Color.white : Color.black)
                Text(game.developer)
                    .font(.subheadline)
                    .foregroundColor(.gray)
            }
            Spacer()
        }
        .padding(.vertical, 8)
    }
}

struct GameListView: View {
    @State var core: Core
    @State private var searchText = ""
    @State var game: Int = 1
    @State var startgame: Bool = false
    @Binding var isGridView: Bool
    @State var showAlert = false
    @State var alertMessage: Alert? = nil

    var body: some View {
        let filteredGames = core.games.filter { game in
            guard let EmulationGame = game as? EmulationGame else { return false }
            return searchText.isEmpty || EmulationGame.title.localizedCaseInsensitiveContains(searchText)
        }
        ScrollView {
            VStack {
                VStack(alignment: .leading) {

                    if isGridView {
                        LazyVGrid(columns: [GridItem(.adaptive(minimum: 160))], spacing: 10) {
                            ForEach(0..<filteredGames.count, id: \.self) { index in
                                let game = filteredGames[index] // Use filteredGames here
                                NavigationLink(destination: EmulationView(game: game).toolbar(.hidden, for: .tabBar)) {
                                    // GameButtonView(game: game)
                                        // .frame(maxWidth: .infinity, minHeight: 200)
                                }
                                .contextMenu {
                                    Button(action: {
                                        do {
                                            try LibraryManager.shared.removerom(filteredGames[index])
                                        } catch {
                                            showAlert = true
                                            alertMessage = Alert(title: Text("Unable to Remove Game"), message: Text(error.localizedDescription))
                                        }
                                    }) {
                                        Text("Remove")
                                    }
                                    Button(action: {
                                        if let documentsURL = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first?.appending(path: "roms") {
                                            UIApplication.shared.open(documentsURL, options: [:], completionHandler: nil)
                                        }
                                    }) {
                                        if ProcessInfo.processInfo.isMacCatalystApp {
                                            Text("Open in Finder")
                                        } else {
                                            Text("Open in Files")
                                        }
                                    }
                                    NavigationLink(destination: EmulationView(game: game).toolbar(.hidden, for: .tabBar)) {
                                        Text("Launch")
                                    }
                                }
                            }
                        }
                    } else {
                        LazyVStack() {
                            ForEach(0..<filteredGames.count, id: \.self) { index in
                                let game = filteredGames[index] // Use filteredGames here
                                NavigationLink(destination: EmulationView(game: game).toolbar(.hidden, for: .tabBar)) {
                                    GameButtonListView(game: game)
                                        .frame(maxWidth: .infinity, minHeight: 75)
                                }
                                .contextMenu {
                                    Button(action: {
                                        do {
                                            try LibraryManager.shared.removerom(filteredGames[index])
                                            try FileManager.default.removeItem(atPath: game.fileURL.path)
                                        } catch {
                                            showAlert = true
                                            alertMessage = Alert(title: Text("Unable to Remove Game"), message: Text(error.localizedDescription))
                                        }
                                    }) {
                                        Text("Remove")
                                    }

                                    Button(action: {
                                        if let documentsURL = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first?.appending(path: "roms") {
                                            UIApplication.shared.open(documentsURL, options: [:], completionHandler: nil)
                                        }
                                    }) {
                                        if ProcessInfo.processInfo.isMacCatalystApp {
                                            Text("Open in Finder")
                                        } else {
                                            Text("Open in Files")
                                        }
                                    }
                                    NavigationLink(destination: EmulationView(game: game).toolbar(.hidden, for: .tabBar)) {
                                        Text("Launch")
                                    }
                                }
                            }
                        }
                    }
                }
                .searchable(text: $searchText)
                .padding()
            }
            .onAppear {
                refreshcore()

                if let documentsDirectory = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first {
                    let romsFolderURL = documentsDirectory.appendingPathComponent("roms")

                    let folderMonitor = FolderMonitor(folderURL: romsFolderURL) {
                        do {
                            core = Core(games: [], root: documentsDirectory)
                            core = try LibraryManager.shared.library()
                        } catch {
                            print("Error refreshing core: \(error)")
                        }
                    }
                }
            }
            .alert(isPresented: $showAlert) {
                alertMessage ?? Alert(title: Text("Error Not Found"))
            }
        }
    }

    func refreshcore() {
        do {
            core = try LibraryManager.shared.library()
        } catch {
            print("Failed to fetch library: \(error)")
            return
        }
    }
}

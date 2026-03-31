// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2024 Pomelo, Stossy11
// SPDX-License-Identifier: GPL-3.0-or-later

import SwiftUI
import Foundation
import UIKit
import UniformTypeIdentifiers
import Combine

struct GameIconView: View {
    var game: EmulationGame
    @Binding var selectedGame: EmulationGame?
    @State var startgame: Bool = false
    @State var timesTapped: Int = 0

    var isSelected: Bool {
        selectedGame == game
    }

    var body: some View {
        NavigationLink(
            destination: EmulationView(game: game).toolbar(.hidden, for: .tabBar),
            isActive: $startgame,
            label: {
                EmptyView()
            }
        )
        VStack(spacing: 5) {
            if isSelected {
                Text(game.title)
                    .foregroundColor(.blue)
                    .font(.title2)
            }
            if let uiImage = UIImage(data: game.imageData) {
                Image(uiImage: uiImage)
                    .resizable()
                    .scaledToFit()
                    .frame(width: isSelected ? 200 : 180, height: isSelected ? 200 : 180)
                    .cornerRadius(10)
                    .overlay(
                        isSelected ? RoundedRectangle(cornerRadius: 10)
                            .stroke(Color.blue, lineWidth: 5)
                            : nil
                    )
                    .onTapGesture {
                        if isSelected {
                            startgame = true
                            print(isSelected)
                        }
                        if !isSelected {
                            selectedGame = game
                        }
                    }
            } else {
                Image(systemName: "questionmark")
                    .resizable()
                    .scaledToFit()
                    .frame(width: 200, height: 200)
                    .cornerRadius(10)
                    .onTapGesture { selectedGame = game }
            }
        }
        .frame(width: 200, height: 250)
    }
}

struct BottomMenuView: View {
    @State var core: Core
    var body: some View {
        HStack(spacing: 40) {
            Button {

            } label: {
                Circle()
                    .overlay {
                        Image(systemName: "message").font(.system(size: 30)).foregroundColor(.red)
                    }
                    .frame(width: 50, height: 50)
                    .foregroundColor(Color.init(uiColor: .lightGray))
            }
            Button {

            } label: {
                Circle()
                    .overlay {
                        Image(systemName: "photo").font(.system(size: 30)).foregroundColor(.blue)
                    }
                    .frame(width: 50, height: 50)
                    .foregroundColor(Color.init(uiColor: .lightGray))
            }
            NavigationLink(destination: SettingsView(core: core)) {
                Circle()
                    .overlay {
                        Image(systemName: "gearshape").foregroundColor(Color.init(uiColor: .darkGray)).font(.system(size: 30))
                    }
                    .frame(width: 50, height: 50)
                    .foregroundColor(Color.init(uiColor: .lightGray))
            }

            Button {

            } label: {
                Circle()
                    .overlay {
                        Image(systemName: "power").foregroundColor(Color.init(uiColor: .darkGray)).font(.system(size: 30))
                    }
                    .frame(width: 50, height: 50)
                    .foregroundColor(Color.init(uiColor: .lightGray))
            }
        }
        .padding(.bottom, 20)
    }
}

struct HomeView: View {
    @State private var selectedGame: EmulationGame? = nil

    @State var core: Core

    init(selectedGame: EmulationGame? = nil, core: Core) {
        _core = State(wrappedValue: core)
        self.selectedGame = selectedGame
        refreshcore()
    }

    var body: some View {
        NavigationStack {
            GeometryReader { geometry in
                VStack {
                    GameCarouselView(core: core, selectedGame: $selectedGame)
                    Spacer()
                    BottomMenuView(core: core)
                }
            }
        }
        .background(Color.gray.opacity(0.1))
        .edgesIgnoringSafeArea(.all)
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
    }

    func refreshcore() {
        print("Loading library...")
        do {
            core = try LibraryManager.shared.library()
            print(core.games)
        } catch {
            print("Failed to fetch library: \(error)")
            return
        }
    }
}


struct GameCarouselView: View {
    // let games: [EmulationGame]
    @State var core: Core
    @Binding var selectedGame: EmulationGame?
    var body: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 20) {
                ForEach(core.games) { game in
                    GameIconView(game: game, selectedGame: $selectedGame)
                }
            }
        }
    }
}

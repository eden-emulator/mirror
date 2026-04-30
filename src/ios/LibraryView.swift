// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

import SwiftUI
import Foundation

struct LibraryView: View {
    @State private var selectedGame: EmulationGame? = nil
    @Binding var urlgame: EmulationGame?
    @ObservedObject var core: EmulationViewModel

    var binding: Binding<Bool> {
        Binding(
            get: { urlgame != nil },
            set: { newValue in
                if !newValue {
                    urlgame = nil
                }
            }
        )
    }

    var body: some View {
        NavigationView {
            GeometryReader { geometry in
                VStack {
                    TopBarView()

                    if UIDevice.current.userInterfaceIdiom == .pad {
                        Spacer()
                    }

                    ScrollView {
                        LazyVGrid(columns: [
                            GridItem(.adaptive(minimum: 140), spacing: 10)
                        ], spacing: 10) {
                            ForEach(core.games, id: \EmulationGame.id) { game in
                                GameCardView(game: game)
                                    .frame(width: 140, height: 160)
                                    .onTapGesture {
                                        selectedGame = game
                                        urlgame = game
                                    }
                            }
                        }
                        .padding()
                    }

                    Spacer()

                    BottomMenuView(core: core)
                }

                NavigationLink(
                    destination: EmulationView(game: urlgame),
                    isActive: binding,
                    label: {
                        EmptyView()
                    }
                )
                .hidden()
            }
        }
        .background(Color.gray.opacity(0.1))
        .edgesIgnoringSafeArea(.all)
        .onAppear {
        }
    }
}

struct GameCardView: View {
    let game: EmulationGame

    var body: some View {
        VStack {
            Rectangle()
                .fill(Color.gray)
                .frame(height: 120)

            Text(game.title)
                .font(.caption)
                .lineLimit(1)
                .truncationMode(.tail)
        }
        .background(Color.white)
        .cornerRadius(8)
        .shadow(radius: 4)
    }
}
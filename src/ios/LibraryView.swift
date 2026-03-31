// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2024 Pomelo, Stossy11
// SPDX-License-Identifier: GPL-3.0-or-later

import SwiftUI
import CryptoKit


struct LibraryView: View {
    @Binding var core: Core
    @State var isGridView: Bool = true
    @State var doesitexist = (false, false)
    @State var importedgame: EmulationGame? = nil
    @State var importgame: Bool = false
    @State var isimportingfirm: Bool = false
    @State var launchGame: Bool = false
    var body: some View {
        NavigationStack {
            if let importedgame = importedgame {
                NavigationLink(
                    isActive: $launchGame,
                    destination: {
                        EmulationView(game: importedgame).toolbar(.hidden, for: .tabBar)
                    },
                    label: {
                        EmptyView() // This keeps the link hidden
                    }
                )
            }

            VStack {
                if doesitexist.0, doesitexist.1 {
                    HomeView(core: core)
                } else {
                    let (doesKeyExist, doesProdExist) = doeskeysexist()
                    ScrollView {
                        Text("You Are Missing These Files:")
                            .font(.headline)
                            .foregroundColor(.red)
                        HStack {
                            if !doesProdExist {
                                Text("Prod.keys")
                                    .font(.subheadline)
                                    .foregroundColor(.red)
                            }
                            if !doesKeyExist {
                                Text("Title.keys")
                                    .font(.subheadline)
                                    .foregroundColor(.red)
                            }
                        }
                        Text("These goes into the Keys folder")
                            .font(.caption)
                            .foregroundColor(.red)
                            .padding(.bottom)

                        if !LibraryManager.shared.homebrewroms().isEmpty {
                            Text("Homebrew Roms:")
                                .font(.headline)
                            LazyVGrid(columns: [GridItem(.adaptive(minimum: 160))], spacing: 10) {
                                ForEach(LibraryManager.shared.homebrewroms()) { game in
                                    NavigationLink(destination: EmulationView(game: game).toolbar(.hidden, for: .tabBar)) {
                                        // GameButtonView(game: game)
                                            // .frame(maxWidth: .infinity, minHeight: 200)
                                    }
                                    .contextMenu {
                                        NavigationLink(destination: EmulationView(game: game)) {
                                            Text("Launch")
                                        }
                                    }
                                }
                            }
                        }
                    }
                    .refreshable {
                        doesitexist = doeskeysexist()
                    }


                }

            }
            .fileImporter(isPresented: $isimportingfirm, allowedContentTypes: [.zip], onCompletion: { result in
                switch result {
                case .success(let elements):
                    core.AddFirmware(at: elements)
                case .failure(let error):

                    print(error.localizedDescription)
                }
            })
            .fileImporter(isPresented: $importgame, allowedContentTypes: [.item], onCompletion: { result in
                switch result {
                case .success(let elements):
                    let iscustom = elements.startAccessingSecurityScopedResource()
                    let information = AppUI.shared.information(for: elements)

                    let game = EmulationGame(developer: information.developer, fileURL: elements,
                                          imageData: information.iconData,
                                          title: information.title)

                    importedgame = game


                    DispatchQueue.main.async {

                        if iscustom {
                            elements.stopAccessingSecurityScopedResource()
                        }

                        launchGame = true
                    }
                case .failure(let error):

                    print(error.localizedDescription)
                }
            })
            .onAppear() {
                doesitexist = doeskeysexist()
            }
            .navigationBarTitle("Library", displayMode: .inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarLeading) { // why did this take me so long to figure out lmfao
                    Button(action: {
                        isGridView.toggle()
                    }) {
                        Image(systemName: isGridView ? "rectangle.grid.1x2" : "square.grid.2x2")
                            .imageScale(.large)
                            .padding()
                    }
                }

                ToolbarItem(placement: .navigationBarTrailing) { // funsies
                    Menu {
                        Button(action: {
                            importgame = true // this part took a while

                        }) {
                            Text("Launch Game")
                        }

                        Button(action: {
                            isimportingfirm = true
                        }) {
                            Text("Import Firmware")
                        }
                    } label: {
                        Image(systemName: "plus.circle.fill")
                            .imageScale(.large)
                            .padding()
                    }

                }
            }
        }
    }


    func doeskeysexist() -> (Bool, Bool) {
        var doesprodexist = false
        var doestitleexist = false


        let title = core.root.appendingPathComponent("keys").appendingPathComponent("title.keys")
        let prod = core.root.appendingPathComponent("keys").appendingPathComponent("prod.keys")
        let fileManager = FileManager.default
        let documentsDirectory = fileManager.urls(for: .documentDirectory, in: .userDomainMask)[0]

        if fileManager.fileExists(atPath: prod.path) {
            doesprodexist = true
        } else {
            print("File does not exist")
        }

        if fileManager.fileExists(atPath: title.path) {
            doestitleexist = true
        } else {
            print("File does not exist")
        }

        return (doestitleexist, doesprodexist)
    }
}

func getDeveloperNames() -> String {
    guard let s = infoDictionary?["CFBundleIdentifier"] as? String else {
        return "Unknown"
    }
    return s
}

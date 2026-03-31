// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2024 Pomelo, Stossy11
// SPDX-License-Identifier: GPL-3.0-or-later

import Foundation

func isSideJITServerDetected(completion: @escaping (Result<Void, Error>) -> Void) {
    let address = UserDefaults.standard.string(forKey: "sidejitserver") ?? ""
    var SJSURL = address
    if (address).isEmpty {
        SJSURL = "http://sidejitserver._http._tcp.local:8080"
    }
    // Create a network operation at launch to Refresh SideJITServer
    let url = URL(string: SJSURL)!
    let task = URLSession.shared.dataTask(with: url) { (data, response, error) in
        if let error = error {
            print("No SideJITServer on Network")
            completion(.failure(error))
            return
        }
        completion(.success(()))
    }
    task.resume()
    return
}

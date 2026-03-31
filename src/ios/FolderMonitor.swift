// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2024 Pomelo, Stossy11
// SPDX-License-Identifier: GPL-3.0-or-later

import Foundation

class FolderMonitor {
    private var folderDescriptor: Int32 = -1
    private var folderMonitorSource: DispatchSourceFileSystemObject?
    private let folderURL: URL
    private let onFolderChange: () -> Void
    init(folderURL: URL, onFolderChange: @escaping () -> Void) {
        self.folderURL = folderURL
        self.onFolderChange = onFolderChange
        startMonitoring()
    }
    private func startMonitoring() {
        folderDescriptor = open(folderURL.path, O_EVTONLY)
        guard folderDescriptor != -1 else {
            print("Failed to open folder descriptor.")
            return
        }

        folderMonitorSource = DispatchSource.makeFileSystemObjectSource(
            fileDescriptor: folderDescriptor,
            eventMask: .write,
            queue: DispatchQueue.global()
        )
        folderMonitorSource?.setEventHandler { [weak self] in
            self?.folderDidChange()
        }
        folderMonitorSource?.setCancelHandler {
            close(self.folderDescriptor)
        }
        folderMonitorSource?.resume()
    }

    private func folderDidChange() {
        // Detect the change and call the refreshcore function
        print("Folder changed! New file added or removed.")
        DispatchQueue.main.async { [weak self] in
            self?.onFolderChange()
        }
    }
    deinit {
        folderMonitorSource?.cancel()
    }
}

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

//
//  DirectoryManager.mm - Sudachi
//  Created by Jarrod Norwell on 1/18/24.
//

#import <Foundation/Foundation.h>
#import "DirectoryManager.h"

NSURL *DocumentsDirectory() {
    return [[[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask] firstObject];
}

const char* DirectoryManager::AppUIDirectory(void) {
    return [[DocumentsDirectory() path] UTF8String];
}

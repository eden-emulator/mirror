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

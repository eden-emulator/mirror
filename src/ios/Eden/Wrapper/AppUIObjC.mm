//
//  AppUIObjC.mm - Sudachi
//  Created by Jarrod Norwell on 1/8/24.
//

#import "AppUIObjC.h"

#import "Config/Config.h"
#import "EmulationSession/EmulationSession.h"
#import "DirectoryManager/DirectoryManager.h"

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/settings.h"
#include "common/fs/fs.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/savedata_factory.h"
#include "core/loader/nro.h"
#include "frontend_common/content_manager.h"
#include "common/settings_enums.h"
#include "network/announce_multiplayer_session.h"
#include "common/announce_multiplayer_room.h"
#include "network/network.h"

#include "common/detached_tasks.h"
#include "common/dynamic_library.h"
#include "common/fs/path_util.h"
#include "common/logging/backend.h"
#include "common/logging/log.h"
#include "common/microprofile.h"
#include "common/scm_rev.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/cpu_manager.h"
#include "core/crypto/key_manager.h"
#include "core/file_sys/card_image.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/fs_filesystem.h"
#include "core/file_sys/submission_package.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/frontend/applets/cabinet.h"
#include "core/frontend/applets/controller.h"
#include "core/frontend/applets/error.h"
#include "core/frontend/applets/general.h"
#include "core/frontend/applets/mii_edit.h"
#include "core/frontend/applets/profile_select.h"
#include "core/frontend/applets/software_keyboard.h"
#include "core/frontend/applets/web_browser.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/frontend/applets.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/loader.h"
#include "frontend_common/yuzu_config.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/hid_types.h"
#include "video_core/renderer_base.h"
#include "video_core/renderer_vulkan/renderer_vulkan.h"
#include "video_core/vulkan_common/vulkan_instance.h"
#include "video_core/vulkan_common/vulkan_surface.h"


#import <mach/mach.h>

@implementation AppUIObjC
-(AppUIObjC *) init {
    if (self = [super init]) {
        _gameInformation = [AppUIGameInformation sharedInstance];


        Common::FS::SetAppDirectory(DirectoryManager::AppUIDirectory());
        Config{"config", Config::ConfigType::GlobalConfig};

        EmulationSession::GetInstance().System().Initialize();
        EmulationSession::GetInstance().InitializeSystem(false);
        EmulationSession::GetInstance().InitializeGpuDriver();


        YuzuSettings::values.dump_shaders.SetValue(true);
        YuzuSettings::values.use_asynchronous_shaders.SetValue(true);
        // YuzuSettings::values.astc_recompression.SetValue(YuzuSettings::AstcRecompression::Bc3);
        YuzuSettings::values.shader_backend.SetValue(YuzuSettings::ShaderBackend::SpirV);
        // YuzuSettings::values.resolution_setup.SetValue(YuzuSettings::ResolutionSetup::Res1X);
        // YuzuSettings::values.scaling_filter.SetValue(YuzuSettings::ScalingFilter::Bilinear);
    } return self;
}


+(AppUIObjC *) sharedInstance {
    static AppUIObjC *sharedInstance = NULL;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sharedInstance = [[self alloc] init];
    });
    return sharedInstance;
}

- (BOOL)ispaused {
    return EmulationSession::GetInstance().IsPaused();
}

-(void) pause {
    EmulationSession::GetInstance().System().Pause();
    EmulationSession::GetInstance().HaltEmulation();
    EmulationSession::GetInstance().PauseEmulation();
}

-(void) play {

    EmulationSession::GetInstance().System().Run();
    EmulationSession::GetInstance().RunEmulation();
    EmulationSession::GetInstance().UnPauseEmulation();
}

-(BOOL)hasfirstfame {
    @try {
        auto* window = &EmulationSession::GetInstance().Window();
        if (window && window->HasFirstFrame()) {
            return YES;
        }
    }
    @catch (NSException *exception) {
        NSLog(@"Exception occurred: %@", exception);
        // Handle the exception, maybe return a default value
        return NO;
    }
    return NO;
}

- (BOOL)canGetFullPath {
    @try {
        Core::System& system = EmulationSession::GetInstance().System();
        auto bis_system = system.GetFileSystemController().GetSystemNANDContents();

        if (bis_system == nullptr) {
            return NO;
        }

        constexpr u64 QLaunchId = static_cast<u64>(Service::AM::AppletProgramId::QLaunch);
        auto qlaunch_applet_nca = bis_system->GetEntry(QLaunchId, FileSys::ContentRecordType::Program);

        if (qlaunch_applet_nca == nullptr) {
            return NO;
        }

        const auto filename = qlaunch_applet_nca->GetFullPath();

        // If GetFullPath() is successful
        return YES;
    } @catch (NSException *exception) {
        // Handle the exception if needed
        return NO;
    }
}

-(void) quit {
    EmulationSession::GetInstance().ShutdownEmulation();
}

-(void) configureLayer:(CAMetalLayer *)layer withSize:(CGSize)size {
    _layer = layer;
    _size = size;
    EmulationSession::GetInstance().SetNativeWindow((__bridge CA::MetalLayer*)layer, size);
}

-(void) bootOS {
    EmulationSession::GetInstance().BootOS();
}

-(void) insertGame:(NSURL *)url {
    EmulationSession::GetInstance().InitializeEmulation([url.path UTF8String], [_gameInformation informationForGame:url].programID, true);
}

-(void) insertGames:(NSArray<NSURL *> *)games {
    for (NSURL *url in games) {
        EmulationSession::GetInstance().ConfigureFilesystemProvider([url.path UTF8String]);
    }
}

-(void) step {
    void(EmulationSession::GetInstance().System().Run());
}

-(void) touchBeganAtPoint:(CGPoint)point index:(NSUInteger)index {
    float h_ratio, w_ratio;
    h_ratio = EmulationSession::GetInstance().Window().GetFramebufferLayout().height / (_size.height * [[UIScreen mainScreen] nativeScale]);
    w_ratio = EmulationSession::GetInstance().Window().GetFramebufferLayout().width / (_size.width * [[UIScreen mainScreen] nativeScale]);

    EmulationSession::GetInstance().Window().OnTouchPressed([[NSNumber numberWithUnsignedInteger:index] intValue],
                                                            (point.x) * [[UIScreen mainScreen] nativeScale] * w_ratio,
                                                            ((point.y) * [[UIScreen mainScreen] nativeScale] * h_ratio));
}

-(void) touchEndedForIndex:(NSUInteger)index {
    EmulationSession::GetInstance().Window().OnTouchReleased([[NSNumber numberWithUnsignedInteger:index] intValue]);
}

-(void) touchMovedAtPoint:(CGPoint)point index:(NSUInteger)index {
    float h_ratio, w_ratio;
    h_ratio = EmulationSession::GetInstance().Window().GetFramebufferLayout().height / (_size.height * [[UIScreen mainScreen] nativeScale]);
    w_ratio = EmulationSession::GetInstance().Window().GetFramebufferLayout().width / (_size.width * [[UIScreen mainScreen] nativeScale]);

    EmulationSession::GetInstance().Window().OnTouchMoved([[NSNumber numberWithUnsignedInteger:index] intValue],
                                                          (point.x) * [[UIScreen mainScreen] nativeScale] * w_ratio,
                                                          ((point.y) * [[UIScreen mainScreen] nativeScale] * h_ratio));
}

-(void) thumbstickMoved:(VirtualControllerAnalogType)analog
                      x:(CGFloat)x
                      y:(CGFloat)y
           controllerId:(int)controllerId {
    EmulationSession::GetInstance().OnGamepadConnectEvent(controllerId);
    EmulationSession::GetInstance().Window().OnGamepadJoystickEvent(controllerId, [[NSNumber numberWithUnsignedInteger:analog] intValue], CGFloat(x), CGFloat(y));
}

-(void) virtualControllerButtonDown:(VirtualControllerButtonType)button
                       controllerId:(int)controllerId {
    EmulationSession::GetInstance().OnGamepadConnectEvent(controllerId);
    EmulationSession::GetInstance().Window().OnGamepadButtonEvent(controllerId, [[NSNumber numberWithUnsignedInteger:button] intValue], true);
}

-(void) virtualControllerGyro:(int)controllerId
            deltaTimestamp:(int)delta_timestamp
                    gyroX:(float)gyro_x
                    gyroY:(float)gyro_y
                    gyroZ:(float)gyro_z
                  accelX:(float)accel_x
                  accelY:(float)accel_y
                  accelZ:(float)accel_z
{
    EmulationSession::GetInstance().OnGamepadConnectEvent(controllerId);
    EmulationSession::GetInstance().Window().OnGamepadMotionEvent(controllerId, delta_timestamp, gyro_x, gyro_y, gyro_z, accel_x, accel_y, accel_z);
}


-(void) virtualControllerButtonUp:(VirtualControllerButtonType)button
                     controllerId:(int)controllerId {
    EmulationSession::GetInstance().OnGamepadConnectEvent(controllerId);
    EmulationSession::GetInstance().Window().OnGamepadButtonEvent(controllerId, [[NSNumber numberWithUnsignedInteger:button] intValue], false);
}


-(void) orientationChanged:(UIInterfaceOrientation)orientation with:(CAMetalLayer *)layer size:(CGSize)size {
    _layer = layer;
    _size = size;
    EmulationSession::GetInstance().Window().OnSurfaceChanged((__bridge CA::MetalLayer*)layer, size);
}

-(void) settingsChanged {
    Config{"config", Config::ConfigType::GlobalConfig};
}



@end

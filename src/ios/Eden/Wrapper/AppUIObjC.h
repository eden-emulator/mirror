//
//  AppUIObjC.h - Sudachi
//  Created by Jarrod Norwell on 1/8/24.
//

#import <Foundation/Foundation.h>
#import <MetalKit/MetalKit.h>

#import "AppUIGameInformation/AppUIGameInformation.h"

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSUInteger, VirtualControllerAnalogType) {
    VirtualControllerAnalogTypeLeft = 0,
    VirtualControllerAnalogTypeRight = 1
};

typedef NS_ENUM(NSUInteger, VirtualControllerButtonType) {
    VirtualControllerButtonTypeA = 0,
    VirtualControllerButtonTypeB = 1,
    VirtualControllerButtonTypeX = 2,
    VirtualControllerButtonTypeY = 3,
    VirtualControllerButtonTypeL = 4,
    VirtualControllerButtonTypeR = 5,
    VirtualControllerButtonTypeTriggerL = 6,
    VirtualControllerButtonTypeTriggerR = 7,
    VirtualControllerButtonTypeTriggerZL = 8,
    VirtualControllerButtonTypeTriggerZR = 9,
    VirtualControllerButtonTypePlus = 10,
    VirtualControllerButtonTypeMinus = 11,
    VirtualControllerButtonTypeDirectionalPadLeft = 12,
    VirtualControllerButtonTypeDirectionalPadUp = 13,
    VirtualControllerButtonTypeDirectionalPadRight = 14,
    VirtualControllerButtonTypeDirectionalPadDown = 15,
    VirtualControllerButtonTypeSL = 16,
    VirtualControllerButtonTypeSR = 17,
    VirtualControllerButtonTypeHome = 18,
    VirtualControllerButtonTypeCapture = 19
};

@interface AppUIObjC : NSObject {
    CAMetalLayer *_layer;
    CGSize _size;
}

@property (nonatomic, strong) AppUIGameInformation *gameInformation;

+(AppUIObjC *) sharedInstance NS_SWIFT_NAME(shared());
-(void) configureLayer:(CAMetalLayer *)layer withSize:(CGSize)size NS_SWIFT_NAME(configure(layer:with:));
-(void) bootOS;
-(void) pause;
-(void) play;
-(BOOL) ispaused;
-(BOOL) canGetFullPath;
-(void) quit;
-(void) insertGame:(NSURL *)url NS_SWIFT_NAME(insert(game:));
-(void) insertGames:(NSArray<NSURL *> *)games NS_SWIFT_NAME(insert(games:));
-(void) step;
-(BOOL) hasfirstfame;

-(void) touchBeganAtPoint:(CGPoint)point index:(NSUInteger)index NS_SWIFT_NAME(touchBegan(at:for:));
-(void) touchEndedForIndex:(NSUInteger)index;
-(void) touchMovedAtPoint:(CGPoint)point index:(NSUInteger)index NS_SWIFT_NAME(touchMoved(at:for:));

-(void) thumbstickMoved:(VirtualControllerAnalogType)analog
                      x:(CGFloat)x
                      y:(CGFloat)y
             controllerId:(int)controllerId;

-(void) virtualControllerGyro:(int)controllerId
            deltaTimestamp:(int)delta_timestamp
                    gyroX:(float)gyro_x
                    gyroY:(float)gyro_y
                    gyroZ:(float)gyro_z
                  accelX:(float)accel_x
                  accelY:(float)accel_y
                       accelZ:(float)accel_z;

-(void) virtualControllerButtonDown:(VirtualControllerButtonType)button
                       controllerId:(int)controllerId;

-(void) virtualControllerButtonUp:(VirtualControllerButtonType)button
                     controllerId:(int)controllerId;


-(void) orientationChanged:(UIInterfaceOrientation)orientation with:(CAMetalLayer *)layer size:(CGSize)size NS_SWIFT_NAME(orientationChanged(orientation:with:size:));

-(void) settingsChanged;

@end

NS_ASSUME_NONNULL_END

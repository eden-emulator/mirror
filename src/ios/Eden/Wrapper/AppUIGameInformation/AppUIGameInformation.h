//
//  AppUIGameInformation.h - Sudachi
//  Created by Jarrod Norwell on 1/20/24.
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface AppUIInformation : NSObject
@property (nonatomic, strong) NSString *developer;
@property (nonatomic, strong) NSData *iconData;
@property (nonatomic) BOOL isHomebrew;
@property (nonatomic) uint64_t programID;
@property (nonatomic, strong) NSString *title, *version;

-(AppUIInformation *) initWithDeveloper:(NSString *)developer iconData:(NSData *)iconData isHomebrew:(BOOL)isHomebrew programID:(uint64_t)programID title:(NSString *)title version:(NSString *)version;
@end

@interface AppUIGameInformation : NSObject
+(AppUIGameInformation *) sharedInstance NS_SWIFT_NAME(shared());

-(AppUIInformation *) informationForGame:(NSURL *)url NS_SWIFT_NAME(information(for:));
@end

NS_ASSUME_NONNULL_END

//
//  OWFullscreenObserver.h
//  electronoverlaywindow
//
//  Thin wrapper to allow for registering an observer for when the window
//  becomes full-screen.
//
//  Created by Harry Yu on 5/27/21.
//

#import <Foundation/Foundation.h>

typedef void (^FullscreenBlock)(void);

NS_ASSUME_NONNULL_BEGIN

@interface OWFullscreenObserver : NSObject

@property(copy) FullscreenBlock fullscreenBlock;

- (void)addBlock:(FullscreenBlock)fullscreenBlock;

@end

NS_ASSUME_NONNULL_END

//
//  OWFullscreenObserver.m
//  electronoverlaywindow
//
//  Thin wrapper to allow for registering an observer for when the window
//  becomes full-screen.
//
//  Created by Harry Yu on 5/27/21.
//

#import "OWFullscreenObserver.h"

@implementation OWFullscreenObserver

- (void)addBlock:(FullscreenBlock)fullscreenBlock {
  self.fullscreenBlock = fullscreenBlock;
}

- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary<NSKeyValueChangeKey, id> *)change
                       context:(void *)context {
  if (self.fullscreenBlock) {
    self.fullscreenBlock();
  }
}

@end

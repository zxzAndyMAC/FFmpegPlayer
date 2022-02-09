//
//  ACECamera.h
//  libAceKidGame
//
//  Created by AndyZheng on 2019/12/20.
//  Copyright © 2019 AndyZheng. All rights reserved.
//

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface ACECamera : NSObject {
@private
    BOOL init_success;
    BOOL res_change;
    BOOL buffer_change;
    size_t width;
    size_t height;
    uint8_t* rgba;
    uint8_t* I420;
    uint8_t* copy_buffer;
    unsigned int refresh_count;
}
-(id)init;
-(BOOL)openCamera;
-(void)closeCamera;
-(size_t)getWidth;
-(size_t)getHeight;
-(uint8_t*)getBuffer:(unsigned int &)interval;
@end

NS_ASSUME_NONNULL_END

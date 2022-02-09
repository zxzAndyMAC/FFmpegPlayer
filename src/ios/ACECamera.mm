//
//  ACECamera.m
//  libAceKidGame
//
//  Created by AndyZheng on 2019/12/20.
//  Copyright © 2019 AndyZheng. All rights reserved.
//

#import "ACECamera.h"
#import <AVFoundation/AVFoundation.h>
#import "libyuv.h"

@interface ACECamera () <AVCaptureVideoDataOutputSampleBufferDelegate>
@property(nonatomic, strong) AVCaptureSession *captureSession;
@end

@implementation ACECamera

-(id)init{
    self = [super init];
    init_success = false;
    res_change = false;
    buffer_change = false;
    width = 0;
    height = 0;
    rgba = NULL;
    I420 = NULL;
    copy_buffer = NULL;
    refresh_count = 0;
    if(self){
        // 1. 创建捕获会话：设置分辨率
        [self setupSession];
        
        // 2. 添加输入
        if ([self setupInput]) {
            // 3. 添加输出
            if ([self setupOutput]) {
                init_success = true;
            }
        }
    }
    return self;
}

- (BOOL)openCamera {
    if (!init_success) {
        return false;
    }
    [_captureSession startRunning];
    return true;
}

- (void)closeCamera {
    if (init_success) {
        [_captureSession stopRunning];
        if (NULL != rgba) {
            free(rgba);
            rgba = NULL;
        }
        if (NULL != I420) {
            free(I420);
            I420 = NULL;
        }
        if (NULL != copy_buffer) {
            free(copy_buffer);
            copy_buffer = NULL;
        }
    }
}

- (void)setupSession {
    AVCaptureSession *captureSession = [[AVCaptureSession alloc] init];
    _captureSession = captureSession;
    if (@available(iOS 13.0, *)) {
        captureSession.sessionPreset = AVCaptureSessionPreset640x480;
    }else {
        captureSession.sessionPreset = AVCaptureSessionPresetLow;//AVCaptureSessionPreset640x480; //AVCaptureSessionPresetLow;
    }
}

- (BOOL)setupOutput {
    AVCaptureVideoDataOutput *videoOutput = [[AVCaptureVideoDataOutput alloc] init];
    //kCVPixelFormatType_32RGBA    kCVPixelFormatType_420YpCbCr8BiPlanarFullRange
    videoOutput.videoSettings = @{(NSString *)kCVPixelBufferPixelFormatTypeKey:@(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange)};// X264_CSP_NV12
    dispatch_queue_t queue = dispatch_queue_create("ACE_CAMERA_QUEUE", DISPATCH_QUEUE_SERIAL);
    [videoOutput setSampleBufferDelegate:self queue:queue];
    // 给会话添加输出对象
    if([_captureSession canAddOutput:videoOutput]) {
        // 给会话添加输入输出就会自动建立起连接
        [_captureSession addOutput:videoOutput];
        // 注意： 一定要在添加之后
        // 获取输入与输出之间的连接
        AVCaptureConnection *connection = [videoOutput connectionWithMediaType:AVMediaTypeVideo];
        // 设置采集数据的方向、镜像
        connection.videoOrientation = AVCaptureVideoOrientationLandscapeRight;
        connection.videoMirrored = YES;
        return true;
    }
    return false;
}

- (BOOL)setupInput {
    AVCaptureDevice *videoDevice = [self deviceWithPosition:AVCaptureDevicePositionFront];
    if (videoDevice) {
        // 设备输入对象
        AVCaptureDeviceInput *videoInput = [AVCaptureDeviceInput deviceInputWithDevice:videoDevice error:nil];
        // 给会话添加输入
        if([_captureSession canAddInput:videoInput]) {
            [_captureSession addInput:videoInput];
            //自动聚焦
            if ([videoDevice isFocusModeSupported:AVCaptureFocusModeAutoFocus]) {
                NSError *error;
                BOOL isLooked = [videoDevice lockForConfiguration:&error];
                if(isLooked && error == nil){
                    //设置帧率
                    videoDevice.activeVideoMinFrameDuration = CMTimeMake(1, 15);
                    videoDevice.activeVideoMaxFrameDuration = CMTimeMake(1, 15);
                    videoDevice.focusMode = AVCaptureFocusModeContinuousAutoFocus;
                    [videoDevice unlockForConfiguration];
                }
            }
            return true;
        }
    }
    return false;
}

- (AVCaptureDevice *)deviceWithPosition:(AVCaptureDevicePosition)position {
    AVCaptureDeviceDiscoverySession *devicesIOS10 = [AVCaptureDeviceDiscoverySession  discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInWideAngleCamera] mediaType:AVMediaTypeVideo position:AVCaptureDevicePositionFront];
    NSArray *devicesIOS  = devicesIOS10.devices;
    for (AVCaptureDevice *device in devicesIOS) {
        if ([device position] == position) {
            return device;
        }
    }
    return nil;
}

- (size_t)getWidth {
    @synchronized (self) {
        return self->width;
    }
}

- (size_t)getHeight {
    @synchronized (self) {
        return self->height;
    }
}

- (uint8_t*)getBuffer:(unsigned int &)interval {
    if (interval < refresh_count) {
        interval = refresh_count;
        
        @synchronized (self) {
            if(self->buffer_change) {
                if(NULL != self->copy_buffer && self->res_change)
                {
                    free(self->copy_buffer);
                    self->copy_buffer = (uint8_t *)malloc(((self->width * self->height * 3) >> 1) * sizeof(uint8_t));
                }
                if(NULL == self->copy_buffer)
                    self->copy_buffer = (uint8_t *)malloc(((self->width * self->height * 3) >> 1) * sizeof(uint8_t));
                memset(self->copy_buffer, 0, ((self->width * self->height * 3) >> 1) * sizeof(uint8_t));
                memcpy(self->copy_buffer, self->I420, ((self->width * self->height * 3) >> 1) * sizeof(uint8_t));
                self->buffer_change = false;
            }else{
                return NULL;
            }
        }
        
        if(NULL != self->rgba && self->res_change)
        {
            self->res_change = false;
            free(self->rgba);
            self->rgba = (uint8_t *)malloc(self->width * self->height * 4 * sizeof(uint8_t));
        }
        if(NULL == self->rgba)
        {
            self->rgba = (uint8_t *)malloc(self->width * self->height * 4 * sizeof(uint8_t));
        }
        memset(self->rgba, 0, self->width * self->height * 4 * sizeof(uint8_t));
        //I420 to RGBA
        int iw = (int)self->width;
        int ih = (int)self->height;
        
        libyuv::I420ToABGR(
                           &self->copy_buffer[0],                                           iw,
                           &self->copy_buffer[self->width * self->height],                  iw >> 1,
                           &self->copy_buffer[(self->width * self->height * 5) >> 2],       iw >> 1,
                           self->rgba,                                                      iw * 4,
                           iw,    ih
                           );
        
        return self->rgba;
    }else{
        return NULL;
    }

}

#pragma mark - AVCaptureVideoDataOutputSampleBufferDelegate
- (void)captureOutput:(AVCaptureOutput *)output didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer fromConnection:(AVCaptureConnection *)connection
{
    CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    CVPixelBufferLockBaseAddress(imageBuffer, 0);
    
    size_t w = CVPixelBufferGetWidth(imageBuffer);
    size_t h = CVPixelBufferGetHeight(imageBuffer);
    //void* data = CVPixelBufferGetBaseAddress(imageBuffer);
    //size_t data_len = CVPixelBufferGetDataSize(imageBuffer);
    
    uint8_t *imageAddress = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(imageBuffer,0);//YYYYYYYY
    size_t row0=CVPixelBufferGetBytesPerRowOfPlane(imageBuffer,0);
    uint8_t *imageAddress1 = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(imageBuffer,1);//UVUVUVUV
    size_t row1=CVPixelBufferGetBytesPerRowOfPlane(imageBuffer,1);
        
    @synchronized (self) {
        if (NULL == self->I420) {
            self->width = w;
            self->height = h;
            self->I420 = (uint8_t *)malloc(((w * h * 3) >> 1) * sizeof(uint8_t));
        }else if(self->width != w || self->height != h) {
            self->res_change = true;
            self->width = w;
            self->height = h;
            free(self->I420);
            self->I420 = (uint8_t *)malloc(((w * h * 3) >> 1) * sizeof(uint8_t));
        }
        
        memset(self->I420, 0, ((w * h * 3) >> 1) * sizeof(uint8_t));
        
        int iw = (int)w;
        int ih = (int)h;
        
        libyuv::NV12ToI420(imageAddress,                      (int)row0,
                           imageAddress1,                     (int)row1,
                           &self->I420[0],                    iw,
                           &self->I420[w * h],                iw >> 1,
                           &self->I420[(w * h * 5) >> 2],     iw >> 1,
                           iw,    ih);
        refresh_count++;

        self->buffer_change = true;
    
    }
    
    CVPixelBufferUnlockBaseAddress(imageBuffer, 1);
}

@end

//
//  VDADecoderChecker.cpp
//  VDADecoderChecker
//
//  Created by Andy Breuhan on 23.06.11.
//  Copyright 2014 andybreuhan.de. All rights reserved.
//

#include <CoreFoundation/CoreFoundation.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoDecodeAcceleration/VDADecoder.h>

#include <iostream>
using namespace std;

//This array of "avcC Data" comes directly from an sample MP4 File.
const UInt8 avcC[]        = {   0x00, 0x00, 0x00, 0x31, 0x61, 0x76, 0x63, 0x43, 0x01, 0x4D,
                                0x40, 0x29, 0xFF, 0xE1, 0x00, 0x1A, 0x67, 0x4D, 0x40, 0x29,
                                0x9A, 0x72, 0x80, 0xF0, 0x04, 0x2D, 0x80, 0x88, 0x00, 0x00,
                                0x03, 0x00, 0x08, 0x00, 0x00, 0x03, 0x01, 0x94, 0x78, 0xC1,
                                0x88, 0xB0, 0x01, 0x00, 0x04, 0x68, 0xEE, 0xBC, 0x80        };

//This array of "avcC Data" is parsed and optimized by Subler, it comes from the same MP4 File.
const UInt8 avcC_subler[] = {   0x01, 0x4d, 0x40, 0x29, 0x03, 0x01, 0x00, 0x1a, 0x67, 0x4d,
                                0x40, 0x29, 0x9a, 0x72, 0x80, 0xf0, 0x04, 0x2d, 0x80, 0x88,
                                0x00, 0x00, 0x03, 0x00, 0x08, 0x00, 0x00, 0x03, 0x01, 0x94,
                                0x78, 0xc1, 0x88, 0xb0, 0x01, 0x00, 0x04, 0x68, 0xee, 0xbc,
                                0x80                                                        };

//Needed for a valid configuration
void myDecoderOutputCallback();
void myDecoderOutputCallback(){}

typedef struct OpaqueVDADecoder* VDADecoder;
OSStatus CreateDecoder(void);

// tracks a frame in and output queue in display order
typedef struct myDisplayFrame {
    int64_t                 frameDisplayTime;
    CVPixelBufferRef        frame;
    struct myDisplayFrame   *nextFrame;
} myDisplayFrame, *myDisplayFramePtr;

// some user data
typedef struct MyUserData
{
    myDisplayFramePtr displayQueue; // display-order queue - next display frame is always at the queue head
    int32_t           queueDepth; // we will try to keep the queue depth around 10 frames
    pthread_mutex_t   queueMutex; // mutex protecting queue manipulation
    
} MyUserData, *MyUserDataPtr;



//OSStatus CreateDecoder(SInt32 inHeight, SInt32 inWidth,
//                     OSType inSourceFormat, CFDataRef inAVCCData,
//                   VDADecoder *decoderOut)
OSStatus CreateDecoder(void)
{
    CFDataRef inAVCCData;
    VDADecoder decoderOut = NULL;
    
    OSType inSourceFormat='avc1';
    SInt32 inHeight = 1920;
    SInt32 inWidth = 1080;
    
    inAVCCData = CFDataCreate(kCFAllocatorDefault, avcC_subler, sizeof(avcC_subler)*sizeof(UInt8));
    
    
    OSStatus status;
    MyUserData myUserData;
    CFMutableDictionaryRef decoderConfiguration = NULL;
    CFMutableDictionaryRef destinationImageBufferAttributes = NULL;
    CFDictionaryRef emptyDictionary;
    
    CFNumberRef height = NULL;
    CFNumberRef width= NULL;
    CFNumberRef sourceFormat = NULL;
    CFNumberRef pixelFormat = NULL;
    
    
    // source must be H.264
    if (inSourceFormat != 'avc1') {
        fprintf(stderr, "Source format is not H.264!\n");
        return paramErr;
    }
    
    // the avcC data chunk from the bitstream must be present
    if (inAVCCData == NULL) {
        fprintf(stderr, "avc1 decoder configuration data cannot be NULL!\n");
        return paramErr;
    }
    
    // create a CFDictionary describing the source material for decoder configuration
    decoderConfiguration = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                                     4,
                                                     &kCFTypeDictionaryKeyCallBacks,
                                                     &kCFTypeDictionaryValueCallBacks);
    
    height = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &inHeight);
    width = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &inWidth);
    sourceFormat = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &inSourceFormat);
    
    CFDictionarySetValue(decoderConfiguration, kVDADecoderConfiguration_Height, height);
    CFDictionarySetValue(decoderConfiguration, kVDADecoderConfiguration_Width, width);
    CFDictionarySetValue(decoderConfiguration, kVDADecoderConfiguration_SourceFormat, sourceFormat);
    CFDictionarySetValue(decoderConfiguration, kVDADecoderConfiguration_avcCData, inAVCCData);
    
    // create a CFDictionary describing the wanted destination image buffer
    destinationImageBufferAttributes = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                                                 2,
                                                                 &kCFTypeDictionaryKeyCallBacks,
                                                                 &kCFTypeDictionaryValueCallBacks);
    
    OSType cvPixelFormatType = kCVPixelFormatType_422YpCbCr8;
    pixelFormat = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &cvPixelFormatType);
    emptyDictionary = CFDictionaryCreate(kCFAllocatorDefault, // our empty IOSurface properties dictionary
                                         NULL,
                                         NULL,
                                         0,
                                         &kCFTypeDictionaryKeyCallBacks,
                                         &kCFTypeDictionaryValueCallBacks);
    
    CFDictionarySetValue(destinationImageBufferAttributes, kCVPixelBufferPixelFormatTypeKey, pixelFormat);
    CFDictionarySetValue(destinationImageBufferAttributes,
                         kCVPixelBufferIOSurfacePropertiesKey,
                         emptyDictionary);
    
    // create the hardware decoder object
    status = VDADecoderCreate(decoderConfiguration,
                              destinationImageBufferAttributes,
                              (VDADecoderOutputCallback*)myDecoderOutputCallback,
                              (void *)&myUserData,
                              &decoderOut);
    
    if (kVDADecoderNoErr != status) {
        fprintf(stderr, "VDADecoderCreate failed. err: %d\n", status);
    }
    
    if(decoderConfiguration) CFRelease(decoderConfiguration);
    if(destinationImageBufferAttributes) CFRelease(destinationImageBufferAttributes);
    if(emptyDictionary) CFRelease(emptyDictionary);
    if(decoderOut) VDADecoderDestroy(decoderOut);
    if(decoderConfiguration) CFRelease(decoderConfiguration);
    if(inAVCCData) CFRelease(inAVCCData);
    
    return status;
}

int main (int argc, const char * argv[])
{
    
    OSStatus status;
    status = CreateDecoder();
    
    switch (status) {
        case kVDADecoderNoErr:
            cout << "Hardware acceleration is fully supported" << "\n";
            break;
        case kVDADecoderHardwareNotSupportedErr:
            cout << "The hardware does not support accelerated video services required for hardware decode." << "\n";
            break;
        case kVDADecoderConfigurationError:
            cout << "Invalid or unsupported configuration parameters were specified in VDADecoderCreate." << "\n";
            break;
        case kVDADecoderDecoderFailedErr:
            cout << "An error was returned by the decoder layer. This may happen for example because of bitstream/data errors during a decode operation. This error may also be returned from VDADecoderCreate when hardware decoder resources are available on the system but currently in use by another process." << "\n";
            break;
        case -50:
            cout << "Parameter error" << "\n";
            break;
            
        default:
            cout << "Unknown Status: " << status << "\n";
            break;
    }
    
    if(kVDADecoderNoErr != status) fprintf(stderr, "VDADecoderCreate failed. err: %d\n", status);
    
    return 0; 
}


LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := crypto_static
LOCAL_MODULE_FILENAME := crypto
LOCAL_SRC_FILES := android/arm/lib/libcrypto.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/android/arm
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := ssl_static
LOCAL_MODULE_FILENAME := ssl
LOCAL_SRC_FILES := android/arm/lib/libssl.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/android/arm
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := avcodec_static
LOCAL_MODULE_FILENAME := avcodec
LOCAL_SRC_FILES := android/arm/lib/libavcodec.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/android/arm/include
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := avfilter_static
LOCAL_MODULE_FILENAME := avfilter
LOCAL_SRC_FILES := android/arm/lib/libavfilter.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/android/arm/include
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := avformat_static
LOCAL_MODULE_FILENAME := avformat
LOCAL_SRC_FILES := android/arm/lib/libavformat.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/android/arm/include
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := avutil_static
LOCAL_MODULE_FILENAME := avutil
LOCAL_SRC_FILES := android/arm/lib/libavutil.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/android/arm/include
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := swresample_static
LOCAL_MODULE_FILENAME := swresample
LOCAL_SRC_FILES := android/arm/lib/libswresample.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/android/arm/include
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := swscale_static
LOCAL_MODULE_FILENAME := swscale
LOCAL_SRC_FILES := android/arm/lib/libswscale.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/android/arm/include
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := ffmpeg_shared

LOCAL_MODULE_FILENAME := libffmpeg

LOCAL_ARM_MODE := arm

LOCAL_LDLIBS := \
    -llog \
    -landroid \
    -ldl \
    -lGLESv1_CM \
    -lGLESv2 \
    -lOpenSLES \
    -llog \
    -lz

LOCAL_ALLOW_UNDEFINED_SYMBOLS := true

LOCAL_SRC_FILES := \
            $(subst $(LOCAL_PATH)/,,$(wildcard $(LOCAL_PATH)/src/*.cpp)) \
            $(subst $(LOCAL_PATH)/,,$(wildcard $(LOCAL_PATH)/src/android/*.cpp))

LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/include \
            $(LOCAL_PATH)/src \
            $(LOCAL_PATH)/android/arm/include \
			$(LOCAL_PATH)/../libyuv/include

LOCAL_C_INCLUDES := $(LOCAL_PATH)/include \
            $(LOCAL_PATH)/android/arm \
            $(LOCAL_PATH)/src \
            $(LOCAL_PATH)/android/arm/include \
			$(LOCAL_PATH)/../libyuv/include
            
LOCAL_CFLAGS   += -fexceptions -D__ANDROID_API__=21
LOCAL_CPPFLAGS := -Wno-deprecated-declarations -D__ANDROID_API__=21

LOCAL_WHOLE_STATIC_LIBRARIES := yuv_static
LOCAL_WHOLE_STATIC_LIBRARIES += crypto_static
LOCAL_WHOLE_STATIC_LIBRARIES += ssl_static
LOCAL_STATIC_LIBRARIES += avfilter_static
LOCAL_STATIC_LIBRARIES += swscale_static
LOCAL_STATIC_LIBRARIES += avformat_static
LOCAL_STATIC_LIBRARIES += avcodec_static
LOCAL_STATIC_LIBRARIES += avutil_static
LOCAL_STATIC_LIBRARIES += swresample_static
LOCAL_STATIC_LIBRARIES += SDL2
LOCAL_STATIC_LIBRARIES += libhidapi

include $(BUILD_SHARED_LIBRARY)

$(call import-add-path, $(LOCAL_PATH)/..)

$(call import-module, libyuv)
$(call import-module, sdl/Android/SDL2)


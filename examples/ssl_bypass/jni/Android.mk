LOCAL_PATH := $(call my-dir)

# ----- arthook (prebuilt static lib + exported headers, shared by examples) -
include $(CLEAR_VARS)
LOCAL_MODULE              := arthook
LOCAL_SRC_FILES           := ../../arthook/prebuilt/$(TARGET_ARCH_ABI)/libarthook.a
LOCAL_EXPORT_C_INCLUDES   := $(LOCAL_PATH)/../../arthook/include
include $(PREBUILT_STATIC_LIBRARY)

# ----- ssl_bypass payload ----------------------------------------------------
include $(CLEAR_VARS)
LOCAL_MODULE              := ssl_bypass
LOCAL_SRC_FILES           := ssl_bypass.cpp
LOCAL_CFLAGS              := -Wall -Wextra
LOCAL_STATIC_LIBRARIES    := arthook
LOCAL_LDLIBS              := -llog -ldl
include $(BUILD_SHARED_LIBRARY)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

CEDARX_PATH:=$(LOCAL_PATH)/../../../../frameworks/av/media/CedarX-Projects/CedarX/

##A31 A20 use suxi mem 
CEDARX_USE_SUNXI_MEM_ALLOCATOR:=N
CEDARX_CHIP_VERSION:=F51

LOCAL_SRC_FILES := \
			koti_awenc.c \
			capture/capture.c \
			
LOCAL_C_INCLUDES := $(KERNEL_HEADERS) \
			$(LOCAL_PATH) \
			$(LOCAL_PATH)/capture \
			$(CEDARX_PATH)/include \
			$(CEDARX_PATH)/include/include_platform/CHIP_$(CEDARX_CHIP_VERSION) \
			$(CEDARX_PATH)/include/include_cedarv \
			$(CEDARX_PATH)/include/include_vencoder \
			$(CEDARX_PATH)/include/include_camera

LOCAL_LDFLAGS += -L$(LOCAL_PATH)

LOCAL_SHARED_LIBRARIES := libcutils libstdc++ libc \
			libutils \
			liblog \

LOCAL_LDLIBS := -ldl 
LOCAL_CFLAGS += -Wfatal-errors -fno-short-enums -D__OS_ANDROID
LOCAL_ARM_MODE := arm	

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libkoti_awenc

LOCAL_SHARED_LIBRARIES := \
						  libcedarv_adapter \
						  libcedarv_base \
						  libcedarxosal \
						  libve \
						  libcedarv \
						  libaw_h264enc
						  
ifeq ($(CEDARX_USE_SUNXI_MEM_ALLOCATOR),Y)
LOCAL_SHARED_LIBRARIES += \
						 libsunxi_alloc \
						 
LOCAL_CFLAGS += -DUSE_SUNXI_MEM_ALLOCATOR						 
endif						    

include $(BUILD_SHARED_LIBRARY)

##################################################################
include $(CLEAR_VARS)

CEDARX_PATH:=$(LOCAL_PATH)/../../../../frameworks/av/media/CedarX-Projects/CedarX/

##A31 A20 use suxi mem
CEDARX_USE_SUNXI_MEM_ALLOCATOR:=N
CEDARX_CHIP_VERSION:=F51

LOCAL_SRC_FILES := \
                        main.c \

LOCAL_C_INCLUDES := $(KERNEL_HEADERS) \
                        $(LOCAL_PATH) \
                        $(LOCAL_PATH)/capture \
                        $(CEDARX_PATH)/include \
                        $(CEDARX_PATH)/include/include_platform/CHIP_$(CEDARX_CHIP_VERSION) \
                        $(CEDARX_PATH)/include/include_cedarv \
                        $(CEDARX_PATH)/include/include_vencoder \
                        $(CEDARX_PATH)/include/include_camera

LOCAL_SHARED_LIBRARIES := libcutils libstdc++ libc \
                        libutils \
                        liblog \

LOCAL_LDLIBS := -ldl
LOCAL_CFLAGS += -Wfatal-errors -fno-short-enums -D__OS_ANDROID
LOCAL_ARM_MODE := arm

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := awenc_test

LOCAL_SHARED_LIBRARIES := libkoti_awenc

include $(BUILD_EXECUTABLE)


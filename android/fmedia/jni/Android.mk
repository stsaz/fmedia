# fmedia/Android: libfmedia.so makefile

LOCAL_PATH := $(call my-dir)
JNI_DIR := ../src/main/c
ROOT := ../../..
FMED_DIR := $(ROOT)/fmedia

include $(CLEAR_VARS)

LOCAL_MODULE := fmedia
LOCAL_SRC_FILES := \
	$(JNI_DIR)/fmt.c \
	$(JNI_DIR)/fmedia-jni.c
LOCAL_C_INCLUDES := \
	$(FMED_DIR)/src \
	$(ROOT)/avpack \
	$(ROOT)/ffos \
	$(ROOT)/ffbase
LOCAL_CFLAGS := -fno-strict-aliasing
# LOCAL_LDFLAGS
LOCAL_LDLIBS := -llog

include $(BUILD_SHARED_LIBRARY)

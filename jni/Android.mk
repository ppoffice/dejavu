LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := record
LOCAL_SRC_FILES := record.c
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := replay
LOCAL_SRC_FILES := replay.c
include $(BUILD_EXECUTABLE)

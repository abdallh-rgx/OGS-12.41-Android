#pragma once
// alog.h — logcat helpers for the Erbium Android port.
#include <android/log.h>

#define ERBIUM_LOG_TAG "Erbium"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, ERBIUM_LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, ERBIUM_LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, ERBIUM_LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, ERBIUM_LOG_TAG, __VA_ARGS__)

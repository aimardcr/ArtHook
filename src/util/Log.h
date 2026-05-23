// SPDX-License-Identifier: Apache-2.0
#ifndef ARTHOOK_UTIL_LOG_H_
#define ARTHOOK_UTIL_LOG_H_

#include <android/log.h>

#define ARTHOOK_LOG_TAG "arthook"

#ifdef ARTHOOK_NO_LOG
#define LOGI(...) ((void)0)
#define LOGW(...) ((void)0)
#define LOGE(...) ((void)0)
#else
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, ARTHOOK_LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, ARTHOOK_LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, ARTHOOK_LOG_TAG, __VA_ARGS__)
#endif

#endif  // ARTHOOK_UTIL_LOG_H_

// pch-android.h — precompiled header for the OGS-12.41-Android port sources.
//
// Adapted from the Erbium-Android port (platform/erbium/): same win32 compat
// layer, but against the full Dumper-7 SDK of the PC 12.41 build (patched for
// Android in SDK/SDK/Basic.hpp + SDK/UnrealContainers.hpp) instead of
// Erbium's minimal SDK.
#pragma once

#define _CRT_SECURE_NO_WARNINGS

#include "compat/win32_compat.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <functional>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

// UTF-16/32 ↔ UTF-8 helpers
#include "AndroidStrings.h"

// The full 12.41 SDK (patched for Android offsets/layouts)
#include "../../SDK/SDK.hpp"
using namespace SDK;

// Module base of the engine library (libUE4.so), set by src/main.cpp at entry.
extern uint64_t ImageBase;
// The RT engine (v6) refers to ModuleBase — alias it.
#define ModuleBase ImageBase

// ─── logging ────────────────────────────────────────────────────────────────
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "OGS", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "OGS", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "OGS", __VA_ARGS__)

// ─── version info (filled at entry; defaults = this port's target build) ───
struct FOGSVersionInfo
{
    float FortniteVersion = 12.41f; // com.epicgames.fortnite1241 (12.41.0-12910313-Android)
    float EngineVersion = 4.25f;    // 4.25.0-12905909+++Fortnite+Release-12.41
};
inline FOGSVersionInfo VersionInfo;

// Memcury (Android ELF64 scanner — same API as the Windows PE version)
#include "Memcury.h"

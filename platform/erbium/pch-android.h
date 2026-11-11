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
#include <iterator>
#include <sstream>
#include <cmath>

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

// MSVC UE macro: TEXT("x") -> char16_t literal on Android
#define TEXT(x) u##x

// u16 string length (wchar wcslen replacement for char16_t)
inline size_t u16len(const char16_t* S)
{
    size_t N = 0;
    while (S && S[N]) ++N;
    return N;
}

// Win32 MultiByteToWideChar (UTF-8 -> UTF-16), the only code page used
#define CP_UTF8 65001
inline int MultiByteToWideChar(unsigned /*CodePage*/, unsigned long /*Flags*/,
    const char* MultiByteStr, int MultiByteLen, char16_t* WideCharStr, int WideCharLen)
{
    // measure or convert; surrogate pairs included
    int Out = 0;
    int i = 0;
    const int InLen = MultiByteLen < 0 ? (int)strlen(MultiByteStr) : MultiByteLen;
    auto Push = [&](char16_t C) -> bool
    {
        if (WideCharStr && Out < WideCharLen) WideCharStr[Out] = C;
        ++Out;
        return true;
    };
    while (i < InLen)
    {
        unsigned char C = (unsigned char)MultiByteStr[i];
        uint32_t Cp = 0; int Len = 0;
        if (C < 0x80) { Cp = C; Len = 1; }
        else if ((C & 0xE0) == 0xC0) { Cp = C & 0x1F; Len = 2; }
        else if ((C & 0xF0) == 0xE0) { Cp = C & 0x0F; Len = 3; }
        else if ((C & 0xF8) == 0xF0) { Cp = C & 0x07; Len = 4; }
        else { ++i; continue; }
        for (int k = 1; k < Len && i + k < InLen; ++k)
            Cp = (Cp << 6) | ((unsigned char)MultiByteStr[i + k] & 0x3F);
        i += Len;
        if (Cp >= 0x10000)
        {
            Push((char16_t)(0xD800 | ((Cp - 0x10000) >> 10)));
            Push((char16_t)(0xDC00 | ((Cp - 0x10000) & 0x3FF)));
        }
        else
            Push((char16_t)Cp);
    }
    if (WideCharStr && Out < WideCharLen)
        WideCharStr[Out] = 0; // null-terminate when space allows
    return Out;
}

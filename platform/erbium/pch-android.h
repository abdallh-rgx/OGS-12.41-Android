// pch-android.h — precompiled header for the Erbium Android port sources.
// Pulls the win32 compat layer + the upstream SDK exactly like Erbium/pch.h,
// minus libcurl/ImGui/Windows console.
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

// UTF-16/32 ↔ UTF-8 helpers (Android UE uses UTF-8 TCHAR)
#include "AndroidStrings.h"

// The upstream SDK — patched with __ANDROID__ branches in Containers/Core/Offsets.
#include "../../SDK/Includes.h"
using namespace SDK;

// Module base of the engine library (libUE4.so / libUnreal.so), set by entry.
extern uint64_t ImageBase;
// The RT engine (v6) refers to ModuleBase — alias it.
#define ModuleBase ImageBase

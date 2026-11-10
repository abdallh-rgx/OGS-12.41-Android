// ============================================================================
// Finders-Android-stubs.cpp — Find_* declared in Finders.h that are not in
// the v6 catalog (Windows-only pattern targets or covered by other chains).
// They try sensible RT string chains and fall back to 0 (caller skips).
// ============================================================================
#include "pch-android.h"
#include "RT.h"
#include "../../Erbium/Erbium/Public/Finders.h"

// The engine module base — set by the entry bootstrap (entry.cpp).
uint64_t ImageBase = 0;

uint64_t FindCanPlaceBuildableClassInStructuralGrid()
{
    static uint64_t Result = 0;
    static bool bInit = false;
    if (!bInit)
    {
        bInit = true;
        const wchar_t* _ss[] = { L"CanPlaceBuildableClassInStructuralGrid" };
        for (auto _s : _ss)
        {
            Result = RT::StringAnchorFunc(_s);
            if (Result) break;
        }
    }
    return Result;
}

uint64_t FindCreateNetDriver()
{
    return 0; // not needed on this path (Windows-only pattern target)
}

uint64_t FindEncryptionPatch()
{
    static uint64_t Result = 0;
    static bool bInit = false;
    if (!bInit)
    {
        bInit = true;
        const wchar_t* _ss[] = { L"PacketHandler", L"Encryption" };
        for (auto _s : _ss)
        {
            Result = RT::StringAnchorFunc(_s);
            if (Result) break;
        }
    }
    return Result;
}

uint64_t FindMinigameSettingsBuilding__BeginPlay()
{
    return 0; // not needed on this path (Windows-only pattern target)
}

uint64_t FindSetIsDoorOpen()
{
    static uint64_t Result = 0;
    static bool bInit = false;
    if (!bInit)
    {
        bInit = true;
        const wchar_t* _ss[] = { L"IsDoorOpen" };
        for (auto _s : _ss)
        {
            Result = RT::StringAnchorFunc(_s);
            if (Result) break;
        }
    }
    return Result;
}

uint64_t FindUpdateIrisReplicationViews()
{
    return 0; // not needed on this path (Windows-only pattern target)
}

// FindLoadPlayset — Windows walks LoadPlayset-bytes recursively; Android uses
// the playset string anchor + backscan (creative portal path only).
uint64_t FindLoadPlayset(const std::vector<uint8_t>& Bytes, int recursive)
{
    (void)Bytes; (void)recursive;
    static uint64_t Result = 0;
    static bool bInit = false;
    if (!bInit)
    {
        bInit = true;
        const wchar_t* ss[] = { L"LoadPlayset", L"Playset" };
        for (auto s : ss)
        {
            Result = RT::StringAnchorFunc(s);
            if (Result) break;
        }
    }
    return Result;
}

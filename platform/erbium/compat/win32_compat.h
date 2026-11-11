// ============================================================================
// win32_compat.h — fake "Windows.h" for the Erbium Android (ARM64) port.
//
// The upstream Erbium sources include <windows.h> via framework.h / pch.h and
// use a small, well-known subset of the Win32 API. On Android we provide the
// same symbols mapped onto POSIX equivalents so the game-logic sources compile
// unmodified. This header is force-included through a shim "windows.h" placed
// on the include path ahead of any system header.
// ============================================================================
#pragma once

#include <android/log.h>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>
#include <strings.h>

// ─── logging (AllocConsole/printf go to logcat via alog.h) ─────────────────
#include "alog.h"

// ─── basic Win32 types ───────────────────────────────────────────────────────
using DWORD = uint32_t;
using WORD = uint16_t;
using BYTE = uint8_t;
using BOOL = int;
using LONG = int32_t;
using ULONG = uint32_t;
using HANDLE = void*;
using HMODULE = void*;
using HINSTANCE = void*;
using LPVOID = void*;
using LPCVOID = const void*;
using SIZE_T = size_t;
using LPDWORD = uint32_t*;
using PDWORD = uint32_t*;
using PBYTE = uint8_t*;
using LPBYTE = uint8_t*;
using PWORD = uint16_t*;
using UINT = uint32_t;
using UINT64_ALIAS = uint64_t;
using PLONG = int32_t*;
using LPTHREAD_START_ROUTINE = DWORD (*)(LPVOID);
using FARPROC = void*;
using CRITICAL_SECTION = struct { void* opaque[8]; };
using errno_t = int;

union LARGE_INTEGER
{
    struct { DWORD LowPart; LONG HighPart; } u;
    int64_t QuadPart;
};
union ULARGE_INTEGER
{
    struct { DWORD LowPart; DWORD HighPart; } u;
    uint64_t QuadPart;
};

// MSVC integer types (macro style — 'unsigned __int8' can't be a using-alias)
#define __int8 int8_t
#define __int16 int16_t
#define __int32 int32_t
#define __int64 int64_t

#define WINAPI
#define CALLBACK
#define APIENTRY
#define VOID void

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef MAX_PATH
#define MAX_PATH 260
#endif

// page protection constants (values are irrelevant; we map by name)
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_EXECUTE_READ 0x20
#define PAGE_READWRITE 0x04
#define PAGE_READONLY 0x02
#define PAGE_EXECUTE 0x10
#define PAGE_NOACCESS 0x01

inline DWORD GetLastError() { return (DWORD)errno; }

// ─── memory protection / allocation ──────────────────────────────────────────
inline BOOL VirtualProtect(LPVOID lpAddress, SIZE_T dwSize, DWORD flNewProtect, PDWORD lpflOldProtect)
{
    const uintptr_t page = 4096;
    uintptr_t addr = (uintptr_t)lpAddress;
    uintptr_t pageStart = addr & ~(page - 1);
    uintptr_t pageEnd = (addr + dwSize + page - 1) & ~(page - 1);

    int prot;
    switch (flNewProtect)
    {
    case PAGE_EXECUTE_READWRITE: prot = PROT_EXEC | PROT_READ | PROT_WRITE; break;
    case PAGE_EXECUTE_READ: prot = PROT_EXEC | PROT_READ; break;
    case PAGE_READWRITE: prot = PROT_READ | PROT_WRITE; break;
    case PAGE_READONLY: prot = PROT_READ; break;
    case PAGE_EXECUTE: prot = PROT_EXEC; break;
    default: prot = PROT_READ | PROT_WRITE; break;
    }

    // query old protection best-effort via /proc/self/maps is overkill; report RW
    if (lpflOldProtect)
        *lpflOldProtect = PAGE_READWRITE;

    return mprotect((void*)pageStart, pageEnd - pageStart, prot) == 0 ? TRUE : FALSE;
}

inline LPVOID VirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect)
{
    (void)flAllocationType; (void)flProtect;
    return mmap(lpAddress, dwSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | (lpAddress ? MAP_FIXED : 0), -1, 0);
}

#define MEM_RELEASE 0x8000
#define MEM_DECOMMIT 0x4000
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000

inline BOOL VirtualFree(LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType)
{
    (void)dwSize; (void)dwFreeType;
    return munmap(lpAddress, dwSize ? dwSize : 4096) == 0 ? TRUE : FALSE;
}

// ─── modules ─────────────────────────────────────────────────────────────────
inline FARPROC GetProcAddress(HMODULE hModule, const char* lpProcName)
{
    return (FARPROC)dlsym(hModule, lpProcName);
}

inline HMODULE LoadLibraryA(const char* lpFileName)
{
    return dlopen(lpFileName, RTLD_NOW);
}

inline HMODULE GetModuleHandleA(const char* lpModuleName)
{
    if (!lpModuleName)
        return dlopen(nullptr, RTLD_NOW | RTLD_NOLOAD);
    return dlopen(lpModuleName, RTLD_NOW | RTLD_NOLOAD);
}

// ─── threads / timing ────────────────────────────────────────────────────────
inline void Sleep(DWORD dwMilliseconds) { usleep(dwMilliseconds * 1000); }

inline BOOL QueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    lpPerformanceCount->QuadPart = (int64_t)ts.tv_sec * 1000000000ll + ts.tv_nsec;
    return TRUE;
}

inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* lpFrequency)
{
    lpFrequency->QuadPart = 1000000000ll;
    return TRUE;
}

inline DWORD GetTickCount() { return (DWORD)(clock() / (CLOCKS_PER_SEC / 1000)); }

inline DWORD GetCurrentThreadId() { return (DWORD)gettid(); }
inline HANDLE GetCurrentProcess() { return (HANDLE)-1; }
inline BOOL TerminateProcess(HANDLE, UINT code) { exit(code); return TRUE; }

inline BOOL FlushInstructionCache(HANDLE, LPCVOID addr, SIZE_T len)
{
    if (addr)
        __builtin___clear_cache((char*)addr, (char*)addr + (len ? len : 4));
    return TRUE;
}

inline unsigned char BitTest(const LONG* b, unsigned char bit) { return (*b >> bit) & 1; }
inline unsigned char BitTestAndReset(LONG* b, unsigned char bit) { unsigned char r = (*b >> bit) & 1; *b &= ~(1L << bit); return r; }

// Interlocked* → GCC/Clang builtins
inline LONG InterlockedIncrement(LONG volatile* p) { return __sync_add_and_fetch(p, 1); }
inline LONG InterlockedDecrement(LONG volatile* p) { return __sync_sub_and_fetch(p, 1); }
inline LONG InterlockedExchange(LONG volatile* p, LONG v) { return __sync_lock_test_and_set(p, v); }

inline int IsBadReadPtr(const void* p, size_t size = 8)
{
    // best-effort: check the first + last page are readable via /proc/self/maps
    if (!p)
        return 1;
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f)
        return 0;
    char line[512];
    uintptr_t a = (uintptr_t)p, b = (uintptr_t)p + (size ? size : 1) - 1;
    int okA = 0, okB = 0;
    while (fgets(line, sizeof(line), f) && !(okA && okB))
    {
        uintptr_t s, e;
        char perms[8] = {0};
        if (sscanf(line, "%lx-%lx %3s", &s, &e, perms) >= 3)
        {
            if (perms[0] != 'r')
                continue;
            if (!okA && s <= a && a < e) okA = 1;
            if (!okB && s <= b && b < e) okB = 1;
        }
    }
    fclose(f);
    return (okA && okB) ? 0 : 1;
}

// MSVC-style unqualified min/max (windows.h provides them on Windows)
#include <algorithm>
using std::min;
using std::max;

struct GUID
{
    uint32_t A;
    uint16_t B, C;
    uint8_t D[8];
};

inline int CoCreateGuid(GUID* g)
{
    // random GUID v4 via /dev/urandom
    FILE* f = fopen("/dev/urandom", "rb");
    if (!f)
        return 1;
    size_t rd = fread(g, 1, sizeof(*g), f);
    fclose(f);
    if (rd != sizeof(*g))
        return 1;
    g->D[0] = (uint8_t)((g->D[0] & 0x3F) | 0x80); // variant
    g->C = (uint16_t)((g->C & 0x0FFF) | 0x4000);  // version 4
    return 0;
}

// ─── console (no console on Android — logcat only) ───────────────────────────
inline BOOL AllocConsole() { return TRUE; }
inline void* GetConsoleWindow() { return nullptr; }
inline BOOL SetConsoleTitleA(const char*) { return TRUE; }
inline BOOL SetConsoleTitleW(const wchar_t*) { return TRUE; }

inline int MessageBoxA(void*, const char* text, const char* caption, unsigned)
{
    LOGE("MessageBoxA: [%s] %s", caption ? caption : "", text ? text : "");
    return 0;
}
inline int MessageBoxW(void*, const wchar_t* text, const wchar_t* caption, unsigned)
{
    LOGE("MessageBoxW (wide): [%ls] %ls", caption ? caption : L"", text ? text : L"");
    return 0;
}

// ─── safe CRT → C99 ─────────────────────────────────────────────────────────
inline int sprintf_s(char* buf, size_t size, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

// MSVC array-size template overload: sprintf_s(char (&)[N], fmt, ...)
template <size_t N>
inline int sprintf_s(char (&buf)[N], const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, N, fmt, ap);
    va_end(ap);
    return r;
}

inline int swprintf_s(wchar_t* buf, size_t size, const wchar_t* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vswprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

inline int sscanf_s(const char* s, const char* fmt, ...) { return 0; } // unused on this path

inline errno_t freopen_s(FILE** out, const char* path, const char* mode, FILE* stream)
{
    *out = freopen(path, mode, stream);
    return *out ? 0 : errno;
}

inline errno_t gmtime_s(struct tm* out, const time_t* t) { return gmtime_r(t, out) ? 0 : errno; }
inline errno_t localtime_s(struct tm* out, const time_t* t) { return localtime_r(t, out) ? 0 : errno; }

inline errno_t strcpy_s(char* dst, size_t size, const char* src)
{
    if (!dst || !src || strlen(src) + 1 > size)
        return ERANGE;
    strcpy(dst, src);
    return 0;
}

inline errno_t wcscpy_s(wchar_t* dst, size_t size, const wchar_t* src)
{
    if (!dst || !src || wcslen(src) + 1 > size)
        return ERANGE;
    wcscpy(dst, src);
    return 0;
}

inline errno_t memcpy_s(void* dst, size_t dstSize, const void* src, size_t count)
{
    if (count > dstSize)
        return ERANGE;
    memcpy(dst, src, count);
    return 0;
}

// case-insensitive compares
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define _snprintf snprintf
#define _vsnprintf vsnprintf

inline int _wcsicmp(const wchar_t* a, const wchar_t* b) { return wcscasecmp(a, b); }
inline int _wcsnicmp(const wchar_t* a, const wchar_t* b, size_t n) { return wcsncasecmp(a, b, n); }

// MSVC intrinsics
#ifndef _MSC_VER
#define __forceinline inline __attribute__((always_inline))
#endif

// warnings/pragma shims
#pragma GCC diagnostic ignored "-Wunused-value"

// __pragma warning suppress → no-op on clang
#define __pragma(x)

// pragma comment(lib, ...) → nothing
#define pragma_comment_ignore

// WinMain / DllMain never used on Android (JNI_OnLoad in entry.cpp instead)

// DbgHelp.h / dbghelp lib — referenced by Memcury.h upstream; our Memcury
// port (android/erbium/Memcury.h) does not include it.

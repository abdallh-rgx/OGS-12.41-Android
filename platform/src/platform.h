#pragma once
// platform.h — Android platform utilities for the Erbium port.
// Replaces the Windows/Memcury module layer with /proc/self/maps based lookups.
#include <stdint.h>
#include <stddef.h>

namespace Erbium
{
    struct ModuleInfo
    {
        uintptr_t base;
        size_t size;
        char path[256];
    };

    // Find a loaded module by soname (e.g. "libUE4.so"). Returns false if not mapped.
    bool GetModuleInfo(const char* moduleName, ModuleInfo* outInfo);

    // Convenience: base address or 0.
    uintptr_t GetModuleBase(const char* moduleName);

    // Page-aligned write protection switch (VirtualProtect replacement).
    bool ProtectMemory(void* address, size_t size, int prot /* PROT_* */);

    // Raw write that flips pages RWX around the target then restores.
    bool WriteMemory(void* address, const void* data, size_t size);

    // Wait until a module is mapped, up to timeoutSeconds. Returns base or 0.
    uintptr_t WaitForModule(const char* moduleName, int timeoutSeconds);
}

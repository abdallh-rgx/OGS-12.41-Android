// platform.cpp — /proc/self/maps based module + memory utilities (Android).
#include "platform.h"

#include <android/log.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
#include <sys/mman.h>
#include <unistd.h>

#define LOGERR(...) __android_log_print(ANDROID_LOG_ERROR, "Erbium", __VA_ARGS__)

namespace Erbium
{
    static bool ParseMapsForModule(const char* moduleName, ModuleInfo* outInfo)
    {
        FILE* f = fopen("/proc/self/maps", "r");
        if (!f)
            return false;

        char line[512];
        uintptr_t base = 0, end = 0;
        char path[256] = {0};
        char foundPath[256] = {0};

        while (fgets(line, sizeof(line), f))
        {
            uintptr_t start, stop;
            char perms[8] = {0};
            char dev[32] = {0};
            unsigned long inode = 0;
            int consumed = 0;
            // addr perms offset dev inode path
            if (sscanf(line, "%lx-%lx %7s %*s %31s %lu %n", &start, &stop, perms, dev, &inode, &consumed) < 5)
                continue;

            const char* mapPath = line + consumed;
            // Trim
            while (*mapPath == ' ') mapPath++;
            size_t plen = strlen(mapPath);
            while (plen > 0 && (mapPath[plen-1] == '\n' || mapPath[plen-1] == '\r')) plen--;

            // Match module by basename
            const char* slash = strrchr(mapPath, '/');
            const char* baseName = slash ? slash + 1 : mapPath;
            if (strncmp(baseName, moduleName, plen - (baseName - mapPath)) == 0 &&
                strlen(moduleName) == plen - (baseName - mapPath))
            {
                if (base == 0)
                {
                    base = start;
                    strncpy(foundPath, mapPath, sizeof(foundPath) - 1);
                }
                end = stop > end ? stop : end;
            }
        }
        fclose(f);

        if (base == 0)
            return false;

        if (outInfo)
        {
            outInfo->base = base;
            outInfo->size = end - base;
            memcpy(outInfo->path, foundPath, sizeof(outInfo->path));
        }
        return true;
    }

    bool GetModuleInfo(const char* moduleName, ModuleInfo* outInfo)
    {
        return ParseMapsForModule(moduleName, outInfo);
    }

    uintptr_t GetModuleBase(const char* moduleName)
    {
        ModuleInfo info;
        if (!GetModuleInfo(moduleName, &info))
            return 0;
        return info.base;
    }

    bool ProtectMemory(void* address, size_t size, int prot)
    {
        const long pageSize = sysconf(_SC_PAGESIZE);
        uintptr_t page = (uintptr_t)address & ~(uintptr_t)(pageSize - 1);
        uintptr_t pageEnd = ((uintptr_t)address + size + pageSize - 1) & ~(uintptr_t)(pageSize - 1);
        return mprotect((void*)page, pageEnd - page, prot) == 0;
    }

    bool WriteMemory(void* address, const void* data, size_t size)
    {
        if (!ProtectMemory(address, size, PROT_READ | PROT_WRITE | PROT_EXEC))
            return false;
        memcpy(address, data, size);
        // Restore to executable (leaving writable would also be acceptable, but be tidy)
        ProtectMemory(address, size, PROT_READ | PROT_EXEC);
        __builtin___clear_cache((char*)address, (char*)address + size);
        return true;
    }

    uintptr_t WaitForModule(const char* moduleName, int timeoutSeconds)
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
        for (;;)
        {
            uintptr_t base = GetModuleBase(moduleName);
            if (base)
                return base;
            if (std::chrono::steady_clock::now() >= deadline)
                return 0;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}

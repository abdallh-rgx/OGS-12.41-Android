// Minimal SDK smoke-test TU (placeholder — replaced by the full port).
#include "../SDK/SDK.hpp"
#include <cstdio>

extern "C" __attribute__((visibility("default"))) void OGS_Entry()
{
    printf("OGS SDK smoke test: GObjects=%p GNames=%p\n",
        (void*)(SDK::InSDKUtils::GetImageBase() + SDK::Offsets::GObjects),
        (void*)(SDK::InSDKUtils::GetImageBase() + SDK::Offsets::GNames));
}

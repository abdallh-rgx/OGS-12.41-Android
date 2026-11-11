#pragma once
#include "framework.h"

namespace PE {
    static void* (*ProcessEventOG)(UObject*, UFunction*, void*);
    void* ProcessEvent(UObject* Obj, UFunction* Function, void* Params)
    {
        if (Function) {

        }

        return ProcessEventOG(Obj, Function, Params);
    }

    void Hook() {
        OGS_HOOK(0x02E13BF0, ProcessEvent, ProcessEventOG);

        Log("PE Hooked!");
    }
}
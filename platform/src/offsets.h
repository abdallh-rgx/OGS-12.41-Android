#pragma once
// offsets.h — version selector for baked offsets.
//
// ERBIUM_TARGET_VERSION is defined by the CMake build (default 21.30, or
// whatever the CI analyze step baked). Only versions with a completed bake
// in android/Generated/ are selectable; an unknown version fails the compile
// on purpose so CI can never ship a liberbium.so with mismatched offsets.
#ifndef ERBIUM_TARGET_VERSION
#define ERBIUM_TARGET_VERSION 21.30
#endif

#define ERBIUM_PP_STR2(x) #x
#define ERBIUM_PP_STR(x) ERBIUM_PP_STR2(x)
#include ERBIUM_PP_STR(offsets-ERBIUM_TARGET_VERSION.h)

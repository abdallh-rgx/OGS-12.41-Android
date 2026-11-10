cmake_minimum_required(VERSION 3.18)
project(OGS-12.41-Android CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(ANDROID_ABI "arm64-v8a" CACHE STRING "Android ABI")
set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)

# ── OGS Android library ────────────────────────────────────────────────────
add_library(OGS-12.41 SHARED
    src/main.cpp
    src/PE.cpp
    src/NativeResolver.cpp
    src/ModuleHooks.cpp
    src/GuiTouchInput.cpp
    src/GuiMenu.cpp
    src/GuiRenderer.cpp
    src/ListenServer.cpp
    src/JoinByIP.cpp
    src/AndroidGlue.cpp
    platform/erbium/HookingARM64.cpp
    platform/erbium/Finders-Android.cpp
)

target_include_directories(OGS-12.41 PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/SDK
    ${CMAKE_SOURCE_DIR}/platform/erbium
    ${CMAKE_SOURCE_DIR}/platform/src
)

target_compile_definitions(OGS-12.41 PRIVATE
    OGS_ANDROID=1
    $<$<CONFIG:RelWithDebInfo>:OGS_LOG_VERBOSE=1>
)

target_compile_options(OGS-12.41 PRIVATE
    -Wall -Wno-unknown-attributes -Wno-unused-function
    -Wno-invalid-constexpr -Wno-c++11-narrowing -Wno-enum-constexpr-conversion
    -Wno-deprecated-copy -Wno-invalid-offsetof -Wno-zero-as-null-pointer-constant
    -Wno-null-conversion -Wno-nullability-completeness
    -fno-omit-frame-pointer
    -ffunction-sections -fdata-sections
)

# SDK TU is huge (~2 GB RAM per clang) — keep parallelism low on CI runners.
# (The GitHub workflow builds with -j 2 for the same reason.)

target_link_options(OGS-12.41 PRIVATE
    -Wl,--gc-sections
    -Wl,-z,max-page-size=16384
    -llog        # __android_log_print
    -landroid    # ANativeActivity / input queue
    -lEGL -lGLESv3  # GUI overlay (eglSwapBuffers hook + OpenGL ES 3)
    -ldl
)

# hide everything by default; export only the constructor/JNI entry
set_target_properties(OGS-12.41 PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

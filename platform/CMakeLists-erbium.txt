cmake_minimum_required(VERSION 3.22)
project(erbium-android LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_compile_options(-Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
                    -Wno-deprecated-declarations -Wno-sign-compare -fdeclspec)

# ── target version bake (fast path; runtime chains cover everything else) ───
set(ERBIUM_TARGET_VERSION "21.30" CACHE STRING "Fortnite version whose baked offsets are compiled in")
message(STATUS "Erbium: target version ${ERBIUM_TARGET_VERSION}")

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Generated/sdk-${ERBIUM_TARGET_VERSION}.h")
    message(FATAL_ERROR "No baked SDK for ${ERBIUM_TARGET_VERSION} — run android/erbium/bake.py")
endif()

# ── the port layer (platform + engine + full server sources) ────────────────
set(ERBIUM_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../Erbium)

set(PLATFORM_SOURCES
    src/platform.cpp
    erbium/SDKBootstrap.cpp
    erbium/HookingARM64.cpp
    erbium/Finders-Android.cpp
    erbium/Finders-Android-stubs.cpp
    erbium/dllmain-android.cpp
)

set(SERVER_SOURCES
    ${ERBIUM_SRC_DIR}/pch.cpp
    ${ERBIUM_SRC_DIR}/Engine/Private/AbilitySystemComponent.cpp
    ${ERBIUM_SRC_DIR}/Engine/Private/NetDriver.cpp
    ${ERBIUM_SRC_DIR}/Erbium/Private/Events.cpp
    ${ERBIUM_SRC_DIR}/Erbium/Private/LateGame.cpp
    ${ERBIUM_SRC_DIR}/Erbium/Private/Misc.cpp
    ${ERBIUM_SRC_DIR}/FortniteGame/Private/BattleRoyaleGamePhaseLogic.cpp
    ${ERBIUM_SRC_DIR}/FortniteGame/Private/BuildingFoundation.cpp
    ${ERBIUM_SRC_DIR}/FortniteGame/Private/BuildingSMActor.cpp
    ${ERBIUM_SRC_DIR}/FortniteGame/Private/FortAthenaCreativePortal.cpp
    ${ERBIUM_SRC_DIR}/FortniteGame/Private/FortAthenaMutator.cpp
    ${ERBIUM_SRC_DIR}/FortniteGame/Private/FortGameMode.cpp
    ${ERBIUM_SRC_DIR}/FortniteGame/Private/FortInventory.cpp
    ${ERBIUM_SRC_DIR}/FortniteGame/Private/FortKismetLibrary.cpp
    ${ERBIUM_SRC_DIR}/FortniteGame/Private/FortLootPackage.cpp
    ${ERBIUM_SRC_DIR}/FortniteGame/Private/FortMinigame.cpp
    ${ERBIUM_SRC_DIR}/FortniteGame/Private/FortMissionLibrary.cpp
    ${ERBIUM_SRC_DIR}/FortniteGame/Private/FortPhysicsPawn.cpp
    ${ERBIUM_SRC_DIR}/FortniteGame/Private/FortPlayerControllerAthena.cpp
    ${ERBIUM_SRC_DIR}/FortniteGame/Private/FortPlayerPawnAthena.cpp
    ${ERBIUM_SRC_DIR}/FortniteGame/Private/FortQuestManager.cpp
    ${ERBIUM_SRC_DIR}/FortniteGame/Private/FortVehicleSeatWeaponComponent.cpp
)

# Windows-only sources EXCLUDED from the Android build (replaced by the port):
#   Erbium/Private/dllmain.cpp    → android/erbium/dllmain-android.cpp
#   Erbium/Private/Finders.cpp    → android/erbium/Finders-Android.cpp (v6)
#   Erbium/Private/Hooking.cpp    → android/erbium/HookingARM64.cpp
#   Erbium/Private/hde64.cpp      → (x64 length disassembler — N/A on ARM64)
#   Erbium/Private/GUI.cpp        → (ImGui/DX11 — no GUI on Android)
#   ImGui/*                       → (same)
#   Plugins/CrashReporter         → (SEH — logcat tombstones instead)

add_library(erbium SHARED
    src/entry.cpp
    src/owen.c
    ${PLATFORM_SOURCES}
    ${SERVER_SOURCES}
)

target_include_directories(erbium PRIVATE
    src
    erbium
    Generated
    ${ERBIUM_SRC_DIR}          # pch.h + "Erbium/Public/..." style includes
    ${ERBIUM_SRC_DIR}/..        # "../SDK/..." style includes
)
target_compile_definitions(erbium PRIVATE
    ERBIUM_TARGET_VERSION=${ERBIUM_TARGET_VERSION}
    ERBIUM_HAS_OWEN=1
)

# logcat + dlopen
find_library(log-lib log)
find_library(dl-lib dl)
target_link_libraries(erbium PRIVATE ${log-lib} ${dl-lib})

# upstream source files include "pch.h" first — force-include our compat layer
# so the Windows headers map to POSIX
set_source_files_properties(${SERVER_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/erbium/pch-android.h")

# sanity: artifact must be arm64
add_custom_command(TARGET erbium POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Built liberbium.so for ${ERBIUM_TARGET_VERSION}")

// GuiTouchInput.cpp — 4-finger / 2-second gesture detection.
#include "framework.h"
#include "GuiTouchInput.h"
#include "Hooks.h"
#include "imgui.h"

#include <android/input.h>
#include <android/log.h>
#include <chrono>
#include <dlfcn.h>
#include <mutex>

namespace Gui
{

bool bMenuOpen = false;

namespace
{

constexpr int32_t kRequiredPointers = 4;
constexpr std::chrono::milliseconds kHoldTime{ 2000 };

std::mutex g_Mutex;
int32_t g_PointerCount = 0;
std::chrono::steady_clock::time_point g_HoldStart{};
bool g_Holding = false;
bool g_ToggleQueued = false;

// ─── AInputQueue_getEvent hook (libandroid) ────────────────────────────────
// Signature: int32_t AInputQueue_getEvent(AInputQueue*, AInputEvent**);
int32_t (*AInputQueue_getEventOG)(void*, void**);

int32_t AInputQueue_getEvent(void* Queue, void** OutEvent)
{
	const int32_t Result = AInputQueue_getEventOG(Queue, OutEvent);
	if (Result >= 0 && OutEvent && *OutEvent)
	{
		const int32_t Type = AInputEvent_getType((AInputEvent*)*OutEvent);
		if (Type == AINPUT_EVENT_TYPE_MOTION)
		{
			const int32_t Action = AMotionEvent_getAction((AInputEvent*)*OutEvent);
			const int32_t ActionMasked = Action & AMOTION_EVENT_ACTION_MASK;
			switch (ActionMasked)
			{
			case AMOTION_EVENT_ACTION_POINTER_DOWN:
			case AMOTION_EVENT_ACTION_DOWN:
			case AMOTION_EVENT_ACTION_MOVE:
			case AMOTION_EVENT_ACTION_POINTER_UP:
			case AMOTION_EVENT_ACTION_UP:
			case AMOTION_EVENT_ACTION_CANCEL:
				OnPointerUpdate(AMotionEvent_getPointerCount((AInputEvent*)*OutEvent));
				break;
			default:
				break;
			}
		}
	}
	return Result;
}

} // namespace

void OnPointerUpdate(int32_t PointerCount)
{
	std::lock_guard<std::mutex> Lock(g_Mutex);
	g_PointerCount = PointerCount;

	const auto Now = std::chrono::steady_clock::now();

	if (PointerCount >= kRequiredPointers)
	{
		if (!g_Holding)
		{
			g_Holding = true;
			g_HoldStart = Now;
		}
		else if (Now - g_HoldStart >= kHoldTime && !g_ToggleQueued)
		{
			// exactly one toggle per gesture
			g_ToggleQueued = true;
			LOGI("Gui: 4-finger 2-second gesture satisfied — toggling menu");
		}
	}
	else
	{
		g_Holding = false;
	}
}

bool IsGestureSatisfied()
{
	std::lock_guard<std::mutex> Lock(g_Mutex);
	return g_Holding && (std::chrono::steady_clock::now() - g_HoldStart) >= kHoldTime;
}

bool ConsumeToggleRequest()
{
	std::lock_guard<std::mutex> Lock(g_Mutex);
	if (g_ToggleQueued)
	{
		g_ToggleQueued = false;
		bMenuOpen = !bMenuOpen;
		return true;
	}
	return false;
}

void InitTouchDetection()
{
	static bool Initialized = false;
	if (Initialized)
		return;
	Initialized = true;

	// AInputQueue_getEvent lives in libandroid.so — a global, exported, and
	// always-mapped system library: hook it directly by dlsym address.
	void* Sym = dlsym(RTLD_DEFAULT, "AInputQueue_getEvent");
	if (!Sym)
		Sym = dlsym(RTLD_DEFAULT, "AInputQueue_preDispatchEvent");

	if (!Sym)
	{
		LOGW("Gui: AInputQueue_getEvent not found — gesture detection disabled");
		return;
	}

	if (!Hooking::InternalHook((uintptr_t)Sym, (void*)AInputQueue_getEvent,
		(void**)&AInputQueue_getEventOG))
	{
		LOGW("Gui: failed to hook AInputQueue_getEvent — gesture detection disabled");
		return;
	}

	LOGI("Gui: AInputQueue_getEvent hooked — 4-finger/2s gesture armed");
}

} // namespace Gui

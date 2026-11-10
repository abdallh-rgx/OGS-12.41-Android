// ============================================================================
// GuiTouchInput.h — multi-touch gesture detection: 4 fingers held for 2
// seconds toggles the GUI (the user's required interaction).
//
// Touch events are intercepted by hooking AInputQueue_getEvent (libandroid);
// every AMotionEvent updates the pointer set, and the gesture thread checks
// whether ≥4 pointers stayed down continuously for 2000 ms.
// ============================================================================
#pragma once

#include <cstdint>

namespace Gui
{
	// Start the input hook + gesture tracker (idempotent).
	void InitTouchDetection();

	// Called by the input hook for every motion event (pointer count changes).
	void OnPointerUpdate(int32_t PointerCount);

	// Gesture state for the renderer/menu.
	bool IsGestureSatisfied();     // 4 fingers held ≥ 2000 ms (level-triggered)
	bool ConsumeToggleRequest();   // edge-triggered toggle (once per gesture)

	// Whether the menu is currently open.
	extern bool bMenuOpen;
}

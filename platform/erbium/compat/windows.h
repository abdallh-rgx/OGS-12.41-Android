// Fake <windows.h> — pulls the Android compat layer. This file sits on the
// include path ahead of everything so upstream `#include <windows.h>` compiles.
#pragma once
#include "win32_compat.h"

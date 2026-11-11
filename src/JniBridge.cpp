#include <jni.h>

namespace Gui { extern bool bMenuOpen; }   // or #include "GuiTouchInput.h"

extern "C" JNIEXPORT void JNICALL
Java_com_epicgames_ue4_GameActivity_onFourFingerHold(JNIEnv*, jclass)
{
    Gui::bMenuOpen = !Gui::bMenuOpen;
}

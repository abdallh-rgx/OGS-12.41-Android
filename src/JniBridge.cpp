#include <jni.h>
#include "GuiTouchInput.h"

extern "C" JNIEXPORT void JNICALL
Java_com_epicgames_ue4_GameActivity_onFourFingerHold(JNIEnv*, jclass)
{
    Gui::QueueMenuToggle();
}

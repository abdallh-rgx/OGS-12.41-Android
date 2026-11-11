#include <jni.h>

extern bool bMenuOpen;

extern "C" JNIEXPORT void JNICALL
Java_com_epicgames_ue4_GameActivity_onFourFingerHold(JNIEnv*, jclass)
{
    bMenuOpen = !bMenuOpen;
}

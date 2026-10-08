#include "BoardProfile.h"

#if defined(HARDWARE_PROFILE_ESP32_DEV)
#include "profiles/Esp32DevProfile.h"
static Esp32DevProfile s_activeProfile;
#elif defined(HARDWARE_PROFILE_WAVESHARE_S3)
#include "profiles/WaveshareS3Profile.h"
static WaveshareS3Profile s_activeProfile;
#else
#include "profiles/Esp32DevProfile.h"
static Esp32DevProfile s_activeProfile;
#endif

IBoardProfile& BoardProfile::current() {
    return s_activeProfile;
}

HwProfile BoardProfile::currentId() {
    return s_activeProfile.id();
}

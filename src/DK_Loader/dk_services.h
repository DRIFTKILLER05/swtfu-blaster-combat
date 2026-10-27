// DK_Loader services (API v2), implemented in dk_services.cpp.
#pragma once
#include <windows.h>
#include "dk_plugin.h"

void Svc_Init(DK_LoaderApi* api, const char* gameDir, volatile LONG* frame, volatile LONG* lastFrameTick);
void Svc_HookPluginConfig(HMODULE plugin);   // ini overlay: the plugin's GetPrivateProfile*A imports
void Svc_InstallCrashFilter();

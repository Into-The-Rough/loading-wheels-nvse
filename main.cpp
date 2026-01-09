#include "nvse/PluginAPI.h"
#include "internal/WheelRandomiser.hpp"
#include <windows.h>

constexpr char PLUGIN_NAME[] = "WheelRandomiserNVSE";
constexpr UInt32 PLUGIN_VERSION = 1;

static PluginHandle g_pluginHandle = kPluginHandle_Invalid;

extern "C" {

bool NVSEPlugin_Query(const NVSEInterface* nvse, PluginInfo* info) {
	info->infoVersion = PluginInfo::kInfoVersion;
	info->name = PLUGIN_NAME;
	info->version = PLUGIN_VERSION;

	if (nvse->nvseVersion < PACKED_NVSE_VERSION) return false;
	if (nvse->isEditor) return false;
	if (nvse->runtimeVersion < RUNTIME_VERSION_1_4_0_525) return false;
	if (nvse->isNogore) return false;

	return true;
}

bool NVSEPlugin_Load(const NVSEInterface* nvse) {
	g_pluginHandle = nvse->GetPluginHandle();

	auto* messaging = static_cast<NVSEMessagingInterface*>(nvse->QueryInterface(kInterface_Messaging));
	if (!messaging) return false;

	bool randomOnly = GetPrivateProfileIntA("General", "bRandomOnly", 0, ".\\Data\\Config\\WheelRandomiserNVSE.ini") != 0;

	WheelRandomiser::Init(messaging, g_pluginHandle, randomOnly);
	return true;
}

}

#include "WheelRandomiser.hpp"
#include "FactionDetection.hpp"
#include "../shared/SafeWrite/SafeWrite.hpp"

#include "nvse/GameData.h"
#include "nvse/GameForms.h"
#include "nvse/GameObjects.h"
#include "internal/netimmerse.h"

#include <windows.h>
#include <string>
#include <vector>
#include <ctime>
#include <algorithm>

NiNode* NiNode::GetNode(const char* name) {
	typedef NiNode* (__thiscall* Fn)(NiNode*, const char*);
	return ((Fn)0x006FFD90)(this, name);
}

NiAVObject* NiNode::GetBlock(const char* name) {
	typedef NiAVObject* (__thiscall* Fn)(NiNode*, const char*);
	return ((Fn)0x006FFE20)(this, name);
}

namespace WheelRandomiser {

static bool g_randomOnly = false;
static PluginHandle g_pluginHandle = kPluginHandle_Invalid;
static NVSEMessagingInterface* g_messagingInterface = nullptr;

class NiSourceTexture;

typedef NiSourceTexture* (__cdecl* CreateNiSourceTexture_t)(const char**, void*, bool, bool);
static CreateNiSourceTexture_t Original_CreateNiSourceTexture = nullptr;

constexpr UInt32 kCreateNiSourceTexture_Addr = 0xA5FD70;
static void* g_pixelLayout = (void*)0x11A9598;
constexpr UInt32 kNiTexture_textureData_Offset = 0x24;

static std::vector<std::string> g_variantFolders;
static std::string g_textureBasePath;
static int g_currentFolderIndex = -1;

static NiSourceTexture* g_variantWheelTexture = nullptr;
static NiSourceTexture* g_variantBallTexture = nullptr;
static NiSourceTexture* g_variantBarsTexture = nullptr;

static NiSourceTexture* g_originalWheelTexture = nullptr;
static NiSourceTexture* g_originalBallTexture = nullptr;
static NiSourceTexture* g_originalBarsTexture = nullptr;

static void* g_originalWheelData = nullptr;
static void* g_originalBallData = nullptr;
static void* g_originalBarsData = nullptr;

static bool g_texturesPreloaded = false;
static bool g_originalDataSaved = false;

static void ScanForVariantFolders() {
	char buffer[MAX_PATH];
	GetModuleFileNameA(NULL, buffer, MAX_PATH);
	std::string exePath(buffer);
	size_t pos = exePath.find_last_of("\\/");
	g_textureBasePath = exePath.substr(0, pos) + "\\Data\\textures\\interface\\loading_variants\\";

	WIN32_FIND_DATAA findData;
	HANDLE hFind = FindFirstFileA((g_textureBasePath + "*").c_str(), &findData);
	if (hFind != INVALID_HANDLE_VALUE) {
		do {
			if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
				strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0) {
				std::string folderPath = g_textureBasePath + findData.cFileName + "\\";
				bool hasWheel = GetFileAttributesA((folderPath + "load_roulette_wheel.dds").c_str()) != INVALID_FILE_ATTRIBUTES;
				bool hasBall = GetFileAttributesA((folderPath + "load_roulette_ball.dds").c_str()) != INVALID_FILE_ATTRIBUTES;
				bool hasBars = GetFileAttributesA((folderPath + "load_roulette_bars.dds").c_str()) != INVALID_FILE_ATTRIBUTES;
				if (hasWheel || hasBall || hasBars) {
					g_variantFolders.push_back(findData.cFileName);
				}
			}
		} while (FindNextFileA(hFind, &findData));
		FindClose(hFind);
	}

	FactionDetection::InitWastelandFolders(g_variantFolders);
	srand(static_cast<unsigned int>(time(nullptr)) ^ static_cast<unsigned int>(GetTickCount()));
}

static NiSourceTexture* LoadVariantTexture(const std::string& folder, const char* filename) {
	std::string fullPath = "textures\\interface\\loading_variants\\" + folder + "\\" + filename;
	std::string diskPath = g_textureBasePath + folder + "\\" + filename;
	if (GetFileAttributesA(diskPath.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
	const char* pathCStr = fullPath.c_str();
	return Original_CreateNiSourceTexture(&pathCStr, g_pixelLayout, true, false);
}

static void LoadCurrentVariantTextures() {
	g_variantWheelTexture = nullptr;
	g_variantBallTexture = nullptr;
	g_variantBarsTexture = nullptr;
	if (g_currentFolderIndex < 0 || g_currentFolderIndex >= (int)g_variantFolders.size()) return;
	const std::string& folder = g_variantFolders[g_currentFolderIndex];
	g_variantWheelTexture = LoadVariantTexture(folder, "load_roulette_wheel.dds");
	g_variantBallTexture = LoadVariantTexture(folder, "load_roulette_ball.dds");
	g_variantBarsTexture = LoadVariantTexture(folder, "load_roulette_bars.dds");
}

static void SaveOriginalTextureData() {
	if (g_originalDataSaved) return;
	if (g_originalWheelTexture) g_originalWheelData = *(void**)((UInt8*)g_originalWheelTexture + kNiTexture_textureData_Offset);
	if (g_originalBallTexture) g_originalBallData = *(void**)((UInt8*)g_originalBallTexture + kNiTexture_textureData_Offset);
	if (g_originalBarsTexture) g_originalBarsData = *(void**)((UInt8*)g_originalBarsTexture + kNiTexture_textureData_Offset);
	g_originalDataSaved = true;
}

static void ApplyVariantTextures() {
	if (g_originalWheelTexture && g_variantWheelTexture) {
		*(void**)((UInt8*)g_originalWheelTexture + kNiTexture_textureData_Offset) = *(void**)((UInt8*)g_variantWheelTexture + kNiTexture_textureData_Offset);
	}
	if (g_originalBallTexture && g_variantBallTexture) {
		*(void**)((UInt8*)g_originalBallTexture + kNiTexture_textureData_Offset) = *(void**)((UInt8*)g_variantBallTexture + kNiTexture_textureData_Offset);
	}
	if (g_originalBarsTexture && g_variantBarsTexture) {
		*(void**)((UInt8*)g_originalBarsTexture + kNiTexture_textureData_Offset) = *(void**)((UInt8*)g_variantBarsTexture + kNiTexture_textureData_Offset);
	}
}

static int FindFolderIndex(const char* folderName) {
	if (!folderName) return -1;
	std::string targetLower = folderName;
	std::transform(targetLower.begin(), targetLower.end(), targetLower.begin(), [](unsigned char c) { return std::tolower(c); });
	for (size_t i = 0; i < g_variantFolders.size(); i++) {
		std::string folderLower = g_variantFolders[i];
		std::transform(folderLower.begin(), folderLower.end(), folderLower.begin(), [](unsigned char c) { return std::tolower(c); });
		if (folderLower == targetLower) return (int)i;
	}
	return -1;
}

static void PickRandomVariant() {
	if (g_variantFolders.empty()) { g_currentFolderIndex = -1; return; }
	LARGE_INTEGER perfCount;
	QueryPerformanceCounter(&perfCount);
	srand(static_cast<unsigned int>(time(nullptr)) ^ static_cast<unsigned int>(GetTickCount()) ^ static_cast<unsigned int>(perfCount.LowPart));
	g_currentFolderIndex = rand() % g_variantFolders.size();
}

static void SelectVariantByLocationString(const std::string& location) {
	if (g_variantFolders.empty()) { g_currentFolderIndex = -1; return; }

	if (g_randomOnly) {
		PickRandomVariant();
		LoadCurrentVariantTextures();
		ApplyVariantTextures();
		return;
	}

	const char* factionFolder = location.empty() ? nullptr : FactionDetection::GetFactionFolder(location);
	if (factionFolder) {
		int idx = FindFolderIndex(factionFolder);
		if (idx >= 0) {
			g_currentFolderIndex = idx;
			LoadCurrentVariantTextures();
			ApplyVariantTextures();
			return;
		}
	}

	const std::string& wastelandFolder = FactionDetection::GetRandomWastelandFolder();
	if (!wastelandFolder.empty()) {
		int idx = FindFolderIndex(wastelandFolder.c_str());
		if (idx >= 0) {
			g_currentFolderIndex = idx;
			LoadCurrentVariantTextures();
			ApplyVariantTextures();
			return;
		}
	}

	PickRandomVariant();
	LoadCurrentVariantTextures();
	ApplyVariantTextures();
}

static void SelectVariantForLocation(const char* savePath) {
	if (g_variantFolders.empty()) { g_currentFolderIndex = -1; return; }

	if (g_randomOnly) {
		PickRandomVariant();
		LoadCurrentVariantTextures();
		return;
	}

	std::string location;
	const char* factionFolder = nullptr;

	if (savePath && *savePath) {
		location = FactionDetection::ExtractLocationFromSave(savePath);
		if (!location.empty()) factionFolder = FactionDetection::GetFactionFolder(location);
	}

	if (factionFolder) {
		int idx = FindFolderIndex(factionFolder);
		if (idx >= 0) {
			g_currentFolderIndex = idx;
			LoadCurrentVariantTextures();
			return;
		}
	}

	const std::string& wastelandFolder = FactionDetection::GetRandomWastelandFolder();
	if (!wastelandFolder.empty()) {
		int idx = FindFolderIndex(wastelandFolder.c_str());
		if (idx >= 0) {
			g_currentFolderIndex = idx;
			LoadCurrentVariantTextures();
			return;
		}
	}

	PickRandomVariant();
	LoadCurrentVariantTextures();
}

typedef TESWorldSpace* (__thiscall* GetTeleportWorldSpace_t)(ExtraTeleport::Data* data);
static GetTeleportWorldSpace_t GetTeleportWorldSpace = (GetTeleportWorldSpace_t)0x43A320;

struct SimpleBSString {
	char* data;
	UInt16 dataLen;
	UInt16 bufLen;
};

typedef bool (__thiscall* WorldSpaceGetMapNameForLocation_t)(TESWorldSpace* thisObj, SimpleBSString* outName, NiPoint3 pos);
static WorldSpaceGetMapNameForLocation_t WorldSpaceGetMapNameForLocation = (WorldSpaceGetMapNameForLocation_t)0x586500;

typedef void (__thiscall* BSStringT_Ctor_t)(SimpleBSString* thisObj);
typedef void (__thiscall* BSStringT_Dtor_t)(SimpleBSString* thisObj);
static BSStringT_Ctor_t BSStringT_Ctor = (BSStringT_Ctor_t)0x403970;
static BSStringT_Dtor_t BSStringT_Dtor = (BSStringT_Dtor_t)0x4037D0;

constexpr UInt32 kPlayerCharacter_Singleton = 0x11DEA3C;

typedef void (__thiscall* HighProcessFadeOut_t)(void* thisObj, Actor* apActor, TESObjectREFR* apRef, bool abTeleport);
static HighProcessFadeOut_t Original_HighProcessFadeOut = nullptr;

static void __fastcall Hook_HighProcessFadeOut(void* thisObj, void* edx, Actor* apActor, TESObjectREFR* apRef, bool abTeleport) {
	TESObjectREFR* player = *(TESObjectREFR**)kPlayerCharacter_Singleton;

	if (abTeleport && apRef && apActor == (Actor*)player && g_texturesPreloaded) {
		ExtraTeleport* teleport = (ExtraTeleport*)apRef->extraDataList.GetByType(kExtraData_Teleport);
		if (teleport && teleport->data && teleport->data->linkedDoor) {
			TESObjectREFR* linkedDoor = teleport->data->linkedDoor;
			bool handled = false;
			TESWorldSpace* destWorldspace = GetTeleportWorldSpace(teleport->data);

			if (destWorldspace) {
				NiPoint3 doorPos = {linkedDoor->posX, linkedDoor->posY, linkedDoor->posZ};
				SimpleBSString locationName;
				BSStringT_Ctor(&locationName);
				WorldSpaceGetMapNameForLocation(destWorldspace, &locationName, doorPos);
				if (locationName.data && locationName.dataLen > 0 && *locationName.data) {
					SelectVariantByLocationString(locationName.data);
					handled = true;
				}
				BSStringT_Dtor(&locationName);
			}
			else if (linkedDoor->parentCell) {
				const char* cellName = linkedDoor->parentCell->fullName.name.c_str();
				if (cellName && *cellName) {
					SelectVariantByLocationString(cellName);
					handled = true;
				}
			}

			if (!handled) {
				TESObjectCELL* destCell = linkedDoor->parentCell;
				if (!destCell) {
					ExtraPersistentCell* persistentExtra = (ExtraPersistentCell*)linkedDoor->extraDataList.GetByType(kExtraData_PersistentCell);
					if (persistentExtra) destCell = persistentExtra->persistentCell;
				}
				if (destCell) {
					const char* cellName = destCell->fullName.name.c_str();
					if (cellName && *cellName) {
						SelectVariantByLocationString(cellName);
						handled = true;
					}
				}
			}

			if (!handled && destWorldspace) {
				const char* wsName = destWorldspace->fullName.name.c_str();
				if (wsName && *wsName) SelectVariantByLocationString(wsName);
			}
		}
	}

	Original_HighProcessFadeOut(thisObj, apActor, apRef, abTeleport);
}

static NiSourceTexture* __cdecl Hook_CreateNiSourceTexture(const char** pathPtr, void* pixelLayout, bool arg3, bool arg4) {
	const char* path = pathPtr ? *pathPtr : nullptr;
	NiSourceTexture* result = Original_CreateNiSourceTexture(pathPtr, pixelLayout, arg3, arg4);

	if (path && result) {
		if (strstr(path, "\\loading\\load_roulette_wheel.dds") && !g_originalWheelTexture) {
			g_originalWheelTexture = result;
		}
		else if (strstr(path, "\\loading\\load_roulette_ball.dds") && !g_originalBallTexture) {
			g_originalBallTexture = result;
		}
		else if (strstr(path, "\\loading\\load_roulette_bars.dds") && !g_originalBarsTexture) {
			g_originalBarsTexture = result;
		}
	}
	return result;
}

static UInt8 g_createNiSourceTextureTrampoline[16];
static UInt8 g_fadeOutTrampoline[16];

static void SetupTrampolines() {
	DWORD oldProtect;

	UInt8* origBytes = (UInt8*)kCreateNiSourceTexture_Addr;
	VirtualProtect(g_createNiSourceTextureTrampoline, sizeof(g_createNiSourceTextureTrampoline), PAGE_EXECUTE_READWRITE, &oldProtect);

	g_createNiSourceTextureTrampoline[0] = origBytes[0];
	g_createNiSourceTextureTrampoline[1] = origBytes[1];
	g_createNiSourceTextureTrampoline[2] = origBytes[2];

	// recreate the call instruction
	g_createNiSourceTextureTrampoline[3] = 0xE8;
	SInt32 origCallOffset = *(SInt32*)(origBytes + 4);
	UInt32 origCallTarget = (kCreateNiSourceTexture_Addr + 3 + 5) + origCallOffset;
	*(SInt32*)&g_createNiSourceTextureTrampoline[4] = origCallTarget - ((UInt32)&g_createNiSourceTextureTrampoline[3] + 5);

	// jmp back
	g_createNiSourceTextureTrampoline[8] = 0xE9;
	*(UInt32*)&g_createNiSourceTextureTrampoline[9] = (kCreateNiSourceTexture_Addr + 8) - ((UInt32)&g_createNiSourceTextureTrampoline[8] + 5);

	Original_CreateNiSourceTexture = (CreateNiSourceTexture_t)(void*)g_createNiSourceTextureTrampoline;

	SafeWrite8(kCreateNiSourceTexture_Addr + 5, 0x90);
	SafeWrite8(kCreateNiSourceTexture_Addr + 6, 0x90);
	SafeWrite8(kCreateNiSourceTexture_Addr + 7, 0x90);
	WriteRelJump(kCreateNiSourceTexture_Addr, (SIZE_T)Hook_CreateNiSourceTexture);

	constexpr UInt32 kHighProcessFadeOut_Addr = 0x8FE960;
	UInt8* fadeOutOrigBytes = (UInt8*)kHighProcessFadeOut_Addr;
	VirtualProtect(g_fadeOutTrampoline, sizeof(g_fadeOutTrampoline), PAGE_EXECUTE_READWRITE, &oldProtect);

	for (int i = 0; i < 7; i++) g_fadeOutTrampoline[i] = fadeOutOrigBytes[i];
	g_fadeOutTrampoline[7] = 0xE9;
	*(UInt32*)&g_fadeOutTrampoline[8] = (kHighProcessFadeOut_Addr + 7) - ((UInt32)&g_fadeOutTrampoline[7] + 5);

	Original_HighProcessFadeOut = (HighProcessFadeOut_t)(void*)g_fadeOutTrampoline;

	WriteRelJump(kHighProcessFadeOut_Addr, (SIZE_T)Hook_HighProcessFadeOut);
	SafeWrite8(kHighProcessFadeOut_Addr + 5, 0x90);
	SafeWrite8(kHighProcessFadeOut_Addr + 6, 0x90);
}

static void MessageHandler(NVSEMessagingInterface::Message* msg) {
	switch (msg->type) {
		case NVSEMessagingInterface::kMessage_PostLoad:
			ScanForVariantFolders();
			break;

		case NVSEMessagingInterface::kMessage_PreLoadGame:
			if (g_texturesPreloaded) {
				const char* savePath = (msg->data && msg->dataLen > 0) ? (const char*)msg->data : nullptr;
				SelectVariantForLocation(savePath);
				ApplyVariantTextures();
			}
			break;

		case NVSEMessagingInterface::kMessage_NewGame:
			if (g_texturesPreloaded) {
				SelectVariantForLocation(nullptr);
				ApplyVariantTextures();
			}
			break;

		case NVSEMessagingInterface::kMessage_MainGameLoop:
			if (!g_texturesPreloaded && Original_CreateNiSourceTexture && g_originalWheelTexture) {
				SaveOriginalTextureData();
				g_texturesPreloaded = true;
				SelectVariantForLocation(nullptr);
			}
			break;
	}
}

void Init(NVSEMessagingInterface* messaging, PluginHandle handle, bool randomOnly) {
	g_messagingInterface = messaging;
	g_pluginHandle = handle;
	g_randomOnly = randomOnly;

	SetupTrampolines();
	messaging->RegisterListener(handle, "NVSE", MessageHandler);
}

}

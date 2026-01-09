#include "FactionDetection.hpp"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace FactionDetection {

struct FactionMapping {
	const char* keyword;
	const char* folder;
};

static const FactionMapping g_factionMappings[] = {
	{"Nellis", "boomers"},
	{"Loyal's house", "boomers"},
	{"Pearl's barracks", "boomers"},
	{"Boomer", "boomers"},
	{"Hangar", "boomers"},
	{"Mess Hall & Munitions", "boomers"},

	{"Arizona Spillway", "Legion"},
	{"Cottonwood", "Legion"},
	{"Aurelius", "Legion"},
	{"Dry Wells", "Legion"},
	{"Legate", "Legion"},
	{"Legion", "Legion"},
	{"Nelson", "Legion"},
	{"Techatticup", "Legion"},
	{"The Fort", "Legion"},
	{"Caesar's tent", "Legion"},
	{"Caesar's Legion", "Legion"},
	{"Weather monitoring station", "Legion"},

	{"Old Mormon Fort", "followers"},
	{"Follower", "followers"},
	{"New Vegas medical clinic", "followers"},

	{"Hidden Valley", "bos"},
	{"Brotherhood", "bos"},

	{"Red Rock", "khans"},
	{"Khan", "khans"},

	{"Correctional", "pg"},
	{"NCRCF", "pg"},
	{"NCRPrison", "pg"},
	{"Powder Ganger", "pg"},
	{"Jean Sky Diving", "pg"},
	{"Whittaker", "pg"},
	{"Hunter's farm", "pg"},
	{"Vault 19", "pg"},

	{"188", "NCR"},
	{"Aerotech", "NCR"},
	{"Bitter Springs", "NCR"},
	{"Boulder City", "NCR"},
	{"Camp Forlorn", "NCR"},
	{"Camp Golf", "NCR"},
	{"Camp Guardian", "NCR"},
	{"Camp McCarran", "NCR"},
	{"Camp Searchlight", "NCR"},
	{"HELIOS", "NCR"},
	{"Hoover Dam", "NCR"},
	{"Long 15", "NCR"},
	{"Mojave Outpost", "NCR"},
	{"NCR Ranger safehouse", "NCR"},
	{"NCR sharecropper", "NCR"},
	{"Ranger Station", "NCR"},
	{"Sharecropper", "NCR"},
	{"Sloan", "NCR"},
	{"Greenhouse", "NCR"},
	{"Oliver's compound", "NCR"},

	{"Lucky 38", "House"},
	{"New Vegas Strip", "House"},
	{"The Strip", "House"},
	{"Tops", "House"},
	{"Ultra-Luxe", "House"},
	{"Gomorrah", "House"},
	{"Vault 21", "House"},

	{"Freeside", "Independent"},
	{"Atomic Wrangler", "Independent"},
	{"Silver Rush", "Independent"},
	{"Kings", "Independent"},
	{"Mick and Ralph", "Independent"},

	{"Vault 3", "fiends"},
	{"South Vegas", "fiends"},
	{"SouthVegas", "fiends"},
	{"Zapp", "fiends"},
	{"Fiend", "fiends"},

	{"Goodsprings", nullptr},
	{"Jacobstown", nullptr},
	{"Nipton", nullptr},
	{"Novac", nullptr},
	{"Primm", nullptr},
	{"Mojave Wasteland", nullptr},
	{"Wasteland", nullptr},
	{nullptr, nullptr}
};

static std::vector<std::string> g_wastelandFolders;

static bool ContainsIgnoreCase(const std::string& haystack, const char* needle) {
	if (!needle || !*needle) return false;
	std::string haystackLower = haystack;
	std::string needleLower = needle;
	std::transform(haystackLower.begin(), haystackLower.end(), haystackLower.begin(),
		[](unsigned char c) { return std::tolower(c); });
	std::transform(needleLower.begin(), needleLower.end(), needleLower.begin(),
		[](unsigned char c) { return std::tolower(c); });
	return haystackLower.find(needleLower) != std::string::npos;
}

void InitWastelandFolders(const std::vector<std::string>& allFolders) {
	g_wastelandFolders.clear();
	for (const auto& folder : allFolders) {
		std::string folderLower = folder;
		std::transform(folderLower.begin(), folderLower.end(), folderLower.begin(),
			[](unsigned char c) { return std::tolower(c); });
		if (folderLower.find("wasteland") == 0) {
			g_wastelandFolders.push_back(folder);
		}
	}
}

const std::string& GetRandomWastelandFolder() {
	static std::string empty;
	if (g_wastelandFolders.empty()) return empty;
	return g_wastelandFolders[rand() % g_wastelandFolders.size()];
}

const char* GetFactionFolder(const std::string& location) {
	if (location.empty()) return nullptr;
	for (int i = 0; g_factionMappings[i].keyword != nullptr; i++) {
		if (ContainsIgnoreCase(location, g_factionMappings[i].keyword)) {
			return g_factionMappings[i].folder;
		}
	}
	return nullptr;
}

static const UInt32 kAddr_PersonalPath = 0x01202FA0;
static const UInt32 kAddr_sLocalSavePath = 0x11C3F74;

static std::string BuildSaveFilePath(const char* saveFileName) {
	if (!saveFileName || !*saveFileName) return "";
	if (strchr(saveFileName, '\\') || strchr(saveFileName, '/')) return saveFileName;
	const char* personalPath = (const char*)kAddr_PersonalPath;
	if (!personalPath || !*personalPath) return saveFileName;
	const char* localSavePath = *(const char**)(kAddr_sLocalSavePath + 4);
	if (!localSavePath) localSavePath = "Saves\\";
	std::string fullPath = personalPath;
	fullPath += localSavePath;
	fullPath += saveFileName;
	return fullPath;
}

std::string ExtractLocationFromSave(const char* savePath) {
	std::string fullPath = BuildSaveFilePath(savePath);
	if (fullPath.empty()) return "";

	HANDLE hFile = CreateFileA(fullPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) return "";

	char buffer[4096];
	DWORD bytesRead = 0;
	if (!ReadFile(hFile, buffer, sizeof(buffer), &bytesRead, NULL) || bytesRead < 64) {
		CloseHandle(hFile);
		return "";
	}
	CloseHandle(hFile);

	if (memcmp(buffer, "FO3SAVEGAME", 11) != 0) return "";

	UInt32 headerSize = *(UInt32*)(buffer + 11);
	if (headerSize > 4000 || headerSize < 32) return "";

	char* headerData = buffer + 15;
	char* headerEnd = headerData + headerSize;
	int fieldIndex = 0;
	char* fieldStart = headerData;

	for (char* p = headerData; p < headerEnd; p++) {
		if (*p == '|') {
			if (fieldIndex == 11) {
				char* strEnd = p;
				char* strStart = fieldStart;
				while (strStart < strEnd && *strStart == '\0') strStart++;
				char* actualEnd = strEnd;
				while (actualEnd > strStart && *(actualEnd-1) == '\0') actualEnd--;
				size_t len = actualEnd - strStart;
				if (len > 0 && len < 256) return std::string(strStart, len);
				break;
			}
			fieldIndex++;
			fieldStart = p + 1;
		}
	}

	if (fieldIndex == 11 && fieldStart < headerEnd) {
		char* strStart = fieldStart;
		while (strStart < headerEnd && *strStart == '\0') strStart++;
		size_t len = headerEnd - strStart;
		while (len > 0 && (strStart[len-1] == '\0' || strStart[len-1] < 32)) len--;
		if (len > 0 && len < 256) return std::string(strStart, len);
	}
	return "";
}

}

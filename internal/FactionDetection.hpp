#pragma once

#include <string>
#include <vector>

namespace FactionDetection {

void InitWastelandFolders(const std::vector<std::string>& allFolders);

const std::string& GetRandomWastelandFolder();
const char* GetFactionFolder(const std::string& location);

std::string ExtractLocationFromSave(const char* savePath);

}

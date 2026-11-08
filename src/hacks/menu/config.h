#pragma once
#include "../../globals/globals.h"
#include <string>
#include <vector>

// visuals = botox config_panel(visuals = true): only the visuals-tab keys (visual_keys.h),
// files in CONFIG_DIR\visuals. A full cfg dropped in that folder still only touches visuals.
void SaveConfig(const char* path = CONFIG_PATH, bool visuals = false);
void LoadConfig(const char* path = CONFIG_PATH, bool visuals = false);

// Named cfgs in ConfigFolder(visuals). Name = file name with or without ".cfg".
const char* ConfigFolder(bool visuals);                     // settings tab "open folder"
std::vector<std::string> ListConfigs(bool visuals = false); // sorted, with ".cfg"
void SaveConfigNamed(const char* name, bool visuals = false);
void LoadConfigNamed(const char* name, bool visuals = false);
bool DeleteConfigNamed(const char* name, bool visuals = false);   // settings tab "remove"

// ==== generic access by key, for the lua port (config.* table in hacks/lua/lua_api.cpp) ====
// Reuses the same kEntries row the save/load path walks -- no second variable registry.
// type: "bool" "int" "float" "float4" "string" "curve" (CURVE rows exist in kEntries but
// have no string form here -- get/set on one fails, same as botox's own "unknown type" path).
const char* CfgEntry_TypeName(const char* key);          // nullptr if key is not in kEntries
bool CfgEntry_GetAsString(const char* key, std::string& out);
bool CfgEntry_SetFromString(const char* key, const char* value);
void CfgEntry_ListNames(std::vector<std::string>& out);

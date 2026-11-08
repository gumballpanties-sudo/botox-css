#pragma once
// Minimal CMenu. dih draws its own ImGui menu (src/hacks/menu/menu.cpp), so this holds only
// the parts the aim pipeline actually touches: the output ring (SDK::Output -> menu
// console), the open/keybind flags KeyHandler reads, and the mutex held while the player
// list is walked.
#include "sdk.h"

#include <deque>
#include <mutex>
#include <string>

struct Output_t
{
	std::string m_sFunction;
	std::string m_sLog;
	size_t m_iID;

	Color_t tAccent;
};

class CMenu
{
	std::deque<Output_t> m_vOutput = {};
	size_t m_iMaxOutputSize = 1000;

public:
	void Render() {}
	void AddOutput(const char* sFunction, const char* sLog, Color_t tColor = { 255, 255, 255, 255 });

	// Read by CBinds/KeyHandler: a bind must not fire while the menu eats input.
	bool m_bIsOpen = false;
	bool m_bInKeybind = false;
	bool m_bWindowHovered = false;

	std::mutex m_tMutex;
};

ADD_FEATURE(CMenu, Menu);

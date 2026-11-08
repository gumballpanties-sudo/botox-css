#pragma once
// Stub core. dih owns the DLL load/unload loop (PollThread), so the only part of this
// that does anything is the fail-text sink every Utils initializer writes into. Text
// goes to dih's debug.log.
#include "utils.h"

#include <string>

class CCore
{
public:
	void Unload() {}

	void AppendFailText(const char* sMessage);

	bool m_bUnload = false;
};

ADD_FEATURE_CUSTOM(CCore, Core, U);

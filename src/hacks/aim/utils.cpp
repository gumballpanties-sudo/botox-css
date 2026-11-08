#include "utils.h"

#include "core.h"
#include "sdk.h"
#include "defs.h"

// ================ Utils/Hooks/Hooks.cpp ================
// Hook table for the aim half. Two things to know about it:
//
//  1. No WndProc::Initialize()/Unload() here -- dih already owns the window proc and the D3D9
//     device hooks (src/render/render.cpp, src/hooks/thread.cpp), and a second hook on
//     the same targets would just fail.
//
//  2. Initialize() takes a whitelist. Every MAKE_HOOK in the binary registers itself, but only
//     the ones the aim pipeline and the demo-angle fix need are installed: several of the rest
//     would collide with hooks dih already has (CreateMove, SetupBones, FireEvent,
//     DrawModelExecute, FindMaterial, ...). MinHook refuses the second hook on a target, so
//     the whitelist is what keeps both halves alive.

CHook::CHook(const std::string& sName, void* pInitFunc)
{
	m_pInitFunc = pInitFunc;
	U::Hooks.m_mHooks[sName] = this;
}

bool CHooks::Initialize(const std::vector<std::string>& vWhitelist)
{
	MH_Initialize();

	for (auto& sName : vWhitelist)
	{
		auto it = m_mHooks.find(sName);
		if (it == m_mHooks.end())
		{
			U::Core.AppendFailText(std::string("Unknown hook in whitelist: " + sName).c_str());
			continue;
		}

		reinterpret_cast<void(__cdecl*)()>(it->second->m_pInitFunc)();
		m_vInstalled.push_back(sName);
	}

	m_bFailed = MH_EnableHook(MH_ALL_HOOKS) != MH_OK;
	if (m_bFailed)
		U::Core.AppendFailText("MinHook failed to enable all hooks!");
	return !m_bFailed;
}

bool CHooks::Unload()
{	// dih calls MH_Uninitialize itself on eject -- disabling only what we installed keeps
	// dih's own hooks alive if the aim half is torn down on its own.
	for (auto& sName : m_vInstalled)
	{
		auto it = m_mHooks.find(sName);
		if (it != m_mHooks.end() && it->second->m_pTarget)
			MH_DisableHook(it->second->m_pTarget);
	}
	m_vInstalled.clear();
	return true;
}

// ================ Utils/Interfaces/Interfaces.cpp ================
#include <TlHelp32.h>
#include <string>
#include <sstream>
#include <format>

#pragma warning (disable: 4172)

InterfaceInit_t::InterfaceInit_t(void** pPtr, const char* sDLL, const char* sName, int8_t nType, int8_t nOffset, int8_t nDereferenceCount)
{
	m_pPtr = pPtr;
	m_sDLL = sDLL;
	m_sName = sName;
	m_nType = nType;
	m_nOffset = nOffset;
	m_nDereferenceCount = nDereferenceCount;

	U::Interfaces.AddInterface(this);
}

bool CInterfaces::Initialize()
{
	for (auto& Interface : m_vInterfaces)
	{
		const char* sModule = nullptr;
		std::vector<std::string> vModules;
		// split on comma OR space, dropping empties.
		{
			const std::string sDLLs = Interface->m_sDLL ? Interface->m_sDLL : "";
			std::string sPart;
			for (char c : sDLLs)
			{
				if (c == ',' || c == ' ')
				{
					if (!sPart.empty()) vModules.push_back(sPart);
					sPart.clear();
				}
				else
					sPart += c;
			}
			if (!sPart.empty()) vModules.push_back(sPart);
		}
		if (vModules.size() == 1)
			sModule = vModules.front().c_str();
		else
		{
			for (auto& sName : vModules)
			{
				if (GetModuleHandle(sName.c_str()))
				{
					sModule = sName.c_str();
					break;
				}
			}
			if (!sModule)
			{
				std::stringstream ssModuleStream;
				for (auto& sName : vModules)
					ssModuleStream << std::format("{}{}", !ssModuleStream.str().empty() ? ", " : "", sName);

				U::Core.AppendFailText(std::format("CInterfaces::Initialize() failed to find module:\n  {}\n  {}", ssModuleStream.str(), Interface->m_sName).c_str());
				m_bFailed = true;
				continue;
			}
		}

		switch (Interface->m_nType)
		{
		case 0:
		{
			*Interface->m_pPtr = U::Memory.FindInterface(sModule, Interface->m_sName);
			break;
		}
		case 1:
		{
			*Interface->m_pPtr = U::Memory.GetModuleExport<void*>(sModule, Interface->m_sName);
			break;
		}
		case 2:
		{
			auto dwDest = U::Memory.FindSignature(sModule, Interface->m_sName);
			if (!dwDest)
			{
				U::Core.AppendFailText(std::format("CInterfaces::Initialize() failed to find signature").c_str());
				break;
			}

			*Interface->m_pPtr = reinterpret_cast<void*>(U::Memory.RelToAbs(dwDest) + Interface->m_nOffset);
			break;
		}
		}

		for (int n = 0; n < Interface->m_nDereferenceCount; n++)
		{
			if (Interface->m_pPtr)
				*Interface->m_pPtr = *reinterpret_cast<void**>(*Interface->m_pPtr);
		}

		if (!*Interface->m_pPtr)
		{
			U::Core.AppendFailText(std::format("CInterfaces::Initialize() failed to initialize:\n  {}\n  {}", sModule, Interface->m_sName).c_str());
			m_bFailed = true;
		}
	}

	if (!H::Interfaces.Initialize())
		m_bFailed = true; // Initialize any null interfaces

	return !m_bFailed;
}

// ================ Utils/KeyHandler/KeyHandler.cpp ================
#include <Windows.h>

// ================ Utils/Memory/Memory.cpp ================
#include <format>
#include <Psapi.h>

std::vector<int> CMemory::PatternToInt(const char* szPattern)
{
	std::vector<int> vPattern = {};

	const auto pStart = const_cast<char*>(szPattern);
	const auto pEnd = const_cast<char*>(szPattern) + strlen(szPattern);
	for (char* pCurrent = pStart; pCurrent < pEnd; ++pCurrent)
	{
		if (*pCurrent == '?') // Is current byte a wildcard? Simply ignore that that byte later
		{
			++pCurrent;
			if (*pCurrent == '?') // Check if following byte is also a wildcard
				++pCurrent;

			vPattern.push_back(-1);
		}
		else
			vPattern.push_back(std::strtoul(pCurrent, &pCurrent, 16));
	}

	return vPattern;
}

uintptr_t CMemory::FindSignature(const char* szModule, const char* szPattern)
{
	if (const auto hModule = GetModuleHandle(szModule))
	{
		// Get module information to search in the given module
		MODULEINFO lpModuleInfo;
		if (!GetModuleInformation(GetCurrentProcess(), hModule, &lpModuleInfo, sizeof(MODULEINFO)))
			return 0x0;

		// The region where we will search for the byte sequence
		const auto dwImageSize = lpModuleInfo.SizeOfImage;

		// Check if the image is faulty
		if (!dwImageSize)
			return 0x0;

		// Convert IDA-Style signature to a byte sequence
		const auto vPattern = PatternToInt(szPattern);
		const auto iPatternSize = vPattern.size();
		const int* iPatternBytes = vPattern.data();

		const auto pImageBytes = reinterpret_cast<byte*>(hModule);

		// Now loop through all bytes and check if the byte sequence matches
		for (auto i = 0ul; i < dwImageSize - iPatternSize; ++i)
		{
			auto bFound = true;

			// Go through all bytes from the signature and check if it matches
			for (auto j = 0ul; j < iPatternSize; ++j)
			{
				if (pImageBytes[i + j] != iPatternBytes[j] // Bytes don't match
					&& iPatternBytes[j] != -1)             // Byte isn't a wildcard either
				{
					bFound = false;
					break;
				}
			}

			if (bFound)
				return uintptr_t(&pImageBytes[i]);
		}

		return 0x0;
	}

	return 0x0;
}

using CreateInterfaceFn = void*(*)(const char* pName, int* pReturnCode);

PVOID CMemory::FindInterface(const char* szModule, const char* szObject)
{
	const auto CreateInterface = GetModuleExport<CreateInterfaceFn>(szModule, "CreateInterface");
	return CreateInterface(szObject, nullptr);
}

std::string CMemory::GetModuleOffset(uintptr_t uAddress)
{
	HMODULE hModule;
	if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, LPCSTR(uAddress), &hModule))
		return std::format("{:#x}", uAddress);

	uintptr_t uBase = uintptr_t(hModule);
	if (char buffer[MAX_PATH]; GetModuleBaseName(GetCurrentProcess(), hModule, buffer, sizeof(buffer) / sizeof(char)))
		return std::format("{}+{:#x}", buffer, uAddress - uBase);

	return std::format("{:#x}+{:#x}", uBase, uAddress - uBase);
}

uintptr_t CMemory::GetOffsetFromBase(uintptr_t uAddress)
{
	HMODULE hModule;
	if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, LPCSTR(uAddress), &hModule))
		return -1;

	uintptr_t uBase = uintptr_t(hModule);
	return uAddress - uBase;
}

// ================ Utils/NetVars/NetVars.cpp ================
#ifdef GetProp
	#undef GetProp
#endif

int CNetVars::GetOffset(RecvTable* pTable, const char* szNetVar)
{
	auto uHash = FNV1A::Hash32(szNetVar);
	for (int i = 0; i < pTable->GetNumProps(); i++)
	{
		RecvProp* pProp = pTable->GetProp(i);
		if (uHash == FNV1A::Hash32(pProp->m_pVarName))
			return pProp->GetOffset();

		if (auto pDataTable = pProp->GetDataTable())
		{
			if (auto nOffset = GetOffset(pDataTable, szNetVar))
				return nOffset + pProp->GetOffset();
		}
	}

	return 0;
}

int CNetVars::GetNetVar(const char* szClass, const char* szNetVar)
{
	auto uHash = FNV1A::Hash32(szClass);
	for (auto pCurrNode = I::Client->GetAllClasses(); pCurrNode; pCurrNode = pCurrNode->m_pNext)
	{
		if (uHash == FNV1A::Hash32(pCurrNode->m_pNetworkName))
			return GetOffset(pCurrNode->m_pRecvTable, szNetVar);
	}

	return 0;
}

RecvProp* CNetVars::GetProp(RecvTable* pTable, const char* szNetVar)
{
	auto uHash = FNV1A::Hash32(szNetVar);
	for (int i = 0; i < pTable->GetNumProps(); i++)
	{
		RecvProp* pProp = pTable->GetProp(i);
		if (uHash == FNV1A::Hash32(pProp->m_pVarName))
			return pProp;

		if (auto pDataTable = pProp->GetDataTable())
		{
			if (pProp = GetProp(pDataTable, szNetVar))
				return pProp;
		}
	}

	return nullptr;
}

// ================ Utils/Signatures/Signatures.cpp ================
#include <string>
#include <format>

CSignature::CSignature(const char* sDLLName, const char* sSignature, int8_t nOffset, const char* sName)
{
	m_dwVal = 0x0;
	m_sDLLName = sDLLName;
	m_sSignature = sSignature;
	m_nOffset = nOffset;
	m_sName = sName;

	U::Signatures.AddSignature(this);
}

bool CSignature::Initialize()
{
	m_dwVal = U::Memory.FindSignature(m_sDLLName, m_sSignature);
	if (!m_dwVal)
	{
		U::Core.AppendFailText(std::format("CSignature::Initialize() failed to initialize:\n  {}\n  {}\n  {}", m_sName, m_sDLLName, m_sSignature).c_str());
		return false;
	}

	m_dwVal += m_nOffset;
	return true;
}

bool CSignatures::Initialize()
{
	for (auto pSignature : m_vSignatures)
	{
		if (!pSignature)
			continue;

		if (!pSignature->Initialize())
			m_bFailed = true;
	}

	return !m_bFailed;
}

// ================ Utils/Timer/Timer.cpp ================
Timer::Timer()
{
	m_flLast = SDK::PlatFloatTime();
}

bool Timer::Check(float flS) const
{
	float flCurrentTime = SDK::PlatFloatTime();
	return flCurrentTime - m_flLast >= flS;
}

bool Timer::Run(float flS)
{
	if (Check(flS))
	{
		Update();
		return true;
	}
	return false;
}

void Timer::Update()
{
	m_flLast = SDK::PlatFloatTime();
}

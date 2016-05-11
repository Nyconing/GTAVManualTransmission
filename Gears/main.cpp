/*
THIS FILE IS A PART OF GTA V SCRIPT HOOK SDK
http://dev-c.com
(C) Alexander Blade 2015
*/

#include "..\..\ScriptHookV_SDK\inc\main.h"
#include "script.h"
#include "Logger.hpp"
#include "MemoryPatcher.hpp"

BOOL APIENTRY DllMain(HMODULE hInstance, DWORD reason, LPVOID lpReserved)
{
	Logger logger(LOGFILE);

	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		scriptRegister(hInstance, ScriptMain);
		logger.Clear();
		logger.Write("Script loaded");
		break;
	case DLL_PROCESS_DETACH:
		logger.Write("Init shutdown");
		bool successI = MemoryPatcher::RestoreInstructions();
		bool successJ = MemoryPatcher::RestoreJustS_LOW();

		if (successI && successJ) {
			logger.Write("Shut down script successfully");
		}
		else {
			logger.Write("Shut down script with leftovers");
		}
		scriptUnregister(hInstance);
		break;
	}
	return TRUE;
}
#include "MemoryPatcher.h"

int MemoryPatcher::total = 4;
int MemoryPatcher::patched = 0;

MemoryAccess MemoryPatcher::mem;

uintptr_t MemoryPatcher::clutchLowAddr;
uintptr_t MemoryPatcher::clutchLowTemp;

uintptr_t MemoryPatcher::clutchS01Addr;
uintptr_t MemoryPatcher::clutchS01Temp;

uintptr_t MemoryPatcher::clutchS04Addr;
uintptr_t MemoryPatcher::clutchS04Temp;

uintptr_t MemoryPatcher::throttleCutAddr;
uintptr_t MemoryPatcher::throttleCutTemp;

uintptr_t MemoryPatcher::clutchStationaryLowAddr;
uintptr_t MemoryPatcher::clutchStationaryLowTemp;



bool MemoryPatcher::PatchInstructions() {
	int success = 0;
	Logger logger("./Gears.log");
	clutchLowTemp = PatchClutchLow();
	if (clutchLowTemp) {
		clutchLowAddr = clutchLowTemp;
		patched++;
	}
	else {
		logger.Write("Clutch_Normal not patched");
	}

	clutchS01Temp = PatchClutchStationary01();
	if (clutchS01Temp && !clutchS01Addr) {
		clutchS01Addr = clutchS01Temp;
		patched++;
	}
	else {
		logger.Write("Clutch_Stationary_01 not patched");
	}

	clutchS04Temp = PatchClutchStationary04();
	if (clutchS04Temp) {
		clutchS04Addr = clutchS04Temp;
		patched++;
	}
	else {
		logger.Write("Clutch_Stationary_04 not patched");
	}

	throttleCutTemp = PatchThrottleRedline();
	if (throttleCutTemp) {
		throttleCutAddr = throttleCutTemp;
		patched++;
	}
	else {
		logger.Write("Throttle_Redline not patched");
	}

	/*clutchStationaryLowTemp = PatchClutchStationaryLow();
	if (clutchStationaryLowTemp) {
		clutchStationaryLowAddr = clutchStationaryLowTemp;
		patched++;
	}
	else {
		logger.Write("Clutch_Stationary_Low not patched");
	}*/

	if (patched == total) {
		logger.Write("Patching success");
		return true;
	}

	logger.Write("Patching failed");
	return false;
}

bool MemoryPatcher::RestoreInstructions() {
	int success = 0;
	Logger logger("./Gears.log");

	if (clutchLowAddr) {
		RestoreClutchLow(clutchLowAddr);
		clutchLowAddr = 0;
		success++;
		patched--;
	}
	else {
		logger.Write("Clutch not restored");
	}

	if (clutchS01Addr) {
		RestoreClutchStationary01(clutchS01Addr);
		clutchS01Addr = 0;
		success++;
		patched--;
	}
	else {
		logger.Write("0.1 not restored");
	}

	if (clutchS04Addr) {
		RestoreClutchStationary04(clutchS04Addr);
		clutchS04Addr = 0;
		success++;
		patched--;
	}
	else {
		logger.Write("0.4 not restored");
	}

	if (throttleCutAddr) {
		RestoreThrottleRedline(throttleCutAddr);
		throttleCutAddr = 0;
		success++;
		patched--;
	}
	else {
		logger.Write("Throttle not restored");
	}

	/*if (clutchStationaryLowAddr) {
		ext.RestoreThrottleRedline(clutchStationaryLowAddr);
		clutchStationaryLowAddr = 0;
		success++;
		patched--;
	}
	else {
		logger.Write("S_Low not restored");
	}*/

	if (success == total || patched == 0)
		logger.Write("Restore success");
		return true;

	logger.Write("Restore failed");
	return false;
}

uintptr_t MemoryPatcher::PatchClutchLow() {
	// Tested on build 350 and build 617
	// We're only interested in the first 7 bytes but we need the correct one
	// C7 43 40 CD CC CC 3D is what we're looking for, the second occurrence, at 
	// 7FF6555FE34A or GTA5.exe+ECE34A in build 617.
	uintptr_t address = mem.FindPattern("\xC7\x43\x40\xCD\xCC\xCC\x3D\x66\x44\x89\x43\x04", "xxxxxxxxxxxx");

	if (address) {
		memset((void *)address, 0x90, 7);
	}
	return address;
}
void MemoryPatcher::RestoreClutchLow(uintptr_t address) {
	byte instrArr[7] = { 0xC7, 0x43, 0x40, 0xCD, 0xCC, 0xCC, 0x3D };
	if (address) {
		for (int i = 0; i < 7; i++) {
			memset((void *)(address + i), instrArr[i], 1);
		}
	}
}

uintptr_t MemoryPatcher::PatchClutchStationary01() {
	// Also looking for C7 43 40 CD CC CC 3D
	// This pattern also works on 350 and 617
	uintptr_t address = mem.FindPattern("\xC7\x43\x40\xCD\xCC\xCC\x3D\xE9\xF6\x04\x00\x00", "xxxxxxxx????");

	if (address) {
		memset((void *)address, 0x90, 7);
	}
	return address;
}
void MemoryPatcher::RestoreClutchStationary01(uintptr_t address) {
	byte instrArr[7] = { 0xC7, 0x43, 0x40, 0xCD, 0xCC, 0xCC, 0x3D };
	if (address) {
		for (int i = 0; i < 7; i++) {
			memset((void *)(address + i), instrArr[i], 1);
		}
	}
}

uintptr_t MemoryPatcher::PatchClutchStationary04() {
	// Looking for F3 0F 11 47 40
	// This pattern works on 350 and 617
	uintptr_t address = mem.FindPattern("\xF3\x0F\x11\x47\x40\xF3\x0F\x59\x3D\xDA\x9C\x8E\x00", "xxxxxxxxx????");

	if (address) {
		memset((void *)address, 0x90, 5);
	}
	return address;
}
void MemoryPatcher::RestoreClutchStationary04(uintptr_t address) {
	byte instrArr[5] = { 0xF3, 0x0F, 0x11, 0x47, 0x40 };
	if (address) {
		for (int i = 0; i < 5; i++) {
			memset((void *)(address + i), instrArr[i], 1);
		}
	}
}

uintptr_t MemoryPatcher::PatchThrottleRedline() {
	// Looking for 44 89 71 44
	uintptr_t address = mem.FindPattern("\x44\x89\x71\x44\xF3\x0F\x11\x75\x47", "xxxxxxx??");

	if (address) {
		memset((void *)address, 0x90, 4);
	}
	return address;
}
void MemoryPatcher::RestoreThrottleRedline(uintptr_t address) {
	byte instrArr[4] = { 0x44, 0x89, 0x71, 0x44 };
	if (address) {
		for (int i = 0; i < 4; i++) {
			memset((void *)(address + i), instrArr[i], 1);
		}
	}
}

uintptr_t MemoryPatcher::PatchClutchStationaryLow() {
	// F3 0F 11 43 40 <- Looking for this
	// 0F B7 43 04    <- Next instruction
	uintptr_t address = mem.FindPattern("\xF3\x0F\x11\x43\x40\x0F\xB7\x43\x04", "xxxxxxx??");

	if (address) {
		memset((void *)address, 0x90, 5);
	}
	return address;
}

void MemoryPatcher::RestoreClutchStationaryLow(uintptr_t address) {
	byte instrArr[7] = { 0xF3, 0x0F, 0x11, 0x43, 0x40 };
	if (address) {
		for (int i = 0; i < 5; i++) {
			memset((void *)(address + i), instrArr[i], 1);
		}
	}
}
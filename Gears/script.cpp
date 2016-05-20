#include "script.h"

#include <string>
#include <sstream>
#include <iomanip>
#include <array>

#include "ScriptSettings.hpp"
#include "ScriptControls.hpp"
#include "Logger.hpp"
#include "VehicleData.hpp"
#include "MemoryPatcher.hpp"
#include "XboxController.hpp"
#include "WheelInput.hpp"

Logger logger(LOGFILE);
ScriptControls controls;
ScriptSettings settings;

Player player;
Ped playerPed;
Vehicle vehicle;
VehicleData vehData;
VehicleExtensions ext;
Hash model;

XboxController controller(1);
WORD buttonState;

WheelInput logiWheel(0);

bool active = false;
//bool runOnceRan = false;
bool patched = false;
bool patchedSpecial = false;
bool simpleBike = false;
int prevNotification = 0;
int prevInput = 0;

void update() {
	player = PLAYER::PLAYER_ID();
	playerPed = PLAYER::PLAYER_PED_ID();

	if (!ENTITY::DOES_ENTITY_EXIST(playerPed) ||
		!PLAYER::IS_PLAYER_CONTROL_ON(player) ||
		ENTITY::IS_ENTITY_DEAD(playerPed) ||
		PLAYER::IS_PLAYER_BEING_ARRESTED(player, TRUE)) {
		reset();
		return;
	}

	if (!PED::IS_PED_IN_ANY_VEHICLE(playerPed, true)) {
		reset();
		return;
	}

	vehicle = PED::GET_VEHICLE_PED_IS_IN(playerPed, false);
	model = ENTITY::GET_ENTITY_MODEL(vehicle);

	if (!ENTITY::DOES_ENTITY_EXIST(vehicle) ||
		VEHICLE::IS_THIS_MODEL_A_BICYCLE(model) ||
		!(VEHICLE::IS_THIS_MODEL_A_CAR(model) ||
			VEHICLE::IS_THIS_MODEL_A_BIKE(model) ||
			VEHICLE::IS_THIS_MODEL_A_QUADBIKE(model)
			)) {
		reset();
		return;
	}

	if (settings.LogiWheel && LogiUpdate()) {
		logiWheel.IsActive = true;
	}
	else {
		logiWheel.IsActive = false;
	}

	if (controller.IsConnected()) {
		buttonState = controller.GetState().Gamepad.wButtons;
		controller.UpdateButtonChangeStates();
	}

	if (controls.IsKeyJustPressed(controls.Control[(int)ScriptControls::KeyboardControlType::Toggle], ScriptControls::KeyboardControlType::Toggle) || 
		controller.WasButtonHeldForMs(controller.StringToButton(controls.ControlXbox[(int)ScriptControls::ControllerControlType::Toggle]), buttonState, controls.CToggleTime) ||
		LogiButtonTriggered(logiWheel.GetIndex(), controls.LogiControl[(int)ScriptControls::LogiControlType::Toggle])) {
		toggleManual();
	}

	
	
	if (controls.IsKeyJustPressed(controls.Control[(int)ScriptControls::KeyboardControlType::ToggleH], ScriptControls::KeyboardControlType::ToggleH) ||
		LogiButtonTriggered(logiWheel.GetIndex(), controls.LogiControl[(int)ScriptControls::LogiControlType::ToggleH])) {
		settings.Hshifter = !settings.Hshifter;
		std::stringstream message;
		message << "Mode: " <<
			(settings.Hshifter ? "H-Shifter" : "Sequential");
		showNotification((char *)message.str().c_str());
		settings.Save();
	}

	vehData.ReadMemData(ext, vehicle);
	vehData.LockGear = (0xFFFF0000 & vehData.LockGears) >> 16;
	simpleBike = vehData.IsBike && settings.SimpleBike;

	if (logiWheel.IsActive) {
		logiWheel.UpdateLogiValues();
	}
	if (prevInput != getLastInputDevice(prevInput)) {
		prevInput = getLastInputDevice(prevInput);
		switch (prevInput) {
		case InputDevices::Keyboard:
			showNotification("Switched to keyboard/mouse");
			break;
		case InputDevices::Controller: // Controller
			if (settings.Hshifter) {
				showNotification("Switched to controller\nSequential re-initiated");
				settings.Hshifter = false;
				settings.Save();
			}
			else {
				showNotification("Switched to controller");
			}			
			break;
		case InputDevices::Wheel: // Wheel
			showNotification("Switched to wheel");
			break;
		default:
			break;
		}
	}
	switch (prevInput) {
	case InputDevices::Keyboard:
		controls.Rtvalf = (controls.IsKeyPressed(controls.Control[(int)ScriptControls::KeyboardControlType::Throttle]) ? 1.0f : 0.0f);
		controls.Ltvalf = (controls.IsKeyPressed(controls.Control[(int)ScriptControls::KeyboardControlType::Brake]) ? 1.0f : 0.0f);
		controls.Clutchvalf = (controls.IsKeyPressed(controls.Control[(int)ScriptControls::KeyboardControlType::Clutch]) ? 1.0f : 0.0f);
		break;
	case InputDevices::Controller: // Controller
		controls.Rtvalf = controller.GetAnalogValue(controller.StringToButton(controls.ControlXbox[(int)ScriptControls::ControllerControlType::Throttle]), buttonState);
		controls.Ltvalf = controller.GetAnalogValue(controller.StringToButton(controls.ControlXbox[(int)ScriptControls::ControllerControlType::Brake]), buttonState);
		controls.Clutchvalf = controller.GetAnalogValue(controller.StringToButton(controls.ControlXbox[(int)ScriptControls::ControllerControlType::Clutch]), buttonState);
		break;
	case InputDevices::Wheel: // Wheel
		controls.Rtvalf = logiWheel.GetLogiThrottleVal();
		controls.Ltvalf = logiWheel.GetLogiBrakeVal();
		controls.Clutchvalf = 1 - logiWheel.GetLogiClutchVal();
		break;
	}

	controls.Accelval = CONTROLS::GET_CONTROL_VALUE(0, ControlVehicleAccelerate);
	controls.Accelvalf = (controls.Accelval - 127) / 127.0f;

	// Other scripts. 0 = nothing, 1 = Shift up, 2 = Shift down
	if (vehData.CurrGear > 1 && vehData.Rpm < 0.4f) {
		DECORATOR::DECOR_SET_INT(vehicle, "hunt_score", 2);
	}
	else if (vehData.CurrGear == vehData.NextGear) {
		DECORATOR::DECOR_SET_INT(vehicle, "hunt_score", 0);
	}
	else if (vehData.CurrGear < vehData.NextGear) {
		DECORATOR::DECOR_SET_INT(vehicle, "hunt_score", 1);
	}

	if (settings.Debug) {
		showDebugInfo();
	}

	if (!settings.EnableManual || 
		(!VEHICLE::IS_VEHICLE_DRIVEABLE(vehicle, false) &&
			VEHICLE::GET_VEHICLE_ENGINE_HEALTH(vehicle) < -100.0f)) {
		return;
	}
	else {
		active = true;
	}

	// Patch clutch on game start
	//if (settings.EnableManual && !runOnceRan) {
	//	logger.Write("Patching functions on start");
	//	patched = MemoryPatcher::PatchInstructions();
	//	vehData.SimulatedNeutral = settings.DefaultNeutral;
	//	runOnceRan = true;
	//}

	///////////////////////////////////////////////////////////////////////////
	// Active whenever Manual is enabled from here
	///////////////////////////////////////////////////////////////////////////

	if (logiWheel.IsActive && prevInput == InputDevices::Wheel) {
		playWheelEffects();
		//logiWheel.PlayWheelEffects(settings, vehData, vehicle);
		logiWheel.DoWheelSteering();
		std::stringstream infos;
	}

	if (playerPed != VEHICLE::GET_PED_IN_VEHICLE_SEAT(vehicle, -1)) {
		if (patchedSpecial) {
			logger.Write("Not a driver:");
			patchedSpecial = !MemoryPatcher::RestoreJustS_LOW();
		}
		return;
	}

	if (!patched && settings.EnableManual) {
		logger.Write("Re-patching functions");
		patched = MemoryPatcher::PatchInstructions();
	}

	// Special case for clutch used by all vehicles
	// Only patch if user desires to have their clutch @ 0
	if (!settings.DisableFullClutch) {
		if (vehData.SimulatedNeutral || controls.Clutchvalf >= 0.96f) {
			if (!patchedSpecial) {
				patchedSpecial = MemoryPatcher::PatchJustS_LOW();
			}
		}
		else {
			if (patchedSpecial) {
				patchedSpecial = !MemoryPatcher::RestoreJustS_LOW();
			}
		}
	}
	
	if (settings.UITips) {
		if (vehData.SimulatedNeutral) {
			showText(settings.UITips_X, settings.UITips_Y, settings.UITips_Size, "N");
		}
		else {
			showText(settings.UITips_X, settings.UITips_Y, settings.UITips_Size, (char *)std::to_string(vehData.CurrGear).c_str());
		}
	}
	
	///////////////////////////////////////////////////////////////////////////
	// Actual mod operations
	///////////////////////////////////////////////////////////////////////////

	// Reverse behavior
	// For bikes, do this automatically.
	if (vehData.IsBike) {
		functionAutoReverse();
	}
	else {
		// New reverse: Reverse with throttle
		if (settings.RealReverse) {
			functionRealReverse();
			if (logiWheel.IsActive && prevInput == InputDevices::Wheel) {
				handlePedalsRealReverse(
					logiWheel.GetLogiThrottleVal(),
					logiWheel.GetLogiBrakeVal());
			}
		}
		// Reversing behavior: just block the direction you don't wanna go to
		else {
			functionSimpleReverse();
			if (logiWheel.IsActive && prevInput == InputDevices::Wheel) {
				handlePedalsDefault(
					logiWheel.GetLogiThrottleVal(),
					logiWheel.GetLogiBrakeVal());
			}
		}
	}
	

	// Limit truck speed per gear upon game wanting to shift, but we block it.
	if (vehData.IsTruck) {
		functionTruckLimiting();
	}

	// Since we know prevGear and the speed for that, let's simulate "engine" braking.
	if (settings.EngBrake) {
		functionEngBrake();
	}

	// Engine damage: RPM Damage
	if (settings.EngDamage) {
		functionEngDamage();
	}

	// Stalling
	if (settings.EngStall && !simpleBike) {
		functionEngStall();
	}

	// Simulate "catch point"
	// When the clutch "grabs" and the car starts moving without input
	// TODO Differentiate between diesel en gasoline?
	if (settings.ClutchCatching && !simpleBike) {
		functionClutchCatch();
	}

	// Manual shifting
	if (settings.Hshifter && !vehData.IsBike) {
		if (logiWheel.IsActive && prevInput == InputDevices::Wheel) {
			functionHShiftLogitech();
		}
		else {
			functionHShiftKeyboard();
		}
	}
	else {
		functionSShift();
	}

	handleVehicleButtons();
	// Finally, update memory each loop
	handleRPM();
	if (vehData.SimulatedNeutral) {
		ext.SetClutch(vehicle, 0.0f);
	}
	else {
		ext.SetClutch(vehicle, 1.0f - controls.Clutchvalf);
	}
	ext.SetGears(vehicle, vehData.LockGears);
}

void main() {
	settings.Read(&controls);
	if (settings.LogiWheel) {
		logiWheel.InitWheel(settings, logger);
	}
	else {
		logger.Write("Wheel disabled");
	}

	while (true) {
		update();
		WAIT(0);
	}
}

void ScriptMain() {
	srand(GetTickCount());
	main();
}

///////////////////////////////////////////////////////////////////////////////
//                           Helper functions/tools
///////////////////////////////////////////////////////////////////////////////

void showText(float x, float y, float scale, char * text) {
	UI::SET_TEXT_FONT(0);
	UI::SET_TEXT_SCALE(scale, scale);
	UI::SET_TEXT_COLOUR(255, 255, 255, 255);
	UI::SET_TEXT_WRAP(0.0, 1.0);
	UI::SET_TEXT_CENTRE(0);
	UI::SET_TEXT_DROPSHADOW(0, 0, 0, 0, 0);
	UI::SET_TEXT_EDGE(1, 0, 0, 0, 205);
	UI::_SET_TEXT_ENTRY("STRING");
	UI::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(text);
	UI::_DRAW_TEXT(x, y);
}

void showNotification(char *message) {
	if (prevNotification)
		UI::_REMOVE_NOTIFICATION(prevNotification);
	UI::_SET_NOTIFICATION_TEXT_ENTRY("STRING");
	UI::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(message);
	prevNotification = UI::_DRAW_NOTIFICATION(false, false);
}

void showDebugInfo() {
	std::stringstream infos;
	infos << "RPM: " << std::setprecision(3) << vehData.Rpm <<
		"\nCurrGear: " << vehData.CurrGear <<
		"\nNextGear: " << vehData.NextGear <<
		"\nClutch: " << std::setprecision(3) << vehData.Clutch <<
		"\nThrottle: " << std::setprecision(3) << vehData.Throttle <<
		"\nTurbo: " << std::setprecision(3) << vehData.Turbo <<
		"\nAddress: " << std::hex << vehData.Address <<
		"\nE: " << (settings.EnableManual ? "Y" : "N");
	const char *infoc = infos.str().c_str();
	showText(0.01f, 0.5f, 0.4f, (char *)infoc);

	// pls no you see dumping data is annoying if you're lazy and then refactor
	/*if (logiWheel.IsActive) {
		std::stringstream throttleDisplay;
		throttleDisplay << "TVal: " << logiThrottleVal;
		std::stringstream brakeDisplay;
		brakeDisplay << "BVal: " << logiBrakeVal;
		std::stringstream clutchDisplay;
		clutchDisplay << "CVal: " << logiClutchVal;
		showText(0.01, 0.04, 0.4, (char *)throttleDisplay.str().c_str());
		showText(0.01, 0.06, 0.4, (char *)brakeDisplay.str().c_str());
		showText(0.01, 0.08, 0.4, (char *)clutchDisplay.str().c_str());
		std::stringstream throttleXDisplay;
		throttleXDisplay << "TPos: " << logiThrottlePos;
		std::stringstream brakeXDisplay;
		brakeXDisplay << "BPos: " << logiBrakePos;
		std::stringstream clutchXDisplay;
		clutchXDisplay << "CPos: " << logiClutchPos;
		showText(0.01, 0.10, 0.4, (char *)throttleXDisplay.str().c_str());
		showText(0.01, 0.12, 0.4, (char *)brakeXDisplay.str().c_str());
		showText(0.01, 0.14, 0.4, (char *)clutchXDisplay.str().c_str());
		std::stringstream wheelDisplay;
		wheelDisplay << "Wheel:" << logiWheelVal;
		showText(0.01, 0.16, 0.4, (char *)wheelDisplay.str().c_str());
	}*/
}

// Limitations: Detects on pressing throttle on any of the 3 input methods
int getLastInputDevice(int previousInput) {
	if (controls.IsKeyJustPressed(controls.Control[(int)ScriptControls::KeyboardControlType::Throttle], ScriptControls::KeyboardControlType::Throttle) ||
		controls.IsKeyPressed(controls.Control[(int)ScriptControls::KeyboardControlType::Throttle])) {
		return InputDevices::Keyboard;
	}
	if (controller.IsButtonJustPressed(controller.StringToButton(controls.ControlXbox[(int)ScriptControls::ControllerControlType::Throttle]), buttonState) ||
		controller.IsButtonPressed(controller.StringToButton(controls.ControlXbox[(int)ScriptControls::ControllerControlType::Throttle]), buttonState)) {
		return InputDevices::Controller;
	}
	if (logiWheel.IsActive &&
		(logiWheel.GetLogiThrottleVal() > 0.1f ||
			logiWheel.GetLogiBrakeVal() > 0.1f ||
			logiWheel.GetLogiClutchVal() < 0.9f)) {
		return InputDevices::Wheel;
	}
	return previousInput;
}

///////////////////////////////////////////////////////////////////////////////
//                           Mod functions: Mod control
///////////////////////////////////////////////////////////////////////////////

void reInit() {
	settings.Read(&controls);
	vehData.LockGears = 0x00010001;
	vehData.SimulatedNeutral = settings.DefaultNeutral;
	if (settings.LogiWheel) {
		logiWheel.InitWheel(settings, logger);
	}
	else {
		logger.Write("Wheel disabled");
	}
}

void reset() {
	if (active) {
		if (logiWheel.IsActive) {
			resetWheelFeedback(logiWheel.GetIndex());
		}
		vehData.Clear();
		vehicle = 0;
		patched = !MemoryPatcher::RestoreInstructions();
		patchedSpecial = !MemoryPatcher::RestoreJustS_LOW();
		active = false;
	}
}

void toggleManual() {
	settings.EnableManual = !settings.EnableManual;
	std::stringstream message;
	message << "Manual Transmission " <<
		(settings.EnableManual ? "Enabled" : "Disabled");
	showNotification((char *)message.str().c_str());
	logger.Write((char *)message.str().c_str());
	if (ENTITY::DOES_ENTITY_EXIST(vehicle)) {
		VEHICLE::SET_VEHICLE_HANDBRAKE(vehicle, false);
		VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, true, false, true);
	}
	if (!settings.EnableManual) {
		patched = !MemoryPatcher::RestoreInstructions();
		patchedSpecial = !MemoryPatcher::RestoreJustS_LOW();
	}
	//if (!runOnceRan)
	//	runOnceRan = true;
	settings.Save();
	reInit();
}

///////////////////////////////////////////////////////////////////////////////
//                           Mod functions: Shifting
///////////////////////////////////////////////////////////////////////////////

void shiftTo(int gear, bool autoClutch) {
	if (autoClutch) {
		controls.Clutchvalf = 1.0f;
		ext.SetThrottle(vehicle, 0.0f);
	}
	vehData.LockGears = gear | (gear << 16);
	vehData.LockTruck = false;
	vehData.PrevGear = vehData.CurrGear;
	vehData.LockSpeeds[vehData.CurrGear] = vehData.Velocity;
}

void functionHShiftTo(int i) {
	if (settings.ClutchShifting) {
		if (controls.Clutchvalf > 1.0f-settings.ClutchCatchpoint) {
			shiftTo(i, false);
			vehData.SimulatedNeutral = false;
		}
		else {
			vehData.SimulatedNeutral = true;
			if (settings.EngDamage) {
				VEHICLE::SET_VEHICLE_ENGINE_HEALTH(
					vehicle, 
					VEHICLE::GET_VEHICLE_ENGINE_HEALTH(vehicle) - settings.MisshiftDamage);
			}
		}
	}
	else {
		shiftTo(i, true);
		vehData.SimulatedNeutral = false;
	}
}

void functionHShiftKeyboard() {
	// All keys checking
	for (uint8_t i = 0; i <= vehData.TopGear; i++) {
		if (i > (int)ScriptControls::KeyboardControlType::H8) // this shit is just silly can I rly do dis?
			i = (int)ScriptControls::KeyboardControlType::H8; // holy shit bad, bad, hacky idea
		if (controls.IsKeyJustPressed(controls.Control[i], (ScriptControls::KeyboardControlType)i)) {
			functionHShiftTo(i);
		}
	}
	if (controls.IsKeyJustPressed(controls.Control[(int)ScriptControls::KeyboardControlType::HN], ScriptControls::KeyboardControlType::HN)) {
		vehData.SimulatedNeutral = !vehData.SimulatedNeutral;
	}
}

void functionHShiftLogitech() {
	for (uint i = 0; i < vehData.TopGear; i++) {
		if (i > (int)ScriptControls::LogiControlType::H6)
			i = (int)ScriptControls::LogiControlType::H6;
		if (LogiButtonTriggered(logiWheel.GetIndex(), controls.LogiControl[i])) {
			functionHShiftTo(i);
		}
	}
	
	if (LogiButtonReleased(logiWheel.GetIndex(), controls.LogiControl[(int)ScriptControls::LogiControlType::H1]) ||
		LogiButtonReleased(logiWheel.GetIndex(), controls.LogiControl[(int)ScriptControls::LogiControlType::H2]) ||
		LogiButtonReleased(logiWheel.GetIndex(), controls.LogiControl[(int)ScriptControls::LogiControlType::H3]) ||
		LogiButtonReleased(logiWheel.GetIndex(), controls.LogiControl[(int)ScriptControls::LogiControlType::H4]) ||
		LogiButtonReleased(logiWheel.GetIndex(), controls.LogiControl[(int)ScriptControls::LogiControlType::H5]) ||
		LogiButtonReleased(logiWheel.GetIndex(), controls.LogiControl[(int)ScriptControls::LogiControlType::H6])) {
		if (settings.ClutchShifting && settings.EngDamage) {
			if (controls.Clutchvalf < 1.0-settings.ClutchCatchpoint) {
				VEHICLE::SET_VEHICLE_ENGINE_HEALTH(
					vehicle,
					VEHICLE::GET_VEHICLE_ENGINE_HEALTH(vehicle) - settings.MisshiftDamage/10);
			}
		}
		vehData.SimulatedNeutral = true;
	}
	
	if (LogiButtonReleased(logiWheel.GetIndex(), controls.LogiControl[(int)ScriptControls::LogiControlType::HR])) {
		shiftTo(1, true);
		vehData.SimulatedNeutral = true;
	}
}

void functionSShift() {
	// Shift up
	if (controller.IsButtonJustReleased(controller.StringToButton(controls.ControlXbox[(int)ScriptControls::ControllerControlType::ShiftUp]), buttonState) ||
		controls.IsKeyJustPressed(controls.Control[(int)ScriptControls::KeyboardControlType::ShiftUp], ScriptControls::KeyboardControlType::ShiftUp) ||
		(logiWheel.IsActive && LogiButtonTriggered(logiWheel.GetIndex(), controls.LogiControl[(int)ScriptControls::LogiControlType::ShiftUp]))) {
		
		// Reverse to Neutral
		if (vehData.CurrGear == 0 && !vehData.SimulatedNeutral) {
			shiftTo(1, true);
			vehData.SimulatedNeutral = true;
			return;
		}

		// Neutral to 1
		if (vehData.CurrGear == 1 && vehData.SimulatedNeutral) {
			vehData.SimulatedNeutral = false;
			return;
		}
		
		// 1 to X
		if (vehData.CurrGear < vehData.TopGear) {
			shiftTo(vehData.LockGear + 1, true);
		}
	}

	// Shift down
	if (controller.IsButtonJustReleased(controller.StringToButton(controls.ControlXbox[(int)ScriptControls::ControllerControlType::ShiftDown]), buttonState) ||
		controls.IsKeyJustPressed(controls.Control[(int)ScriptControls::KeyboardControlType::ShiftDown], ScriptControls::KeyboardControlType::ShiftDown) ||
		(logiWheel.IsActive && LogiButtonTriggered(logiWheel.GetIndex(), controls.LogiControl[(int)ScriptControls::LogiControlType::ShiftDown]))) {
		
		// 1 to Neutral
		if (vehData.CurrGear == 1 && !vehData.SimulatedNeutral) {
			vehData.SimulatedNeutral = true;
			return;
		}

		// Neutral to R
		if (vehData.CurrGear == 1 && vehData.SimulatedNeutral) {
			shiftTo(0, true);
			vehData.SimulatedNeutral = false;
			return;
		}

		// X to 1
		if (vehData.CurrGear > 1) {
			shiftTo(vehData.LockGear - 1, true);
		}
	}

}

///////////////////////////////////////////////////////////////////////////////
//                          Mod functions: Features
///////////////////////////////////////////////////////////////////////////////

void functionClutchCatch() {
	if (vehData.Clutch >= settings.ClutchCatchpoint) {
		// Forward
		if (vehData.CurrGear > 0 && vehData.Velocity < vehData.CurrGear * 2.2f &&
			controls.Rtvalf < 0.25f && controls.Ltvalf < 0.95 ) {
			CONTROLS::_SET_CONTROL_NORMAL(0, ControlVehicleAccelerate, 0.37f);
		}
	}
}

void functionEngStall() {
	if (vehData.Clutch > 1.0f-settings.StallingThreshold && vehData.Rpm < 0.25f &&
		((vehData.Speed < vehData.CurrGear * 1.4f) || (vehData.CurrGear == 0 && vehData.Speed < 1.0f))) {
		if (VEHICLE::_IS_VEHICLE_ENGINE_ON(vehicle)) {
			VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, false, true, true);
		}
	}
}

void functionEngDamage() {
	if ( vehData.Rpm > 0.98f && controls.Accelvalf > 0.99f) {
		VEHICLE::SET_VEHICLE_ENGINE_HEALTH(vehicle, VEHICLE::GET_VEHICLE_ENGINE_HEALTH(vehicle) - (settings.RPMDamage));
	}
}

void functionEngBrake() {
	// Save speed @ shift
	if (vehData.CurrGear < vehData.NextGear) {
		if (vehData.PrevGear <= vehData.CurrGear ||
			vehData.Velocity <= vehData.LockSpeed ||
			vehData.LockSpeed < 0.01f) {
			vehData.LockSpeed = vehData.Velocity;
		}
	}
	// Braking
	if (vehData.CurrGear > 0 &&
		vehData.Velocity > vehData.LockSpeeds.at(vehData.CurrGear) &&
		controls.Rtvalf < 0.1 && vehData.Rpm > 0.80) {
		float brakeForce = -0.1f * (1.0f - controls.Clutchvalf) * vehData.Rpm;
		ENTITY::APPLY_FORCE_TO_ENTITY_CENTER_OF_MASS(vehicle, 1, 0.0f, brakeForce, 0.0f, true, true, true, true);
	}
}

///////////////////////////////////////////////////////////////////////////////
//                       Mod functions: Gearbox control
///////////////////////////////////////////////////////////////////////////////
void handleRPM() {
	// Game wants to shift up. Triggered at high RPM, high speed.
	// Desired result: high RPM, same gear, no more accelerating
	// Result:	Is as desired. Speed may drop a bit because of game clutch.
	if (vehData.CurrGear > 0 &&
		(vehData.CurrGear < vehData.NextGear && vehData.Speed > 2.0f)) {
		ext.SetThrottle(vehicle, 1.2f);
		ext.SetCurrentRPM(vehicle, 1.07f);
	}

	// Emulate previous "shift down wanted" behavior.
	if (vehData.CurrGear > 1 && vehData.Rpm < 0.4f) {
		VEHICLE::_SET_VEHICLE_ENGINE_TORQUE_MULTIPLIER(vehicle, vehData.Rpm * 2.5f);
	}

	// Game doesn't really support revving on disengaged clutch in any gear but 1
	// Simulate this
	if (vehData.CurrGear > 1 && controls.Clutchvalf > 0.4f) {
		float revValue = vehData.Throttle;
		if (revValue > 0.2f) {
			ext.SetCurrentRPM(vehicle, revValue <= 0.99f ? revValue : 1.05f);
		}
	}
}

void functionTruckLimiting() {
	// Update speed
	if (vehData.CurrGear < vehData.NextGear) {
		vehData.LockSpeed = vehData.Velocity;
		vehData.LockTruck = true;
	}

	// Limit
	if ((vehData.Velocity > vehData.LockSpeed && vehData.LockTruck) ||
		(vehData.Velocity > vehData.LockSpeed && vehData.PrevGear > vehData.CurrGear)) {
		controls.Clutchvalf = 1.0f;
		ext.SetThrottle(vehicle, 0.0f);
		DECORATOR::DECOR_SET_INT(vehicle, "hunt_score", 1); // Uglyyyyy
	}
}

///////////////////////////////////////////////////////////////////////////////
//                       Mod functions: Reverse
///////////////////////////////////////////////////////////////////////////////
void functionRealReverse() {
	// Forward gear
	// Desired: Only brake
	if (vehData.CurrGear > 0) {
		// LT behavior when still
		if (controls.Ltvalf > 0.02f && controls.Rtvalf < controls.Ltvalf &&
			vehData.Velocity <= 0.5f && vehData.Velocity >= -0.1f) {
			CONTROLS::DISABLE_CONTROL_ACTION(0, ControlVehicleBrake, true);
			ext.SetThrottleP(vehicle, 0.0f);
			VEHICLE::SET_VEHICLE_BRAKE_LIGHTS(vehicle, true);
			VEHICLE::SET_VEHICLE_HANDBRAKE(vehicle, true);
		}
		else {
			VEHICLE::SET_VEHICLE_HANDBRAKE(vehicle, false);
		}
		// LT behavior when rolling back
		if (controls.Ltvalf > 0.02f && controls.Rtvalf < controls.Ltvalf &&
			vehData.Velocity < -0.1f) {
			VEHICLE::SET_VEHICLE_BRAKE_LIGHTS(vehicle, true);
			CONTROLS::DISABLE_CONTROL_ACTION(0, ControlVehicleBrake, true);
			CONTROLS::_SET_CONTROL_NORMAL(0, ControlVehicleAccelerate, controls.Ltvalf);
			ext.SetThrottle(vehicle, 0.0f);
			ext.SetThrottleP(vehicle, 0.1f);
		}
	}
	// Reverse gear
	// Desired: RT reverses, LT brakes
	if (vehData.CurrGear == 0) {
		ext.SetThrottleP(vehicle, -0.1f);
		// RT behavior
		if (controls.Rtvalf > 0.02f && controls.Rtvalf > controls.Ltvalf) {
			CONTROLS::DISABLE_CONTROL_ACTION(0, ControlVehicleAccelerate, true);
			CONTROLS::_SET_CONTROL_NORMAL(0, ControlVehicleBrake, controls.Rtvalf);
		}
		// LT behavior when still
		if (controls.Ltvalf > 0.0f && controls.Rtvalf <= controls.Ltvalf &&
			vehData.Velocity > -0.55f && vehData.Velocity <= 0.5f) {
			VEHICLE::SET_VEHICLE_BRAKE_LIGHTS(vehicle, true);
			CONTROLS::DISABLE_CONTROL_ACTION(0, ControlVehicleBrake, true);
			VEHICLE::SET_VEHICLE_HANDBRAKE(vehicle, true);
		}
		else {
			VEHICLE::SET_VEHICLE_HANDBRAKE(vehicle, false);
		}
		// LT behavior when reversing
		if (controls.Ltvalf > 0.02f &&
			vehData.Velocity <= -0.5f) {
			CONTROLS::DISABLE_CONTROL_ACTION(0, ControlVehicleBrake, true);
			CONTROLS::_SET_CONTROL_NORMAL(0, ControlVehicleAccelerate, controls.Ltvalf);
		}
	}
}


// Forward gear: Throttle accelerates, Brake brakes (exclusive)
// Reverse gear: Throttle reverses, Brake brakes (exclusive)

void handlePedalsRealReverse(float logiThrottleVal, float logiBrakeVal) {
	if (vehData.CurrGear > 0) {
		// Throttle Pedal normal
		if (logiThrottleVal > 0.02f) {
			CONTROLS::_SET_CONTROL_NORMAL(0, ControlVehicleAccelerate, logiThrottleVal);
		}
		// Brake Pedal normal
		if (logiBrakeVal > 0.02f) {
			CONTROLS::_SET_CONTROL_NORMAL(0, ControlVehicleBrake, logiBrakeVal);
		}
		// Brake Pedal still
		if (logiBrakeVal > 0.02f && logiThrottleVal < logiBrakeVal &&
			vehData.Velocity <= 0.5f && vehData.Velocity >= -0.1f) {
			CONTROLS::DISABLE_CONTROL_ACTION(0, ControlVehicleBrake, true);
			ext.SetThrottleP(vehicle, 0.0f);
			VEHICLE::SET_VEHICLE_BRAKE_LIGHTS(vehicle, true);
			VEHICLE::SET_VEHICLE_HANDBRAKE(vehicle, true);
		}
	}
	
	if (vehData.CurrGear == 0) {
		// Throttle Pedal Reverse
		if (logiThrottleVal > 0.02f && logiThrottleVal > logiBrakeVal) {
			CONTROLS::_SET_CONTROL_NORMAL(0, ControlVehicleBrake, logiThrottleVal);
		}
		// Brake Pedal stationary
		if (logiBrakeVal > 0.02f && logiThrottleVal <= logiBrakeVal &&
			vehData.Velocity > -0.55f && vehData.Velocity <= 0.5f) {
			VEHICLE::SET_VEHICLE_BRAKE_LIGHTS(vehicle, true);
			CONTROLS::DISABLE_CONTROL_ACTION(0, ControlVehicleBrake, true);
			VEHICLE::SET_VEHICLE_HANDBRAKE(vehicle, true);
		}

		// Brake Pedal Reverse
		if (logiBrakeVal > 0.02f &&
			vehData.Velocity <= -0.5f) {
			CONTROLS::_SET_CONTROL_NORMAL(0, ControlVehicleAccelerate, logiBrakeVal);
		}
	}
}

// Pedals behave like RT/LT
void handlePedalsDefault(float logiThrottleVal, float logiBrakeVal) {
	if (logiThrottleVal > 0.02f) {
		CONTROLS::_SET_CONTROL_NORMAL(0, ControlVehicleAccelerate, logiThrottleVal);
	}
	if (logiBrakeVal > 0.02f) {
		CONTROLS::_SET_CONTROL_NORMAL(0, ControlVehicleBrake, logiBrakeVal);
	}
}


void functionSimpleReverse() {
	// Prevent going forward in gear 0.
	if (vehData.CurrGear == 0 && vehData.Velocity > -0.55f && vehData.Velocity <= 0.5f
		&& controls.Rtvalf > 0) {
		VEHICLE::SET_VEHICLE_BRAKE_LIGHTS(vehicle, true);
		CONTROLS::DISABLE_CONTROL_ACTION(0, ControlVehicleAccelerate, true);
	}
	// Prevent reversing in gear >= 1.
	if (vehData.CurrGear > 0 && vehData.Velocity > -0.55f && vehData.Velocity <= 0.5f
		&& controls.Ltvalf > 0 ) {
		VEHICLE::SET_VEHICLE_BRAKE_LIGHTS(vehicle, true);
		CONTROLS::DISABLE_CONTROL_ACTION(0, ControlVehicleBrake, true);
	}
}

void functionAutoReverse() {
	// Go forward
	if (CONTROLS::IS_CONTROL_PRESSED(0, ControlVehicleAccelerate)
		&& !CONTROLS::IS_CONTROL_PRESSED(0, ControlVehicleBrake) && vehData.Velocity > -1.0f) {
		if (vehData.CurrGear == 0) {
			vehData.LockGears = 0x00010001;
		}
	}

	// Reverse
	if (CONTROLS::IS_CONTROL_PRESSED(0, ControlVehicleBrake)
		&& !CONTROLS::IS_CONTROL_PRESSED(0, ControlVehicleAccelerate) && vehData.Velocity < 1.0f) {
		if (vehData.CurrGear > 0) {	
			if (vehData.SimulatedNeutral) {
				vehData.SimulatedNeutral = false;
			}
			vehData.LockGears = 0x00000000;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
//                       Mod functions: Buttons
///////////////////////////////////////////////////////////////////////////////
void handleVehicleButtons() {
	if (!VEHICLE::_IS_VEHICLE_ENGINE_ON(vehicle) &&
		(	controller.IsButtonJustPressed(controller.StringToButton(controls.ControlXbox[(int)ScriptControls::ControllerControlType::Engine]), buttonState) ||
			controls.IsKeyJustPressed(controls.Control[(int)ScriptControls::KeyboardControlType::Engine], ScriptControls::KeyboardControlType::Engine) ||
			LogiButtonTriggered(logiWheel.GetIndex(), controls.LogiControl[(int)ScriptControls::LogiControlType::Engine]))) {
		VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, true, false, true);
	}
	if (logiWheel.IsActive) {
		if (LogiGetState(logiWheel.GetIndex())->rgbButtons[controls.LogiControl[(int)ScriptControls::LogiControlType::Handbrake]]) {
			VEHICLE::SET_VEHICLE_HANDBRAKE(vehicle, true);
		}
		if (LogiGetState(logiWheel.GetIndex())->rgbButtons[controls.LogiControl[(int)ScriptControls::LogiControlType::Horn]]) {
			CONTROLS::_SET_CONTROL_NORMAL(0, ControlVehicleHorn, 1.0f);
		}
		if (LogiButtonTriggered(logiWheel.GetIndex(), controls.LogiControl[(int)ScriptControls::LogiControlType::Lights])) {
			CONTROLS::_SET_CONTROL_NORMAL(0, ControlVehicleHeadlight, 1.0f);
		}
		if (LogiGetState(logiWheel.GetIndex())->rgbButtons[controls.LogiControl[(int)ScriptControls::LogiControlType::LookBack]]) {
			CONTROLS::_SET_CONTROL_NORMAL(0, ControlVehicleLookBehind, 1.0f);
		}
		if (LogiButtonTriggered(logiWheel.GetIndex(), controls.LogiControl[(int)ScriptControls::LogiControlType::Camera])) {
			CONTROLS::_SET_CONTROL_NORMAL(0, ControlNextCamera, 1.0f);
		}
		if (LogiButtonTriggered(logiWheel.GetIndex(), controls.LogiControl[(int)ScriptControls::LogiControlType::RadioNext])) {
			AUDIO::SET_RADIO_TO_STATION_INDEX(AUDIO::GET_PLAYER_RADIO_STATION_INDEX() + 1);
		}
		if (LogiButtonTriggered(logiWheel.GetIndex(), controls.LogiControl[(int)ScriptControls::LogiControlType::RadioPrev])) {
			AUDIO::SET_RADIO_TO_STATION_INDEX(AUDIO::GET_PLAYER_RADIO_STATION_INDEX() - 1);
		}
		switch (LogiGetState(logiWheel.GetIndex())->rgdwPOV[0]) {
		case 0:
			break;
		case 9000:
			CONTROLS::_SET_CONTROL_NORMAL(0, ControlVehicleNextRadio, 1.0f);
			break;
		case 18000:
			break;
		case 27000:
			CONTROLS::_SET_CONTROL_NORMAL(0, ControlVehiclePrevRadio, 1.0f);
			break;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
//                             Wheel functions
///////////////////////////////////////////////////////////////////////////////
/*
void playWheelEffects() {
	LogiPlayLeds(logiWheel.GetIndex(), vehData.Rpm, 0.5f, 0.95f);

	int damperforce = 0;
	if (settings.FFDamperStationary < settings.FFDamperMoving) {
		settings.FFDamperMoving = settings.FFDamperStationary;
	}
	int ratio = (settings.FFDamperStationary - settings.FFDamperMoving) / 10;

	damperforce = settings.FFDamperStationary - ratio * (int)(vehData.Speed);
	if (vehData.Speed > 10.0f) {
		damperforce = settings.FFDamperMoving + (int)(0.5f * (vehData.Speed - 10.0f));
	}

	
	//damperforce = 80 - 4 * (int)(vehData.Speed);
	//if (vehData.Speed > 10.0f) {
	//	damperforce = 40 + (int)(0.5f * (vehData.Speed - 10.0f));
	//}
	//
	//if (damperforce > 60 {
	//	damperforce = 60;
	//}
	

	if (damperforce > (settings.FFDamperStationary + settings.FFDamperMoving) / 2) {
		damperforce = (settings.FFDamperStationary + settings.FFDamperMoving) / 2;
	}
	LogiPlayDamperForce(logiWheel.GetIndex(), damperforce);

	Vector3 accelVals = vehData.getAccelerationVectors(ENTITY::GET_ENTITY_SPEED_VECTOR(vehicle, true));
	LogiPlayConstantForce(logiWheel.GetIndex(), (int)(-settings.FFPhysics*accelVals.x));
	LogiPlaySpringForce(logiWheel.GetIndex(), 0, (int)vehData.Speed, (int)vehData.Speed);

	if (!VEHICLE::IS_VEHICLE_ON_ALL_WHEELS(vehicle) && ENTITY::GET_ENTITY_HEIGHT_ABOVE_GROUND(vehicle) > 1.25f) {
		LogiPlayCarAirborne(logiWheel.GetIndex());
	}
	else if (LogiIsPlaying(logiWheel.GetIndex(), LOGI_FORCE_CAR_AIRBORNE)) {
		LogiStopCarAirborne(logiWheel.GetIndex());
	}

	//if (PLAYER::GET_TIME_SINCE_PLAYER_HIT_VEHICLE(player) < 5) {
	//	LogiPlayFrontalCollisionForce(logiWheel.GetIndex(), 25);
	//	showNotification("Crash");
	//}
	
	
	//if (PLAYER::GET_TIME_SINCE_PLAYER_HIT_PED(player) < 5) {
	//	LogiPlayBumpyRoadEffect(logiWheel.GetIndex(), 10);
	//	showNotification("Bump");
	//}
	//else if (LogiIsPlaying(logiWheel.GetIndex(), LOGI_FORCE_BUMPY_ROAD)) {
	//	LogiStopBumpyRoadEffect(logiWheel.GetIndex());
	//}
	//
	//Vector3 vehCoords = ENTITY::GET_ENTITY_COORDS(vehicle, true);
	//if (!PATHFIND::IS_POINT_ON_ROAD(vehCoords.x, vehCoords.y, vehCoords.z, vehicle)) {
	//	LogiPlayDirtRoadEffect(logiWheel.GetIndex(), (int)(6.0f*powf(vehData.Speed, 0.25f)));
	//}
	//else if (LogiIsPlaying(logiWheel.GetIndex(), LOGI_FORCE_DIRT_ROAD)) {
	//	LogiStopDirtRoadEffect(logiWheel.GetIndex());
	//}
}

// Updates logiWheelVal, logiThrottleVal, logiBrakeVal, logiClutchVal
void updateLogiValues() {
	logiSteeringWheelPos = LogiGetState(logiWheel.GetIndex())->lX;
	logiThrottlePos = LogiGetState(logiWheel.GetIndex())->lY;
	logiBrakePos = LogiGetState(logiWheel.GetIndex())->lRz;
	logiClutchPos = LogiGetState(logiWheel.GetIndex())->rglSlider[1];
	//LogiGenerateNonLinearValues(logiWheel.GetIndex(), 33);
	//logiSteeringWheelPos = LogiGetNonLinearValue(logiWheel.GetIndex(), LogiGetState(logiWheel.GetIndex())->lX);
	//  32767 @ nope | 0
	// -32768 @ full | 1
	logiWheelVal = ((float)(logiSteeringWheelPos - 32767) / -65535.0f)-0.5f;
	logiThrottleVal = (float)(logiThrottlePos - 32767) / -65535.0f;
	logiBrakeVal =    (float)(logiBrakePos - 32767) / -65535.0f;
	logiClutchVal =   1.0f+(float)(logiClutchPos - 32767) / 65535.0f;
}

void doWheelSteering() {
	// Anti-deadzone
	int additionalOffset = 2560;
	float antiDeadzoned = 0.0f;
	antiDeadzoned = logiSteeringWheelPos / 32768.0f;
	if (//logiSteeringWheelPos > -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE &&
		logiSteeringWheelPos <= 0) {
		antiDeadzoned = (logiSteeringWheelPos - XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE - additionalOffset) / (32768.0f + XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE + additionalOffset);
	}
	if (//logiSteeringWheelPos > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE &&
		logiSteeringWheelPos > 0) {
		antiDeadzoned = (logiSteeringWheelPos + XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE + additionalOffset) / (32768.0f + XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE + additionalOffset);
	}
	// Gotta find a way to make this no-delay
	CONTROLS::_SET_CONTROL_NORMAL(27, ControlVehicleMoveLeftRight, antiDeadzoned);
}

void initWheel() {
	if (settings.LogiWheel) {
		LogiSteeringInitialize(TRUE);
		if (LogiUpdate() && LogiIsConnected(logiWheel.GetIndex())) {
			logger.Write("Wheel detected");
		}
		else {
			logger.Write("No wheel detected");
		}
	}
	else {
		logger.Write("Wheel disabled");
	}
}
*/


// Despair: Won't work if arguments are passed?!
void playWheelEffects() {
	Vector3 accelVals = vehData.getAccelerationVectors(ENTITY::GET_ENTITY_SPEED_VECTOR(vehicle, true));
	
	LogiPlayLeds(logiWheel.GetIndex(), vehData.Rpm, 0.5f, 0.95f);

	int damperforce = 0;
	if (settings.FFDamperStationary < settings.FFDamperMoving) {
		settings.FFDamperMoving = settings.FFDamperStationary;
	}
	int ratio = (settings.FFDamperStationary - settings.FFDamperMoving) / 10;

	damperforce = settings.FFDamperStationary - ratio * (int)(vehData.Speed);
	if (vehData.Speed > 10.0f) {
		damperforce = settings.FFDamperMoving + (int)(0.5f * (vehData.Speed - 10.0f));
	}

	damperforce += (int)(-10.0f * accelVals.y);
	showText(0.1, 0.1, 1.0, (char *)std::to_string(accelVals.y).c_str());

	if (damperforce > (settings.FFDamperStationary + settings.FFDamperMoving) / 2) {
		damperforce = (settings.FFDamperStationary + settings.FFDamperMoving) / 2;
	}
	LogiPlayDamperForce(logiWheel.GetIndex(), damperforce);

	LogiPlayConstantForce(logiWheel.GetIndex(), (int)(-settings.FFPhysics*accelVals.x));
	LogiPlaySpringForce(logiWheel.GetIndex(), 0, (int)vehData.Speed, (int)vehData.Speed);

	if (!VEHICLE::IS_VEHICLE_ON_ALL_WHEELS(vehicle) && ENTITY::GET_ENTITY_HEIGHT_ABOVE_GROUND(vehicle) > 1.25f) {
		LogiPlayCarAirborne(logiWheel.GetIndex());
	}
	else if (LogiIsPlaying(logiWheel.GetIndex(), LOGI_FORCE_CAR_AIRBORNE)) {
		LogiStopCarAirborne(logiWheel.GetIndex());
	}

	if (accelVals.y > 5.0f || accelVals.y < -5.0f ) {
		LogiPlayFrontalCollisionForce(logiWheel.GetIndex(), abs((int)(accelVals.y*4.0f)));
		//showNotification("Crash");
	}
}

// Since playing back is f'd - This too, will be in the main class
void resetWheelFeedback(int index)
{
	LogiStopSpringForce(index);
	LogiStopConstantForce(index);
	LogiStopDamperForce(index);
	LogiStopDirtRoadEffect(index);
	LogiStopBumpyRoadEffect(index);
	LogiStopSlipperyRoadEffect(index);
	LogiStopSurfaceEffect(index);
	LogiStopCarAirborne(index);
	LogiStopSoftstopForce(index);
}
#include "global.h"
#include "InputHandler_Vita.h"
#include "InputEventPlus.h"
#include "RageLog.h"
#include "RageTimer.h"

#ifdef VITA
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#endif

REGISTER_INPUT_HANDLER_CLASS_Vita;

InputHandler_Vita::InputHandler_Vita()
	: m_iLastButtons(0)
{
#ifdef VITA
	// Enable analog sticks + touch sampling.
	// sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
	// sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
#endif
}

InputHandler_Vita::~InputHandler_Vita()
{
}

void InputHandler_Vita::HandleButton(DeviceInput di, bool bDown)
{
	di.ts.Touch();
	if (bDown)
		ButtonPressed(di);
	else {
		// ButtonReleased path goes through InputFilter; queue release event.
		// InputHandler::ButtonPressed handles press; release uses di with bDown=false
		// via ButtonPressed(di) with level 0 — see InputHandler.cpp convention.
		ButtonPressed(di);
	}
}

void InputHandler_Vita::Update()
{
#ifdef VITA
	SceCtrlData pad;
	if (sceCtrlPeekBufferPositive(0, &pad, 1) < 0)
		return;

	uint32_t changed = pad.buttons ^ m_iLastButtons;
	if (changed == 0) {
		UpdateTimer();
		return;
	}

#if defined(VITA)
	// Temporary bring-up logging: prove sceCtrl events reach the handler.
	// TODO(Vita): remove once input is verified on device.
	LOG->Info("VitaPad: buttons=0x%08x changed=0x%08x", pad.buttons, changed);
#endif

	// Menu navigation emits keyboard events so the default InputMapper
	// bindings (arrows/enter/esc) work with zero configuration.
	auto emit = [&](uint32_t mask, DeviceButton btn) {
		if (!(changed & mask))
			return;
		DeviceInput di(DEVICE_KEYBOARD, btn);
		di.ts.Touch();
		di.level = (pad.buttons & mask) ? 1.0f : 0.0f;
		ButtonPressed(di);
	};

	emit(SCE_CTRL_UP,       KEY_UP);
	emit(SCE_CTRL_DOWN,     KEY_DOWN);
	emit(SCE_CTRL_LEFT,     KEY_LEFT);
	emit(SCE_CTRL_RIGHT,    KEY_RIGHT);
	emit(SCE_CTRL_CROSS,    KEY_ENTER);
	emit(SCE_CTRL_START,    KEY_ENTER);
	emit(SCE_CTRL_CIRCLE,   KEY_ESC);
	emit(SCE_CTRL_TRIANGLE, KEY_TAB);
	emit(SCE_CTRL_SQUARE,   KEY_SPACE);
	emit(SCE_CTRL_SELECT,   KEY_F1);
	emit(SCE_CTRL_LTRIGGER, KEY_LEFT);
	emit(SCE_CTRL_RTRIGGER, KEY_RIGHT);
	emit(SCE_CTRL_L1,       KEY_LEFT);
	emit(SCE_CTRL_R1,       KEY_RIGHT);

	m_iLastButtons = pad.buttons;
#endif
	UpdateTimer();
}

void InputHandler_Vita::GetDevicesAndDescriptions(std::vector<InputDeviceInfo> &vDevicesOut)
{
	vDevicesOut.push_back(InputDeviceInfo(DEVICE_KEYBOARD, "Vita Pad"));
	vDevicesOut.push_back(InputDeviceInfo(DEVICE_JOY1, "Vita Pad"));
}

InputDeviceState InputHandler_Vita::GetInputDeviceState(InputDevice id)
{
	if (id == DEVICE_KEYBOARD || id == DEVICE_JOY1)
		return InputDeviceState_Connected;
	return InputDeviceState_NoInputHandler;
}

#include "global.h"
#include "InputHandler_Vita.h"
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
}

InputHandler_Vita::~InputHandler_Vita()
{
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

	// NOTE: level must go through the constructor: DeviceInput derives
	// bDown from it at construction time.
#define VITA_EMIT(mask, btn) \
	if (changed & (mask)) { \
		DeviceInput di(DEVICE_KEYBOARD, (btn), (pad.buttons & (mask)) ? 1.0f : 0.0f); \
		di.ts.Touch(); \
		ButtonPressed(di); \
	}

	VITA_EMIT(SCE_CTRL_UP,       KEY_UP);
	VITA_EMIT(SCE_CTRL_DOWN,     KEY_DOWN);
	VITA_EMIT(SCE_CTRL_LEFT,     KEY_LEFT);
	VITA_EMIT(SCE_CTRL_RIGHT,    KEY_RIGHT);
	VITA_EMIT(SCE_CTRL_CROSS,    KEY_ENTER);
	VITA_EMIT(SCE_CTRL_START,    KEY_ENTER);
	VITA_EMIT(SCE_CTRL_CIRCLE,   KEY_ESC);
	VITA_EMIT(SCE_CTRL_TRIANGLE, KEY_TAB);
	VITA_EMIT(SCE_CTRL_SQUARE,   KEY_SPACE);
	VITA_EMIT(SCE_CTRL_SELECT,   KEY_F1);
	VITA_EMIT(SCE_CTRL_LTRIGGER, KEY_LEFT);
	VITA_EMIT(SCE_CTRL_RTRIGGER, KEY_RIGHT);
	VITA_EMIT(SCE_CTRL_L1,       KEY_LEFT);
	VITA_EMIT(SCE_CTRL_R1,       KEY_RIGHT);
#undef VITA_EMIT

	m_iLastButtons = pad.buttons;
#endif
	UpdateTimer();
}

void InputHandler_Vita::GetDevicesAndDescriptions(vector<InputDeviceInfo> &vDevicesOut)
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

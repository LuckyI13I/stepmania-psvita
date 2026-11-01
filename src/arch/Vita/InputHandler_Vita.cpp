#include "global.h"
#include "InputHandler_Vita.h"
#include "VitaTouch.h"
#include "RageLog.h"
#include "RageTimer.h"

#ifdef VITA
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#endif

REGISTER_INPUT_HANDLER_CLASS_Vita;

InputHandler_Vita::InputHandler_Vita()
	: m_bUp(false), m_bDown(false), m_bLeft(false), m_bRight(false),
	m_bEnter(false), m_bSlash(false), m_bEsc(false)
{
	VitaTouch::Init();
}

InputHandler_Vita::~InputHandler_Vita()
{
}

void InputHandler_Vita::Update()
{
#ifdef VITA
	bool bUp = false, bDown = false, bLeft = false, bRight = false;
	bool bEnter = false, bSlash = false, bEsc = false;

	SceCtrlData pad;
	if (sceCtrlPeekBufferPositive(0, &pad, 1) >= 0)
	{
		const uint32_t b = pad.buttons;

		// D-pad navigates menus and plays the four arrow panels in gameplay.
		// The four face buttons mirror it, so both work with zero config.
		bUp    = (b & (SCE_CTRL_UP      | SCE_CTRL_TRIANGLE)) != 0;
		bDown  = (b & (SCE_CTRL_DOWN    | SCE_CTRL_CROSS))    != 0;
		bLeft  = (b & (SCE_CTRL_LEFT    | SCE_CTRL_SQUARE))   != 0;
		bRight = (b & (SCE_CTRL_RIGHT   | SCE_CTRL_CIRCLE))   != 0;

		// Start = confirm/enter, Select = the game Select button.
		bEnter = (b & SCE_CTRL_START) != 0;
		bSlash = (b & SCE_CTRL_SELECT) != 0;
		bEsc   = (b & (SCE_CTRL_LTRIGGER | SCE_CTRL_L1)) != 0;
	}

	// Touch lanes drive the notefield only while gameplay is running: the same
	// keys move the menus, and taps should not scroll the song wheel.
	VitaTouch::Poll();
	if (VitaTouch::InputActive())
	{
		bLeft  = bLeft  || VitaTouch::ZoneDown(VitaTouch::ZONE_LEFT);
		bDown  = bDown  || VitaTouch::ZoneDown(VitaTouch::ZONE_DOWN);
		bUp    = bUp    || VitaTouch::ZoneDown(VitaTouch::ZONE_UP);
		bRight = bRight || VitaTouch::ZoneDown(VitaTouch::ZONE_RIGHT);
	}

	// NOTE: the level must go through the constructor: DeviceInput derives
	// bDown from it at construction time.
#define VITA_EMIT_KEY(state, btn, now) \
	if ((now) != (state)) { \
		(state) = (now); \
		DeviceInput di(DEVICE_KEYBOARD, (btn), (now) ? 1.0f : 0.0f); \
		di.ts.Touch(); \
		ButtonPressed(di); \
	}

	VITA_EMIT_KEY(m_bUp,    KEY_UP,    bUp);
	VITA_EMIT_KEY(m_bDown,  KEY_DOWN,  bDown);
	VITA_EMIT_KEY(m_bLeft,  KEY_LEFT,  bLeft);
	VITA_EMIT_KEY(m_bRight, KEY_RIGHT, bRight);
	VITA_EMIT_KEY(m_bEnter, KEY_ENTER, bEnter);
	VITA_EMIT_KEY(m_bSlash, KEY_SLASH, bSlash);
	VITA_EMIT_KEY(m_bEsc,   KEY_ESC,   bEsc);
#undef VITA_EMIT_KEY
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

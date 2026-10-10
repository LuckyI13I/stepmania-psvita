#ifndef INPUT_HANDLER_VITA_H
#define INPUT_HANDLER_VITA_H

#include "arch/InputHandler/InputHandler.h"

// InputHandler_Vita: polls sceCtrlPeekBufferPositive + touch/analog.
// Mapping (draft, to be tuned in Prefs/InputMapper):
//   CROSS    -> Start / Confirm
//   CIRCLE   -> Back
//   TRIANGLE -> Select/Options
//   SQUARE   -> Coin/Menu
//   DPAD     -> Menu navigation / gameplay arrows
//   L/R, START/SELECT -> Admin/Escape, Service
//   Left analog -> remapped to DPAD for menus
//   Right analog + front/back touch -> mouse emulation (future)
class InputHandler_Vita : public InputHandler
{
public:
	InputHandler_Vita();
	~InputHandler_Vita() override;

	void Update() override;
	void GetDevicesAndDescriptions(std::vector<InputDeviceInfo> &vDevicesOut) override;
	InputDeviceState GetInputDeviceState(InputDevice id) override;

private:
	void HandleButton(DeviceInput di, bool bDown);
	uint32_t m_iLastButtons;
};

#define REGISTER_INPUT_HANDLER_CLASS_Vita REGISTER_INPUT_HANDLER_CLASS2(Vita, Vita)

#endif

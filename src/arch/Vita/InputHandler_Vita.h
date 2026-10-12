#ifndef INPUT_HANDLER_VITA_H
#define INPUT_HANDLER_VITA_H

#include "arch/InputHandler/InputHandler.h"

// InputHandler_Vita: polls sceCtrlPeekBufferPositive (StepMania 5.0).
// Menu navigation emits keyboard events so the default InputMapper
// bindings (arrows/enter/esc) work with zero configuration.
class InputHandler_Vita : public InputHandler
{
public:
	InputHandler_Vita();
	virtual ~InputHandler_Vita();

	void Update();
	void GetDevicesAndDescriptions(vector<InputDeviceInfo> &vDevicesOut);
	InputDeviceState GetInputDeviceState(InputDevice id);

private:
	uint32_t m_iLastButtons;
};

#define REGISTER_INPUT_HANDLER_CLASS_Vita REGISTER_INPUT_HANDLER_CLASS2(Vita, Vita)

#endif

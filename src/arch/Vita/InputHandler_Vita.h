#ifndef INPUT_HANDLER_VITA_H
#define INPUT_HANDLER_VITA_H

#include "arch/InputHandler/InputHandler.h"

// InputHandler_Vita: polls sceCtrlPeekBufferPositive and the front touch panel
// (StepMania 5.0). Menu navigation emits keyboard events so the default
// InputMapper bindings (arrows/enter/esc) work with zero configuration; touch
// lanes map to the four arrow keys while ScreenGameplay is running.
class InputHandler_Vita : public InputHandler
{
public:
	InputHandler_Vita();
	virtual ~InputHandler_Vita();

	void Update();
	void GetDevicesAndDescriptions(vector<InputDeviceInfo> &vDevicesOut);
	InputDeviceState GetInputDeviceState(InputDevice id);

private:
	bool m_bUp;
	bool m_bDown;
	bool m_bLeft;
	bool m_bRight;
	bool m_bEnter;
	bool m_bSlash;
	bool m_bEsc;
};

#define REGISTER_INPUT_HANDLER_CLASS_Vita REGISTER_INPUT_HANDLER_CLASS2(Vita, Vita)

#endif

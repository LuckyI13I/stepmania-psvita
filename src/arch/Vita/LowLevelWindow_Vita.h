#ifndef LOW_LEVEL_WINDOW_VITA_H
#define LOW_LEVEL_WINDOW_VITA_H

#include "arch/LowLevelWindow/LowLevelWindow.h"
#include "RageDisplay.h" // VideoModeParams (full type for m_ActualParams)

// LowLevelWindow_Vita: vitaGL window for PS Vita (StepMania 5.0).
// - TryVideoMode: vglInit() once; fixed 960x544 framebuffer.
// - SwapBuffers: vglSwapBuffers() with vsync flag.
// - GetProcAddress: stub (vitaGL is statically linked).
class LowLevelWindow_Vita : public LowLevelWindow
{
public:
	LowLevelWindow_Vita();
	virtual ~LowLevelWindow_Vita();

	void *GetProcAddress(RString s);
	RString TryVideoMode(const VideoModeParams &p, bool &bNewDeviceOut);
	void GetDisplayResolutions(DisplayResolutions &out) const;
	void SwapBuffers();
	void Update() { }

	const VideoModeParams &GetActualVideoModeParams() const { return m_ActualParams; }
	bool SupportsRenderToTexture() const { return true; }

private:
	bool m_bInited;
	VideoModeParams m_ActualParams;
};

#endif

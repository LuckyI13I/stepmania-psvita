#ifndef LOW_LEVEL_WINDOW_VITA_H
#define LOW_LEVEL_WINDOW_VITA_H

#include "arch/LowLevelWindow/LowLevelWindow.h"

// LowLevelWindow_Vita: EGL/vitaGL window for PS Vita.
// - TryVideoMode: vglInit() once, create EGL context via vitaGL EGL subset,
//   fixed 960x544 framebuffer. Sets bNewDeviceOut=true on first init.
// - SwapBuffers: vglSwapBuffers(GL_FALSE) + sceDisplayWaitVblankStart() if vsync.
// - GetProcAddress: eglGetProcAddress() fallback.
class LowLevelWindow_Vita : public LowLevelWindow
{
public:
	LowLevelWindow_Vita();
	~LowLevelWindow_Vita() override;

	void *GetProcAddress(RString s) override;
	RString TryVideoMode(const VideoModeParams &p, bool &bNewDeviceOut) override;
	void GetDisplaySpecs(DisplaySpecs &out) const override;
	void SwapBuffers() override;
	void Update() override;
	const ActualVideoModeParams GetActualVideoModeParams() const override;
	bool SupportsRenderToTexture() const override { return true; }

private:
	bool m_bInited;
	int m_iWidth;
	int m_iHeight;
	bool m_bVsync;
};

#endif

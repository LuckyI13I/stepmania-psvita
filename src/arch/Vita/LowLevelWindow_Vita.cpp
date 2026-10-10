#include "global.h"
#include "LowLevelWindow_Vita.h"
#include "RageLog.h"
#include "RageDisplay.h" // VideoModeParams
#include "DisplaySpec.h"

#ifdef VITA
#include <psp2/display.h>
#include <psp2/gxm.h>
#if __has_include(<vitaGL.h>)
#include <vitaGL.h>
#endif
#endif

LowLevelWindow_Vita::LowLevelWindow_Vita()
	: m_bInited(false), m_iWidth(960), m_iHeight(544), m_bVsync(true)
{
}

LowLevelWindow_Vita::~LowLevelWindow_Vita()
{
#ifdef VITA
	// vglEnd() on shutdown; guarded so null-swap in tests won't crash.
	// vglEnd();
#endif
}

void *LowLevelWindow_Vita::GetProcAddress(RString s)
{
#ifdef VITA
	// vitaGL exposes eglGetProcAddress; dlsym fallback for core symbols.
	// void *p = eglGetProcAddress(s.c_str());
	// if (p) return p;
#endif
	return nullptr;
}

RString LowLevelWindow_Vita::TryVideoMode(const VideoModeParams &p, bool &bNewDeviceOut)
{
#ifdef VITA
	if (!m_bInited) {
		// First call: bring up vitaGL (creates the GXM context, EGL state
		// and the 960x544 framebuffer internally). 16MB VRAM pool; Vita has
		// 128MB VRAM total, SM5's theme textures need room to breathe.
		// Must run on the main thread; TryVideoMode always does.
		vglInit(0x1000000);
		vglWaitVblankStart(GL_TRUE);
		m_bInited = true;
		bNewDeviceOut = true;
		LOG->Info("LowLevelWindow_Vita: vitaGL initialized (960x544)");
	} else {
		bNewDeviceOut = false;
	}
	m_iWidth = 960;
	m_iHeight = 544;
	m_bVsync = p.vsync;
	return RString();
#else
	bNewDeviceOut = true;
	return RString();
#endif
}

void LowLevelWindow_Vita::GetDisplaySpecs(DisplaySpecs &out) const
{
	DisplayMode mode{ 960, 544, 60.0 };
	out.insert(DisplaySpec("Vita LCD", "PS Vita LCD", mode));
}

void LowLevelWindow_Vita::SwapBuffers()
{
#ifdef VITA
	vglSwapBuffers(m_bVsync ? GL_TRUE : GL_FALSE);
#endif
}

void LowLevelWindow_Vita::Update()
{
	// Poll sceCommonDialog / IME updates here if needed.
}

const ActualVideoModeParams LowLevelWindow_Vita::GetActualVideoModeParams() const
{
	ActualVideoModeParams avmp;
	avmp.width = m_iWidth;
	avmp.height = m_iHeight;
	avmp.bpp = 32;
	avmp.rate = 60;
	avmp.windowWidth = m_iWidth;
	avmp.windowHeight = m_iHeight;
	return avmp;
}

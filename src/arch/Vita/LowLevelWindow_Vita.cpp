#include "global.h"
#include "LowLevelWindow_Vita.h"
#include "RageLog.h"
#include "RageDisplay.h" // VideoModeParams
#include "DisplayResolutions.h"

#ifdef VITA
#include <psp2/display.h>
#include <psp2/gxm.h>
#if __has_include(<vitaGL.h>)
#include <vitaGL.h>
#endif
#include "VitaBootSplash.h"
#endif

LowLevelWindow_Vita::LowLevelWindow_Vita()
	: m_bInited(false)
{
	m_ActualParams.width = 960;
	m_ActualParams.height = 544;
	m_ActualParams.bpp = 32;
	m_ActualParams.rate = 60;
	m_ActualParams.vsync = true;
}

LowLevelWindow_Vita::~LowLevelWindow_Vita()
{
}

void *LowLevelWindow_Vita::GetProcAddress(RString s)
{
	(void)s;
	return NULL;
}

RString LowLevelWindow_Vita::TryVideoMode(const VideoModeParams &p, bool &bNewDeviceOut)
{
#ifdef VITA
	if (!m_bInited) {
		/* Hand the display over from the boot splash (raw framebuffer) to
		 * vitaGL/GXM before initializing the GL context. Must run on the
		 * main thread. */
		VitaBootSplash::Shutdown();
		vglInit(0x1000000);
		vglWaitVblankStart(GL_TRUE);
		m_bInited = true;
		bNewDeviceOut = true;
		LOG->Info("LowLevelWindow_Vita: vitaGL initialized (960x544)");
	} else {
		bNewDeviceOut = false;
	}
	m_ActualParams.width = 960;
	m_ActualParams.height = 544;
	m_ActualParams.bpp = 32;
	m_ActualParams.rate = 60;
	m_ActualParams.vsync = p.vsync;
	m_ActualParams.windowed = p.windowed;
	return RString();
#else
	bNewDeviceOut = true;
	return RString();
#endif
}

void LowLevelWindow_Vita::GetDisplayResolutions(DisplayResolutions &out) const
{
	DisplayResolution res;
	res.iWidth = 960;
	res.iHeight = 544;
	res.bStretched = false;
	out.insert(res);
}

void LowLevelWindow_Vita::SwapBuffers()
{
#ifdef VITA
	vglSwapBuffers(m_ActualParams.vsync ? GL_TRUE : GL_FALSE);
#endif
}

#ifndef ARCH_HOOKS_VITA_H
#define ARCH_HOOKS_VITA_H

#include "arch/ArchHooks/ArchHooks.h"

// ArchHooks_Vita: paths, clocks and URLs for PS Vita (StepMania 5.0).
// - Read-only payload: app0: (VPK content: Themes, Songs, NoteSkins, ...)
// - User data:         ux0:data/stepmania-5.0/ (profiles, cache, logs)
// - Clock:             sceKernelGetProcessTimeWide() (microseconds)
// - URL:               stub (returns false for now)
class ArchHooks_Vita : public ArchHooks
{
public:
	void Init();
	RString GetArchName() const { return "Vita"; }
	void DumpDebugInfo();

	void SetTime(tm newtime);
	float GetDisplayAspectRatio() { return 960.0f / 544.0f; }

	bool GoToURL(RString sUrl);
	RString GetClipboard() { return RString(); }
};

#ifdef ARCH_HOOKS
#error "More than one ArchHooks selected!"
#endif
#define ARCH_HOOKS ArchHooks_Vita

#ifdef VITA
// Crash-safe boot tracer: appends a line to ux0:data/stepmania-5.0/vita-boot.log
// with plain sceIo (no RageFile/LOG dependency). Used to bisect startup crashes.
void VitaBootMark(const char *stage);
#endif

#endif

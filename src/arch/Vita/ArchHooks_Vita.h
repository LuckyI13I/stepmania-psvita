#ifndef ARCH_HOOKS_VITA_H
#define ARCH_HOOKS_VITA_H

#include "arch/ArchHooks/ArchHooks.h"

// ArchHooks_Vita: paths, clocks and URLs for PS Vita.
// - Read-only payload: app0: (VPK content: Themes, Songs, NoteSkins, ...)
// - User data:         ux0:data/stepmania-5.1/ (profiles, cache, logs)
// - Clock:             sceKernelGetProcessTimeWide() (microseconds)
// - URL:               stub (returns false for now)
class ArchHooks_Vita : public ArchHooks
{
public:
	void Init() override;
	RString GetArchName() const override { return "Vita"; }
	void DumpDebugInfo() override;

	void SetTime(tm newtime) override;
	float GetDisplayAspectRatio() override { return 960.0f / 544.0f; }

	bool GoToURL(RString sUrl) override;
	RString GetClipboard() override { return RString(); }
};

#ifdef ARCH_HOOKS
#error "More than one ArchHooks selected!"
#endif
#define ARCH_HOOKS ArchHooks_Vita

#ifdef VITA
// Crash-safe boot tracer: appends a line to ux0:data/stepmania-5.1/vita-boot.log
// with plain sceIo (no RageFile/LOG dependency). Used to bisect startup crashes.
void VitaBootMark(const char *stage);
#endif

#endif

#include "global.h"
#include "RageSoundDriver_Vita.h"
#include "RageLog.h"
#include "RageUtil.h"

#ifdef VITA
#include <psp2/audioout.h>
#endif

REGISTER_SOUND_DRIVER_CLASS(Vita);

RageSoundDriver_Vita::RageSoundDriver_Vita()
	: m_iAudioPort(-1), m_iFrameCounter(0)
{
}

RageSoundDriver_Vita::~RageSoundDriver_Vita()
{
#ifdef VITA
	// if (m_iAudioPort >= 0) sceAudioOutReleasePort(m_iAudioPort);
#endif
}

RString RageSoundDriver_Vita::Init()
{
#ifdef VITA
	// Draft port open:
	// m_iAudioPort = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM,
	//     1024, 44100, SCE_AUDIO_OUT_MODE_STEREO);
	// if (m_iAudioPort < 0) return "sceAudioOutOpenPort failed";
	//
	// StartDecodeThread() spins the mixer; actual sceAudioOutOutput() push
	// happens in a dedicated audio thread (see RageSoundDriver_Generic_Software).
	LOG->Info("RageSoundDriver_Vita: sceAudioOut stub init (44100 stereo)");
	StartDecodeThread();
	return RString();
#else
	// Host-side (non-Vita) build: act as Null so configure/test still works.
	StartDecodeThread();
	return RString();
#endif
}

std::int64_t RageSoundDriver_Vita::GetPosition() const
{
	// TODO: query sceAudioOutGetRestSample(m_iAudioPort) for exact HW pos.
	// Skeleton returns monotonic counter advanced by Mix().
	return m_iFrameCounter;
}

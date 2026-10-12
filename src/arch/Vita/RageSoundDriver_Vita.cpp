#include "global.h"
#include "RageSoundDriver_Vita.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "PrefsManager.h"

#ifdef VITA
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#else
#include <unistd.h>
#endif

REGISTER_SOUND_DRIVER_CLASS(Vita);

RageSoundDriver_Vita::RageSoundDriver_Vita()
	: m_iAudioPort(-1), m_bShutdownAudio(false), m_iPlayedFrames(0)
{
	m_iSampleRate = PREFSMAN->m_iSoundPreferredSampleRate;
	if (m_iSampleRate == 0)
		m_iSampleRate = 44100;
}

RageSoundDriver_Vita::~RageSoundDriver_Vita()
{
	m_bShutdownAudio = true;
	if (m_AudioThread.IsCreated())
		m_AudioThread.Wait();
#ifdef VITA
	if (m_iAudioPort >= 0)
	{
		sceAudioOutReleasePort(m_iAudioPort);
		m_iAudioPort = -1;
	}
#endif
}

RString RageSoundDriver_Vita::Init()
{
#ifdef VITA
	m_iAudioPort = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM,
		1024, m_iSampleRate, SCE_AUDIO_OUT_MODE_STEREO);
	if (m_iAudioPort < 0)
	{
		LOG->Warn("RageSoundDriver_Vita: sceAudioOutOpenPort failed (%d); silent mode",
			m_iAudioPort);
		m_iAudioPort = -1;
	}
	else
	{
		LOG->Info("RageSoundDriver_Vita: sceAudioOut BGM port %d (%d Hz stereo)",
			m_iAudioPort, m_iSampleRate);
	}
#else
	LOG->Info("RageSoundDriver_Vita: host stub (silent)");
#endif
	StartDecodeThread();
	m_AudioThread.SetName("VitaAudioOut");
	m_AudioThread.Create(AudioThread_start, this);
	return RString();
}

int RageSoundDriver_Vita::AudioThread_start(void *p)
{
	static_cast<RageSoundDriver_Vita *>(p)->AudioThread();
	return 0;
}

void RageSoundDriver_Vita::AudioThread()
{
	RageThreadRegister thread("VitaAudioOut");
	while (!m_bShutdownAudio)
	{
		int16_t buf[kFramesPerChunk * kChannels];
		int64_t pos = m_iPlayedFrames.load();
		this->Mix(buf, kFramesPerChunk, pos, pos);
#ifdef VITA
		int ret = 0;
		if (m_iAudioPort >= 0)
			ret = sceAudioOutOutput(m_iAudioPort, buf);
		(void)ret;
#else
		// Host stub: pace at realtime instead of spinning.
		usleep(5000);
#endif
		m_iPlayedFrames.fetch_add(kFramesPerChunk);
	}
}

void RageSoundDriver_Vita::Update()
{
	// Mixing/output happens on the audio thread; nothing to do here except
	// the base bookkeeping. Never block the frame loop on audio.
	RageSoundDriver::Update();
}

int64_t RageSoundDriver_Vita::GetPosition() const
{
	return m_iPlayedFrames.load();
}

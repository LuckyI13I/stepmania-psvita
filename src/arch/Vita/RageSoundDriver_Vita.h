#ifndef RAGE_SOUND_DRIVER_VITA_H
#define RAGE_SOUND_DRIVER_VITA_H

#include "arch/Sound/RageSoundDriver.h"

// RageSoundDriver_Vita: minimal audio output via sceAudioOut.
// - 44100 Hz stereo 16-bit, 2 buffers round-robin.
// - GetPosition() from sceAudioOutGetRestSample() or frame counter.
// - If sceAudioOut unavailable at configure time, falls back to Null (no crash).
class RageSoundDriver_Vita : public RageSoundDriver
{
public:
	RageSoundDriver_Vita();
	~RageSoundDriver_Vita() override;

	RString Init() override;
	std::int64_t GetPosition() const override;
	float GetPlayLatency() const override { return 0.05f; }
	int GetSampleRate() const override { return 44100; }

private:
	int m_iAudioPort;
	mutable std::int64_t m_iFrameCounter;
};

#endif

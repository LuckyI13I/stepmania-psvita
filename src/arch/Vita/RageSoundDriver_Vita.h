#ifndef RAGE_SOUND_DRIVER_VITA_H
#define RAGE_SOUND_DRIVER_VITA_H

#include "arch/Sound/RageSoundDriver.h"
#include "RageThreads.h"

#include <atomic>

// RageSoundDriver_Vita: audio output via sceAudioOut (BGM port).
//
// A dedicated output thread Mix()es fixed chunks and pushes them with the
// blocking sceAudioOutOutput call, so the main thread never stalls on
// audio. The played-frame counter IS the hardware position.
class RageSoundDriver_Vita : public RageSoundDriver
{
public:
	RageSoundDriver_Vita();
	virtual ~RageSoundDriver_Vita();

	virtual RString Init();
	virtual void Update();
	virtual int64_t GetPosition() const;
	virtual float GetPlayLatency() const { return 0.05f; }
	virtual int GetSampleRate() const { return m_iSampleRate; }

private:
	static const int kChannels = 2;
	// Must equal the port len passed to sceAudioOutOpenPort: Output()
	// requires exactly len samples per call, else SCE_AUDIO_OUT errors.
	static const int kFramesPerChunk = 1024;

	static int AudioThread_start(void *p);
	void AudioThread();

	int m_iAudioPort;
	int m_iSampleRate;
	RageThread m_AudioThread;
	bool m_bShutdownAudio;
	std::atomic<int64_t> m_iPlayedFrames;
};

#endif

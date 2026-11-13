#pragma once
#include <lhdc_pcm.h>
class LhdcPcmBridge {
public:
    LhdcPcmBridge();
    void Configure(const WAVEFORMATEXTENSIBLE* format);
    void SetRunning(bool running);
    void SetProtected(bool protected_content);
    void Push(const BYTE* data,ULONG bytes);
    NTSTATUS Property(PPCPROPERTY_REQUEST request);
    // The delay the Bluetooth path adds on top of the render buffer. The
    // service publishes it and the miniport reports it to the audio engine, so
    // players can delay the video by what the earbud actually plays.
    void SetLatencyMs(ULONG milliseconds);
    ULONG LatencyFrames(ULONG sample_rate) const;
private:
    // Retain 200 ms of stereo PCM even at 192 kHz / 24 bit.
    static constexpr ULONG capacity=192000*2*3/5;
    KSPIN_LOCK lock_{};
    LHDC_PCM_STATE state_{};
    ULONG read_{},used_{},limit_{};
    bool protected_{};
    volatile LONG latency_ms_{};
    BYTE data_[capacity]{};
};
NTSTATUS PropertyHandler_LhdcPcm(PPCPROPERTY_REQUEST request);

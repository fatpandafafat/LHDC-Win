#include "definitions.h"
#include "endpoints.h"
#include "minwavert.h"
#include "pcm_bridge.h"
LhdcPcmBridge::LhdcPcmBridge() {
    KeInitializeSpinLock(&lock_);
    state_.Size=sizeof(state_); state_.Version=LHDC_PCM_ABI_VERSION;
}
void LhdcPcmBridge::Configure(const WAVEFORMATEXTENSIBLE* format) {
    KIRQL previous;
    KeAcquireSpinLock(&lock_,&previous);
    state_.SampleRate=format->Format.nSamplesPerSec;
    state_.Bits=format->Format.wBitsPerSample;
    state_.Channels=format->Format.nChannels;
    state_.BlockAlign=format->Format.nBlockAlign;
    state_.FloatingPoint=(format->Format.wFormatTag==WAVE_FORMAT_IEEE_FLOAT ||
        (format->Format.wFormatTag==WAVE_FORMAT_EXTENSIBLE && IsEqualGUIDAligned(format->SubFormat,KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)));
    limit_=min(capacity,format->Format.nAvgBytesPerSec/5);
    limit_-=limit_%state_.BlockAlign;
    read_=used_=0;
    state_.ProducedBytes=state_.DroppedBytes=0;
    ++state_.Epoch;
    KeReleaseSpinLock(&lock_,previous);
}
void LhdcPcmBridge::SetRunning(bool running) {
    KIRQL previous;
    KeAcquireSpinLock(&lock_,&previous);
    state_.Running=running;
    if (running) {
        read_=used_=0; state_.ProducedBytes=state_.DroppedBytes=0; ++state_.Epoch;
    }
    KeReleaseSpinLock(&lock_,previous);
}
void LhdcPcmBridge::Push(const BYTE* data,ULONG bytes) {
    KIRQL previous;
    KeAcquireSpinLock(&lock_,&previous);
    if (state_.Running && !protected_ && limit_ && bytes) {
        state_.ProducedBytes+=bytes;
        if (bytes>limit_) {
            state_.DroppedBytes+=used_+bytes-limit_;
            data+=bytes-limit_; bytes=limit_; read_=used_=0;
        }
        const ULONG discard=(used_+bytes>limit_)?used_+bytes-limit_:0;
        read_=(read_+discard)%limit_; used_-=discard; state_.DroppedBytes+=discard;
        const ULONG offset=(read_+used_)%limit_;
        const ULONG first=min(bytes,limit_-offset);
        RtlCopyMemory(data_+offset,data,first);
        RtlCopyMemory(data_,data+first,bytes-first);
        used_+=bytes;
    }
    KeReleaseSpinLock(&lock_,previous);
}
void LhdcPcmBridge::SetProtected(bool protected_content) {
    KIRQL previous;
    KeAcquireSpinLock(&lock_,&previous);
    protected_=protected_content;
    if (protected_) read_=used_=0;
    KeReleaseSpinLock(&lock_,previous);
}
void LhdcPcmBridge::SetLatencyMs(ULONG milliseconds) {
    InterlockedExchange(&latency_ms_,static_cast<LONG>(milliseconds));
}
ULONG LhdcPcmBridge::LatencyFrames(ULONG sample_rate) const {
    const LONG milliseconds=InterlockedCompareExchange(&const_cast<volatile LONG&>(latency_ms_),0,0);
    if (milliseconds<=0) return 0;
    const auto rate=sample_rate?sample_rate:48000;
    return static_cast<ULONG>((static_cast<ULONGLONG>(milliseconds)*rate+999)/1000);
}
NTSTATUS LhdcPcmBridge::Property(PPCPROPERTY_REQUEST request) {
    const ULONG id=request->PropertyItem->Id;
    if (id==LHDC_PCM_LATENCY) {
        if (!(request->Verb&KSPROPERTY_TYPE_SET) || request->ValueSize!=sizeof(ULONG) || !request->Value)
            return STATUS_INVALID_PARAMETER;
        SetLatencyMs(*static_cast<ULONG*>(request->Value));
        return STATUS_SUCCESS;
    }
    if (id==LHDC_PCM_RESET) {
        if (!(request->Verb&KSPROPERTY_TYPE_SET) || request->ValueSize!=sizeof(ULONG) || !request->Value || *static_cast<ULONG*>(request->Value)!=LHDC_PCM_ABI_VERSION)
            return STATUS_INVALID_PARAMETER;
        KIRQL previous;
        KeAcquireSpinLock(&lock_,&previous);
        read_=used_=0; state_.ProducedBytes=state_.DroppedBytes=0;
        KeReleaseSpinLock(&lock_,previous);
        return STATUS_SUCCESS;
    }
    if (!(request->Verb&KSPROPERTY_TYPE_GET)) return STATUS_INVALID_DEVICE_REQUEST;
    if (request->ValueSize<sizeof(state_)) { request->ValueSize=sizeof(state_); return STATUS_BUFFER_TOO_SMALL; }
    if (!request->Value) return STATUS_INVALID_PARAMETER;
    const ULONG capacity_bytes=min(request->ValueSize-sizeof(state_),16384u);
    BYTE* payload=nullptr;
    if (id==LHDC_PCM_READ && capacity_bytes) {
        payload=static_cast<BYTE*>(ExAllocatePool2(POOL_FLAG_NON_PAGED,capacity_bytes,'PchL'));
        if (!payload) return STATUS_INSUFFICIENT_RESOURCES;
    }
    LHDC_PCM_STATE snapshot{};
    KIRQL previous;
    KeAcquireSpinLock(&lock_,&previous);
    if (protected_) {
        KeReleaseSpinLock(&lock_,previous);
        if (payload) ExFreePoolWithTag(payload,'PchL');
        return STATUS_ACCESS_DENIED;
    }
    ULONG bytes=0;
    if (payload && state_.BlockAlign && limit_) {
        bytes=min(used_,capacity_bytes);
        bytes-=bytes%state_.BlockAlign;
        const ULONG first=min(bytes,limit_-read_);
        RtlCopyMemory(payload,data_+read_,first);
        RtlCopyMemory(payload+first,data_,bytes-first);
        read_=(read_+bytes)%limit_; used_-=bytes;
    }
    state_.BufferedBytes=used_; state_.PayloadBytes=bytes;
    snapshot=state_;
    KeReleaseSpinLock(&lock_,previous);
    // KS property buffers can be pageable. Copy to them after lowering IRQL.
    RtlCopyMemory(request->Value,&snapshot,sizeof(snapshot));
    if (payload) {
        RtlCopyMemory(static_cast<BYTE*>(request->Value)+sizeof(snapshot),payload,bytes);
        ExFreePoolWithTag(payload,'PchL');
    }
    request->ValueSize=sizeof(state_)+bytes;
    return STATUS_SUCCESS;
}
NTSTATUS PropertyHandler_LhdcPcm(PPCPROPERTY_REQUEST request) {
    auto* miniport=reinterpret_cast<CMiniportWaveRT*>(request->MajorTarget);
    return miniport->m_LhdcPcm.Property(request);
}

#include "bitrate_control.hpp"
#include <algorithm>
namespace lhdc {
namespace {
// The ladder never goes below the rate the protocol guarantees, and it starts
// at the rate the user asked for.
constexpr std::uint32_t floor_kbps=400;
constexpr std::uint64_t sample_interval_ms=100,stable_interval_ms=15000,recovery_interval_ms=30000;
// A level that just failed is not probed again before this delay. It doubles
// with every failure, so a level that cannot be carried is left alone.
constexpr std::uint64_t initial_retry_ms=15000,max_retry_ms=15*60000;
constexpr double stable_queue_ms=80,pressure_queue_ms=90,urgent_queue_ms=110;
constexpr double pressure_pending_ms=40,urgent_pending_ms=100;
constexpr unsigned pressure_sample_count=3;
}
BitrateControl::BitrateControl(Profile ceiling,bool adaptive,std::uint64_t now_ms)
    :last_sample_(now_ms),stable_since_(now_ms),next_raise_(now_ms+stable_interval_ms),adaptive_(adaptive) {
    validate_profile(ceiling);
    const auto floor=std::min(floor_kbps,ceiling.kbps);
    // The ladder holds every rate the encoder can produce, so the steps
    // between two rates exist: a ceiling of 800 has 600 and 700 below it and a
    // pressure event moves one step down instead of falling to the bottom.
    for (const auto kbps:selectable_bitrates(ceiling.sample_rate))
        if (kbps>=floor && kbps<=ceiling.kbps) levels_.push_back(kbps);
    // A ceiling that the encoder table cannot express is still the top step.
    if (levels_.empty()||levels_.back()!=ceiling.kbps) levels_.push_back(ceiling.kbps);
    retry_after_.resize(levels_.size());
    retry_delay_.resize(levels_.size(),initial_retry_ms);
    // The rate the user chose is the level the stream runs at. The ladder only
    // moves down from there when the link cannot carry it, and back up when it
    // can. A rate that was chosen while the stream was running starts there
    // as well, instead of at the conservative level.
    index_=levels_.size()-1;
}
std::uint32_t BitrateControl::observe(std::uint64_t now_ms,double queue_ms,std::uint64_t dropped_bytes,double pending_ms) {
    if (!adaptive_) return bitrate();
    max_pending_ms_=std::max(max_pending_ms_,pending_ms);
    if (now_ms-last_sample_<sample_interval_ms) return bitrate();
    last_sample_=now_ms;
    pending_ms=max_pending_ms_;
    max_pending_ms_=0;
    const bool dropped=dropped_bytes>last_dropped_;
    last_dropped_=dropped_bytes;
    // Recovery can retain 60ms of PCM. React before the bounded queues overflow.
    const bool sending_backlog=pending_ms>=pressure_pending_ms && queue_ms>stable_queue_ms;
    pressure_samples_=queue_ms>=pressure_queue_ms || sending_backlog ? pressure_samples_+1 : 0;
    const bool urgent=dropped || queue_ms>=urgent_queue_ms || (pending_ms>=urgent_pending_ms && queue_ms>stable_queue_ms);
    if (urgent || pressure_samples_>=pressure_sample_count) {
        if (index_>0) {
            // A failed rate should not be probed every fifteen seconds. Keep
            // lower supported rates available while backing off that level.
            retry_after_[index_]=now_ms+retry_delay_[index_];
            retry_delay_[index_]=std::min(max_retry_ms,retry_delay_[index_]*2);
        }
        // The ladder has a step between every two rates now, so the stream
        // gives up one step at a time. An urgent event gives up two: the queue
        // is at its limit then, and one step would not free it in time.
        if (index_>0) --index_;
        if (urgent && index_>0) --index_;
        pressure_samples_=0;
        stable_since_=now_ms;
        next_raise_=now_ms+recovery_interval_ms;
    } else if (queue_ms>stable_queue_ms) {
        stable_since_=now_ms;
    } else if (now_ms-stable_since_>=stable_interval_ms && now_ms>=next_raise_ && index_+1<levels_.size() && now_ms>=retry_after_[index_+1]) {
        ++index_;
        stable_since_=now_ms;
        next_raise_=now_ms+stable_interval_ms;
    }
    return bitrate();
}
}

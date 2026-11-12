#pragma once
#include "windows_audio.hpp"
#include <atomic>
#include <memory>
#include <thread>
namespace lhdc {
class HfpActivity {
public:
    HfpActivity();
    ~HfpActivity();
    bool active() const;
    ComPtr<IMMDevice> capture_device() const;
private:
    bool matches(IMMDevice* device) const;
    GUID container_{};
    struct Sessions;
    std::unique_ptr<Sessions> sessions_;
};
// Watches the call state on its own thread.
//
// Enumerating the audio endpoints is a COM operation that takes milliseconds and
// can stall much longer when the system is busy. Doing that on the thread that
// submits the media packets holds the Bluetooth send window empty, which shows up
// as an audible gap and as growing send latency. The watcher therefore polls on
// its own thread and the media thread only reads the published flag.
class HfpWatcher {
public:
    HfpWatcher();
    ~HfpWatcher();
    // The call state as of the last poll. Never touches COM.
    bool active() const { return active_.load(std::memory_order_relaxed); }
    // How long the last poll took, in microseconds.
    std::int64_t last_poll_us() const { return last_poll_us_.load(std::memory_order_relaxed); }
    // The number of polls that failed; the watcher keeps the last known state.
    unsigned long long poll_failures() const { return failures_.load(std::memory_order_relaxed); }
private:
    void watch();
    std::atomic<bool> active_{false};
    std::atomic<bool> stop_{false};
    std::atomic<std::int64_t> last_poll_us_{0};
    std::atomic<unsigned long long> failures_{0};
    std::thread worker_;
};
void inspect_hfp_activity();
}

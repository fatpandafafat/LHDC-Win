#pragma once
#include <cstdint>
#include <vector>
namespace lhdc {
struct Profile {
    std::uint32_t sample_rate = 48000;
    std::uint32_t bits = 16;
    std::uint32_t kbps = 400;
};
// The rates the pinned encoder holds in its own table. The adaptive ladder
// is built from this list, so it stays exactly as it was.
std::vector<std::uint32_t> bitrates(std::uint32_t sample_rate);
// Rates outside that table are produced by targeting a bitrate directly.
// They are offered next to the table entries but never enter the ladder.
bool manual_bitrate(std::uint32_t kbps);
std::vector<std::uint32_t> selectable_bitrates(std::uint32_t sample_rate);
std::uint32_t quality_index(const Profile& profile);
// Accepts a table entry and a targeted rate, and rejects everything else.
void validate_profile(const Profile& profile);
std::uint32_t encoded_frame_bytes(const Profile& profile);
Profile follow_pcm(const Profile& quality,unsigned sample_rate,unsigned bits,bool floating_point);
}

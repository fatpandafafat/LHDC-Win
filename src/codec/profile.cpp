#include "profile.hpp"
#include <algorithm>
#include <stdexcept>
namespace lhdc {
namespace {
// Rates the pinned encoder cannot express through its own table. They are
// produced by targeting a bitrate directly. The adaptive ladder keeps the
// table, so these are manual choices only.
constexpr std::uint32_t manual_rates[] = {600,700,800};
}
std::vector<std::uint32_t> bitrates(std::uint32_t rate) {
    if (rate!=44100 && rate!=48000 && rate!=96000 && rate!=192000)
        throw std::invalid_argument("Unsupported LHDC V5 sample rate");
    // Exact entries in the pinned encoder's table; 44.1k uses 240/480.
    return {64,160,192,rate==44100?240u:256u,320,400,rate==44100?480u:500u,900,1000};
}
bool manual_bitrate(std::uint32_t kbps) {
    for (const auto rate:manual_rates) if (rate==kbps) return true;
    return false;
}
std::vector<std::uint32_t> selectable_bitrates(std::uint32_t rate) {
    auto choices=bitrates(rate);
    for (const auto kbps:manual_rates) {
        const auto position=std::lower_bound(choices.begin(),choices.end(),kbps);
        if (position==choices.end()||*position!=kbps) choices.insert(position,kbps);
    }
    return choices;
}
std::uint32_t quality_index(const Profile& profile) {
    if (profile.bits!=16 && profile.bits!=24) throw std::invalid_argument("PCM must be 16 or 24 bit");
    const auto choices=bitrates(profile.sample_rate);
    const auto entry=std::find(choices.begin(),choices.end(),profile.kbps);
    if (entry==choices.end()) throw std::invalid_argument("Bitrate is not an exact encoder table entry for this sample rate");
    return static_cast<std::uint32_t>(entry-choices.begin());
}
void validate_profile(const Profile& profile) {
    if (profile.bits!=16 && profile.bits!=24) throw std::invalid_argument("PCM must be 16 or 24 bit");
    const auto choices=selectable_bitrates(profile.sample_rate);
    if (std::find(choices.begin(),choices.end(),profile.kbps)==choices.end())
        throw std::invalid_argument("Bitrate is neither an encoder table entry nor a supported target rate for this sample rate");
}
Profile follow_pcm(const Profile& quality,unsigned sample_rate,unsigned bits,bool floating_point) {
    if (floating_point && bits!=32) throw std::invalid_argument("Floating PCM must be 32 bit");
    if (manual_bitrate(quality.kbps)) {
        // A targeted rate has no table entry to translate. The encoder derives
        // the frame length from the sample rate on its own.
        Profile result{sample_rate,floating_point?24u:bits,quality.kbps};
        validate_profile(result);
        return result;
    }
    const auto index=quality_index(quality);
    Profile result{sample_rate,floating_point?24u:bits,bitrates(sample_rate).at(index)};
    quality_index(result);
    return result;
}
std::uint32_t encoded_frame_bytes(const Profile& profile) {
    validate_profile(profile);
    // Pinned encoder: two channel payloads plus the two-byte frame header.
    // 44.1 kHz still uses 240 samples per block, so its frame is longer than 5 ms.
    const auto denominator=profile.sample_rate==44100?147000u:160000u;
    return 2+2*(profile.kbps*1000u*50u/denominator);
}
}

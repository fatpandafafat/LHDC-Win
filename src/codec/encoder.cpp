#include "encoder.hpp"
#include "lhdcv5_bps.h"
#include <algorithm>
#include <stdexcept>
#include <string>
namespace lhdc {
static void check(std::int32_t status, const char* operation) {
    if (status != LHDC_FRET_SUCCESS)
        throw std::runtime_error(std::string(operation)+" failed: "+std::to_string(status));
}
namespace {
// The pinned encoder is addressed through its own index table. A rate that
// the table does not hold is initialised with the entry the table maps it
// onto and is retargeted to the exact rate afterwards.
std::uint32_t table_index(const Profile& profile) {
    const auto choices=bitrates(profile.sample_rate);
    const auto entry=std::find(choices.begin(),choices.end(),profile.kbps);
    if (entry!=choices.end()) return static_cast<std::uint32_t>(entry-choices.begin());
    const auto mapped=std::lower_bound(choices.begin(),choices.end(),profile.kbps);
    if (mapped==choices.end()) throw std::invalid_argument("Bitrate exceeds the encoder's range");
    return static_cast<std::uint32_t>(mapped-choices.begin());
}
}
Encoder::Encoder(std::uint32_t payload_mtu) : Encoder(Profile{},payload_mtu) {}
Encoder::Encoder(Profile profile,std::uint32_t payload_mtu) : mtu_(payload_mtu),profile_(profile) {
    validate_profile(profile);
    if(encoded_frame_bytes(profile)>payload_mtu) throw std::invalid_argument("A single LHDC frame exceeds the negotiated payload MTU; lower the bitrate");
    handle_.reset(lhdcv5_enc_new(LHDC_VERSION_1));
    if (!handle_) throw std::runtime_error("Encoder allocation failed");
    check(lhdcv5_enc_init_encoder(handle_.get(),profile.sample_rate,profile.bits,table_index(profile),mtu_,20),"encoder init");
    if (manual_bitrate(profile.kbps))
        check(lhdcv5_enc_set_target_bps(handle_.get(),profile.kbps*1000u),"encoder bitrate target");
    check(lhdcv5_enc_get_block_size(handle_.get(), &block_samples_),"block size");
}
EncodedPacket Encoder::encode(std::span<const std::uint8_t> pcm) {
    if (pcm.size() != block_bytes()) throw std::invalid_argument("PCM must contain exactly one stereo encoder block");
    EncodedPacket packet{std::vector<std::uint8_t>(mtu_),0};
    std::uint32_t written=0;
    check(lhdcv5_enc_encode(handle_.get(),pcm.data(),pcm.size(),packet.payload.data(),packet.payload.size(),&written,&packet.frames),"encode");
    if (written>mtu_ || ((written==0)!=(packet.frames==0))) throw std::runtime_error("Invalid encoder output boundary");
    packet.payload.resize(written);
    ++buffered_frames_;
    if (packet.frames>buffered_frames_) throw std::runtime_error("Encoder emitted more frames than supplied");
    buffered_frames_-=packet.frames;
    return packet;
}
void Encoder::set_bitrate(std::uint32_t kbps) {
    if (buffered_frames_) throw std::logic_error("Bitrate changes require a complete packet boundary");
    const Profile selected{profile_.sample_rate,profile_.bits,kbps};
    validate_profile(selected);
    if (encoded_frame_bytes(selected)>mtu_) throw std::invalid_argument("New bitrate exceeds the negotiated payload MTU");
    if (manual_bitrate(kbps)) {
        check(lhdcv5_enc_set_target_bps(handle_.get(),kbps*1000u),"encoder bitrate update");
    } else {
        check(lhdcv5_enc_set_bitrate_index(handle_.get(),quality_index(selected),true),"encoder bitrate update");
        // The library skips an update when the table index does not change.
        // A rate that was targeted directly can land on the same index, so
        // confirm that the encoder took the new rate.
        std::uint32_t actual=0;
        check(lhdcv5_enc_get_last_bitrate(handle_.get(),&actual),"encoder bitrate readback");
        if (actual!=kbps) check(lhdcv5_enc_set_target_bps(handle_.get(),kbps*1000u),"encoder bitrate update");
    }
    profile_=selected;
}
}

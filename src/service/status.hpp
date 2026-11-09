#pragma once
namespace lhdc {
bool adaptive_bitrate_enabled();
void publish_active_bitrate(unsigned kbps);
void publish_active_format(unsigned sample_rate, unsigned bits);
void inspect_audio_service();
}

#pragma once
namespace lhdc {
bool adaptive_bitrate_enabled();
void publish_active_bitrate(unsigned kbps);
void publish_active_format(unsigned sample_rate, unsigned bits);
void inspect_audio_service();
// The live bitrate the service publishes, read from the registry alone. It
// touches neither the Bluetooth stack nor the device tree, so a panel that
// polls it does not query the radio while the earbuds are playing.
void inspect_active_bitrate();
}

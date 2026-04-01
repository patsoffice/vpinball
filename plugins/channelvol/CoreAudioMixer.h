// license:GPLv3+

#pragma once

#ifdef __APPLE__

#include "MixerBackend.h"
#include <AudioToolbox/AudioToolbox.h>

namespace ChannelVol {

class CoreAudioMixer : public MixerBackend
{
public:
   CoreAudioMixer() = default;
   ~CoreAudioMixer() override = default;

   bool GetChannelVolume(const std::string& deviceName, int channel, float& volume) override;
   bool SetChannelVolume(const std::string& deviceName, int channel, float volume) override;
   int GetChannelCount(const std::string& deviceName) override;
   bool SaveVolumes(const std::string& deviceName, std::vector<float>& outVolumes) override;
   bool RestoreVolumes(const std::string& deviceName, const std::vector<float>& volumes) override;

private:
   // Find the CoreAudio device ID matching an SDL device name
   AudioDeviceID FindDeviceByName(const std::string& deviceName);

   // Check if per-channel volume is supported, or only master volume
   bool HasPerChannelVolume(AudioDeviceID deviceId, int numChannels);
};

} // namespace ChannelVol

#endif // __APPLE__

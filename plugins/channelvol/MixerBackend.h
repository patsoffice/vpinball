// license:GPLv3+

#pragma once

#include <string>
#include <vector>

namespace ChannelVol {

// Abstract interface for OS-level audio mixer control.
// Implementations adjust per-channel volume on system audio devices.
// Channel indices follow SDL layout:
//   0=FL, 1=FR, 2=FC, 3=LFE, 4=BL, 5=BR, 6=SL, 7=SR
class MixerBackend
{
public:
   virtual ~MixerBackend() = default;

   // Get/set per-channel volume (0.0 - 1.0) for a named device
   virtual bool GetChannelVolume(const std::string& deviceName, int channel, float& volume) = 0;
   virtual bool SetChannelVolume(const std::string& deviceName, int channel, float volume) = 0;

   // Get number of channels for a device
   virtual int GetChannelCount(const std::string& deviceName) = 0;

   // Save all channel volumes for a device (for later restore)
   virtual bool SaveVolumes(const std::string& deviceName, std::vector<float>& outVolumes) = 0;

   // Restore previously saved channel volumes
   virtual bool RestoreVolumes(const std::string& deviceName, const std::vector<float>& volumes) = 0;
};

} // namespace ChannelVol

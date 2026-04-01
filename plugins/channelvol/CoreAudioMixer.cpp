// license:GPLv3+

#ifdef __APPLE__

#include "CoreAudioMixer.h"
#include <CoreAudio/CoreAudio.h>
#include <algorithm>

namespace ChannelVol {

AudioDeviceID CoreAudioMixer::FindDeviceByName(const std::string& deviceName)
{
   AudioObjectPropertyAddress prop = {
      kAudioHardwarePropertyDevices,
      kAudioObjectPropertyScopeGlobal,
      kAudioObjectPropertyElementMain
   };

   UInt32 dataSize = 0;
   OSStatus status = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &prop, 0, nullptr, &dataSize);
   if (status != noErr || dataSize == 0)
      return kAudioObjectUnknown;

   const int deviceCount = static_cast<int>(dataSize / sizeof(AudioDeviceID));
   std::vector<AudioDeviceID> devices(deviceCount);
   status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &prop, 0, nullptr, &dataSize, devices.data());
   if (status != noErr)
      return kAudioObjectUnknown;

   for (const AudioDeviceID deviceId : devices)
   {
      // Get device name
      CFStringRef cfName = nullptr;
      UInt32 nameSize = sizeof(cfName);
      AudioObjectPropertyAddress nameProp = {
         kAudioObjectPropertyName,
         kAudioObjectPropertyScopeGlobal,
         kAudioObjectPropertyElementMain
      };
      status = AudioObjectGetPropertyData(deviceId, &nameProp, 0, nullptr, &nameSize, &cfName);
      if (status != noErr || cfName == nullptr)
         continue;

      char nameBuf[256];
      bool gotName = CFStringGetCString(cfName, nameBuf, sizeof(nameBuf), kCFStringEncodingUTF8);
      CFRelease(cfName);

      if (gotName && deviceName == nameBuf)
         return deviceId;
   }

   return kAudioObjectUnknown;
}

bool CoreAudioMixer::HasPerChannelVolume(AudioDeviceID deviceId, int numChannels)
{
   // Check if channel 1 (first individual channel) has volume control
   // Element 0 = master, elements 1..N = individual channels
   AudioObjectPropertyAddress prop = {
      kAudioDevicePropertyVolumeScalar,
      kAudioDevicePropertyScopeOutput,
      1 // first individual channel
   };
   return AudioObjectHasProperty(deviceId, &prop);
}

int CoreAudioMixer::GetChannelCount(const std::string& deviceName)
{
   const AudioDeviceID deviceId = FindDeviceByName(deviceName);
   if (deviceId == kAudioObjectUnknown)
      return 0;

   // Get output stream configuration to determine channel count
   AudioObjectPropertyAddress prop = {
      kAudioDevicePropertyStreamConfiguration,
      kAudioDevicePropertyScopeOutput,
      kAudioObjectPropertyElementMain
   };

   UInt32 dataSize = 0;
   OSStatus status = AudioObjectGetPropertyDataSize(deviceId, &prop, 0, nullptr, &dataSize);
   if (status != noErr || dataSize == 0)
      return 0;

   std::vector<uint8_t> buffer(dataSize);
   auto* bufferList = reinterpret_cast<AudioBufferList*>(buffer.data());
   status = AudioObjectGetPropertyData(deviceId, &prop, 0, nullptr, &dataSize, bufferList);
   if (status != noErr)
      return 0;

   int totalChannels = 0;
   for (UInt32 i = 0; i < bufferList->mNumberBuffers; i++)
      totalChannels += static_cast<int>(bufferList->mBuffers[i].mNumberChannels);

   return totalChannels;
}

bool CoreAudioMixer::GetChannelVolume(const std::string& deviceName, int channel, float& volume)
{
   const AudioDeviceID deviceId = FindDeviceByName(deviceName);
   if (deviceId == kAudioObjectUnknown)
      return false;

   const int numChannels = GetChannelCount(deviceName);

   // CoreAudio uses element 0 for master, elements 1..N for individual channels
   // If per-channel volume is supported, use channel+1 as element
   // Otherwise fall back to master (element 0)
   UInt32 element = 0;
   if (channel < numChannels && HasPerChannelVolume(deviceId, numChannels))
      element = static_cast<UInt32>(channel + 1);

   AudioObjectPropertyAddress prop = {
      kAudioDevicePropertyVolumeScalar,
      kAudioDevicePropertyScopeOutput,
      element
   };

   if (!AudioObjectHasProperty(deviceId, &prop))
      return false;

   Float32 scalar = 0.f;
   UInt32 dataSize = sizeof(scalar);
   OSStatus status = AudioObjectGetPropertyData(deviceId, &prop, 0, nullptr, &dataSize, &scalar);
   if (status != noErr)
      return false;

   volume = scalar;
   return true;
}

bool CoreAudioMixer::SetChannelVolume(const std::string& deviceName, int channel, float volume)
{
   const AudioDeviceID deviceId = FindDeviceByName(deviceName);
   if (deviceId == kAudioObjectUnknown)
      return false;

   volume = std::clamp(volume, 0.f, 1.f);
   const int numChannels = GetChannelCount(deviceName);

   UInt32 element = 0;
   if (channel < numChannels && HasPerChannelVolume(deviceId, numChannels))
      element = static_cast<UInt32>(channel + 1);

   AudioObjectPropertyAddress prop = {
      kAudioDevicePropertyVolumeScalar,
      kAudioDevicePropertyScopeOutput,
      element
   };

   if (!AudioObjectHasProperty(deviceId, &prop))
      return false;

   Float32 scalar = volume;
   OSStatus status = AudioObjectSetPropertyData(deviceId, &prop, 0, nullptr, sizeof(scalar), &scalar);
   return status == noErr;
}

bool CoreAudioMixer::SaveVolumes(const std::string& deviceName, std::vector<float>& outVolumes)
{
   const int numChannels = GetChannelCount(deviceName);
   if (numChannels == 0)
      return false;

   outVolumes.resize(numChannels);
   for (int i = 0; i < numChannels; i++)
   {
      if (!GetChannelVolume(deviceName, i, outVolumes[i]))
         outVolumes[i] = 1.f; // default to full volume if read fails
   }
   return true;
}

bool CoreAudioMixer::RestoreVolumes(const std::string& deviceName, const std::vector<float>& volumes)
{
   bool allOk = true;
   for (int i = 0; i < static_cast<int>(volumes.size()); i++)
   {
      if (!SetChannelVolume(deviceName, i, volumes[i]))
         allOk = false;
   }
   return allOk;
}

} // namespace ChannelVol

#endif // __APPLE__

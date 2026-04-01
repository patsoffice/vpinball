// license:GPLv3+

#ifdef __APPLE__

#include "CoreAudioMixer.h"
#include <CoreAudio/CoreAudio.h>
#include <algorithm>
#include <unordered_map>

namespace ChannelVol {

// Cache device ID lookups to avoid enumerating all devices on every call
static std::unordered_map<std::string, AudioDeviceID> s_deviceCache;

AudioDeviceID CoreAudioMixer::FindDeviceByName(const std::string& deviceName)
{
   // Check cache first
   auto it = s_deviceCache.find(deviceName);
   if (it != s_deviceCache.end())
      return it->second;

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

      if (gotName)
      {
         // Cache all discovered devices
         s_deviceCache[nameBuf] = deviceId;
         if (deviceName == nameBuf)
            return deviceId;
      }
   }

   return kAudioObjectUnknown;
}

bool CoreAudioMixer::HasPerChannelVolume(AudioDeviceID deviceId, int numChannels)
{
   AudioObjectPropertyAddress prop = {
      kAudioDevicePropertyVolumeScalar,
      kAudioDevicePropertyScopeOutput,
      1
   };
   return AudioObjectHasProperty(deviceId, &prop);
}

int CoreAudioMixer::GetChannelCount(const std::string& deviceName)
{
   const AudioDeviceID deviceId = FindDeviceByName(deviceName);
   if (deviceId == kAudioObjectUnknown)
      return 0;

   AudioObjectPropertyAddress prop = {
      kAudioDevicePropertyStreamConfiguration,
      kAudioDevicePropertyScopeOutput,
      kAudioObjectPropertyElementMain
   };

   UInt32 dataSize = 0;
   OSStatus status = AudioObjectGetPropertyDataSize(deviceId, &prop, 0, nullptr, &dataSize);
   if (status == noErr && dataSize > 0)
   {
      std::vector<uint8_t> buffer(dataSize);
      auto* bufferList = reinterpret_cast<AudioBufferList*>(buffer.data());
      status = AudioObjectGetPropertyData(deviceId, &prop, 0, nullptr, &dataSize, bufferList);
      if (status == noErr)
      {
         int totalChannels = 0;
         for (UInt32 i = 0; i < bufferList->mNumberBuffers; i++)
            totalChannels += static_cast<int>(bufferList->mBuffers[i].mNumberChannels);
         if (totalChannels > 0)
            return totalChannels;
      }
   }

   // Fallback: probe individual channel volume elements
   int count = 0;
   for (UInt32 element = 1; element <= 32; element++)
   {
      AudioObjectPropertyAddress volProp = {
         kAudioDevicePropertyVolumeScalar,
         kAudioDevicePropertyScopeOutput,
         element
      };
      if (AudioObjectHasProperty(deviceId, &volProp))
         count = static_cast<int>(element);
      else
         break;
   }
   if (count > 0)
      return count;

   // Last resort: check if master volume exists
   AudioObjectPropertyAddress masterProp = {
      kAudioDevicePropertyVolumeScalar,
      kAudioDevicePropertyScopeOutput,
      kAudioObjectPropertyElementMain
   };
   if (AudioObjectHasProperty(deviceId, &masterProp))
      return 1;

   return 0;
}

bool CoreAudioMixer::GetChannelVolume(const std::string& deviceName, int channel, float& volume)
{
   const AudioDeviceID deviceId = FindDeviceByName(deviceName);
   if (deviceId == kAudioObjectUnknown)
      return false;

   const int numChannels = GetChannelCount(deviceName);

   UInt32 element = 0;
   if (channel < numChannels && HasPerChannelVolume(deviceId, numChannels))
      element = static_cast<UInt32>(channel + 1);

   AudioObjectPropertyAddress prop = {
      kAudioDevicePropertyVolumeScalar,
      kAudioDevicePropertyScopeOutput,
      element
   };

   if (AudioObjectHasProperty(deviceId, &prop))
   {
      Float32 scalar = 0.f;
      UInt32 dataSize = sizeof(scalar);
      OSStatus status = AudioObjectGetPropertyData(deviceId, &prop, 0, nullptr, &dataSize, &scalar);
      if (status == noErr)
      {
         volume = scalar;
         return true;
      }
   }

   // Fallback: virtual main volume
   AudioObjectPropertyAddress vProp = {
      kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
      kAudioDevicePropertyScopeOutput,
      kAudioObjectPropertyElementMain
   };
   if (AudioObjectHasProperty(deviceId, &vProp))
   {
      Float32 scalar = 0.f;
      UInt32 dataSize = sizeof(scalar);
      OSStatus status = AudioObjectGetPropertyData(deviceId, &vProp, 0, nullptr, &dataSize, &scalar);
      if (status == noErr)
      {
         volume = scalar;
         return true;
      }
   }

   return false;
}

bool CoreAudioMixer::SetChannelVolume(const std::string& deviceName, int channel, float volume)
{
   const AudioDeviceID deviceId = FindDeviceByName(deviceName);
   if (deviceId == kAudioObjectUnknown)
      return false;

   volume = std::clamp(volume, 0.f, 1.f);
   const int numChannels = GetChannelCount(deviceName);
   bool perChannel = HasPerChannelVolume(deviceId, numChannels);

   UInt32 element = 0;
   if (channel < numChannels && perChannel)
      element = static_cast<UInt32>(channel + 1);

   // Try direct device volume first
   AudioObjectPropertyAddress prop = {
      kAudioDevicePropertyVolumeScalar,
      kAudioDevicePropertyScopeOutput,
      element
   };

   if (AudioObjectHasProperty(deviceId, &prop))
   {
      Boolean settable = false;
      AudioObjectIsPropertySettable(deviceId, &prop, &settable);
      if (settable)
      {
         Float32 scalar = volume;
         OSStatus status = AudioObjectSetPropertyData(deviceId, &prop, 0, nullptr, sizeof(scalar), &scalar);
         return status == noErr;
      }
   }

   // Fallback: virtual main volume
   AudioObjectPropertyAddress vProp = {
      kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
      kAudioDevicePropertyScopeOutput,
      kAudioObjectPropertyElementMain
   };

   if (AudioObjectHasProperty(deviceId, &vProp))
   {
      Boolean settable = false;
      AudioObjectIsPropertySettable(deviceId, &vProp, &settable);
      if (settable)
      {
         Float32 scalar = volume;
         OSStatus status = AudioObjectSetPropertyData(deviceId, &vProp, 0, nullptr, sizeof(scalar), &scalar);
         return status == noErr;
      }
   }

   return false;
}

bool CoreAudioMixer::SaveVolumes(const std::string& deviceName, std::vector<float>& outVolumes)
{
   const int numChannels = GetChannelCount(deviceName);
   if (numChannels == 0)
   {
      // Device may still have virtual main volume
      float vol;
      if (GetChannelVolume(deviceName, 0, vol))
      {
         outVolumes = { vol };
         return true;
      }
      return false;
   }

   outVolumes.resize(numChannels);
   for (int i = 0; i < numChannels; i++)
   {
      if (!GetChannelVolume(deviceName, i, outVolumes[i]))
         outVolumes[i] = 1.f;
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

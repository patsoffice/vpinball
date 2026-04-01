// license:GPLv3+

#include "plugins/MsgPlugin.h"
#include "plugins/VPXPlugin.h"
#include "MixerBackend.h"

#ifdef __APPLE__
#include "CoreAudioMixer.h"
#endif

#include <memory>
#include <vector>
#include <string>
#include <cstdio>

namespace ChannelVol {

const MsgPluginAPI* msgApi = nullptr;
VPXPluginAPI* vpxApi = nullptr;
std::unique_ptr<MixerBackend> mixer;

uint32_t endpointId;
unsigned int getVpxApiId, onGameStartId, onGameEndId, onKeyInputId;

// Saved OS volumes for restore on game end
std::vector<float> savedBGVolumes;
std::vector<float> savedPFVolumes;
std::string bgDeviceName;
std::string pfDeviceName;

// Tracked current volumes (avoid re-reading from OS each keypress)
int currentBGVol = -1;
int currentSSFFrontVol = -1;
int currentSSFRearVol = -1;

// --- Settings ---

// Per-table volume settings (-1 = use current OS volume, 0-100 = override)
MSGPI_INT_VAL_SETTING(bgVolProp,       "BackglassVolume",    "Backglass Volume",    "Per-table backglass device volume (0-100, -1=use current)", true, -1, 100, -1);
MSGPI_INT_VAL_SETTING(ssfFrontVolProp, "SSFFrontVolume",     "SSF Front Volume",    "Per-table SSF front speaker volume (0-100, -1=use current)", true, -1, 100, -1);
MSGPI_INT_VAL_SETTING(ssfRearVolProp,  "SSFRearVolume",      "SSF Rear Volume",     "Per-table SSF rear speaker volume (0-100, -1=use current)", true, -1, 100, -1);

// Hotkey scancodes (0 = disabled)
MSGPI_INT_VAL_SETTING(bgVolUpKeyProp,       "BGVolumeUpKey",       "BG Volume Up Key",       "SDL scancode for backglass volume up (0=disabled)", true, 0, 999, 0);
MSGPI_INT_VAL_SETTING(bgVolDownKeyProp,     "BGVolumeDownKey",     "BG Volume Down Key",     "SDL scancode for backglass volume down (0=disabled)", true, 0, 999, 0);
MSGPI_INT_VAL_SETTING(ssfFrontUpKeyProp,    "SSFFrontVolumeUpKey", "SSF Front Vol Up Key",   "SDL scancode for SSF front volume up (0=disabled)", true, 0, 999, 0);
MSGPI_INT_VAL_SETTING(ssfFrontDownKeyProp,  "SSFFrontVolumeDownKey","SSF Front Vol Down Key","SDL scancode for SSF front volume down (0=disabled)", true, 0, 999, 0);
MSGPI_INT_VAL_SETTING(ssfRearUpKeyProp,     "SSFRearVolumeUpKey",  "SSF Rear Vol Up Key",    "SDL scancode for SSF rear volume up (0=disabled)", true, 0, 999, 0);
MSGPI_INT_VAL_SETTING(ssfRearDownKeyProp,   "SSFRearVolumeDownKey","SSF Rear Vol Down Key",  "SDL scancode for SSF rear volume down (0=disabled)", true, 0, 999, 0);

// Volume step per keypress
MSGPI_INT_VAL_SETTING(volStepProp, "VolumeStep", "Volume Step", "Volume change per keypress (%)", true, 1, 25, 5);

// --- Helpers ---

static void registerGlobalSettings()
{
   msgApi->RegisterSetting(endpointId, &bgVolUpKeyProp);
   msgApi->RegisterSetting(endpointId, &bgVolDownKeyProp);
   msgApi->RegisterSetting(endpointId, &ssfFrontUpKeyProp);
   msgApi->RegisterSetting(endpointId, &ssfFrontDownKeyProp);
   msgApi->RegisterSetting(endpointId, &ssfRearUpKeyProp);
   msgApi->RegisterSetting(endpointId, &ssfRearDownKeyProp);
   msgApi->RegisterSetting(endpointId, &volStepProp);
}

static void registerVolumeSettings()
{
   msgApi->RegisterSetting(endpointId, &bgVolProp);
   msgApi->RegisterSetting(endpointId, &ssfFrontVolProp);
   msgApi->RegisterSetting(endpointId, &ssfRearVolProp);
}

static void applyBGVolume(int percent)
{
   if (!mixer || bgDeviceName.empty() || percent < 0)
      return;
   const float vol = static_cast<float>(percent) / 100.f;
   const int count = mixer->GetChannelCount(bgDeviceName);
   if (count == 0)
   {
      mixer->SetChannelVolume(bgDeviceName, 0, vol);
      return;
   }
   for (int i = 0; i < count; i++)
      mixer->SetChannelVolume(bgDeviceName, i, vol);
}

static void applySSFFrontVolume(int percent)
{
   if (!mixer || pfDeviceName.empty() || percent < 0)
      return;
   const float vol = static_cast<float>(percent) / 100.f;
   mixer->SetChannelVolume(pfDeviceName, 0, vol); // FL
   mixer->SetChannelVolume(pfDeviceName, 1, vol); // FR
}

static void applySSFRearVolume(int percent)
{
   if (!mixer || pfDeviceName.empty() || percent < 0)
      return;
   const float vol = static_cast<float>(percent) / 100.f;
   mixer->SetChannelVolume(pfDeviceName, 4, vol); // BL
   mixer->SetChannelVolume(pfDeviceName, 5, vol); // BR
}

static int readOSVolume(const std::string& device, int channel)
{
   if (!mixer || device.empty())
      return -1;
   float vol = 1.f;
   if (!mixer->GetChannelVolume(device, channel, vol))
      return -1;
   return static_cast<int>(vol * 100.f + 0.5f);
}

static void initTrackedVolumes()
{
   currentBGVol = readOSVolume(bgDeviceName, 0);
   if (currentBGVol < 0) currentBGVol = 100;
   currentSSFFrontVol = readOSVolume(pfDeviceName, 0);
   if (currentSSFFrontVol < 0) currentSSFFrontVol = 100;
   currentSSFRearVol = readOSVolume(pfDeviceName, 4);
   if (currentSSFRearVol < 0) currentSSFRearVol = 100;
}

static void showNotification(const char* label, int percent)
{
   if (!vpxApi)
      return;
   char buf[64];
   snprintf(buf, sizeof(buf), "%s: %d%%", label, percent);
   vpxApi->PushNotification(buf, 2000);
}

// --- Callbacks ---

void onGameStart(const unsigned int eventId, void* userData, void* eventData)
{
   if (!vpxApi || !mixer)
      return;

   bgDeviceName = vpxApi->GetBackglassDeviceName();
   pfDeviceName = vpxApi->GetPlayfieldDeviceName();

   // Save current OS volumes before applying per-table overrides
   mixer->SaveVolumes(bgDeviceName, savedBGVolumes);
   mixer->SaveVolumes(pfDeviceName, savedPFVolumes);

   // Read current OS volumes into tracked state
   initTrackedVolumes();

   // Load per-table volume settings from table INI
   registerVolumeSettings();

   // Apply per-table volumes (skip if -1 = use current)
   if (bgVolProp_Val >= 0) { applyBGVolume(bgVolProp_Val); currentBGVol = bgVolProp_Val; }
   if (ssfFrontVolProp_Val >= 0) { applySSFFrontVolume(ssfFrontVolProp_Val); currentSSFFrontVol = ssfFrontVolProp_Val; }
   if (ssfRearVolProp_Val >= 0) { applySSFRearVolume(ssfRearVolProp_Val); currentSSFRearVol = ssfRearVolProp_Val; }
}

void onGameEnd(const unsigned int eventId, void* userData, void* eventData)
{
   if (!mixer)
      return;

   if (!savedBGVolumes.empty())
      mixer->RestoreVolumes(bgDeviceName, savedBGVolumes);
   if (!savedPFVolumes.empty())
      mixer->RestoreVolumes(pfDeviceName, savedPFVolumes);

   savedBGVolumes.clear();
   savedPFVolumes.clear();
   bgDeviceName.clear();
   pfDeviceName.clear();
}

void onKeyInput(const unsigned int eventId, void* userData, void* eventData)
{
   if (!mixer || !vpxApi)
      return;

   const auto* keyEvent = static_cast<const VPXKeyEvent*>(eventData);
   if (!keyEvent->isPressed)
      return;

   const int sc = keyEvent->scancode;
   const int step = volStepProp_Val;
   int vol;

   // Backglass volume
   if (bgVolUpKeyProp_Val > 0 && sc == bgVolUpKeyProp_Val)
   {
      vol = (currentBGVol + step > 100) ? 100 : currentBGVol + step;
      applyBGVolume(vol);
      currentBGVol = vol;
      bgVolProp_Val = vol;
      msgApi->SaveSetting(endpointId, &bgVolProp);
      showNotification("Backglass", vol);
      return;
   }
   if (bgVolDownKeyProp_Val > 0 && sc == bgVolDownKeyProp_Val)
   {
      vol = (currentBGVol - step < 0) ? 0 : currentBGVol - step;
      applyBGVolume(vol);
      currentBGVol = vol;
      bgVolProp_Val = vol;
      msgApi->SaveSetting(endpointId, &bgVolProp);
      showNotification("Backglass", vol);
      return;
   }

   // SSF Front volume
   if (ssfFrontUpKeyProp_Val > 0 && sc == ssfFrontUpKeyProp_Val)
   {
      vol = (currentSSFFrontVol + step > 100) ? 100 : currentSSFFrontVol + step;
      applySSFFrontVolume(vol);
      currentSSFFrontVol = vol;
      ssfFrontVolProp_Val = vol;
      msgApi->SaveSetting(endpointId, &ssfFrontVolProp);
      showNotification("SSF Front", vol);
      return;
   }
   if (ssfFrontDownKeyProp_Val > 0 && sc == ssfFrontDownKeyProp_Val)
   {
      vol = (currentSSFFrontVol - step < 0) ? 0 : currentSSFFrontVol - step;
      applySSFFrontVolume(vol);
      currentSSFFrontVol = vol;
      ssfFrontVolProp_Val = vol;
      msgApi->SaveSetting(endpointId, &ssfFrontVolProp);
      showNotification("SSF Front", vol);
      return;
   }

   // SSF Rear volume
   if (ssfRearUpKeyProp_Val > 0 && sc == ssfRearUpKeyProp_Val)
   {
      vol = (currentSSFRearVol + step > 100) ? 100 : currentSSFRearVol + step;
      applySSFRearVolume(vol);
      currentSSFRearVol = vol;
      ssfRearVolProp_Val = vol;
      msgApi->SaveSetting(endpointId, &ssfRearVolProp);
      showNotification("SSF Rear", vol);
      return;
   }
   if (ssfRearDownKeyProp_Val > 0 && sc == ssfRearDownKeyProp_Val)
   {
      vol = (currentSSFRearVol - step < 0) ? 0 : currentSSFRearVol - step;
      applySSFRearVolume(vol);
      currentSSFRearVol = vol;
      ssfRearVolProp_Val = vol;
      msgApi->SaveSetting(endpointId, &ssfRearVolProp);
      showNotification("SSF Rear", vol);
      return;
   }
}

} // namespace ChannelVol

using namespace ChannelVol;

MSGPI_EXPORT void MSGPIAPI ChannelVolPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api)
{
   msgApi = api;
   endpointId = sessionId;
   msgApi->BroadcastMsg(endpointId, getVpxApiId = msgApi->GetMsgID(VPXPI_NAMESPACE, VPXPI_MSG_GET_API), &vpxApi);

#ifdef __APPLE__
   mixer = std::make_unique<CoreAudioMixer>();
#endif
   // TODO: Linux backends (PulseAudio, ALSA)

   registerGlobalSettings();
   registerVolumeSettings();

   msgApi->SubscribeMsg(endpointId, onGameStartId = msgApi->GetMsgID(VPXPI_NAMESPACE, VPXPI_EVT_ON_GAME_START), onGameStart, nullptr);
   msgApi->SubscribeMsg(endpointId, onGameEndId = msgApi->GetMsgID(VPXPI_NAMESPACE, VPXPI_EVT_ON_GAME_END), onGameEnd, nullptr);
   msgApi->SubscribeMsg(endpointId, onKeyInputId = msgApi->GetMsgID(VPXPI_NAMESPACE, VPXPI_EVT_ON_KEY_INPUT), onKeyInput, nullptr);
}

MSGPI_EXPORT void MSGPIAPI ChannelVolPluginUnload()
{
   msgApi->UnsubscribeMsg(onGameStartId, onGameStart, nullptr);
   msgApi->UnsubscribeMsg(onGameEndId, onGameEnd, nullptr);
   msgApi->UnsubscribeMsg(onKeyInputId, onKeyInput, nullptr);
   msgApi->ReleaseMsgID(getVpxApiId);
   msgApi->ReleaseMsgID(onGameStartId);
   msgApi->ReleaseMsgID(onGameEndId);
   msgApi->ReleaseMsgID(onKeyInputId);

   mixer.reset();
   vpxApi = nullptr;
   msgApi = nullptr;
}

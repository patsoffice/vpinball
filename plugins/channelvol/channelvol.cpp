// license:GPLv3+

#include "plugins/MsgPlugin.h"
#include "plugins/VPXPlugin.h"

namespace ChannelVol {

const MsgPluginAPI* msgApi = nullptr;
VPXPluginAPI* vpxApi = nullptr;

uint32_t endpointId;
unsigned int getVpxApiId, onGameStartId, onGameEndId, onKeyInputId;

void onGameStart(const unsigned int eventId, void* userData, void* eventData)
{
   // TODO: Re-register settings to load per-table values
   // TODO: Save current OS volumes
   // TODO: Apply per-table volumes to OS mixer
}

void onGameEnd(const unsigned int eventId, void* userData, void* eventData)
{
   // TODO: Restore saved OS volumes
}

void onKeyInput(const unsigned int eventId, void* userData, void* eventData)
{
   // TODO: Check scancode against configured hotkeys
   // TODO: Adjust OS channel volume
   // TODO: Show notification + trigger overlay
}

}

using namespace ChannelVol;

MSGPI_EXPORT void MSGPIAPI ChannelVolPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api)
{
   msgApi = api;
   endpointId = sessionId;
   msgApi->BroadcastMsg(endpointId, getVpxApiId = msgApi->GetMsgID(VPXPI_NAMESPACE, VPXPI_MSG_GET_API), &vpxApi);
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
   vpxApi = nullptr;
   msgApi = nullptr;
}

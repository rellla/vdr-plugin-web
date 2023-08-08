/*
 * status.c: keep track of VDR device status
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include <chrono>
#include <vdr/channels.h>
#include "status.h"
#include "ait.h"
#include "browserclient.h"
#include "webosdpage.h"

const char *channelJson = R"(
    {
        "channelId": "%s",
        "channelType": %d,
        "ccid": "ccid://1.0",
        "nid": %d,
        "dsd": "",
        "onid": %d,
        "tsid": %d,
        "sid": %d,
        "name": "%s",
        "longName": "%s",
        "description": "OIPF (SD&amp;S) - TCServiceData doesn’t support yet!",
        "authorised": true,
        "genre": null,
        "hidden": false,
        "idType": "%d",
        "channelMaxBitRate": 0,
        "manualBlock": false,
        "majorChannel": 1,
        "ipBroadcastID": "rtp://1.2.3.4/",
        "locked": false
    }
)";

inline const char* toChannelJson(const cChannel* channel) {
    // longName, Name => currentChannel
    // nid            => ??? (use 1 as default)
    // onid           => channel, Nid
    // sid            => channel, Sid
    // tsid           => channel, Tid
    // channelType    => HDTV 0x19, TV 0x01, Radio 0x02
    // idType         => ??? (use 15 as default)
    int channelType;

    if (strstr(channel->Name(), "HD") != nullptr) {
        channelType = 0x19;
    } else if (channel->Rid() > 0) {
        channelType = 0x02;
    } else {
        channelType = 0x01;
    }

    char *cmd;
    asprintf(&cmd, channelJson, *channel->GetChannelID().ToString(), channelType, 1, channel->Nid(), channel->Tid(), channel->Sid(), channel->Name(), channel->Name(), 15);
    return cmd;
}


bool runVolumeTrigger = false;
cHbbtvDeviceStatus::cHbbtvDeviceStatus() {
   device = nullptr;
   aitFilter = nullptr;
   sid = -1;
}

cHbbtvDeviceStatus::~cHbbtvDeviceStatus() {
   if (volumeTriggerThread) {
      runVolumeTrigger = false;
      volumeTriggerThread->join();
   }
}

void cHbbtvDeviceStatus::ChannelSwitch(const cDevice * vdrDevice, int channelNumber, bool LiveView) {
   // Indicates a channel switch on the given DVB device.
   // If ChannelNumber is 0, this is before the channel is being switched,
   // otherwise ChannelNumber is the number of the channel that has been switched to.
   // LiveView tells whether this channel switch is for live viewing.
   if (LiveView) {
      if (device) {
         isyslog("[vdrweb] Detaching HbbTV ait filter from device %d", device->CardIndex()+1);
         device->Detach(aitFilter);
         device = nullptr;
         aitFilter = nullptr;
         sid = -1;
      }

      if (channelNumber) {
         device = cDevice::ActualDevice();

#if APIVERSNUM >= 20301
         LOCK_CHANNELS_READ
         auto channel = Channels->GetByNumber(channelNumber);
#else
         auto channel = Channels->GetByNumber(channelNumber);
#endif
         sid = channel->Sid();

         const char* buffer = toChannelJson(channel);
         browserClient->InsertChannel(buffer);
         free((void *) buffer);

         device->AttachFilter(aitFilter = new cAitFilter(sid));
         isyslog("[vdrweb] Attached HbbTV ait filter to device %d, vdrDev=%d actDev=%d, Sid=0x%04x", device->CardIndex()+1, vdrDevice->CardIndex()+1,cDevice::ActualDevice()->CardIndex()+1, sid);
      }
   }
}

void triggerVolumeThread(time_t lastVolumeTime) {
   int waitTime = 400;

   // wait until display time is reached
   while (runVolumeTrigger) {
      if (time(NULL) - lastVolumeTime > 3) {
          runVolumeTrigger = false;
       } else {
         std::this_thread::sleep_for(std::chrono::milliseconds(waitTime));
      }
   }

   // deleting the volume bar on exit
   WebOSDPage *page = WebOSDPage::Get();
   if (page)
     page->DeleteVolume();
}

void cHbbtvDeviceStatus::SetVolume(int Volume, bool Absolute) {
    // instead using the parameter (relative volume change), use the absolute volume of the Device
//    browserClient->SetVolume(cDevice::CurrentVolume(), MAXVOLUME);

   // First, stop a running thread if we have one
/*   if (volumeTriggerThread) {
      runVolumeTrigger = false;
      volumeTriggerThread->join();
   }*/

   volume = Absolute ? Volume : volume + Volume;
   WebOSDPage* page = WebOSDPage::Get();
   if (page)
       page->DrawVolume(volume);
   lastVolumeTime = time(NULL);
   // start thread for display time
   runVolumeTrigger = true;
   volumeTriggerThread = new std::thread(triggerVolumeThread, lastVolumeTime);
}

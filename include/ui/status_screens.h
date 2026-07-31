#pragma once

#include <time.h>

void statusScreenPortal();
void statusScreenConnectFailed();
void statusScreenWifiReset();

void statusScreenOfflinePortal();
void statusScreenOfflineReady(
    time_t utc_time);

/** Saved-network connect animation. */
void statusScreenConnectingBegin(
    const char* ssid);

void statusScreenConnectingTick();
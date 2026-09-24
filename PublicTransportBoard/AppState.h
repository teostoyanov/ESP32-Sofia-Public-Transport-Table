#pragma once
// Shared state between the background GTFS polling task and the LVGL UI task.
// Guarded by AppState_Lock()/AppState_Unlock() (see AppState.cpp).

#include <stdint.h>
#include "RouteConfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

struct RouteArrivals {
  int64_t etaEpoch[MAX_ETAS_PER_ROUTE]; // sorted ascending, soonest first
  uint8_t count;
};

struct AppState {
  RouteArrivals routes[NUM_ROUTES];
  uint32_t consecutiveFailures; // resets to 0 on any successful fetch
  unsigned long lastSuccessMillis; // millis() of the last successful fetch, 0 if never
  bool everSucceeded;
};

extern AppState g_appState;

void AppState_Init();
void AppState_Lock();
void AppState_Unlock();

// True once 3 consecutive polls have failed (~45-60s with no successful fetch).
// Below this threshold, stale arrival data is still shown (it's probably
// close enough to correct); above it, arrivals are blanked since the vehicle
// may have already come and gone.
bool AppState_IsDataStale(const AppState &snapshot);

#include "AppState.h"
#include <string.h>

AppState g_appState;
static SemaphoreHandle_t s_mutex = nullptr;

void AppState_Init() {
  s_mutex = xSemaphoreCreateMutex();
  memset(&g_appState, 0, sizeof(g_appState));
}

void AppState_Lock() { xSemaphoreTake(s_mutex, portMAX_DELAY); }
void AppState_Unlock() { xSemaphoreGive(s_mutex); }

bool AppState_IsDataStale(const AppState &snapshot) {
  return snapshot.consecutiveFailures >= 3;
}

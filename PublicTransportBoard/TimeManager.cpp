#include "TimeManager.h"
#include <time.h>
#include <sys/time.h>
#include <esp_sntp.h>

// Standard POSIX TZ string for Europe/Sofia (EET, UTC+2 / EEST UTC+3 DST),
// last Sunday of March 02:00 -> last Sunday of October 03:00.
static const char *kTzInfo = "EET-2EEST,M3.5.0/3,M10.5.0/4";

static bool s_synced = false;

static void onTimeSynced(struct timeval *tv) {
  (void)tv;
  s_synced = true;
}

void Time_Init() {
  setenv("TZ", kTzInfo, 1);
  tzset();
  sntp_set_time_sync_notification_cb(onTimeSynced);
  configTzTime(kTzInfo, "pool.ntp.org", "time.google.com");
}

bool Time_IsSynced() { return s_synced; }

void Time_GetHHMM(char *buf, size_t bufLen) {
  time_t now = time(nullptr);
  struct tm local;
  localtime_r(&now, &local);
  strftime(buf, bufLen, "%H:%M", &local);
}

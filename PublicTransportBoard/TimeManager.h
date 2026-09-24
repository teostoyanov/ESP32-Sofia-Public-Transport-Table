#pragma once
#include <Arduino.h>

// NTP sync over WiFi, Europe/Sofia timezone (EET/EEST, DST-aware).
void Time_Init();

// Returns true once NTP has synced at least once.
bool Time_IsSynced();

// Formats current local time as "HH:MM" into buf (must be >= 6 bytes).
void Time_GetHHMM(char *buf, size_t bufLen);

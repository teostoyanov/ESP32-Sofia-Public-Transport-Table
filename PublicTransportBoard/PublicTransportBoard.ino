/*
 * Public Transport Board
 * Waveshare ESP32-S3-Touch-LCD-2.8-V2 display showing live arrival times
 * for the bus/tram stops configured in RouteConfig.h.
 * See ../README.md for setup instructions.
 */
#include <WiFi.h>
#include "Display_ST7789.h"
#include "I2C_Driver.h"
#include "LVGL_Driver.h"
#include "AppState.h"
#include "TimeManager.h"
#include "GtfsClient.h"
#include "UiScreens.h"
#include "secrets.h"

static void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting to WiFi SSID '%s'...\n", WIFI_SSID);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi connected, IP=%s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("WiFi connect timed out - will keep retrying in the background.");
  }
}

// Reconnects WiFi in the background if it ever drops, independent of the
// GTFS polling task (which just skips a poll while disconnected).
static void wifiWatchdogTask(void *param) {
  (void)param;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] disconnected, reconnecting...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      unsigned long start = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
        vTaskDelay(pdMS_TO_TICKS(250));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

void setup() {
  Serial.begin(115200);

  I2C_Init();
  Backlight_Init();
  LCD_Init(); // also initializes the touch controller
  Lvgl_Init();

  AppState_Init();
  connectWifi();
  Time_Init();
  Ui_Init();
  Gtfs_StartPollingTask();

  xTaskCreatePinnedToCore(wifiWatchdogTask, "wifi_watchdog", 4096, nullptr, 1, nullptr, 0);
}

void loop() {
  Lvgl_Loop();
  vTaskDelay(pdMS_TO_TICKS(5));
}

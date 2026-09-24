#include "GtfsClient.h"
#include "AppState.h"
#include "RouteConfig.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <esp_heap_caps.h>

#include "pb_decode.h"
#include "gtfs-realtime.pb.h"

static const char *kHost = "gtfs.sofiatraffic.bg";
static const uint16_t kPort = 443;
static const char *kPath = "/api/v1/trip-updates";
static const uint32_t kPollIntervalMs = 15000;
static const uint32_t kSocketTimeoutMs = 20000;

// --- HTTP body fetch: pulls the whole response off the TLS socket into a
// PSRAM buffer, transparently unwrapping chunked transfer-encoding. ---
//
// This buffers the full ~1MB feed instead of streaming it straight into
// nanopb. That's a deliberate change from the original streaming design:
// nanopb's pb_decode() can only recognize "clean end of message" when
// stream.bytes_left reaches exactly 0 (see pb_decode.c's pb_readbyte/pb_read),
// which requires knowing the total message length *before* decoding starts.
// Chunked transfer-encoding doesn't give us that upfront, so a callback-based
// stream reporting EOF via a false return (with bytes_left left at a sentinel
// "unknown" value) is treated as a hard I/O error, not a clean finish - this
// was confirmed on real hardware (logs showed the full ~990KB body being
// read correctly, then "protobuf decode error: io error" right at the end).
// The ESP32-S3 has 8MB of PSRAM, so buffering the ~1MB feed is cheap and
// sidesteps the problem entirely: decode it exactly like the host test in
// Public Transport/tools/test_decode.c, from a buffer of known size.

struct HttpBodyReader {
  WiFiClientSecure *client;
  bool chunked;
  long contentLength;   // >=0 when Content-Length is known, -1 otherwise
  long bytesRemaining;  // for contentLength mode: bytes left to read
  long chunkRemaining;  // for chunked mode: bytes left in the current chunk
  bool finished;        // true once the body is fully consumed
};

// Reads exactly `len` raw bytes from the socket (blocking, with the client's
// configured timeout). Returns false on timeout/disconnect.
static bool readRaw(WiFiClientSecure *client, uint8_t *buf, size_t len) {
  size_t got = 0;
  unsigned long start = millis();
  while (got < len) {
    if (!client->connected() && client->available() == 0) return false;
    int n = client->read(buf + got, len - got);
    if (n > 0) {
      got += (size_t)n;
      continue;
    }
    if (millis() - start > kSocketTimeoutMs) return false;
    delay(1);
  }
  return true;
}

// Reads one CRLF-terminated line (used for chunk-size lines). Strips the CRLF.
static bool readLine(WiFiClientSecure *client, char *buf, size_t bufLen) {
  size_t i = 0;
  unsigned long start = millis();
  while (true) {
    if (client->available()) {
      int c = client->read();
      if (c < 0) continue;
      if (c == '\n') {
        if (i > 0 && buf[i - 1] == '\r') i--;
        buf[i] = '\0';
        return true;
      }
      if (i + 1 < bufLen) buf[i++] = (char)c;
    } else {
      if (!client->connected()) return false;
      if (millis() - start > kSocketTimeoutMs) return false;
      delay(1);
    }
  }
}

static bool httpBodyFillChunk(HttpBodyReader *r) {
  char line[16];
  if (!readLine(r->client, line, sizeof(line))) return false;
  long size = strtol(line, nullptr, 16);
  if (size <= 0) {
    r->finished = true;
    return false;
  }
  r->chunkRemaining = size;
  return true;
}

static bool fetchBodyToBuffer(HttpBodyReader *r, uint8_t **outBuf, size_t *outSize) {
  size_t capacity = 256 * 1024;
  uint8_t *buf = (uint8_t *)heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM);
  if (!buf) {
    Serial.println("[GTFS] PSRAM alloc failed");
    return false;
  }

  size_t size = 0;
  uint8_t scratch[2048];
  bool bodyComplete = false;

  while (!bodyComplete) {
    size_t want = sizeof(scratch);

    if (r->chunked) {
      if (r->chunkRemaining <= 0) {
        if (r->finished) {
          bodyComplete = true;
          break;
        }
        if (!httpBodyFillChunk(r)) {
          bodyComplete = r->finished; // finished => clean end; else => real error
          break;
        }
        continue; // re-check with the freshly filled chunk size
      }
      if (want > (size_t)r->chunkRemaining) want = (size_t)r->chunkRemaining;
      if (!readRaw(r->client, scratch, want)) break;
      r->chunkRemaining -= (long)want;
      if (r->chunkRemaining == 0) {
        char crlf[3];
        readLine(r->client, crlf, sizeof(crlf)); // consume trailing CRLF after chunk data
      }
    } else if (r->contentLength >= 0) {
      if (r->bytesRemaining <= 0) {
        bodyComplete = true;
        break;
      }
      if (want > (size_t)r->bytesRemaining) want = (size_t)r->bytesRemaining;
      if (!readRaw(r->client, scratch, want)) break;
      r->bytesRemaining -= (long)want;
    } else {
      // Neither chunked nor a known Content-Length: read until the peer closes.
      if (!r->client->connected() && r->client->available() == 0) {
        bodyComplete = true;
        break;
      }
      int n = r->client->read(scratch, want);
      if (n <= 0) {
        if (!r->client->connected()) {
          bodyComplete = true;
          break;
        }
        delay(1);
        continue;
      }
      want = (size_t)n;
    }

    if (size + want > capacity) {
      size_t newCap = capacity * 2;
      while (newCap < size + want) newCap *= 2;
      uint8_t *nb = (uint8_t *)heap_caps_realloc(buf, newCap, MALLOC_CAP_SPIRAM);
      if (!nb) {
        Serial.println("[GTFS] PSRAM realloc failed");
        heap_caps_free(buf);
        return false;
      }
      buf = nb;
      capacity = newCap;
    }
    memcpy(buf + size, scratch, want);
    size += want;
  }

  if (!bodyComplete) {
    Serial.println("[GTFS] body fetch failed/truncated");
    heap_caps_free(buf);
    return false;
  }

  *outBuf = buf;
  *outSize = size;
  return true;
}

// --- nanopb decode callbacks (validated against a live feed capture in
// Public Transport/tools/test_decode.c before being ported here) ---

struct StrDest {
  char buf[20];
};

static bool readStringCb(pb_istream_t *stream, const pb_field_t *field, void **arg) {
  (void)field;
  StrDest *dest = (StrDest *)*arg;
  size_t len = stream->bytes_left;
  if (len >= sizeof(dest->buf)) len = sizeof(dest->buf) - 1;
  if (!pb_read(stream, (pb_byte_t *)dest->buf, len)) return false;
  dest->buf[len] = '\0';
  return true;
}

struct StuEntry {
  char stopId[20];
  bool hasArrival;
  int64_t arrivalTime;
  bool hasDeparture;
  int64_t departureTime;
};

#define MAX_STU_PER_TRIP 16
struct StuCollector {
  StuEntry entries[MAX_STU_PER_TRIP];
  int count;
};

static bool collectStuCb(pb_istream_t *stream, const pb_field_t *field, void **arg) {
  (void)field;
  StuCollector *coll = (StuCollector *)*arg;

  transit_realtime_TripUpdate_StopTimeUpdate stu = transit_realtime_TripUpdate_StopTimeUpdate_init_zero;
  StrDest stopSd = {{0}};
  stu.stop_id.funcs.decode = readStringCb;
  stu.stop_id.arg = &stopSd;

  if (!pb_decode(stream, transit_realtime_TripUpdate_StopTimeUpdate_fields, &stu)) return false;

  if (coll->count < MAX_STU_PER_TRIP) {
    StuEntry *e = &coll->entries[coll->count++];
    strncpy(e->stopId, stopSd.buf, sizeof(e->stopId) - 1);
    e->stopId[sizeof(e->stopId) - 1] = '\0';
    e->hasArrival = stu.has_arrival && stu.arrival.has_time;
    e->arrivalTime = e->hasArrival ? stu.arrival.time : 0;
    e->hasDeparture = stu.has_departure && stu.departure.has_time;
    e->departureTime = e->hasDeparture ? stu.departure.time : 0;
  }
  return true;
}

struct ResultCollector {
  int64_t etas[NUM_ROUTES][MAX_ETAS_PER_ROUTE];
  int etaCount[NUM_ROUTES];
};

static int findRouteIndex(const char *routeId) {
  for (size_t i = 0; i < NUM_ROUTES; i++) {
    if (strcmp(routeId, kRoutes[i].gtfsRouteId) == 0) return (int)i;
  }
  return -1;
}

static bool stopMatchesRoute(int routeIdx, const char *stopId) {
  return strcmp(stopId, kRoutes[routeIdx].gtfsStopId) == 0;
}

static void resultsAdd(ResultCollector *r, int routeIdx, int64_t t) {
  int64_t *arr = r->etas[routeIdx];
  int n = r->etaCount[routeIdx];
  if (n == MAX_ETAS_PER_ROUTE && t >= arr[MAX_ETAS_PER_ROUTE - 1]) return;
  if (n < MAX_ETAS_PER_ROUTE) r->etaCount[routeIdx] = n + 1;
  int i = (n < MAX_ETAS_PER_ROUTE) ? n : MAX_ETAS_PER_ROUTE - 1;
  while (i > 0 && arr[i - 1] > t) {
    arr[i] = arr[i - 1];
    i--;
  }
  arr[i] = t;
}

static bool decodeEntityCb(pb_istream_t *stream, const pb_field_t *field, void **arg) {
  (void)field;
  ResultCollector *results = (ResultCollector *)*arg;

  static transit_realtime_FeedEntity entity;
  memset(&entity, 0, sizeof(entity));

  StrDest routeSd = {{0}};
  entity.trip_update.trip.route_id.funcs.decode = readStringCb;
  entity.trip_update.trip.route_id.arg = &routeSd;

  static StuCollector stuCollector;
  memset(&stuCollector, 0, sizeof(stuCollector));
  entity.trip_update.stop_time_update.funcs.decode = collectStuCb;
  entity.trip_update.stop_time_update.arg = &stuCollector;

  if (!pb_decode(stream, transit_realtime_FeedEntity_fields, &entity)) return false;

  if (!entity.has_trip_update) return true;
  if (routeSd.buf[0] == '\0') return true;

  int routeIdx = findRouteIndex(routeSd.buf);
  if (routeIdx < 0) return true;

  for (int i = 0; i < stuCollector.count; i++) {
    StuEntry *e = &stuCollector.entries[i];
    if (!stopMatchesRoute(routeIdx, e->stopId)) continue;
    int64_t t = -1;
    if (e->hasArrival) t = e->arrivalTime;
    else if (e->hasDeparture) t = e->departureTime;
    if (t < 0) continue;
    resultsAdd(results, routeIdx, t);
  }
  return true;
}

// --- HTTP request + header parsing ---

static bool sendRequestAndSkipHeaders(WiFiClientSecure *client, HttpBodyReader *reader) {
  client->printf("GET %s HTTP/1.1\r\n", kPath);
  client->printf("Host: %s\r\n", kHost);
  client->print("User-Agent: PublicTransportBoard/1.0\r\n");
  client->print("Accept: application/octet-stream\r\n");
  client->print("Connection: close\r\n\r\n");

  char line[128];
  if (!readLine(client, line, sizeof(line))) {
    Serial.println("[GTFS] failed to read status line");
    return false;
  }
  int statusCode = 0;
  sscanf(line, "HTTP/%*d.%*d %d", &statusCode);
  if (statusCode != 200) {
    Serial.printf("[GTFS] unexpected HTTP status: %s\n", line);
    return false;
  }

  reader->chunked = false;
  reader->contentLength = -1;
  while (readLine(client, line, sizeof(line))) {
    if (line[0] == '\0') break; // blank line == end of headers
    if (strncasecmp(line, "Transfer-Encoding:", strlen("Transfer-Encoding:")) == 0 && strstr(line, "chunked")) {
      reader->chunked = true;
    } else if (strncasecmp(line, "Content-Length:", strlen("Content-Length:")) == 0) {
      reader->contentLength = atol(line + 15);
    }
  }
  reader->bytesRemaining = reader->contentLength;
  return true;
}

// --- One poll cycle: connect, fetch, decode, publish results ---

static bool pollOnce() {
  WiFiClientSecure client;
  client.setInsecure(); // public, non-sensitive transit data; avoids brittle cert pinning
  client.setTimeout(kSocketTimeoutMs); // ESP32 Arduino's setTimeout() takes milliseconds

  if (!client.connect(kHost, kPort)) {
    Serial.println("[GTFS] TLS connect failed");
    return false;
  }

  HttpBodyReader reader = {};
  reader.client = &client;
  if (!sendRequestAndSkipHeaders(&client, &reader)) {
    client.stop();
    return false;
  }

  uint8_t *body = nullptr;
  size_t bodySize = 0;
  bool fetched = fetchBodyToBuffer(&reader, &body, &bodySize);
  client.stop();
  if (!fetched) {
    Serial.println("[GTFS] failed to fetch feed body");
    return false;
  }

  pb_istream_t stream = pb_istream_from_buffer(body, bodySize);

  transit_realtime_FeedMessage message = transit_realtime_FeedMessage_init_zero;
  static ResultCollector results;
  memset(&results, 0, sizeof(results));
  message.entity.funcs.decode = decodeEntityCb;
  message.entity.arg = &results;

  bool ok = pb_decode(&stream, transit_realtime_FeedMessage_fields, &message);
  Serial.printf("[GTFS] decoded %u bytes: %s\n", (unsigned)bodySize, ok ? "OK" : PB_GET_ERROR(&stream));
  heap_caps_free(body);

  if (!ok) {
    return false;
  }

  AppState_Lock();
  for (size_t i = 0; i < NUM_ROUTES; i++) {
    g_appState.routes[i].count = (uint8_t)results.etaCount[i];
    for (int j = 0; j < results.etaCount[i]; j++) {
      g_appState.routes[i].etaEpoch[j] = results.etas[i][j];
    }
  }
  g_appState.consecutiveFailures = 0;
  g_appState.lastSuccessMillis = millis();
  g_appState.everSucceeded = true;
  AppState_Unlock();
  return true;
}

static void gtfsTask(void *param) {
  (void)param;
  for (;;) {
    bool ok = false;
    if (WiFi.status() == WL_CONNECTED) {
      ok = pollOnce();
    } else {
      Serial.println("[GTFS] WiFi not connected, skipping poll");
    }

    if (!ok) {
      AppState_Lock();
      g_appState.consecutiveFailures++;
      if (AppState_IsDataStale(g_appState)) {
        // 3rd consecutive failure: the last known arrival times can no longer
        // be trusted (the vehicle may have already come and gone) - clear them.
        for (size_t i = 0; i < NUM_ROUTES; i++) g_appState.routes[i].count = 0;
      }
      AppState_Unlock();
    }

    vTaskDelay(pdMS_TO_TICKS(kPollIntervalMs));
  }
}

void Gtfs_StartPollingTask() {
  xTaskCreatePinnedToCore(gtfsTask, "gtfs_poll", 12288, nullptr, 1, nullptr, 0);
}

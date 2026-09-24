#pragma once
// Fetches a GTFS-realtime Trip Updates feed over HTTPS, de-chunks it into a
// PSRAM buffer, then decodes the protobuf via nanopb from that buffer (see
// GtfsClient.cpp for why streaming decode of an unknown-length HTTP body
// doesn't work reliably with this nanopb version).

// Starts the background FreeRTOS task that polls the feed every 15s and
// writes results into g_appState (AppState.h).
void Gtfs_StartPollingTask();

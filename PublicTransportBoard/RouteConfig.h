#pragma once
// Route/stop mapping — EDIT THIS for your own stops before building.
//
// The values below are placeholders, not real Sofia data. To fill in your
// own:
//   1. Download your city's static GTFS feed (routes.txt + stops.txt) — for
//      Sofia this is https://gtfs.sofiatraffic.bg/api/v1/otp/routers/default/
//      or the operator's published static GTFS zip.
//   2. Find the stop_id in stops.txt for the physical stop you want ETAs for.
//   3. Find the route_id + route_color in routes.txt for the bus/tram line
//      that serves that stop.
//   4. displayNumber is just what's shown on screen — it doesn't have to
//      match the raw GTFS route_id (Sofia's feed uses internal codes like
//      "A84" for what riders call "bus 204").
//
// Display order matters: routes are cycled in the array order below, so
// group related routes (e.g. both directions of the same line) adjacently.

#include <stdint.h>

enum VehicleKind { VEHICLE_BUS, VEHICLE_TRAM };

struct RouteDef {
  const char *gtfsRouteId; // route_id in the GTFS-realtime feed
  const char *gtfsStopId;  // stop_id this route is tracked at
  uint16_t displayNumber;  // the number shown on screen (mapped, not the raw GTFS id)
  VehicleKind kind;
  uint32_t colorHex; // official GTFS route_color (routes.txt), 0xRRGGBB
};

// EXAMPLE placeholder values — replace with your own stop_id/route_id/color
// from your city's static GTFS feed (see instructions above).
static const RouteDef kRoutes[] = {
    {"ROUTE_1", "STOP_1", 1, VEHICLE_BUS, 0xBE1E2D},
    {"ROUTE_2", "STOP_1", 2, VEHICLE_BUS, 0x27AAE1},
    {"ROUTE_3", "STOP_2", 3, VEHICLE_TRAM, 0xF7941D},
    {"ROUTE_4", "STOP_2", 4, VEHICLE_TRAM, 0xF7941D},
};

#define NUM_ROUTES (sizeof(kRoutes) / sizeof(kRoutes[0]))
#define MAX_ETAS_PER_ROUTE 3

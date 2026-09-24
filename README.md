# Public Transport Board

A small ESP32-S3 desk display showing live bus/tram arrival times, fetched
directly from a city's GTFS-realtime feed and decoded on-device — no phone
app, no server/proxy in between.

Built and tested against Sofia, Bulgaria's public GTFS-realtime feed, but the
route/stop configuration is generic — point it at any GTFS-realtime Trip
Updates feed and fill in your own stop IDs.

## Features

- Fetches a GTFS-realtime feed over HTTPS every 15s and decodes the protobuf
  on-device (nanopb) — no cloud service or middleman server required.
- Full-screen carousel: one route at a time, cycling automatically every 10s
  (or immediately on tap), showing the soonest arrival large and the next two
  upcoming arrivals smaller underneath.
- Local clock (NTP-synced at boot, timezone-aware) and a "NO CONNECTION"
  indicator with a stale-data grace window if the feed becomes unreachable.
- Vehicle sprites (bus/tram) with idle-bob and slide-off animations.

## Hardware

- [Waveshare ESP32-S3-Touch-LCD-2.8-V2](https://www.waveshare.com/esp32-s3-touch-lcd-2.8.htm)
  (320x240 landscape ST7789 LCD, CST328/CST3530 capacitive touch, ESP32-S3
  with 8MB PSRAM / 16MB flash).
- **Note:** this exact board revision ("2.8-V2") differs in pin wiring from
  the older "2.8B" revision Waveshare also sells — the display/touch/I2C
  driver code here (`Display_ST7789.*`, `Touch_CST328.*`, `Touch_CST3530.*`,
  `I2C_Driver.*`, `LVGL_Driver.*`) is wired for the -V2 board specifically. If
  you're on different hardware, these are the files you'll need to adapt or
  replace; everything else (`GtfsClient`, `RouteConfig`, `UiScreens`,
  `AppState`, `TimeManager`) is display-driver-agnostic.

## Setup

### 1. Arduino IDE / libraries

- Install the [`arduino-esp32`](https://github.com/espressif/arduino-esp32)
  board package for ESP32-S3.
- This project uses [LVGL](https://lvgl.io/) v8.3.x. Install it into your
  Arduino `libraries/` folder and make sure `lv_conf.h` (inside
  `lvgl/src/`) has these enabled (both off by default in stock `lv_conf.h`):
  ```c
  #define LV_FONT_MONTSERRAT_24  1
  #define LV_FONT_UNSCII_16      1
  ```
- In the Arduino IDE **Tools** menu, set:
  - **USB CDC On Boot** → Enabled
  - **PSRAM** → OPI PSRAM
  - **Flash Size** → 16MB (match your board)
  - **Partition Scheme** → Huge APP (3MB No OTA/1MB SPIFFS) — the default
    1.3MB app partition is **too small** once WiFi/TLS is linked in.

  Or with `arduino-cli`:
  ```bash
  arduino-cli compile \
    --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=opi,FlashSize=16M,PartitionScheme=huge_app" \
    -u -p /dev/ttyACM0 PublicTransportBoard
  ```

### 2. WiFi credentials

```bash
cp PublicTransportBoard/secrets.h.example PublicTransportBoard/secrets.h
```
Edit `secrets.h` with your WiFi SSID/password. This file is gitignored —
never commit it. **Note:** ESP32/ESP32-S3 only has a 2.4GHz radio; it cannot
join a 5GHz-only network.

### 3. Route/stop configuration

Edit `PublicTransportBoard/RouteConfig.h` — the shipped values are
placeholders. See the comment block at the top of that file for how to find
your own `stop_id`/`route_id`/`route_color` from your city's static GTFS
feed (`stops.txt`/`routes.txt`).

If your city's GTFS-realtime endpoint differs from Sofia's, update `kHost`
(and the request path) in `GtfsClient.cpp`. If you're not in the
Europe/Sofia timezone, update the POSIX TZ string in `TimeManager.cpp`.

### 4. Build and flash

See the `arduino-cli` command above, or open `PublicTransportBoard.ino` in
the Arduino IDE with the Tools settings from step 1 and hit Upload.

## Architecture

- `PublicTransportBoard.ino` — `setup()`/`loop()`, WiFi connect + a
  background reconnect watchdog task.
- `GtfsClient.h/.cpp` — HTTPS fetch (de-chunks the response into a PSRAM
  buffer) + nanopb decode, as its own FreeRTOS task polling every 15s.
- `AppState.h/.cpp` — shared state between the polling task and the UI task,
  mutex-guarded.
- `TimeManager.h/.cpp` — NTP sync, timezone, HH:MM formatting.
- `UiScreens.h/.cpp` — the LVGL carousel UI.
- `Sprites.h/.cpp`, `VehicleImages.h/.cpp` — vehicle artwork and animation.
- `RouteConfig.h` — your route/stop mapping, display order, and colors
  (single source of truth).
- `pb.h`, `pb_common.*`, `pb_decode.*`, `gtfs-realtime.pb.*` — nanopb runtime
  + generated code for a **trimmed** copy of the GTFS-realtime spec
  (`gtfs-realtime.proto` in this folder), keeping only the
  FeedMessage/FeedEntity/TripUpdate/StopTimeUpdate fields actually read
  (`route_id`, `stop_id`, `arrival.time`, `departure.time`). Field numbers
  match the real spec, so a real feed still decodes correctly — protobuf
  decoders skip unknown field numbers on the wire.

## Known limitations

- Some GTFS-realtime feeds (Sofia's included) declare `arrival.delay`,
  `vehicle.id`/`vehicle.label`, and `schedule_relationship` in the schema but
  don't actually populate them in practice. If your feed doesn't populate
  these either, don't expect a delay/vehicle/cancellation indicator to show
  real data — this build only relies on `route_id`, `stop_id`, and
  arrival/departure `time`, which were reliably present.
- WiFi credentials and route/stop IDs are hardcoded at compile time (no
  runtime config UI) — this is a single-purpose device for one home network.

## Credits / third-party code

- [nanopb](https://github.com/nanopb/nanopb) (zlib license) — the protobuf
  runtime (`pb.h`, `pb_common.*`, `pb_decode.*`) and code generator used to
  produce `gtfs-realtime.pb.*` from the trimmed `.proto`.
- Field definitions in `gtfs-realtime.proto` are trimmed from the
  [GTFS-realtime specification](https://gtfs.org/documentation/realtime/reference/)
  (Apache-2.0, Google/MobilityData).

## License

MIT — see [LICENSE](LICENSE).

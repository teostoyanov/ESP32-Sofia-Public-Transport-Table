#pragma once
// Vehicle bitmap placeholders, sourced from a user-supplied `images.bin`
// (raw RGB565, big-endian pixel pairs, no header: a 128px-wide canvas with
// the bus stacked above the tram). Cropped to each vehicle's content bounds
// and re-emitted here as plain lv_img_dsc_t data so no LVGL filesystem/SD
// card is needed - the pixel data ships in flash like the rest of the UI.
#include <lvgl.h>

extern const lv_img_dsc_t g_busImage;
extern const lv_img_dsc_t g_tramImage;

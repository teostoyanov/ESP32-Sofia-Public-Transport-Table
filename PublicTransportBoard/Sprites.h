#pragma once
#include <lvgl.h>
#include "RouteConfig.h"

// Builds the bus/tram vehicle image (from VehicleImages.h) as a child of
// `parent`, sized to the source bitmap. Returns the lv_img object so the
// caller can position/animate it.
lv_obj_t *Sprite_Build(lv_obj_t *parent, VehicleKind kind);

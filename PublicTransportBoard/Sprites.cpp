#include "Sprites.h"
#include "VehicleImages.h"

lv_obj_t *Sprite_Build(lv_obj_t *parent, VehicleKind kind) {
  lv_obj_t *img = lv_img_create(parent);
  lv_img_set_src(img, kind == VEHICLE_TRAM ? &g_tramImage : &g_busImage);
  // Decorative only - let taps pass through to the screen-level tap-to-advance handler.
  lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
  return img;
}

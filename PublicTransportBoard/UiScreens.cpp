#include "UiScreens.h"
#include "Display_ST7789.h"
#include "AppState.h"
#include "RouteConfig.h"
#include "TimeManager.h"
#include "Sprites.h"

#include <time.h>
#include <string.h>
#include <stdio.h>

#define SCREEN_W LCD_WIDTH
#define SCREEN_H LCD_HEIGHT
#define DWELL_MS 10000

// Landscape card: sprite+title centered as a block in the left half, ETAs
// centered in the right half.
#define RIGHT_DX  (SCREEN_W / 4)

#define BG_COLOR      0xFFFFFF
#define TEXT_PRIMARY  0x101418
#define TEXT_MUTED    0x5B6472
#define DIVIDER_COLOR 0xD0D0D0

// Clock row + divider live above this; the sprite+title block (sprite on
// top, title below, both horizontally centered in the left column) is
// vertically centered between here and the bottom margin.
#define CONTENT_TOP 40
#define CONTENT_BOTTOM_MARGIN 10
#define TITLE_ROW_GAP 10

#define DIVIDER_Y 28
#define DIVIDER_H 2

struct RouteCardWidgets {
  lv_obj_t *bigEta;
  lv_obj_t *smallEta;
  lv_obj_t *sprite;
};

#define BOUNCE_AMPLITUDE_PX 6
#define BOUNCE_HALF_PERIOD_MS 900
#define EXIT_ANIM_MS 300

// Vertical idle bob, started once per card and left running forever (cheap -
// only 4 objects, and it doesn't matter that off-screen cards keep animating).
static void startBounce(lv_obj_t *sprite, lv_coord_t restY) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, sprite);
  lv_anim_set_values(&a, restY, restY - BOUNCE_AMPLITUDE_PX);
  lv_anim_set_time(&a, BOUNCE_HALF_PERIOD_MS);
  lv_anim_set_playback_time(&a, BOUNCE_HALF_PERIOD_MS);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
  lv_anim_start(&a);
}

// Snaps a sprite back to its resting x once its "drive off" animation
// finishes - invisible to the user since by then the whole card has scrolled
// out of view too, and it's what makes the card reusable next time it cycles
// back into the carousel.
static void spriteExitReady(lv_anim_t *a) {
  lv_obj_t *sprite = (lv_obj_t *)a->var;
  lv_coord_t restX = (lv_coord_t)(intptr_t)lv_obj_get_user_data(sprite);
  lv_obj_set_x(sprite, restX);
}

// Animates a card's vehicle sprite driving off the right edge of the screen,
// simulating it pulling away as the carousel advances to the next route.
static void startExitRight(lv_obj_t *sprite) {
  lv_coord_t curX = lv_obj_get_x(sprite);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, sprite);
  lv_anim_set_values(&a, curX, curX + SCREEN_W);
  lv_anim_set_time(&a, EXIT_ANIM_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
  lv_anim_set_ready_cb(&a, spriteExitReady);
  lv_anim_start(&a);
}

static RouteCardWidgets s_cards[NUM_ROUTES];
static lv_obj_t *s_carousel;
static lv_obj_t *s_clockLabel;
static lv_obj_t *s_bannerLabel;
static int s_activeIndex = 0;
static uint32_t s_lastAdvanceMs = 0;

static void goToIndex(int idx, bool animate) {
  int prevIndex = s_activeIndex;
  s_activeIndex = ((idx % (int)NUM_ROUTES) + (int)NUM_ROUTES) % (int)NUM_ROUTES;
  int targetX = -s_activeIndex * SCREEN_W;

  if (animate) {
    if (prevIndex != s_activeIndex) {
      startExitRight(s_cards[prevIndex].sprite);
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_carousel);
    lv_anim_set_values(&a, lv_obj_get_x(s_carousel), targetX);
    lv_anim_set_time(&a, 350);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
  } else {
    lv_obj_set_x(s_carousel, targetX);
  }
  s_lastAdvanceMs = millis();
}

static void onScreenClicked(lv_event_t *e) {
  (void)e;
  goToIndex(s_activeIndex + 1, true);
}

static void buildCard(int index) {
  const RouteDef &route = kRoutes[index];

  lv_obj_t *card = lv_obj_create(s_carousel);
  lv_obj_remove_style_all(card);
  lv_obj_set_pos(card, index * SCREEN_W, 0);
  lv_obj_set_size(card, SCREEN_W, SCREEN_H);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(card, lv_color_hex(BG_COLOR), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);

  const char *kindWord = (route.kind == VEHICLE_TRAM) ? "Tram" : "Bus";
  char title[24];
  snprintf(title, sizeof(title), "%s %u", kindWord, route.displayNumber);

  lv_obj_t *sprite = Sprite_Build(card, route.kind);

  lv_obj_t *titleLabel = lv_label_create(card);
  lv_obj_set_style_text_font(titleLabel, &lv_font_unscii_16, 0);
  lv_obj_set_style_text_color(titleLabel, lv_color_hex(route.colorHex), 0);
  lv_label_set_text(titleLabel, title);

  // lv_obj_align()/lv_label auto-sizing only resolve on the next layout
  // pass, not synchronously - force it now so the get_width/height() calls
  // below see real sizes instead of stale pre-layout coords.
  lv_obj_update_layout(card);

  lv_coord_t spriteW = lv_obj_get_width(sprite);
  lv_coord_t spriteH = lv_obj_get_height(sprite);
  lv_coord_t titleW = lv_obj_get_width(titleLabel);
  lv_coord_t titleH = lv_obj_get_height(titleLabel);

  // Sprite above, title below, both horizontally centered in the left
  // column, the pair vertically centered as a unit below the divider.
  lv_coord_t colCenterX = SCREEN_W / 4;
  lv_coord_t blockH = spriteH + TITLE_ROW_GAP + titleH;
  lv_coord_t availH = (SCREEN_H - CONTENT_BOTTOM_MARGIN) - CONTENT_TOP;
  lv_coord_t blockTop = CONTENT_TOP + (availH - blockH) / 2;

  lv_coord_t spriteX = colCenterX - spriteW / 2;
  lv_obj_set_pos(sprite, spriteX, blockTop);
  lv_obj_set_user_data(sprite, (void *)(intptr_t)spriteX);
  startBounce(sprite, blockTop);
  s_cards[index].sprite = sprite;

  lv_obj_set_pos(titleLabel, colCenterX - titleW / 2, blockTop + spriteH + TITLE_ROW_GAP);

  lv_obj_t *bigEta = lv_label_create(card);
  lv_obj_set_style_text_font(bigEta, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(bigEta, lv_color_hex(TEXT_PRIMARY), 0);
  lv_label_set_text(bigEta, "...");
  lv_obj_align(bigEta, LV_ALIGN_TOP_MID, RIGHT_DX, 88);

  lv_obj_t *smallEta = lv_label_create(card);
  lv_obj_set_style_text_font(smallEta, &lv_font_unscii_16, 0);
  lv_obj_set_style_text_color(smallEta, lv_color_hex(TEXT_MUTED), 0);
  lv_label_set_text(smallEta, "");
  lv_obj_align(smallEta, LV_ALIGN_TOP_MID, RIGHT_DX, 152);

  s_cards[index].bigEta = bigEta;
  s_cards[index].smallEta = smallEta;
}

static void tickCb(lv_timer_t *t) {
  (void)t;

  if (Time_IsSynced()) {
    char hhmm[6];
    Time_GetHHMM(hhmm, sizeof(hhmm));
    lv_label_set_text(s_clockLabel, hhmm);
  }

  AppState snapshot;
  AppState_Lock();
  snapshot = g_appState;
  AppState_Unlock();

  if (snapshot.consecutiveFailures > 0) {
    lv_obj_clear_flag(s_bannerLabel, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(s_bannerLabel, LV_OBJ_FLAG_HIDDEN);
  }

  time_t now = time(nullptr);
  for (size_t i = 0; i < NUM_ROUTES; i++) {
    RouteArrivals &ra = snapshot.routes[i];
    if (ra.count == 0) {
      lv_label_set_text(s_cards[i].bigEta, snapshot.everSucceeded ? "--" : "...");
      lv_label_set_text(s_cards[i].smallEta, "");
      continue;
    }

    long mins0 = (long)((ra.etaEpoch[0] - (int64_t)now) / 60);
    if (mins0 < 0) mins0 = 0;
    char big[12];
    snprintf(big, sizeof(big), "%ld min", mins0);
    lv_label_set_text(s_cards[i].bigEta, big);

    char small[40] = "";
    for (int j = 1; j < ra.count; j++) {
      long m = (long)((ra.etaEpoch[j] - (int64_t)now) / 60);
      if (m < 0) m = 0;
      char part[16];
      snprintf(part, sizeof(part), "%s%ldm", (j > 1 ? ", " : ""), m);
      strncat(small, part, sizeof(small) - strlen(small) - 1);
    }
    lv_label_set_text(s_cards[i].smallEta, small);
  }

  if (millis() - s_lastAdvanceMs >= DWELL_MS) {
    goToIndex(s_activeIndex + 1, true);
  }
}

void Ui_Init() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(BG_COLOR), 0);
  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scr, onScreenClicked, LV_EVENT_CLICKED, nullptr);

  s_carousel = lv_obj_create(scr);
  lv_obj_remove_style_all(s_carousel);
  lv_obj_set_pos(s_carousel, 0, 0);
  lv_obj_set_size(s_carousel, SCREEN_W * NUM_ROUTES, SCREEN_H);
  lv_obj_clear_flag(s_carousel, LV_OBJ_FLAG_CLICKABLE);

  for (size_t i = 0; i < NUM_ROUTES; i++) buildCard((int)i);

  s_clockLabel = lv_label_create(scr);
  lv_obj_set_style_text_font(s_clockLabel, &lv_font_unscii_16, 0);
  lv_obj_set_style_text_color(s_clockLabel, lv_color_hex(TEXT_PRIMARY), 0);
  lv_label_set_text(s_clockLabel, "--:--");
  lv_obj_align(s_clockLabel, LV_ALIGN_TOP_LEFT, 8, 6);

  lv_obj_t *divider = lv_obj_create(scr);
  lv_obj_remove_style_all(divider);
  lv_obj_set_pos(divider, 0, DIVIDER_Y);
  lv_obj_set_size(divider, SCREEN_W, DIVIDER_H);
  lv_obj_set_style_bg_color(divider, lv_color_hex(DIVIDER_COLOR), 0);
  lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
  lv_obj_clear_flag(divider, LV_OBJ_FLAG_CLICKABLE);

  s_bannerLabel = lv_label_create(scr);
  lv_obj_set_style_text_font(s_bannerLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_bannerLabel, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_color(s_bannerLabel, lv_color_hex(0xCC2222), 0);
  lv_obj_set_style_bg_opa(s_bannerLabel, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(s_bannerLabel, 6, 0);
  lv_obj_set_style_pad_ver(s_bannerLabel, 2, 0);
  lv_obj_set_style_radius(s_bannerLabel, 3, 0);
  lv_label_set_text(s_bannerLabel, "NO CONNECTION");
  lv_obj_align(s_bannerLabel, LV_ALIGN_TOP_RIGHT, -8, 4);
  lv_obj_add_flag(s_bannerLabel, LV_OBJ_FLAG_HIDDEN);

  s_activeIndex = 0;
  s_lastAdvanceMs = millis();
  lv_obj_set_x(s_carousel, 0);

  lv_timer_create(tickCb, 500, nullptr);
}

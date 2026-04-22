#include <pebble.h>

#define SCREEN_W 200
#define SCREEN_H 228

// ── Layout zones ───────────────────────────────────────────────────────────
#define SKY_TOP        0
#define SKY_BOTTOM     85
#define CITY_BOTTOM    180
#define STREET_BOTTOM  208
// y=208-228: info bar

// ── Billboard (sky zone) ───────────────────────────────────────────────────
#define BB_X      32
#define BB_Y      10
#define BB_W      136
#define BB_H      66
#define BB_FX     (BB_X + 4)
#define BB_FY     (BB_Y + 4)
#define BB_FW     (BB_W - 8)
#define BB_FH     (BB_H - 8)
#define BB_CX     (BB_X + BB_W / 2)
#define BB_CY     (BB_Y + BB_H / 2)
#define BB_POLE_L (BB_X + 22)
#define BB_POLE_R (BB_X + BB_W - 22)

// ── Persist keys ──────────────────────────────────────────────────────────
#define PERSIST_CLOCK_STYLE  100
#define PERSIST_TEMP_UNIT    101
#define PERSIST_HOUR_FORMAT  102
#define PERSIST_ANIMATIONS   103

// ── State ──────────────────────────────────────────────────────────────────
static Window      *s_window;
static Layer       *s_scene_layer;
static TextLayer   *s_time_layer;
static TextLayer   *s_info_layer;
static GFont        s_time_font;
static GFont        s_info_font;

static char s_time_buf[8];
static char s_info_buf[48];

static int  s_temp    = -999;
static int  s_wx_code = -1;
static int  s_sunrise = -1;
static int  s_sunset  = -1;

static int  s_hour   = 12;
static int  s_minute = 0;
static int  s_frame  = 0;

static int  s_clock_style = 0;
static int  s_temp_unit   = 0;
static bool s_hour_format = false;
static bool s_animations  = true;

// Rush hour (wrist tap)
#define RUSH_FRAMES 50
static bool      s_rush_active = false;
static int       s_rush_frame  = 0;
static AppTimer *s_rush_timer  = NULL;

// ── Weather helpers ────────────────────────────────────────────────────────
enum WxCat { WX_UNKNOWN, WX_CLEAR, WX_CLOUDY, WX_RAIN, WX_SNOW, WX_FOG, WX_STORM };

static int wx_category(int code) {
  if (code < 0)  return WX_UNKNOWN;
  if (code == 0) return WX_CLEAR;
  if (code <= 3) return WX_CLOUDY;
  if (code <= 48) return WX_FOG;
  if (code <= 67) return WX_RAIN;
  if (code <= 77) return WX_SNOW;
  if (code <= 82) return WX_RAIN;
  if (code <= 86) return WX_SNOW;
  return WX_STORM;
}

static bool is_night(void) {
  if (s_sunrise < 0 || s_sunset < 0) return (s_hour < 6 || s_hour >= 20);
  int now_min = s_hour * 60 + s_minute;
  return (now_min < s_sunrise || now_min >= s_sunset);
}

static GColor sky_color(void) {
  int now_min = s_hour * 60 + s_minute;
  int sr = s_sunrise >= 0 ? s_sunrise : 6 * 60;
  int ss = s_sunset  >= 0 ? s_sunset  : 20 * 60;
  if (now_min < sr - 30 || now_min >= ss + 30) return GColorOxfordBlue;
  if (now_min < sr + 30)  return GColorOrange;
  if (now_min >= ss - 30) return GColorOrange;
  return GColorVividCerulean;
}

// ── Deterministic PRNG ─────────────────────────────────────────────────────
static uint32_t rng_seed = 1;
static void rng_set(uint32_t s) { rng_seed = s ? s : 1; }
static uint32_t rng_next(void) {
  rng_seed = (rng_seed * 1664525u) + 1013904223u;
  return rng_seed;
}

// ── Draw: sky ──────────────────────────────────────────────────────────────
static void draw_sky(GContext *ctx) {
  graphics_context_set_fill_color(ctx, sky_color());
  graphics_fill_rect(ctx, GRect(0, SKY_TOP, SCREEN_W, SKY_BOTTOM), 0, GCornerNone);

  bool night = is_night();

  if (night) {
    rng_set(20250419 + (s_hour * 60 + s_minute) / 60);
    for (int i = 0; i < 40; i++) {
      int x = rng_next() % SCREEN_W;
      int y = rng_next() % (SKY_BOTTOM - 4) + 2;
      GColor c = (i % 3 == 0) ? GColorLightGray : GColorWhite;
      graphics_context_set_stroke_color(ctx, c);
      graphics_draw_pixel(ctx, GPoint(x, y));
      if (i % 7 == 0) {
        graphics_draw_pixel(ctx, GPoint(x + 1, y));
        graphics_draw_pixel(ctx, GPoint(x - 1, y));
        graphics_draw_pixel(ctx, GPoint(x, y + 1));
        graphics_draw_pixel(ctx, GPoint(x, y - 1));
      }
    }
  }

  int now_min  = s_hour * 60 + s_minute;
  int sr       = s_sunrise >= 0 ? s_sunrise : 6 * 60;
  int ss       = s_sunset  >= 0 ? s_sunset  : 20 * 60;
  int day_len  = ss - sr;
  int night_len = (24 * 60) - day_len;

  GPoint orb_pt;
  GColor orb_color;
  if (!night && day_len > 0) {
    int frac = ((now_min - sr) * 1000) / day_len;
    int x = (SCREEN_W * frac) / 1000;
    int y = 60 - ((frac < 500 ? frac : 1000 - frac) * 45 / 500);
    orb_pt = GPoint(x, y + 10);
    orb_color = GColorYellow;
  } else if (night_len > 0) {
    int night_min = (now_min >= ss) ? (now_min - ss) : (now_min + 24 * 60 - ss);
    int frac = (night_min * 1000) / night_len;
    int x = (SCREEN_W * frac) / 1000;
    int y = 55 - ((frac < 500 ? frac : 1000 - frac) * 40 / 500);
    orb_pt = GPoint(x, y + 10);
    orb_color = GColorWhite;
  } else {
    orb_pt = GPoint(SCREEN_W / 2, 30);
    orb_color = GColorWhite;
  }
  graphics_context_set_fill_color(ctx, orb_color);
  graphics_fill_circle(ctx, orb_pt, 9);

  int wx = wx_category(s_wx_code);
  int n_clouds = (wx == WX_CLOUDY || wx == WX_RAIN || wx == WX_STORM || wx == WX_SNOW) ? 5 : 2;
  graphics_context_set_fill_color(ctx, night ? GColorLightGray : GColorWhite);
  for (int i = 0; i < n_clouds; i++) {
    int drift = (s_frame * (3 + i)) % (SCREEN_W + 40);
    int cx = (drift - 20 + i * 60) % (SCREEN_W + 40) - 20;
    int cy = 8 + (i * 9) % 14;
    graphics_fill_circle(ctx, GPoint(cx, cy), 7);
    graphics_fill_circle(ctx, GPoint(cx + 8, cy - 2), 8);
    graphics_fill_circle(ctx, GPoint(cx + 15, cy + 1), 7);
  }

  if (wx == WX_RAIN || wx == WX_STORM) {
    graphics_context_set_stroke_color(ctx, GColorCeleste);
    for (int i = 0; i < 30; i++) {
      int x = (i * 13 + s_frame * 7) % SCREEN_W;
      int y = 30 + (i * 17 + s_frame * 11) % (SKY_BOTTOM - 35);
      graphics_draw_line(ctx, GPoint(x, y), GPoint(x - 2, y + 5));
    }
  }

  if (wx == WX_SNOW) {
    graphics_context_set_fill_color(ctx, GColorWhite);
    for (int i = 0; i < 35; i++) {
      int x = (i * 17 + s_frame * 5) % SCREEN_W;
      int y = 25 + (i * 19 + s_frame * 9) % (SKY_BOTTOM - 30);
      graphics_fill_rect(ctx, GRect(x, y, 2, 2), 0, GCornerNone);
    }
  }

  if (wx == WX_STORM && (s_frame % 4) == 0) {
    graphics_context_set_stroke_color(ctx, GColorYellow);
    int bx = 50 + (s_frame * 23) % 100;
    graphics_draw_line(ctx, GPoint(bx, 20),   GPoint(bx - 4, 40));
    graphics_draw_line(ctx, GPoint(bx - 4, 40), GPoint(bx + 2, 45));
    graphics_draw_line(ctx, GPoint(bx + 2, 45), GPoint(bx - 3, 65));
  }
}

// ── Draw: billboard ────────────────────────────────────────────────────────
static void draw_billboard(GContext *ctx) {
  // Support poles from billboard bottom to SKY_BOTTOM (buildings will overdraw the ends)
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  int pole_h = SKY_BOTTOM - (BB_Y + BB_H) + 6;
  graphics_fill_rect(ctx, GRect(BB_POLE_L - 1, BB_Y + BB_H, 3, pole_h), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(BB_POLE_R - 1, BB_Y + BB_H, 3, pole_h), 0, GCornerNone);

  // Outer frame
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, GRect(BB_X, BB_Y, BB_W, BB_H), 3, GCornersAll);

  // Inner face
  graphics_context_set_fill_color(ctx, GColorPastelYellow);
  graphics_fill_rect(ctx, GRect(BB_FX, BB_FY, BB_FW, BB_FH), 2, GCornersAll);

  // Borders
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_round_rect(ctx, GRect(BB_X, BB_Y, BB_W, BB_H), 3);
  graphics_draw_round_rect(ctx, GRect(BB_FX, BB_FY, BB_FW, BB_FH), 2);

  if (s_clock_style == 0) {
    int cx = BB_CX;
    int cy = BB_CY;
    int r  = BB_FH / 2 - 2;

    // Clock circle background
    graphics_context_set_fill_color(ctx, GColorPastelYellow);
    graphics_fill_circle(ctx, GPoint(cx, cy), r);
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_draw_circle(ctx, GPoint(cx, cy), r);

    // Cardinal ticks
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_draw_line(ctx, GPoint(cx,     cy - r + 1), GPoint(cx,     cy - r + 3));
    graphics_draw_line(ctx, GPoint(cx + r - 1, cy),     GPoint(cx + r - 3, cy));
    graphics_draw_line(ctx, GPoint(cx,     cy + r - 1), GPoint(cx,     cy + r - 3));
    graphics_draw_line(ctx, GPoint(cx - r + 1, cy),     GPoint(cx - r + 3, cy));

    // Minute hand (2px thick)
    int min_angle = TRIG_MAX_ANGLE * s_minute / 60;
    int min_ex = cx + (sin_lookup(min_angle) * (r - 2) / TRIG_MAX_RATIO);
    int min_ey = cy - (cos_lookup(min_angle) * (r - 2) / TRIG_MAX_RATIO);
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_draw_line(ctx, GPoint(cx,     cy), GPoint(min_ex,     min_ey));
    graphics_draw_line(ctx, GPoint(cx + 1, cy), GPoint(min_ex + 1, min_ey));

    // Hour hand (3px thick)
    int hr_angle = TRIG_MAX_ANGLE * ((s_hour % 12) * 60 + s_minute) / (12 * 60);
    int hr_ex = cx + (sin_lookup(hr_angle) * (r * 2 / 3) / TRIG_MAX_RATIO);
    int hr_ey = cy - (cos_lookup(hr_angle) * (r * 2 / 3) / TRIG_MAX_RATIO);
    graphics_draw_line(ctx, GPoint(cx - 1, cy), GPoint(hr_ex - 1, hr_ey));
    graphics_draw_line(ctx, GPoint(cx,     cy), GPoint(hr_ex,     hr_ey));
    graphics_draw_line(ctx, GPoint(cx + 1, cy), GPoint(hr_ex + 1, hr_ey));

    // Center dot
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, GPoint(cx, cy), 2);
  }
  // Digital mode: TextLayer renders over the face
}

// ── Draw: city (12 full-width buildings, no tower) ─────────────────────────
typedef struct {
  int16_t x, w, h;
  GColor  color;
} Building;

static const Building s_buildings[] = {
  {   0, 16, 85, { .argb = GColorDarkGrayARGB8       } },
  {  16, 14, 72, { .argb = GColorImperialPurpleARGB8 } },
  {  30, 18, 92, { .argb = GColorOxfordBlueARGB8     } },
  {  48, 14, 65, { .argb = GColorDarkGrayARGB8       } },
  {  62, 16, 80, { .argb = GColorImperialPurpleARGB8 } },
  {  78, 20, 90, { .argb = GColorDarkGrayARGB8       } },
  {  98, 16, 68, { .argb = GColorOxfordBlueARGB8     } },
  { 114, 20, 95, { .argb = GColorImperialPurpleARGB8 } },
  { 134, 16, 80, { .argb = GColorDarkGrayARGB8       } },
  { 150, 14, 68, { .argb = GColorOxfordBlueARGB8     } },
  { 164, 18, 85, { .argb = GColorImperialPurpleARGB8 } },
  { 182, 18, 75, { .argb = GColorDarkGrayARGB8       } },
};
#define N_BUILDINGS (sizeof(s_buildings) / sizeof(s_buildings[0]))

static void draw_city(GContext *ctx) {
  bool night = is_night();

  for (size_t i = 0; i < N_BUILDINGS; i++) {
    Building b = s_buildings[i];
    int top = CITY_BOTTOM - b.h;
    graphics_context_set_fill_color(ctx, b.color);
    graphics_fill_rect(ctx, GRect(b.x, top, b.w, b.h), 0, GCornerNone);

    // Windows — 3×4 px, spaced 5×7
    for (int wy = top + 6; wy < CITY_BOTTOM - 4; wy += 7) {
      for (int wx = b.x + 3; wx < b.x + b.w - 3; wx += 5) {
        uint32_t seed = (uint32_t)wx * 31u + (uint32_t)wy * 97u + (uint32_t)(s_hour / 2);
        bool lit = night && ((seed % 10) < 7);
        GColor wc = lit ? (((seed % 5) == 0) ? GColorOrange : GColorYellow) : GColorDarkGray;
        graphics_context_set_fill_color(ctx, wc);
        graphics_fill_rect(ctx, GRect(wx, wy, 3, 4), 0, GCornerNone);
      }
    }
  }
}

// ── Draw: street + rush hour ────────────────────────────────────────────────
typedef struct { int speed; int y; GColor body; bool rtl; } CarSpec;

static void draw_car(GContext *ctx, int cx, int cy, GColor body, bool rtl) {
  int bx = rtl ? cx - 20 : cx;
  graphics_context_set_fill_color(ctx, body);
  graphics_fill_rect(ctx, GRect(bx, cy, 20, 8), 2, GCornersAll);
  graphics_context_set_fill_color(ctx, GColorCeleste);
  graphics_fill_rect(ctx, GRect(bx + 4, cy - 3, 10, 4), 1, GCornersTop);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, GPoint(bx + 4,  cy + 8), 2);
  graphics_fill_circle(ctx, GPoint(bx + 16, cy + 8), 2);
  if (is_night()) {
    graphics_context_set_fill_color(ctx, GColorYellow);
    int hx = rtl ? bx - 2 : bx + 19;
    graphics_fill_rect(ctx, GRect(hx, cy + 2, 2, 2), 0, GCornerNone);
  }
}

static void rush_step(void *data);
static void start_rush_timer(void) {
  if (s_rush_timer) app_timer_cancel(s_rush_timer);
  s_rush_timer = app_timer_register(50, rush_step, NULL);
}

static void draw_street(GContext *ctx) {
  // Sidewalk
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx, GRect(0, CITY_BOTTOM, SCREEN_W, 4), 0, GCornerNone);

  // Road
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, CITY_BOTTOM + 4, SCREEN_W, STREET_BOTTOM - CITY_BOTTOM - 4), 0, GCornerNone);

  // Lane dashes
  graphics_context_set_fill_color(ctx, GColorYellow);
  int lane_y   = CITY_BOTTOM + 16;
  int dash_off = (s_frame * 12) % 20;
  for (int dx = -dash_off; dx < SCREEN_W; dx += 20) {
    graphics_fill_rect(ctx, GRect(dx, lane_y, 12, 2), 0, GCornerNone);
  }

  if (!s_animations) return;

  // Regular cars
  static const CarSpec reg[3] = {
    {  7, CITY_BOTTOM + 7,  { .argb = GColorRedARGB8    }, false },
    { 11, CITY_BOTTOM + 20, { .argb = GColorYellowARGB8 }, false },
    {  5, CITY_BOTTOM + 7,  { .argb = GColorBlueARGB8   }, false },
  };
  for (int i = 0; i < 3; i++) {
    int cx = (s_frame * reg[i].speed + i * 80) % (SCREEN_W + 30) - 15;
    draw_car(ctx, cx, reg[i].y, reg[i].body, false);
  }

  // Rush hour bonus cars (6 fast cars, mixed directions)
  if (s_rush_active) {
    static const CarSpec rush[6] = {
      { 18, CITY_BOTTOM + 6,  { .argb = GColorOrangeARGB8        }, false },
      { 22, CITY_BOTTOM + 19, { .argb = GColorGreenARGB8         }, false },
      { 15, CITY_BOTTOM + 6,  { .argb = GColorWhiteARGB8         }, true  },
      { 20, CITY_BOTTOM + 19, { .argb = GColorRedARGB8           }, true  },
      { 25, CITY_BOTTOM + 6,  { .argb = GColorMagentaARGB8       }, false },
      { 17, CITY_BOTTOM + 19, { .argb = GColorCelesteARGB8       }, true  },
    };
    for (int i = 0; i < 6; i++) {
      int travel = (s_rush_frame * rush[i].speed + i * 45) % (SCREEN_W + 30);
      int cx = rush[i].rtl ? (SCREEN_W + 15 - travel) : (travel - 15);
      draw_car(ctx, cx, rush[i].y, rush[i].body, rush[i].rtl);
    }
  }
}

static void rush_step(void *data) {
  s_rush_frame++;
  layer_mark_dirty(s_scene_layer);
  if (s_rush_frame < RUSH_FRAMES) {
    s_rush_timer = app_timer_register(50, rush_step, NULL);
  } else {
    s_rush_active = false;
    s_rush_frame  = 0;
    s_rush_timer  = NULL;
    layer_mark_dirty(s_scene_layer);
  }
}

// ── Draw: info bar ─────────────────────────────────────────────────────────
static void draw_info_bar(GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, STREET_BOTTOM, SCREEN_W, SCREEN_H - STREET_BOTTOM), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorYellow);
  graphics_fill_rect(ctx, GRect(0, STREET_BOTTOM, SCREEN_W, 1), 0, GCornerNone);
}

// ── Scene update ────────────────────────────────────────────────────────────
static void scene_update(Layer *layer, GContext *ctx) {
  draw_sky(ctx);
  draw_billboard(ctx);
  draw_city(ctx);
  draw_street(ctx);
  draw_info_bar(ctx);
}

// ── Time / info text ────────────────────────────────────────────────────────
static void update_time_text(struct tm *tt) {
  s_hour   = tt->tm_hour;
  s_minute = tt->tm_min;

  if (s_clock_style == 1) {
    if (s_hour_format) {
      snprintf(s_time_buf, sizeof(s_time_buf), "%02d:%02d", tt->tm_hour, tt->tm_min);
    } else {
      int h12 = tt->tm_hour % 12;
      if (h12 == 0) h12 = 12;
      snprintf(s_time_buf, sizeof(s_time_buf), "%d:%02d", h12, tt->tm_min);
    }
    text_layer_set_text(s_time_layer, s_time_buf);
  }
}

static void update_info_text(struct tm *tt) {
  static const char *days[]   = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  static const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                  "Jul","Aug","Sep","Oct","Nov","Dec"};
  if (s_temp > -999) {
    const char *unit = s_temp_unit ? "\xb0" "C" : "\xb0" "F";
    snprintf(s_info_buf, sizeof(s_info_buf), "%s %s %d   %d%s",
             days[tt->tm_wday], months[tt->tm_mon], tt->tm_mday, s_temp, unit);
  } else {
    snprintf(s_info_buf, sizeof(s_info_buf), "%s %s %d",
             days[tt->tm_wday], months[tt->tm_mon], tt->tm_mday);
  }
  text_layer_set_text(s_info_layer, s_info_buf);
}

// ── Tick handler ────────────────────────────────────────────────────────────
static void tick_handler(struct tm *tt, TimeUnits changed) {
  s_frame++;
  update_time_text(tt);
  update_info_text(tt);

  if (tt->tm_min % 30 == 0 && s_wx_code >= -1) {
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
      dict_write_uint8(iter, MESSAGE_KEY_REQUEST_WEATHER, 1);
      app_message_outbox_send();
    }
  }

  layer_mark_dirty(s_scene_layer);
}

// ── Tap handler (rush hour) ─────────────────────────────────────────────────
static void tap_handler(AccelAxisType axis, int32_t direction) {
  if (s_rush_active) return;
  s_rush_active = true;
  s_rush_frame  = 0;
  start_rush_timer();
  vibes_short_pulse();
}

// ── Settings ────────────────────────────────────────────────────────────────
static void apply_settings(void) {
  layer_set_hidden(text_layer_get_layer(s_time_layer), s_clock_style != 1);
}

// ── AppMessage ──────────────────────────────────────────────────────────────
static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t;
  bool settings_changed = false;

  if ((t = dict_find(iter, MESSAGE_KEY_TEMPERATURE))) s_temp    = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_CONDITIONS)))  s_wx_code = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_SUNRISE)))     s_sunrise = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_SUNSET)))      s_sunset  = t->value->int32;

  if ((t = dict_find(iter, MESSAGE_KEY_CLOCK_STYLE))) {
    s_clock_style = t->value->int32;
    persist_write_int(PERSIST_CLOCK_STYLE, s_clock_style);
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_TEMP_UNIT))) {
    s_temp_unit = t->value->int32;
    persist_write_int(PERSIST_TEMP_UNIT, s_temp_unit);
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_HOUR_FORMAT))) {
    s_hour_format = t->value->int32 != 0;
    persist_write_bool(PERSIST_HOUR_FORMAT, s_hour_format);
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_ANIMATIONS))) {
    s_animations = t->value->int32 != 0;
    persist_write_bool(PERSIST_ANIMATIONS, s_animations);
    settings_changed = true;
  }

  if (settings_changed) apply_settings();

  time_t now = time(NULL);
  update_time_text(localtime(&now));
  update_info_text(localtime(&now));
  layer_mark_dirty(s_scene_layer);
}

// ── Window lifecycle ────────────────────────────────────────────────────────
static void window_load(Window *win) {
  Layer *root = window_get_root_layer(win);
  GRect bounds = layer_get_bounds(root);

  if (persist_exists(PERSIST_CLOCK_STYLE)) s_clock_style = persist_read_int(PERSIST_CLOCK_STYLE);
  if (persist_exists(PERSIST_TEMP_UNIT))   s_temp_unit   = persist_read_int(PERSIST_TEMP_UNIT);
  if (persist_exists(PERSIST_HOUR_FORMAT)) s_hour_format = persist_read_bool(PERSIST_HOUR_FORMAT);
  if (persist_exists(PERSIST_ANIMATIONS))  s_animations  = persist_read_bool(PERSIST_ANIMATIONS);

  s_scene_layer = layer_create(bounds);
  layer_set_update_proc(s_scene_layer, scene_update);
  layer_add_child(root, s_scene_layer);

  // Digital clock TextLayer centered in billboard inner face
  // Face: GRect(BB_FX, BB_FY, BB_FW, BB_FH) = GRect(36, 14, 128, 58)
  // LECO_28 is ~28px tall; center: BB_FY + (BB_FH-28)/2 = 14+15 = 29
  s_time_font  = fonts_get_system_font(FONT_KEY_LECO_28_LIGHT_NUMBERS);
  s_time_layer = text_layer_create(GRect(BB_FX, BB_FY + 15, BB_FW, 30));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorBlack);
  text_layer_set_font(s_time_layer, s_time_font);
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(s_scene_layer, text_layer_get_layer(s_time_layer));

  // Info bar — bottom strip
  s_info_font  = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  s_info_layer = text_layer_create(GRect(0, STREET_BOTTOM + 2, SCREEN_W, SCREEN_H - STREET_BOTTOM - 2));
  text_layer_set_background_color(s_info_layer, GColorClear);
  text_layer_set_text_color(s_info_layer, GColorWhite);
  text_layer_set_font(s_info_layer, s_info_font);
  text_layer_set_text_alignment(s_info_layer, GTextAlignmentCenter);
  layer_add_child(s_scene_layer, text_layer_get_layer(s_info_layer));

  apply_settings();

  time_t now = time(NULL);
  struct tm *tt = localtime(&now);
  update_time_text(tt);
  update_info_text(tt);
}

static void window_unload(Window *win) {
  if (s_rush_timer) { app_timer_cancel(s_rush_timer); s_rush_timer = NULL; }
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_info_layer);
  layer_destroy(s_scene_layer);
}

// ── Init / deinit ───────────────────────────────────────────────────────────
static void init(void) {
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = window_load,
    .unload = window_unload
  });

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  accel_tap_service_subscribe(tap_handler);

  app_message_register_inbox_received(inbox_received);
  app_message_open(256, 64);

  window_stack_push(s_window, true);
}

static void deinit(void) {
  accel_tap_service_unsubscribe();
  tick_timer_service_unsubscribe();
  app_message_deregister_callbacks();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}

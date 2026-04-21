#include <pebble.h>

#define SCREEN_W 200
#define SCREEN_H 228

// ── Layout zones ───────────────────────────────────────────────────────────
#define SKY_TOP        0
#define SKY_BOTTOM     85      // y=0-85: sky
#define CITY_BOTTOM    180     // y=85-180: buildings
#define STREET_BOTTOM  208     // y=180-208: street
// y=208-228: info bar

// ── Persist keys ──────────────────────────────────────────────────────────
#define PERSIST_CLOCK_STYLE  100
#define PERSIST_TEMP_UNIT    101
#define PERSIST_HOUR_FORMAT  102
#define PERSIST_ANIMATIONS   103

// ── State ──────────────────────────────────────────────────────────────────
static Window      *s_window;
static Layer       *s_scene_layer;
static TextLayer   *s_time_layer;    // digital clock display on tower
static TextLayer   *s_info_layer;
static GFont        s_time_font;
static GFont        s_info_font;

static char s_time_buf[8];
static char s_info_buf[48];

// Weather (from pkjs)
static int s_temp    = -999;
static int s_wx_code = -1;
static int s_sunrise = -1;  // mins since midnight
static int s_sunset  = -1;

// Time tracking
static int s_hour    = 12;
static int s_minute  = 0;
static int s_frame   = 0;   // per-minute frame counter for deterministic animation

// User settings (persisted)
static int  s_clock_style  = 0;   // 0=analog, 1=digital
static int  s_temp_unit    = 0;   // 0=°F, 1=°C
static bool s_hour_format  = false; // false=12h, true=24h
static bool s_animations   = true;  // car animations on/off

// Fireworks state (triggered by wrist flick)
#define FIREWORKS_FRAMES 30
#define FIREWORKS_PARTICLES 20
static bool s_fw_active = false;
static int  s_fw_frame  = 0;
static AppTimer *s_fw_timer = NULL;
typedef struct {
  int16_t x, y;
  int16_t vx, vy;
  GColor  color;
} Particle;
static Particle s_particles[FIREWORKS_PARTICLES];

// ── Weather code helpers ───────────────────────────────────────────────────
// WMO weather codes → visual category
enum WxCat { WX_UNKNOWN, WX_CLEAR, WX_CLOUDY, WX_RAIN, WX_SNOW, WX_FOG, WX_STORM };

static int wx_category(int code) {
  if (code < 0) return WX_UNKNOWN;
  if (code == 0) return WX_CLEAR;
  if (code <= 3) return WX_CLOUDY;
  if (code <= 48) return WX_FOG;
  if (code <= 67) return WX_RAIN;
  if (code <= 77) return WX_SNOW;
  if (code <= 82) return WX_RAIN;
  if (code <= 86) return WX_SNOW;
  return WX_STORM;
}

// Is it currently nighttime? (before sunrise or after sunset)
static bool is_night(void) {
  if (s_sunrise < 0 || s_sunset < 0) {
    // No data — fall back to simple 6pm-6am
    return (s_hour < 6 || s_hour >= 20);
  }
  int now_min = s_hour * 60 + s_minute;
  return (now_min < s_sunrise || now_min >= s_sunset);
}

// Sky color based on time of day
static GColor sky_color(void) {
  int now_min = s_hour * 60 + s_minute;
  int sr = s_sunrise >= 0 ? s_sunrise : 6*60;
  int ss = s_sunset  >= 0 ? s_sunset  : 20*60;

  // Twilight windows: 30 min around sunrise/sunset
  if (now_min < sr - 30 || now_min >= ss + 30) return GColorOxfordBlue;
  if (now_min < sr + 30) return GColorOrange;       // sunrise
  if (now_min >= ss - 30) return GColorOrange;      // sunset
  return GColorVividCerulean;                       // daytime
}

// ── Deterministic PRNG for stars/clouds ────────────────────────────────────
static uint32_t rng_seed = 1;
static void rng_set(uint32_t s) { rng_seed = s ? s : 1; }
static uint32_t rng_next(void) {
  rng_seed = (rng_seed * 1664525u) + 1013904223u;
  return rng_seed;
}

// ── Drawing: sky ───────────────────────────────────────────────────────────
static void draw_sky(GContext *ctx) {
  // Fill sky
  graphics_context_set_fill_color(ctx, sky_color());
  graphics_fill_rect(ctx, GRect(0, SKY_TOP, SCREEN_W, SKY_BOTTOM), 0, GCornerNone);

  bool night = is_night();

  // Stars at night — deterministic based on current day
  if (night) {
    rng_set(20250419 + (s_hour * 60 + s_minute) / 60);  // new stars each hour
    graphics_context_set_stroke_color(ctx, GColorWhite);
    for (int i = 0; i < 40; i++) {
      int x = rng_next() % SCREEN_W;
      int y = rng_next() % (SKY_BOTTOM - 4) + 2;
      // Vary brightness
      GColor c = (i % 3 == 0) ? GColorLightGray : GColorWhite;
      graphics_context_set_stroke_color(ctx, c);
      graphics_draw_pixel(ctx, GPoint(x, y));
      // Bright stars get a cross
      if (i % 7 == 0) {
        graphics_draw_pixel(ctx, GPoint(x+1, y));
        graphics_draw_pixel(ctx, GPoint(x-1, y));
        graphics_draw_pixel(ctx, GPoint(x, y+1));
        graphics_draw_pixel(ctx, GPoint(x, y-1));
      }
    }
  }

  // Sun or moon
  int now_min = s_hour * 60 + s_minute;
  int sr = s_sunrise >= 0 ? s_sunrise : 6*60;
  int ss = s_sunset  >= 0 ? s_sunset  : 20*60;
  int day_len  = ss - sr;
  int night_len = (24*60) - day_len;

  GPoint orb_pt;
  GColor orb_color;
  if (!night && day_len > 0) {
    // Sun travels an arc across the sky
    int frac = ((now_min - sr) * 1000) / day_len;
    int x = (SCREEN_W * frac) / 1000;
    int y = 60 - ((frac < 500 ? frac : 1000 - frac) * 45 / 500);
    orb_pt = GPoint(x, y + 10);
    orb_color = GColorYellow;
  } else if (night_len > 0) {
    // Moon travels across the night sky
    int night_min;
    if (now_min >= ss) night_min = now_min - ss;
    else night_min = now_min + (24*60 - ss);
    int frac = (night_min * 1000) / night_len;
    int x = (SCREEN_W * frac) / 1000;
    int y = 55 - ((frac < 500 ? frac : 1000 - frac) * 40 / 500);
    orb_pt = GPoint(x, y + 10);
    orb_color = GColorWhite;
  } else {
    orb_pt = GPoint(SCREEN_W/2, 30);
    orb_color = GColorWhite;
  }
  graphics_context_set_fill_color(ctx, orb_color);
  graphics_fill_circle(ctx, orb_pt, 9);

  // Clouds (3 drifting based on minute counter)
  int wx = wx_category(s_wx_code);
  int n_clouds = (wx == WX_CLOUDY || wx == WX_RAIN || wx == WX_STORM || wx == WX_SNOW) ? 5 : 2;
  graphics_context_set_fill_color(ctx, night ? GColorLightGray : GColorWhite);
  for (int i = 0; i < n_clouds; i++) {
    int drift = (s_frame * (3 + i)) % (SCREEN_W + 40);
    int cx = (drift - 20 + i * 60) % (SCREEN_W + 40) - 20;
    int cy = 15 + (i * 11) % 30;
    graphics_fill_circle(ctx, GPoint(cx, cy), 7);
    graphics_fill_circle(ctx, GPoint(cx + 8, cy - 2), 8);
    graphics_fill_circle(ctx, GPoint(cx + 15, cy + 1), 7);
  }

  // Rain lines
  if (wx == WX_RAIN || wx == WX_STORM) {
    graphics_context_set_stroke_color(ctx, GColorCeleste);
    for (int i = 0; i < 30; i++) {
      int x = (i * 13 + s_frame * 7) % SCREEN_W;
      int y = 30 + (i * 17 + s_frame * 11) % (SKY_BOTTOM - 35);
      graphics_draw_line(ctx, GPoint(x, y), GPoint(x - 2, y + 5));
    }
  }

  // Snow flakes
  if (wx == WX_SNOW) {
    graphics_context_set_fill_color(ctx, GColorWhite);
    for (int i = 0; i < 35; i++) {
      int x = (i * 17 + s_frame * 5) % SCREEN_W;
      int y = 25 + (i * 19 + s_frame * 9) % (SKY_BOTTOM - 30);
      graphics_fill_rect(ctx, GRect(x, y, 2, 2), 0, GCornerNone);
    }
  }

  // Lightning bolt occasionally during storm
  if (wx == WX_STORM && (s_frame % 4) == 0) {
    graphics_context_set_stroke_color(ctx, GColorYellow);
    int bx = 50 + (s_frame * 23) % 100;
    graphics_draw_line(ctx, GPoint(bx, 20),     GPoint(bx-4, 40));
    graphics_draw_line(ctx, GPoint(bx-4, 40),   GPoint(bx+2, 45));
    graphics_draw_line(ctx, GPoint(bx+2, 45),   GPoint(bx-3, 65));
  }
}

// ── Drawing: city skyline ──────────────────────────────────────────────────
typedef struct {
  int16_t x, w, h;
  GColor  color;
} Building;

static const Building s_buildings[] = {
  // Background — fill sky gap on both sides
  {   0, 58,  95, { .argb = GColorOxfordBlueARGB8 } },       // 0: left bg (top=85)
  { 142, 58,  95, { .argb = GColorOxfordBlueARGB8 } },       // 1: right bg (top=85)
  // Left foreground
  {   2, 18,  80, { .argb = GColorImperialPurpleARGB8 } },   // 2: x=2-20, top=100
  {  16, 22,  95, { .argb = GColorDarkGrayARGB8 } },         // 3: x=16-38, top=85 (tall)
  {  34, 14,  68, { .argb = GColorImperialPurpleARGB8 } },   // 4: x=34-48, top=112
  {  44, 16,  84, { .argb = GColorDarkGrayARGB8 } },         // 5: x=44-60, top=96
  // Clock tower
  {  56, 88, 110, { .argb = GColorBlackARGB8 } },            // 6: CLOCK TOWER
  // Right foreground
  { 144, 16,  84, { .argb = GColorDarkGrayARGB8 } },         // 7: x=144-160, top=96
  { 154, 14,  68, { .argb = GColorImperialPurpleARGB8 } },   // 8: x=154-168, top=112
  { 162, 22,  95, { .argb = GColorDarkGrayARGB8 } },         // 9: x=162-184, top=85 (tall)
  { 178, 18,  80, { .argb = GColorImperialPurpleARGB8 } },   // 10: x=178-196, top=100
};
#define N_BUILDINGS (sizeof(s_buildings)/sizeof(s_buildings[0]))
#define TOWER_IDX 6

static void draw_city(GContext *ctx) {
  bool night = is_night();

  for (size_t i = 0; i < N_BUILDINGS; i++) {
    Building b = s_buildings[i];
    int top = CITY_BOTTOM - b.h;
    graphics_context_set_fill_color(ctx, b.color);
    graphics_fill_rect(ctx, GRect(b.x, top, b.w, b.h), 0, GCornerNone);

    // Skip windows on the background fills (0,1) and clock tower
    if (i == 0 || i == 1 || i == TOWER_IDX) continue;

    // Windows — 3px x 4px grid, spaced 5x7
    for (int wy = top + 6; wy < CITY_BOTTOM - 4; wy += 7) {
      for (int wx = b.x + 3; wx < b.x + b.w - 3; wx += 5) {
        // Deterministic "lit" state based on position + hour
        uint32_t seed = (uint32_t)wx * 31u + (uint32_t)wy * 97u + (uint32_t)(s_hour / 2);
        bool lit = night && ((seed % 10) < 7);
        GColor win_color;
        if (lit) {
          win_color = ((seed % 5) == 0) ? GColorOrange : GColorYellow;
        } else {
          win_color = GColorDarkGray;
        }
        graphics_context_set_fill_color(ctx, win_color);
        graphics_fill_rect(ctx, GRect(wx, wy, 3, 4), 0, GCornerNone);
      }
    }
  }

  // Clock tower special features
  Building tower = s_buildings[TOWER_IDX];
  int tower_top = CITY_BOTTOM - tower.h;

  // Tower roof (triangle/spire)
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_stroke_color(ctx, GColorDarkGray);
  // Spire
  graphics_draw_line(ctx,
    GPoint(tower.x + tower.w/2, tower_top - 12),
    GPoint(tower.x + tower.w/2, tower_top));
  graphics_draw_line(ctx,
    GPoint(tower.x + tower.w/2 - 1, tower_top - 12),
    GPoint(tower.x + tower.w/2 - 1, tower_top));

  // Clock face background (big rect on tower)
  GRect face = GRect(tower.x + 4, tower_top + 6, tower.w - 8, 40);
  graphics_context_set_fill_color(ctx, GColorPastelYellow);
  graphics_fill_rect(ctx, face, 3, GCornersAll);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_round_rect(ctx, face, 3);

  int cx = face.origin.x + face.size.w / 2;
  int cy = face.origin.y + face.size.h / 2;

  if (s_clock_style == 0) {
    // ── Analog hands ──
    int r_min = face.size.h / 2 - 2;
    int r_hr  = r_min * 2 / 3;

    // Tick marks at 12, 3, 6, 9
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_draw_pixel(ctx, GPoint(cx,                             face.origin.y + 2));
    graphics_draw_pixel(ctx, GPoint(face.origin.x + face.size.w - 3, cy));
    graphics_draw_pixel(ctx, GPoint(cx,                             face.origin.y + face.size.h - 3));
    graphics_draw_pixel(ctx, GPoint(face.origin.x + 2,             cy));

    // Minute hand
    int min_angle = TRIG_MAX_ANGLE * s_minute / 60;
    int min_ex = cx + (sin_lookup(min_angle) * r_min / TRIG_MAX_RATIO);
    int min_ey = cy - (cos_lookup(min_angle) * r_min / TRIG_MAX_RATIO);
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_draw_line(ctx, GPoint(cx, cy), GPoint(min_ex, min_ey));

    // Hour hand (including minute fraction)
    int hr_angle = TRIG_MAX_ANGLE * ((s_hour % 12) * 60 + s_minute) / (12 * 60);
    int hr_ex = cx + (sin_lookup(hr_angle) * r_hr / TRIG_MAX_RATIO);
    int hr_ey = cy - (cos_lookup(hr_angle) * r_hr / TRIG_MAX_RATIO);
    graphics_draw_line(ctx, GPoint(cx, cy), GPoint(hr_ex, hr_ey));
    graphics_draw_line(ctx, GPoint(cx + 1, cy), GPoint(hr_ex + 1, hr_ey));

    // Center dot
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, GPoint(cx, cy), 2);
  }
  // Digital mode: the TextLayer (s_time_layer) is visible and renders over the face
}

// ── Drawing: street ────────────────────────────────────────────────────────
static void draw_street(GContext *ctx) {
  // Sidewalk
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx, GRect(0, CITY_BOTTOM, SCREEN_W, 4), 0, GCornerNone);

  // Road
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, CITY_BOTTOM + 4, SCREEN_W, STREET_BOTTOM - CITY_BOTTOM - 4), 0, GCornerNone);

  // Lane dashes (center line)
  graphics_context_set_fill_color(ctx, GColorYellow);
  int lane_y = CITY_BOTTOM + 16;
  int dash_off = (s_frame * 12) % 20;
  for (int dx = -dash_off; dx < SCREEN_W; dx += 20) {
    graphics_fill_rect(ctx, GRect(dx, lane_y, 12, 2), 0, GCornerNone);
  }

  if (!s_animations) return;

  // Cars — 3 cars at different speeds
  struct CarSpec { int speed; int y; GColor body; GColor window; };
  struct CarSpec cars[3] = {
    { 7,  CITY_BOTTOM + 7,  GColorRed,       GColorCeleste },
    { 11, CITY_BOTTOM + 20, GColorYellow,    GColorCeleste },
    { 5,  CITY_BOTTOM + 7,  GColorBlue,      GColorCeleste }
  };
  for (int i = 0; i < 3; i++) {
    int cx = (s_frame * cars[i].speed + i * 80) % (SCREEN_W + 30) - 15;
    int cy = cars[i].y;
    // Body
    graphics_context_set_fill_color(ctx, cars[i].body);
    graphics_fill_rect(ctx, GRect(cx, cy, 20, 8), 2, GCornersAll);
    // Cabin
    graphics_context_set_fill_color(ctx, cars[i].window);
    graphics_fill_rect(ctx, GRect(cx + 4, cy - 3, 10, 4), 1, GCornersTop);
    // Wheels
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, GPoint(cx + 4, cy + 8), 2);
    graphics_fill_circle(ctx, GPoint(cx + 16, cy + 8), 2);
    // Headlights at night
    if (is_night()) {
      graphics_context_set_fill_color(ctx, GColorYellow);
      graphics_fill_rect(ctx, GRect(cx + 19, cy + 2, 2, 2), 0, GCornerNone);
    }
  }
}

// ── Drawing: info bar ──────────────────────────────────────────────────────
static void draw_info_bar(GContext *ctx) {
  // Background strip
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, STREET_BOTTOM, SCREEN_W, SCREEN_H - STREET_BOTTOM), 0, GCornerNone);

  // Top separator
  graphics_context_set_fill_color(ctx, GColorYellow);
  graphics_fill_rect(ctx, GRect(0, STREET_BOTTOM, SCREEN_W, 1), 0, GCornerNone);
}

// ── Drawing: fireworks (wrist flick) ───────────────────────────────────────
static void spawn_fireworks(void) {
  rng_set(time(NULL));
  int cx = 40 + (rng_next() % (SCREEN_W - 80));
  int cy = 30 + (rng_next() % 30);
  GColor palette[] = { GColorRed, GColorYellow, GColorMagenta, GColorVividCerulean, GColorGreen, GColorOrange };
  for (int i = 0; i < FIREWORKS_PARTICLES; i++) {
    s_particles[i].x = cx;
    s_particles[i].y = cy;
    // Pseudo-uniform spread: 0-360°
    int angle = (i * (TRIG_MAX_ANGLE / FIREWORKS_PARTICLES));
    int speed = 3 + (rng_next() % 3);
    s_particles[i].vx = (cos_lookup(angle) * speed) / TRIG_MAX_RATIO;
    s_particles[i].vy = (sin_lookup(angle) * speed) / TRIG_MAX_RATIO;
    s_particles[i].color = palette[i % 6];
  }
}

static void draw_fireworks(GContext *ctx) {
  if (!s_fw_active) return;
  for (int i = 0; i < FIREWORKS_PARTICLES; i++) {
    Particle *p = &s_particles[i];
    // Fade color as they fall
    graphics_context_set_fill_color(ctx, p->color);
    graphics_fill_rect(ctx, GRect(p->x, p->y, 2, 2), 0, GCornerNone);
    // Trail
    graphics_context_set_fill_color(ctx, GColorLightGray);
    graphics_fill_rect(ctx, GRect(p->x - p->vx/2, p->y - p->vy/2, 1, 1), 0, GCornerNone);
  }
}

static void fireworks_step(void *data);
static void start_fireworks_timer(void) {
  if (s_fw_timer) app_timer_cancel(s_fw_timer);
  s_fw_timer = app_timer_register(50, fireworks_step, NULL);
}
static void fireworks_step(void *data) {
  s_fw_frame++;
  // Update particle positions
  for (int i = 0; i < FIREWORKS_PARTICLES; i++) {
    Particle *p = &s_particles[i];
    p->x += p->vx;
    p->y += p->vy;
    // Gravity
    p->vy += 1;
  }
  layer_mark_dirty(s_scene_layer);

  if (s_fw_frame < FIREWORKS_FRAMES) {
    s_fw_timer = app_timer_register(50, fireworks_step, NULL);
  } else {
    s_fw_active = false;
    s_fw_frame  = 0;
    s_fw_timer  = NULL;
    layer_mark_dirty(s_scene_layer);
  }
}

// ── Main scene update_proc ─────────────────────────────────────────────────
static void scene_update(Layer *layer, GContext *ctx) {
  draw_sky(ctx);
  draw_city(ctx);
  draw_street(ctx);
  draw_info_bar(ctx);
  draw_fireworks(ctx);
}

// ── Time/info updating ─────────────────────────────────────────────────────
static void update_time_text(struct tm *tt) {
  s_hour   = tt->tm_hour;
  s_minute = tt->tm_min;

  // Update digital clock TextLayer (visible only in digital mode)
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
  static const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
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

// ── Tick handler ───────────────────────────────────────────────────────────
static void tick_handler(struct tm *tt, TimeUnits changed) {
  s_frame++;
  update_time_text(tt);
  update_info_text(tt);

  // Request weather every 30 minutes
  if (tt->tm_min % 30 == 0 && s_wx_code >= -1) {
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
      dict_write_uint8(iter, MESSAGE_KEY_REQUEST_WEATHER, 1);
      app_message_outbox_send();
    }
  }

  layer_mark_dirty(s_scene_layer);
}

// ── Accelerometer tap (wrist flick) ───────────────────────────────────────
static void tap_handler(AccelAxisType axis, int32_t direction) {
  if (s_fw_active) return;
  s_fw_active = true;
  s_fw_frame  = 0;
  spawn_fireworks();
  start_fireworks_timer();
  // Gentle vibration feedback
  vibes_short_pulse();
}

// ── Settings apply ─────────────────────────────────────────────────────────
static void apply_settings(void) {
  // Show/hide digital clock TextLayer based on clock style
  layer_set_hidden(text_layer_get_layer(s_time_layer), s_clock_style != 1);
}

// ── AppMessage (weather + settings from pkjs) ──────────────────────────────
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

// ── Window lifecycle ───────────────────────────────────────────────────────
static void window_load(Window *win) {
  Layer *root = window_get_root_layer(win);
  GRect bounds = layer_get_bounds(root);

  // Load persisted settings
  if (persist_exists(PERSIST_CLOCK_STYLE))  s_clock_style = persist_read_int(PERSIST_CLOCK_STYLE);
  if (persist_exists(PERSIST_TEMP_UNIT))    s_temp_unit   = persist_read_int(PERSIST_TEMP_UNIT);
  if (persist_exists(PERSIST_HOUR_FORMAT))  s_hour_format = persist_read_bool(PERSIST_HOUR_FORMAT);
  if (persist_exists(PERSIST_ANIMATIONS))   s_animations  = persist_read_bool(PERSIST_ANIMATIONS);

  // Scene layer (draws everything)
  s_scene_layer = layer_create(bounds);
  layer_set_update_proc(s_scene_layer, scene_update);
  layer_add_child(root, s_scene_layer);

  // Digital clock TextLayer — shown only in digital mode, centered on the clock tower face
  // Tower: x=56, w=88 → face GRect(60, tower_top+6, 80, 40), tower_top = 180-110 = 70 → face y=76
  s_time_font = fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS);
  s_time_layer = text_layer_create(GRect(60, 82, 80, 26));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorBlack);
  text_layer_set_font(s_time_layer, s_time_font);
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(s_scene_layer, text_layer_get_layer(s_time_layer));

  // Info bar (bottom strip) — shows date + weather
  s_info_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  s_info_layer = text_layer_create(GRect(0, STREET_BOTTOM + 2, SCREEN_W, SCREEN_H - STREET_BOTTOM - 2));
  text_layer_set_background_color(s_info_layer, GColorClear);
  text_layer_set_text_color(s_info_layer, GColorWhite);
  text_layer_set_font(s_info_layer, s_info_font);
  text_layer_set_text_alignment(s_info_layer, GTextAlignmentCenter);
  layer_add_child(s_scene_layer, text_layer_get_layer(s_info_layer));

  // Apply loaded settings
  apply_settings();

  // Initial update
  time_t now = time(NULL);
  struct tm *tt = localtime(&now);
  update_time_text(tt);
  update_info_text(tt);
}

static void window_unload(Window *win) {
  if (s_fw_timer) { app_timer_cancel(s_fw_timer); s_fw_timer = NULL; }
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_info_layer);
  layer_destroy(s_scene_layer);
}

// ── Init / deinit ──────────────────────────────────────────────────────────
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

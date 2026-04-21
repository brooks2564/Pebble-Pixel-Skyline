# Pixel Skyline

A pixel-art city watchface for Pebble Time Steel / Emery. A living urban scene that reacts to the time of day and real weather conditions.

## Features

### Sky
- **Day/night cycle** — sky transitions from deep blue night to orange twilight to cerulean blue as the sun rises and sets
- **Sun & moon arc** — sun and moon travel realistic arcs across the sky timed to actual sunrise/sunset from your location
- **Stars** — appear at night with cross-shaped bright stars; shift slowly each hour
- **Clouds** — 2 clouds in clear weather, 5 during cloudy/rain/storm/snow; drift across the sky each minute
- **Weather effects** — rain streaks, falling snowflakes, and lightning bolts rendered in the sky based on live conditions

### City
- **Pixel skyline** — layered buildings in deep blue, purple, and black with lit windows at night (70% of windows lit, warm yellow/orange glow)
- **Clock tower** — central landmark with a clock face you can read. Choose **analog hands** or **digital** display in settings
- **Spire** — clock tower has a drawn spire above the clock face

### Street
- **Animated road** — sidewalk, road, and scrolling yellow center-lane dashes
- **Cars** — three cars (red, yellow, blue) driving across the screen at different speeds with cabin windows and night headlights. Can be toggled off in settings

### Info Bar
- Date (day, month, date) and current temperature from live weather

### Wrist Flick
- Shake your wrist to launch a fireworks burst over the skyline — 20 particles in red, yellow, magenta, cyan, green, and orange explode outward with gravity

## Settings

Long-press the watchface in the Pebble app to open the configuration page.

| Setting | Options |
|---|---|
| Clock Style | Analog Hands / Digital |
| 24-Hour Time | On / Off (applies to digital mode) |
| Temperature Unit | °F Fahrenheit / °C Celsius |
| Street Cars | On / Off |

Settings are saved to the watch and persist across restarts.

## Weather

Powered by [Open-Meteo](https://open-meteo.com/) — free, no API key required. Fetches temperature, weather condition, and sunrise/sunset times for your location. Refreshes every 30 minutes.

## Author

Brooman Inks

// Pixel Skyline — PebbleKit JS
// Weather from Open-Meteo + Clay config page

var Clay = require('pebble-clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

// ── Config page ────────────────────────────────────────────────────────────
Pebble.addEventListener('showConfiguration', function() {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (e && !e.response) return;
  var dict = clay.getSettings(e.response);
  Pebble.sendAppMessage(dict,
    function() { console.log('[Skyline] settings sent'); },
    function(err) { console.log('[Skyline] settings send failed: ' + JSON.stringify(err)); }
  );
});

// ── Weather ────────────────────────────────────────────────────────────────
function isoToMins(iso) {
  if (!iso) return -1;
  var t = iso.split('T')[1] || '';
  var parts = t.split(':');
  var h = parseInt(parts[0], 10);
  var m = parseInt(parts[1], 10);
  if (isNaN(h) || isNaN(m)) return -1;
  return h * 60 + m;
}

function sendWeather(lat, lon, useCelsius) {
  var unit = useCelsius ? 'celsius' : 'fahrenheit';
  var url = 'https://api.open-meteo.com/v1/forecast?' +
    'latitude=' + lat +
    '&longitude=' + lon +
    '&current=temperature_2m,weather_code' +
    '&daily=sunrise,sunset' +
    '&timezone=auto' +
    '&temperature_unit=' + unit;

  var xhr = new XMLHttpRequest();
  xhr.onload = function() {
    try {
      var j = JSON.parse(this.responseText);
      var temp = Math.round(j.current.temperature_2m);
      var code = j.current.weather_code;
      var srMin = isoToMins(j.daily.sunrise[0]);
      var ssMin = isoToMins(j.daily.sunset[0]);
      Pebble.sendAppMessage({
        TEMPERATURE: temp,
        CONDITIONS: code,
        SUNRISE: srMin,
        SUNSET: ssMin
      });
    } catch (e) {
      console.log('[Skyline] parse error: ' + e);
    }
  };
  xhr.open('GET', url, true);
  xhr.send();
}

function getWeather() {
  var settings = clay.getSettings();
  var useCelsius = settings && settings.TEMP_UNIT && settings.TEMP_UNIT.value === '1';
  navigator.geolocation.getCurrentPosition(
    function(pos) { sendWeather(pos.coords.latitude, pos.coords.longitude, useCelsius); },
    function(err) { console.log('[Skyline] geo error: ' + err.message); },
    { timeout: 15000, maximumAge: 60000 }
  );
}

Pebble.addEventListener('ready', function() {
  getWeather();
});

Pebble.addEventListener('appmessage', function(e) {
  if (e.payload.REQUEST_WEATHER) getWeather();
});

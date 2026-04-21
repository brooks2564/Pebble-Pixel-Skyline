// Pixel Skyline — PebbleKit JS
// Fetches weather + sunrise/sunset from Open-Meteo (no API key required)

function sendWeather(lat, lon) {
  var url = "https://api.open-meteo.com/v1/forecast?" +
    "latitude=" + lat +
    "&longitude=" + lon +
    "&current=temperature_2m,weather_code" +
    "&daily=sunrise,sunset" +
    "&timezone=auto" +
    "&temperature_unit=fahrenheit";

  var xhr = new XMLHttpRequest();
  xhr.onload = function() {
    try {
      var j = JSON.parse(this.responseText);
      var temp = Math.round(j.current.temperature_2m);
      var code = j.current.weather_code;
      // sunrise/sunset are ISO strings like "2024-04-19T06:42"
      var sr = j.daily.sunrise[0];
      var ss = j.daily.sunset[0];
      var srMin = isoToMins(sr);
      var ssMin = isoToMins(ss);

      var msg = {
        TEMPERATURE: temp,
        CONDITIONS: code,
        SUNRISE: srMin,
        SUNSET: ssMin
      };
      Pebble.sendAppMessage(msg);
    } catch(e) {
      console.log("[Skyline] parse error: " + e);
    }
  };
  xhr.open("GET", url, true);
  xhr.send();
}

function isoToMins(iso) {
  // "2024-04-19T06:42"
  if (!iso) return -1;
  var t = iso.split("T")[1] || "";
  var parts = t.split(":");
  var h = parseInt(parts[0], 10);
  var m = parseInt(parts[1], 10);
  if (isNaN(h) || isNaN(m)) return -1;
  return h * 60 + m;
}

function getWeather() {
  navigator.geolocation.getCurrentPosition(
    function(pos) { sendWeather(pos.coords.latitude, pos.coords.longitude); },
    function(err) { console.log("[Skyline] geo error: " + err.message); },
    { timeout: 15000, maximumAge: 60000 }
  );
}

Pebble.addEventListener("ready", function() { getWeather(); });
Pebble.addEventListener("appmessage", function(e) {
  if (e.payload.REQUEST_WEATHER) getWeather();
});

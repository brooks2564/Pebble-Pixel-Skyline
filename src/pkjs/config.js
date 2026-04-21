module.exports = [
  {
    "type": "heading",
    "defaultValue": "Pixel Skyline"
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Clock Tower"
      },
      {
        "type": "radiogroup",
        "messageKey": "CLOCK_STYLE",
        "label": "Clock Style",
        "defaultValue": "0",
        "options": [
          { "label": "Analog Hands", "value": "0" },
          { "label": "Digital", "value": "1" }
        ]
      },
      {
        "type": "toggle",
        "messageKey": "HOUR_FORMAT",
        "label": "24-Hour Time (digital mode)",
        "defaultValue": false
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Weather"
      },
      {
        "type": "radiogroup",
        "messageKey": "TEMP_UNIT",
        "label": "Temperature Unit",
        "defaultValue": "0",
        "options": [
          { "label": "\u00b0F  Fahrenheit", "value": "0" },
          { "label": "\u00b0C  Celsius", "value": "1" }
        ]
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Animation"
      },
      {
        "type": "toggle",
        "messageKey": "ANIMATIONS",
        "label": "Street Cars",
        "defaultValue": true
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save Settings"
  }
];

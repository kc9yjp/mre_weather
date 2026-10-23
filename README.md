# CoreS3 SE Weather Display

A small weather display for an M5Stack CoreS3 SE. It connects directly to Wi-Fi and OpenWeather, then shows the current outdoor temperature and conditions on the device display. The design does not require an intermediate server.

## Planned behavior

- Show the current temperature, a short condition description, and the last successful update time.
- Let the owner configure Wi-Fi, OpenWeather API key, location, and units through an on-device setup portal.
- Refresh weather periodically and show the last known result if a request fails.
- Keep setup and recovery possible without a computer or a separately hosted service.

## Hardware and software direction

- **Device:** M5Stack CoreS3 SE.
- **Firmware:** ESP32 Arduino framework, using M5Unified (or the appropriate M5Stack library for the exact SE hardware revision).
- **Network setup:** ESP32 captive-portal Wi-Fi manager, for example WiFiManager, with a custom configuration page for weather settings.
- **Weather source:** OpenWeather Current Weather API over HTTPS.

These are initial choices, not implemented or validated yet. Confirm the exact CoreS3 SE display/touch and library support before starting firmware work.

## Initial setup

1. Create an OpenWeather account and obtain an API key with access to the Current Weather API.
2. Flash the firmware to the device.
3. On first boot, connect a phone or computer to the setup Wi-Fi network advertised by the device and open its captive portal.
4. Enter the home Wi-Fi credentials, API key, location (ZIP/postal code and country, or latitude and longitude), and preferred units. Save and allow the device to connect.
5. The device fetches and displays current conditions. If it cannot connect, restart setup using the documented device recovery action.

The exact setup network name, recovery action, and setup page fields will be settled during implementation.

## Location and units

Choose either a ZIP/postal code plus country code or manually entered latitude and longitude. ZIP/postal codes are not globally unique, so the country is required. For a ZIP/postal code, the device resolves it to coordinates through OpenWeather's Geocoding API, then uses those coordinates for current-weather requests. This is a direct device-to-OpenWeather lookup, not an intermediate server. Cache the resolved coordinates so weather refreshes do not need to repeat geocoding. If the code cannot be resolved, show an actionable setup error and allow coordinates as a fallback. The device should display the configured location (or a user-provided short label) so it is clear which place the reading represents.

OpenWeather units:

- `metric`: Celsius
- `imperial`: Fahrenheit
- `standard`: Kelvin (not recommended for the display)

## Direct API and key considerations

For a configured ZIP/postal code, first resolve the location using the OpenWeather Geocoding API. Then call the Current Weather endpoint directly using the resolved coordinates:

```text
https://api.openweathermap.org/data/2.5/weather?lat={latitude}&lon={longitude}&units={units}&appid={api_key}
```

The geocoding request uses the ZIP/postal code and country code with the OpenWeather ZIP code lookup endpoint. Support depends on OpenWeather's available geocoding data; provide latitude/longitude as a fallback.

No server is needed, but the API key and saved Wi-Fi credentials must be stored on the device. Device flash is not a secure secret vault: someone with physical access may be able to extract them. Use a dedicated, revocable OpenWeather key, do not publish it in source code or firmware shared publicly, and avoid logging request URLs or credentials. HTTPS certificate validation should be enabled.

## Reliability and privacy

- Fetch only the selected location; do not request GPS or collect user data.
- Use a configurable refresh interval that respects the OpenWeather account's request limits.
- Retain the last successful reading and its timestamp so temporary network/API failures do not leave the screen blank.
- Report connection, API, and configuration errors on-screen without exposing credentials.

## Project status

This repository currently contains project documentation only. See [PLAN.md](./PLAN.md) for the proposed implementation sequence and acceptance criteria.

## References

- [OpenWeather Current Weather API](https://openweathermap.org/current)
- [OpenWeather Geocoding API](https://openweathermap.org/api/geocoding-api) (not needed for the initial manually configured location)
- [M5Stack documentation](https://docs.m5stack.com/)

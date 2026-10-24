# CoreS3 SE Weather Display

A small weather display for an M5Stack CoreS3 SE. It connects directly to Wi-Fi and OpenWeather, then shows the current outdoor temperature and conditions on the device display. The design does not require an intermediate server.

## Planned behavior

- Show a very large current temperature, a large weather-condition icon, a readable condition description, and the last successful update time.
- Let the owner configure Wi-Fi, OpenWeather API key, location, and units through an on-device setup portal.
- Refresh weather periodically and show the last known result if a request fails.
- Keep setup and recovery possible without a computer or a separately hosted service.

## Hardware and software

- **Device:** M5Stack CoreS3 SE.
- **Firmware:** Arduino framework for ESP32-S3, built with PlatformIO and M5Unified.
- **Network setup:** WiFiManager captive portal hosted by the device.
- **Weather source:** OpenWeather Geocoding and Current Weather APIs over HTTPS.

M5Unified lists CoreS3 SE support. The current display emphasizes the temperature across the upper half of the screen, with a large condition icon and larger condition, location, and update text below it.

## Build and flash

1. Install [Visual Studio Code](https://code.visualstudio.com/) and the PlatformIO IDE extension, or install PlatformIO Core.
2. Open this project and build the `cores3` environment.
3. Connect the CoreS3 SE over USB, select its serial port if needed, and upload the firmware.
4. Open the serial monitor at 115200 baud for non-sensitive diagnostics. The API key and Wi-Fi password are not printed.

PlatformIO's first build downloads the ESP32 platform and libraries defined in [platformio.ini](./platformio.ini).

With PlatformIO Core installed, the equivalent terminal commands are:

```text
pio run
pio run --target upload
pio device monitor --baud 115200
```

## Initial setup

1. Create an OpenWeather account and obtain an API key with access to the Geocoding and Current Weather APIs.
2. On first boot, the device opens a temporary setup network. Its name and randomly generated password appear on the display.
3. Connect a phone or computer to that network and follow the captive-portal prompt. If it does not appear automatically, open `http://192.168.4.1`.
4. Enter home Wi-Fi credentials, API key, location, and units. Use either ZIP/postal code and country code, or coordinates; clear the ZIP/postal field to use coordinates.
5. Save and allow the device to connect. It then resolves the location if needed and fetches current conditions.
6. Hold a finger on the touchscreen for two seconds to reopen setup later. The setup network is temporary and closes after five minutes.

If setup times out or Wi-Fi cannot connect, hold the display or reboot the device to retry setup.

## Screen and battery

Wi-Fi is switched off between refreshes and reconnects using the last known channel and access point, falling back to a full scan if that fails. On battery, the screen turns off after the configured timeout (default 30 seconds) and the processor light-sleeps until the next refresh; tap the screen to wake it. The screen stays on while USB power is connected. Set the timeout to `0` in setup to keep the screen on, and lower the brightness (10–255, default 128) to extend battery life.

## Location and units

Choose either a ZIP/postal code plus country code or manually entered latitude and longitude. ZIP/postal codes are not globally unique, so the country is required. For a ZIP/postal code, the device resolves it to coordinates through OpenWeather's Geocoding API, then uses those coordinates for current-weather requests. This is a direct device-to-OpenWeather lookup, not an intermediate server. The coordinates are cached so weather refreshes do not need to repeat geocoding. If the code cannot be resolved, clear the ZIP/postal field and enter coordinates as a fallback. The display uses the user-provided location label when set.

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

No server is needed, but the API key and saved Wi-Fi credentials must be stored on the device. Device flash is not a secure secret vault: someone with physical access may be able to extract them. Use a dedicated, revocable OpenWeather key and do not publish it in source code or firmware shared publicly. Setup uses a temporary password-protected local access point, but its captive portal uses HTTP; configure it only in a trusted, physically private location. Firmware validates the OpenWeather HTTPS connection against the Sectigo root certificate included in [sectigo_root_r46.h](./src/sectigo_root_r46.h), which may need updating if OpenWeather changes certificate authorities.

## Reliability and privacy

- Fetch only the selected location; do not request GPS or collect user data.
- Refresh every 20 minutes. After an error, retry after one minute, doubling the wait on each further failure up to 20 minutes.
- Retain the last successful reading and its timestamp so temporary network/API failures do not leave the screen blank.
- Report connection, API, and configuration errors on-screen without exposing credentials.

## Project status

Firmware now includes device-hosted Wi-Fi/weather setup, ZIP-code geocoding (with coordinate fallback), and a large-text current-conditions display. On-device validation and further readability tuning remain. See [PLAN.md](./PLAN.md) for implementation details and remaining checks.

## References

- [OpenWeather Current Weather API](https://openweathermap.org/current)
- [OpenWeather Geocoding API](https://openweathermap.org/api/geocoding-api) (used to resolve ZIP/postal-code locations)
- [M5Stack documentation](https://docs.m5stack.com/)

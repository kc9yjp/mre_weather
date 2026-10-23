# Implementation Plan

## Goal

Build a dependable, at-a-glance outdoor weather display for the M5Stack CoreS3 SE that is provisioned and operated without an intermediate server.

## Proposed architecture

```text
Phone/computer
  └─ connects to device setup AP and captive portal (initial setup/recovery)
       └─ saves Wi-Fi + OpenWeather + location settings on device

CoreS3 SE
  ├─ joins configured Wi-Fi
  ├─ obtains time for TLS validation
  ├─ requests OpenWeather Current Weather over HTTPS
  └─ renders current reading and status on its display
```

The device initiates all network connections. No inbound internet access, cloud relay, or always-on local server is required. The setup portal runs locally on the device only while configuring or recovering settings.

## Decisions for the first version

- Configure location with ZIP/postal code plus country code, or latitude/longitude; do not assume a ZIP/postal code is globally unique.
- Resolve ZIP/postal code to coordinates directly through OpenWeather's Geocoding API and cache the result; use coordinates for current-weather requests.
- Configure metric or imperial units; avoid Kelvin in the normal UI.
- Fetch only current weather, not forecasts.
- Retain the most recent successful result and show its age when offline.
- Keep the API key configurable at runtime; never commit it to the repository.
- Use a captive-portal library only after confirming compatibility with the chosen CoreS3 SE firmware stack.

## Work sequence

1. **Confirm hardware and toolchain**
   - Verify exact CoreS3 SE revision, screen dimensions/orientation, input controls, and supported M5Stack library.
   - Select Arduino + PlatformIO or the supported M5Stack build workflow.
   - Create and build a minimal display/Wi-Fi firmware.
2. **Add device configuration**
   - Add a setup AP/captive portal for Wi-Fi, API key, location mode (ZIP/postal code plus country code, or latitude/longitude), units, and optional location label.
   - Persist settings using the selected ESP32 storage mechanism.
   - Resolve postal-code locations through OpenWeather geocoding, cache the coordinates, and provide a clear error/fallback for unsupported or unrecognized codes.
   - Define a reliable recovery action for reopening setup and a clear indication while setup is active.
3. **Implement weather retrieval**
   - Connect to Wi-Fi and synchronize time before HTTPS requests.
   - Call OpenWeather Current Weather with configured coordinates and units.
   - Validate HTTP status and required JSON fields; handle unavailable network, invalid key, rate limits, malformed responses, and timeout.
   - Avoid logging secrets or complete credential-bearing URLs.
4. **Build the display**
   - Show temperature prominently, condition text/icon, configured location, last-updated time, and connection/error state.
   - Use clear units and distinguish fresh data from cached/stale data.
   - Keep the last good reading across transient request failures and, if feasible, device restarts.
5. **Harden and verify**
   - Verify HTTPS certificate validation and ensure TLS time setup is reliable.
   - Add bounded retries/backoff and a configurable refresh period compatible with API limits.
   - Test setup, restart/reconnect, invalid credentials, API errors, offline operation, and both unit modes.
   - Document flashing, first-time setup, recovery, and API-key replacement.

## Acceptance criteria

- A freshly flashed device can be configured using only a phone/computer and the device; no separate server is required.
- The setup flow stores Wi-Fi and weather settings and reconnects after a reboot.
- The owner can set a ZIP/postal code and country or enter coordinates; postal-code lookup either resolves to a cached location or reports a clear error and permits coordinate entry.
- The display shows a correctly unit-labeled current temperature, conditions, configured location, and update age.
- The API key is not present in tracked source and is not printed in logs or UI.
- HTTPS certificate validation is enabled for weather requests.
- Network/API failures are visible and do not erase the last valid displayed reading.
- Setup can be reopened after incorrect Wi-Fi details or a changed API key.

## Risks and open checks

- Confirm the exact CoreS3 SE display and input/library compatibility before committing to UI code.
- OpenWeather API access, activation delay, limits, and terms depend on the user's account and plan.
- A direct-to-provider design necessarily stores the API key on the device; physical access can expose it. A backend could hide the key, but would add hosting and maintenance, contrary to the preferred design.
- Captive portals behave differently across phone/OS combinations; include a fallback instruction to join the setup AP and open its local portal address manually.
- Ensure the chosen ESP32 TLS/certificate approach works after cold boot when system time is not yet synchronized.

## Out of scope for version one

- Forecasts, severe-weather alerts, multiple locations, automatic GPS/geolocation, remote administration, and a cloud backend.

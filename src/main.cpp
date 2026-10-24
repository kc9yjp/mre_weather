#include <Arduino.h>
#include <ArduinoJson.h>
#include <math.h>
#include <stdlib.h>
#include <HTTPClient.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <time.h>

#include "sectigo_root_r46.h"

namespace {
constexpr char kWeatherHost[] = "api.openweathermap.org";
constexpr uint32_t kRefreshIntervalMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kRetryIntervalMs = 60UL * 1000UL;
constexpr time_t kValidEpoch = 1700000000;
constexpr uint16_t kSetupHoldMs = 2000;
constexpr uint32_t kMaxScreenTimeoutSec = 3600;
constexpr uint8_t kMinBrightness = 10;

struct Settings {
  String apiKey;
  String postalCode;
  String countryCode;
  String units = "imperial";
  String locationLabel;
  String latitudeInput;
  String longitudeInput;
  String geoPostalCode;
  String geoCountryCode;
  double cachedLatitude = 0;
  double cachedLongitude = 0;
  uint32_t screenTimeoutSec = 30;  // 0 keeps the screen on
  uint8_t brightness = 128;
};

struct Reading {
  float temperature = 0;
  int conditionId = 800;
  String description;
  String location;
  String units;
  time_t updatedAt = 0;
  bool valid = false;
};

Settings settings;
Reading reading;
Preferences preferences;
uint32_t nextRefreshAt = 0;
uint32_t nextClockRedrawAt = 0;
uint32_t screenOffAt = 0;
bool screenOn = true;
bool ignoreTouchUntilRelease = false;
String statusMessage = "Starting";

bool loadSettings() {
  if (!preferences.begin("weather", false)) {
    return false;
  }
  settings.apiKey = preferences.getString("apiKey", "");
  settings.postalCode = preferences.getString("postal", "");
  settings.countryCode = preferences.getString("country", "");
  settings.units = preferences.getString("units", "imperial");
  settings.locationLabel = preferences.getString("label", "");
  settings.latitudeInput = preferences.getString("latitude", "");
  settings.longitudeInput = preferences.getString("longitude", "");
  settings.geoPostalCode = preferences.getString("geoPostal", "");
  settings.geoCountryCode = preferences.getString("geoCountry", "");
  settings.cachedLatitude = preferences.getDouble("geoLat", 0);
  settings.cachedLongitude = preferences.getDouble("geoLon", 0);
  settings.screenTimeoutSec = preferences.getUInt("screenTimeout", 30);
  settings.brightness = preferences.getUChar("brightness", 128);

  reading.temperature = preferences.getFloat("temperature", 0);
  reading.conditionId = preferences.getInt("conditionId", 800);
  reading.description = preferences.getString("description", "");
  reading.location = preferences.getString("readingLocation", "");
  reading.units = preferences.getString("readingUnits", "");
  reading.updatedAt = preferences.getULong("updatedAt", 0);
  reading.valid = preferences.getBool("hasReading", false);
  return true;
}

String urlEncode(const String& value) {
  String encoded;
  encoded.reserve(value.length() * 3);
  const char hex[] = "0123456789ABCDEF";
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(value[i]);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == '~') {
      encoded += static_cast<char>(c);
    } else {
      encoded += '%';
      encoded += hex[c >> 4];
      encoded += hex[c & 0x0F];
    }
  }
  return encoded;
}

String cleanField(const char* value) {
  String result(value);
  result.trim();
  return result;
}

bool isAlpha(char value) {
  return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

// putString() returns the byte count written, so an empty value reads as 0
// even on success; compare against the expected length instead.
bool putText(const char* key, const String& value) {
  return preferences.putString(key, value) == value.length();
}

bool saveSettings() {
  return putText("apiKey", settings.apiKey) &&
         putText("postal", settings.postalCode) &&
         putText("country", settings.countryCode) &&
         putText("units", settings.units) &&
         putText("label", settings.locationLabel) &&
         putText("latitude", settings.latitudeInput) &&
         putText("longitude", settings.longitudeInput) &&
         preferences.putUInt("screenTimeout", settings.screenTimeoutSec) > 0 &&
         preferences.putUChar("brightness", settings.brightness) > 0;
}

bool saveReading() {
  return preferences.putFloat("temperature", reading.temperature) > 0 &&
         preferences.putInt("conditionId", reading.conditionId) > 0 &&
         putText("description", reading.description) &&
         putText("readingLocation", reading.location) &&
         putText("readingUnits", reading.units) &&
         preferences.putULong("updatedAt",
                              static_cast<uint32_t>(reading.updatedAt)) > 0 &&
         preferences.putBool("hasReading", reading.valid) > 0;
}

String temperatureUnit(const String& units) {
  return units == "metric" ? "C" : "F";
}

void drawSun(int centerX, int centerY, int radius, uint16_t color) {
  M5.Display.fillCircle(centerX, centerY, radius, color);
  for (int angle = 0; angle < 360; angle += 45) {
    const float radians = angle * 0.0174533F;
    const int innerX = centerX + static_cast<int>(cosf(radians) * (radius + 5));
    const int innerY = centerY + static_cast<int>(sinf(radians) * (radius + 5));
    const int outerX = centerX + static_cast<int>(cosf(radians) * (radius + 12));
    const int outerY = centerY + static_cast<int>(sinf(radians) * (radius + 12));
    M5.Display.drawLine(innerX, innerY, outerX, outerY, color);
  }
}

void drawCloud(int x, int y, uint16_t color) {
  M5.Display.fillCircle(x + 20, y + 30, 15, color);
  M5.Display.fillCircle(x + 39, y + 20, 19, color);
  M5.Display.fillCircle(x + 59, y + 31, 14, color);
  M5.Display.fillRoundRect(x + 12, y + 29, 56, 22, 10, color);
}

void drawWeatherIcon(int conditionId, int x, int y) {
  const bool partlyCloudy = conditionId == 801;
  if (conditionId >= 700 && conditionId < 800) {
    M5.Display.drawLine(x + 13, y + 20, x + 68, y + 20, TFT_LIGHTGREY);
    M5.Display.drawLine(x + 7, y + 39, x + 61, y + 39, TFT_LIGHTGREY);
    M5.Display.drawLine(x + 15, y + 58, x + 70, y + 58, TFT_LIGHTGREY);
    return;
  }
  if (conditionId == 800 || partlyCloudy) {
    drawSun(x + 42, y + 31, 19, TFT_YELLOW);
    if (partlyCloudy) {
      drawCloud(x + 3, y + 22, TFT_WHITE);
    }
    return;
  }

  const uint16_t cloudColor = TFT_LIGHTGREY;
  drawCloud(x + 2, y + 4, cloudColor);
  if (conditionId >= 200 && conditionId < 300) {
    M5.Display.drawLine(x + 43, y + 48, x + 32, y + 66, TFT_YELLOW);
    M5.Display.drawLine(x + 32, y + 66, x + 43, y + 66, TFT_YELLOW);
    M5.Display.drawLine(x + 43, y + 66, x + 35, y + 78, TFT_YELLOW);
  } else if ((conditionId >= 300 && conditionId < 600) ||
             (conditionId >= 600 && conditionId < 700)) {
    const uint16_t precipitationColor =
        conditionId >= 600 ? TFT_WHITE : TFT_CYAN;
    for (int dropX = x + 24; dropX <= x + 58; dropX += 17) {
      if (conditionId >= 600) {
        M5.Display.drawLine(dropX - 4, y + 62, dropX + 4, y + 70,
                            precipitationColor);
        M5.Display.drawLine(dropX + 4, y + 62, dropX - 4, y + 70,
                            precipitationColor);
        M5.Display.drawLine(dropX, y + 59, dropX, y + 73,
                            precipitationColor);
      } else {
        M5.Display.drawLine(dropX + 3, y + 59, dropX - 3, y + 71,
                            precipitationColor);
      }
    }
  } else {
    M5.Display.drawLine(x + 14, y + 61, x + 65, y + 61, TFT_LIGHTGREY);
    M5.Display.drawLine(x + 21, y + 70, x + 58, y + 70, TFT_LIGHTGREY);
    M5.Display.drawLine(x + 29, y + 79, x + 50, y + 79, TFT_LIGHTGREY);
  }
}

void drawFittedText(const String& text, int x, int y, int maxWidth,
                    int maxTextSize) {
  M5.Display.setFont(nullptr);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  int textSize = maxTextSize;
  M5.Display.setTextSize(textSize);
  while (textSize > 1 && M5.Display.textWidth(text) > maxWidth) {
    M5.Display.setTextSize(--textSize);
  }
  String visibleText = text;
  while (visibleText.length() > 3 && M5.Display.textWidth(visibleText) > maxWidth) {
    visibleText.remove(visibleText.length() - 1);
  }
  if (visibleText.length() < text.length() && visibleText.length() > 3) {
    visibleText.remove(visibleText.length() - 3);
    visibleText += "...";
  }
  M5.Display.setCursor(x, y);
  M5.Display.print(visibleText);
}

void drawScreen() {
  if (!screenOn) {
    return;
  }
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(12, 10);
  M5.Display.print("WEATHER");
  drawFittedText(settings.locationLabel.length() ? settings.locationLabel
                                                  : "Outdoor conditions",
                 12, 34, M5.Display.width() - 24, 2);

  if (reading.valid) {
    // Temperature and icon share the band below the header; the text lines
    // get the full width underneath so nothing overlaps.
    constexpr int kTemperatureTop = 60;
    constexpr int kIconWidth = 80;
    const int iconX = M5.Display.width() - kIconWidth - 12;
    const int maxTemperatureWidth = iconX - 12 - 30;

    char temperature[16];
    snprintf(temperature, sizeof(temperature), "%.1f", reading.temperature);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setFont(&fonts::Font8);
    if (M5.Display.textWidth(temperature) > maxTemperatureWidth) {
      M5.Display.setFont(&fonts::Font7);
    }
    const int temperatureWidth = M5.Display.textWidth(temperature);
    M5.Display.setCursor(12, kTemperatureTop);
    M5.Display.print(temperature);
    M5.Display.setFont(nullptr);
    M5.Display.setTextSize(2);
    const int unitX = 12 + temperatureWidth + 6;
    M5.Display.drawCircle(unitX + 3, kTemperatureTop + 4, 3, TFT_WHITE);
    M5.Display.setCursor(unitX + 10, kTemperatureTop);
    M5.Display.print(temperatureUnit(reading.units));

    drawWeatherIcon(reading.conditionId, iconX, kTemperatureTop - 4);
    drawFittedText(reading.description, 12, 148, M5.Display.width() - 24, 2);
    drawFittedText(reading.location, 12, 174, M5.Display.width() - 24, 2);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    M5.Display.setCursor(12, 200);
    const time_t now = time(nullptr);
    if (reading.updatedAt > 0 && now >= reading.updatedAt) {
      const long minutes = static_cast<long>((now - reading.updatedAt) / 60);
      M5.Display.printf("Updated %ld min ago", minutes);
    } else {
      M5.Display.print("Last saved reading");
    }
  } else {
    M5.Display.setTextSize(2);
    M5.Display.setCursor(12, 86);
    M5.Display.print("No weather data yet");
    M5.Display.setCursor(12, 122);
    M5.Display.print("Hold screen to configure");
  }

  drawFittedText(statusMessage + "  |  Hold to set up", 12, 233,
                 M5.Display.width() - 24, 1);
}

bool onExternalPower() { return M5.Power.Axp2101.isVBUS(); }

void extendScreenTimeout() {
  screenOffAt = millis() + settings.screenTimeoutSec * 1000UL;
}

void wakeScreen() {
  extendScreenTimeout();
  if (screenOn) {
    return;
  }
  screenOn = true;
  M5.Display.wakeup();
  M5.Display.setBrightness(settings.brightness);
  drawScreen();
  nextClockRedrawAt = millis() + 60000UL;
}

void sleepScreen() {
  if (!screenOn) {
    return;
  }
  screenOn = false;
  M5.Display.setBrightness(0);
  M5.Display.sleep();
}

// The backlight is the largest power draw, so blank it after the timeout
// unless the owner disabled the timeout or USB power is present.
void maybeSleepScreen() {
  if (!screenOn) {
    return;
  }
  if (settings.screenTimeoutSec == 0 || onExternalPower()) {
    extendScreenTimeout();
    return;
  }
  if (static_cast<int32_t>(millis() - screenOffAt) >= 0) {
    sleepScreen();
  }
}

void showPortalCredentials(const String& apName, const String& apPassword) {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(10, 12);
  M5.Display.print("Wi-Fi setup");
  M5.Display.setTextSize(1);
  M5.Display.setCursor(10, 56);
  M5.Display.print("Connect to this network:");
  M5.Display.setCursor(10, 76);
  M5.Display.print(apName);
  M5.Display.setCursor(10, 102);
  M5.Display.print("Password:");
  M5.Display.setCursor(10, 122);
  M5.Display.print(apPassword);
  M5.Display.setCursor(10, 164);
  M5.Display.print("Then open the sign-in page");
  M5.Display.setCursor(10, 180);
  M5.Display.print("to enter Wi-Fi and weather settings.");
}

String makeApPassword() {
  char password[13];
  snprintf(password, sizeof(password), "%08lX",
           static_cast<unsigned long>(esp_random()));
  return String(password);
}

bool validLocationSettings() {
  if (settings.apiKey.length() == 0) {
    statusMessage = "Open setup: missing API key";
    return false;
  }
  if (settings.units != "metric" && settings.units != "imperial") {
    statusMessage = "Use metric or imperial units";
    return false;
  }
  if (settings.postalCode.length() == 0) {
    char* latitudeEnd = nullptr;
    char* longitudeEnd = nullptr;
    const double latitude = strtod(settings.latitudeInput.c_str(), &latitudeEnd);
    const double longitude =
        strtod(settings.longitudeInput.c_str(), &longitudeEnd);
    if (settings.latitudeInput.length() == 0 ||
        settings.longitudeInput.length() == 0 ||
        latitudeEnd == settings.latitudeInput.c_str() || *latitudeEnd != '\0' ||
        longitudeEnd == settings.longitudeInput.c_str() ||
        *longitudeEnd != '\0' || !isfinite(latitude) || !isfinite(longitude) ||
        latitude < -90 || latitude > 90 || longitude < -180 ||
        longitude > 180) {
      statusMessage = "Set ZIP/country or valid coordinates";
      return false;
    }
    return true;
  }
  if (settings.countryCode.length() != 2 ||
      !isAlpha(settings.countryCode[0]) || !isAlpha(settings.countryCode[1])) {
    statusMessage = "Set postal code and 2-letter country";
    return false;
  }
  return true;
}

bool resolveLocation(double& latitude, double& longitude, String& placeName) {
  if (settings.postalCode.length() == 0) {
    latitude = strtod(settings.latitudeInput.c_str(), nullptr);
    longitude = strtod(settings.longitudeInput.c_str(), nullptr);
    return true;
  }

  const bool cached =
      settings.geoPostalCode == settings.postalCode &&
      settings.geoCountryCode == settings.countryCode &&
      settings.cachedLatitude >= -90 && settings.cachedLatitude <= 90 &&
      settings.cachedLongitude >= -180 && settings.cachedLongitude <= 180 &&
      (settings.cachedLatitude != 0 || settings.cachedLongitude != 0);
  if (cached) {
    latitude = settings.cachedLatitude;
    longitude = settings.cachedLongitude;
    return true;
  }

  WiFiClientSecure client;
  client.setCACert(OPENWEATHER_ROOT_CA);
  HTTPClient http;
  const String url = String("https://") + kWeatherHost +
                     "/geo/1.0/zip?zip=" + urlEncode(settings.postalCode) +
                     "%2C" + urlEncode(settings.countryCode) +
                     "&appid=" + urlEncode(settings.apiKey);
  http.setTimeout(12000);
  if (!http.begin(client, url)) {
    statusMessage = "Could not start location lookup";
    return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    statusMessage = code == 401 ? "OpenWeather rejected API key"
                                : "Postal-code lookup failed";
    http.end();
    return false;
  }

  JsonDocument document;
  const DeserializationError error = deserializeJson(document, http.getStream());
  http.end();
  if (error || !document["lat"].is<double>() ||
      !document["lon"].is<double>()) {
    statusMessage = "Invalid postal-code response";
    return false;
  }

  latitude = document["lat"].as<double>();
  longitude = document["lon"].as<double>();
  if (latitude < -90 || latitude > 90 || longitude < -180 ||
      longitude > 180) {
    statusMessage = "Invalid coordinates from lookup";
    return false;
  }
  placeName = document["name"] | "";
  settings.geoPostalCode = settings.postalCode;
  settings.geoCountryCode = settings.countryCode;
  settings.cachedLatitude = latitude;
  settings.cachedLongitude = longitude;
  preferences.putString("geoPostal", settings.geoPostalCode);
  preferences.putString("geoCountry", settings.geoCountryCode);
  preferences.putDouble("geoLat", latitude);
  preferences.putDouble("geoLon", longitude);
  return true;
}

bool fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    statusMessage = "Wi-Fi disconnected";
    return false;
  }
  if (!validLocationSettings()) {
    return false;
  }

  double latitude = 0;
  double longitude = 0;
  String geocodedPlace;
  if (!resolveLocation(latitude, longitude, geocodedPlace)) {
    return false;
  }

  statusMessage = "Updating";
  drawScreen();

  WiFiClientSecure client;
  client.setCACert(OPENWEATHER_ROOT_CA);
  HTTPClient http;
  const String url = String("https://") + kWeatherHost +
                     "/data/2.5/weather?lat=" + String(latitude, 6) +
                     "&lon=" + String(longitude, 6) + "&units=" +
                     settings.units + "&appid=" + urlEncode(settings.apiKey);
  http.setTimeout(12000);
  if (!http.begin(client, url)) {
    statusMessage = "Could not start weather request";
    return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    statusMessage = code == 401 ? "OpenWeather rejected API key"
                                : code == 429 ? "Weather request limit reached"
                                              : "Weather request failed";
    http.end();
    return false;
  }

  JsonDocument document;
  const DeserializationError error = deserializeJson(document, http.getStream());
  http.end();
  if (error || !document["main"]["temp"].is<float>() ||
      !document["weather"][0]["description"].is<const char*>()) {
    statusMessage = "Invalid weather response";
    return false;
  }

  reading.temperature = document["main"]["temp"].as<float>();
  reading.conditionId = document["weather"][0]["id"].as<int>();
  reading.description = document["weather"][0]["description"].as<String>();
  reading.description.toLowerCase();
  reading.location = settings.locationLabel;
  if (reading.location.length() == 0) {
    reading.location = document["name"] | geocodedPlace;
  }
  reading.units = settings.units;
  reading.updatedAt = time(nullptr);
  reading.valid = true;
  statusMessage = saveReading() ? "Online" : "Online; cache save failed";
  return true;
}

bool syncClock() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  const uint32_t startedAt = millis();
  while (time(nullptr) < kValidEpoch &&
         static_cast<uint32_t>(millis() - startedAt) < 20000) {
    delay(250);
    M5.update();
  }
  return time(nullptr) >= kValidEpoch;
}

bool runPortal(bool forcePortal) {
  const String suffix = WiFi.macAddress().substring(12);
  const String apName = "Weather-" + suffix;
  const String apPassword = makeApPassword();
  wakeScreen();
  showPortalCredentials(apName, apPassword);

  WiFiManager portal;
  portal.setDebugOutput(false);
  portal.setConfigPortalTimeout(300);
  portal.setConnectTimeout(20);

  char apiKey[129];
  char postalCode[25];
  char countryCode[8];
  char units[16];
  char locationLabel[49];
  char latitude[20];
  char longitude[20];
  char screenTimeout[8];
  char brightness[8];
  snprintf(apiKey, sizeof(apiKey), "%s", settings.apiKey.c_str());
  snprintf(postalCode, sizeof(postalCode), "%s", settings.postalCode.c_str());
  snprintf(countryCode, sizeof(countryCode), "%s",
           settings.countryCode.c_str());
  snprintf(units, sizeof(units), "%s", settings.units.c_str());
  snprintf(locationLabel, sizeof(locationLabel), "%s",
           settings.locationLabel.c_str());
  snprintf(latitude, sizeof(latitude), "%s", settings.latitudeInput.c_str());
  snprintf(longitude, sizeof(longitude), "%s",
           settings.longitudeInput.c_str());
  snprintf(screenTimeout, sizeof(screenTimeout), "%lu",
           static_cast<unsigned long>(settings.screenTimeoutSec));
  snprintf(brightness, sizeof(brightness), "%u", settings.brightness);

  WiFiManagerParameter apiKeyParameter("apiKey", "OpenWeather API key", apiKey,
                                       sizeof(apiKey) - 1);
  WiFiManagerParameter postalParameter("postal", "ZIP / postal code",
                                       postalCode, sizeof(postalCode) - 1);
  WiFiManagerParameter countryParameter("country", "Country code (2 letters)",
                                       countryCode, sizeof(countryCode) - 1);
  WiFiManagerParameter unitsParameter("units", "Units: metric or imperial",
                                      units, sizeof(units) - 1);
  WiFiManagerParameter labelParameter("label", "Display location (optional)",
                                      locationLabel,
                                      sizeof(locationLabel) - 1);
  WiFiManagerParameter latitudeParameter(
      "latitude", "Latitude (use if not using ZIP/postal code)", latitude,
      sizeof(latitude) - 1);
  WiFiManagerParameter longitudeParameter(
      "longitude", "Longitude (use if not using ZIP/postal code)", longitude,
      sizeof(longitude) - 1);
  WiFiManagerParameter screenTimeoutParameter(
      "screenTimeout", "Screen off after seconds on battery (0 = always on)",
      screenTimeout, sizeof(screenTimeout) - 1);
  WiFiManagerParameter brightnessParameter(
      "brightness", "Screen brightness (10-255)", brightness,
      sizeof(brightness) - 1);
  portal.addParameter(&apiKeyParameter);
  portal.addParameter(&postalParameter);
  portal.addParameter(&countryParameter);
  portal.addParameter(&unitsParameter);
  portal.addParameter(&labelParameter);
  portal.addParameter(&latitudeParameter);
  portal.addParameter(&longitudeParameter);
  portal.addParameter(&screenTimeoutParameter);
  portal.addParameter(&brightnessParameter);

  const bool connected = forcePortal
                             ? portal.startConfigPortal(apName.c_str(),
                                                        apPassword.c_str())
                             : portal.autoConnect(apName.c_str(),
                                                  apPassword.c_str());
  if (connected) {
    settings.apiKey = cleanField(apiKeyParameter.getValue());
    settings.postalCode = cleanField(postalParameter.getValue());
    settings.countryCode = cleanField(countryParameter.getValue());
    settings.countryCode.toUpperCase();
    settings.units = cleanField(unitsParameter.getValue());
    settings.units.toLowerCase();
    settings.locationLabel = cleanField(labelParameter.getValue());
    settings.latitudeInput = cleanField(latitudeParameter.getValue());
    settings.longitudeInput = cleanField(longitudeParameter.getValue());
    // Out-of-range entries are clamped rather than rejected so setup still
    // completes.
    settings.screenTimeoutSec = static_cast<uint32_t>(
        min(strtoul(screenTimeoutParameter.getValue(), nullptr, 10),
            static_cast<unsigned long>(kMaxScreenTimeoutSec)));
    settings.brightness = static_cast<uint8_t>(constrain(
        strtol(brightnessParameter.getValue(), nullptr, 10),
        static_cast<long>(kMinBrightness), 255L));
    M5.Display.setBrightness(settings.brightness);
    if (!saveSettings()) {
      statusMessage = "Could not save settings";
      return false;
    }
    statusMessage = "Wi-Fi connected";
    return true;
  }

  statusMessage = "Setup timed out; retrying";
  return false;
}

// A tap wakes a blank screen; holding opens setup. The touch that woke the
// screen is ignored until release so it cannot also start the portal.
void handleTouch() {
  if (M5.Touch.getCount() == 0) {
    ignoreTouchUntilRelease = false;
    return;
  }
  const auto touch = M5.Touch.getDetail(0);
  if (!screenOn) {
    if (touch.wasPressed()) {
      wakeScreen();
      ignoreTouchUntilRelease = true;
    }
    return;
  }
  extendScreenTimeout();
  if (ignoreTouchUntilRelease || !touch.wasHold()) {
    return;
  }
  if (runPortal(true)) {
    if (syncClock()) {
      fetchWeather();
    }
    nextRefreshAt = millis() + kRefreshIntervalMs;
  } else {
    nextRefreshAt = millis() + kRetryIntervalMs;
  }
  extendScreenTimeout();
  drawScreen();
}
}  // namespace

void setup() {
  auto config = M5.config();
  M5.begin(config);
  M5.Display.setRotation(1);
  M5.Touch.setHoldThresh(kSetupHoldMs);
  if (!loadSettings()) {
    M5.Display.setBrightness(128);
    statusMessage = "Settings storage unavailable";
    drawScreen();
    while (true) {
      delay(1000);
    }
  }
  M5.Display.setBrightness(settings.brightness);
  extendScreenTimeout();

  statusMessage = "Connecting to Wi-Fi";
  drawScreen();
  if (!runPortal(false)) {
    nextRefreshAt = millis() + kRetryIntervalMs;
    extendScreenTimeout();
    drawScreen();
    return;
  }

  if (syncClock()) {
    fetchWeather();
  } else {
    statusMessage = "Time sync failed; check Wi-Fi";
  }
  nextRefreshAt = millis() +
                  (reading.valid ? kRefreshIntervalMs : kRetryIntervalMs);
  extendScreenTimeout();
  drawScreen();
}

void loop() {
  M5.update();
  handleTouch();
  maybeSleepScreen();

  if (reading.valid &&
      static_cast<int32_t>(millis() - nextClockRedrawAt) >= 0) {
    drawScreen();
    nextClockRedrawAt = millis() + 60000UL;
  }

  if (static_cast<int32_t>(millis() - nextRefreshAt) >= 0) {
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();
      const uint32_t reconnectStartedAt = millis();
      while (WiFi.status() != WL_CONNECTED &&
             static_cast<uint32_t>(millis() - reconnectStartedAt) < 10000) {
        delay(250);
      }
    }
    bool clockReady = time(nullptr) >= kValidEpoch;
    if (WiFi.status() == WL_CONNECTED && !clockReady) {
      clockReady = syncClock();
    }
    const bool updated = clockReady && fetchWeather();
    if (!clockReady) {
      statusMessage = "Time sync failed; retrying";
    }
    nextRefreshAt =
        millis() + (updated ? kRefreshIntervalMs : kRetryIntervalMs);
    drawScreen();
  }

  delay(30);
}

#include "services/wifi_setup.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include <cstdio>

#include <Preferences.h>
#include <esp_system.h>
#include <esp_wifi.h>

#ifdef WM_MDNS
#include <ESPmDNS.h>
#endif

#include "config.h"
#include "services/radar_location.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

portMUX_TYPE s_boot_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_boot_tap_pending = false;
volatile bool s_boot_is_down = false;
volatile unsigned long s_boot_down_ms = 0;
bool s_long_press_handled = false;
bool s_boot_interrupt_attached = false;
volatile bool s_boot_info_toggle_pending = false;
constexpr unsigned long kBootInfoHoldMs = 1800UL;

void IRAM_ATTR onBootButtonIsr() {
  const bool down =
      digitalRead(config::kBootPin) == LOW;

  const unsigned long now = millis();

  portENTER_CRITICAL_ISR(&s_boot_mux);

  if (down) {
    s_boot_is_down = true;
    s_boot_down_ms = now;
  } else if (s_boot_is_down) {
    const unsigned long held =
        now - s_boot_down_ms;

    if (held >= kBootInfoHoldMs &&
        held < config::kBootResetHoldMs) {
      s_boot_info_toggle_pending = true;
    } else if (held >= config::kBootTapMinMs &&
               held < kBootInfoHoldMs) {
      s_boot_tap_pending = true;
    }

    s_boot_is_down = false;
  }

  portEXIT_CRITICAL_ISR(&s_boot_mux);
}

void initBootButton() {
  pinMode(config::kBootPin, INPUT_PULLUP);
  if (s_boot_interrupt_attached) {
    return;
  }
  attachInterrupt(digitalPinToInterrupt(static_cast<uint8_t>(config::kBootPin)),
                  onBootButtonIsr, CHANGE);
  s_boot_interrupt_attached = true;
}

namespace {

/** Separate from planeradar prefs (rangeInit) to avoid NVS handle conflicts. */
constexpr char kWifiPrefsNamespace[] = "wifi";
constexpr char kPrefsForcePortalKey[] = "portal";

bool s_force_config_portal = false;
WiFiManager s_wm;
bool s_wm_configured = false;

void ensureWifiManager();
void startLanWebPortal();
void stopLanWebPortal();
bool wifiLinkUp();

enum class FirstSetupChoice : uint8_t {
  kNone,
  kOnline,
  kOffline,
};

constexpr char kFirstSetupHtml[] = R"HTML(
<!doctype html>
<html lang="de">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Satellite Radar Setup</title>
  <style>
    :root {
      color-scheme: dark;
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      display: grid;
      place-items: center;
      padding: 22px;
      background:
        radial-gradient(circle at top, #14395a 0, #08131f 44%, #03070c 100%);
      color: #eef7ff;
    }
    main {
      width: min(100%, 520px);
      padding: 28px 22px;
      border: 1px solid rgba(126, 211, 255, .25);
      border-radius: 22px;
      background: rgba(5, 17, 28, .92);
      box-shadow: 0 18px 60px rgba(0, 0, 0, .45);
    }
    .eyebrow {
      margin: 0 0 8px;
      color: #67d8ff;
      font-size: .78rem;
      font-weight: 800;
      letter-spacing: .16em;
      text-transform: uppercase;
    }
    h1 {
      margin: 0 0 10px;
      font-size: clamp(1.8rem, 8vw, 2.5rem);
      line-height: 1.05;
    }
    .intro {
      margin: 0 0 24px;
      color: #b7c9d8;
      line-height: 1.5;
    }
    form { margin: 14px 0 0; }
    button {
      width: 100%;
      border: 0;
      border-radius: 15px;
      padding: 17px 18px;
      font: inherit;
      font-size: 1rem;
      font-weight: 850;
      cursor: pointer;
    }
    .online {
      color: #03131b;
      background: linear-gradient(135deg, #61dcff, #74ffa9);
    }
    .offline {
      color: #f3f8fc;
      background: #23384a;
      border: 1px solid #42627c;
    }
    small {
      display: block;
      margin: 8px 3px 0;
      color: #91a8ba;
      line-height: 1.4;
    }
    .note {
      margin: 22px 0 0;
      padding-top: 18px;
      border-top: 1px solid rgba(255, 255, 255, .1);
      color: #7f96a8;
      font-size: .82rem;
      line-height: 1.45;
    }
  </style>
</head>
<body>
  <main>
    <p class="eyebrow">Satellite Radar</p>
    <h1>Startmodus wählen</h1>
    <p class="intro">
      Es sind noch keine WLAN-Zugangsdaten gespeichert.
      Wie möchtest du das Radar starten?
    </p>

    <form action="/online" method="get">
      <button class="online" type="submit">ONLINE EINRICHTEN</button>
      <small>WLAN auswählen, Zugangsdaten speichern und Internetfunktionen nutzen.</small>
    </form>

    <form action="/offline" method="get">
      <button class="offline" type="submit">OFFLINE STARTEN</button>
      <small>Uhrzeit vom Handy übernehmen und vorhandene TLE-Daten verwenden.</small>
    </form>

    <p class="note">
      Diese Auswahl erscheint nur, solange noch kein WLAN gespeichert ist.
    </p>
  </main>
</body>
</html>
)HTML";

constexpr int kCoordParamLen = 20;
constexpr char kCoordInputAttrs[] =
    " type=\"number\" step=\"0.000001\"";

WiFiManagerParameter s_param_lat("radar_lat", "Latitude (deg)", "0",
                                kCoordParamLen, kCoordInputAttrs);
WiFiManagerParameter s_param_lon("radar_lon", "Longitude (deg)", "0",
                                kCoordParamLen, kCoordInputAttrs);

char s_miles_checkbox_attrs[32] = "type=\"checkbox\"";
WiFiManagerParameter s_param_miles("use_miles", "Display distances in miles", "T", 2,
                                   s_miles_checkbox_attrs, WFM_LABEL_AFTER);

char s_runways_checkbox_attrs[32] = "type=\"checkbox\"";
WiFiManagerParameter s_param_runways("show_runways", "Show airport runways", "T", 2,
                                     s_runways_checkbox_attrs, WFM_LABEL_AFTER);

void refreshPortalParamDefaults() {
  char lat_buf[kCoordParamLen + 1];
  char lon_buf[kCoordParamLen + 1];
  snprintf(lat_buf, sizeof(lat_buf), "%.6f", services::location::lat());
  snprintf(lon_buf, sizeof(lon_buf), "%.6f", services::location::lon());
  s_param_lat.setValue(lat_buf, kCoordParamLen);
  s_param_lon.setValue(lon_buf, kCoordParamLen);
  snprintf(s_miles_checkbox_attrs, sizeof(s_miles_checkbox_attrs), "type=\"checkbox\"%s",
           ui::radar::useMiles() ? " checked" : "");
  s_param_miles.setValue("T", 2);
  snprintf(s_runways_checkbox_attrs, sizeof(s_runways_checkbox_attrs),
           "type=\"checkbox\"%s", ui::radar::showRunways() ? " checked" : "");
  s_param_runways.setValue("T", 2);
}

void onPortalParamsSaved() {
  if (!services::location::saveFromStrings(s_param_lat.getValue(),
                                           s_param_lon.getValue())) {
    Serial.println("Invalid lat/lon in portal — keeping previous location");
  }
  ui::radar::saveMilesFromPortal(s_param_miles.getValue());
  ui::radar::saveRunwaysFromPortal(s_param_runways.getValue());
}

void attachPortalParams(WiFiManager& wm) {
  refreshPortalParamDefaults();
  wm.addParameter(&s_param_lat);
  wm.addParameter(&s_param_lon);
  wm.addParameter(&s_param_miles);
  wm.addParameter(&s_param_runways);
  wm.setSaveParamsCallback(onPortalParamsSaved);
}

void markForceConfigPortal() {
  s_force_config_portal = true;
  Preferences prefs;
  if (!prefs.begin(kWifiPrefsNamespace, false)) {
    return;
  }
  prefs.putBool(kPrefsForcePortalKey, true);
  prefs.end();
}

bool consumeForceConfigPortal() {
  if (s_force_config_portal) {
    s_force_config_portal = false;
    Preferences prefs;
    if (prefs.begin(kWifiPrefsNamespace, false)) {
      prefs.remove(kPrefsForcePortalKey);
      prefs.end();
    }
    return true;
  }

  Preferences prefs;
  if (!prefs.begin(kWifiPrefsNamespace, true)) {
    return false;
  }
  const bool pending = prefs.getBool(kPrefsForcePortalKey, false);
  prefs.end();
  if (!pending) {
    return false;
  }

  if (prefs.begin(kWifiPrefsNamespace, false)) {
    prefs.remove(kPrefsForcePortalKey);
    prefs.end();
  }
  return true;
}

bool storedWifiCredentials() {
  wifi_mode_t mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL) {
    WiFi.mode(WIFI_STA);
    delay(50);
  }

  wifi_config_t conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) {
    return false;
  }
  return conf.sta.ssid[0] != '\0';
}

void eraseWifiCredentials() {
  stopLanWebPortal();
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_OFF);
  delay(100);

  ensureWifiManager();
  WiFi.persistent(true);
  s_wm.resetSettings();
  s_wm.erase();
  WiFi.disconnect(true, true);
  WiFi.persistent(false);

  WiFi.mode(WIFI_OFF);
  delay(100);
}

void resetWifiCredentials() {
  markForceConfigPortal();
  eraseWifiCredentials();
  services::location::clear();
  ui::radar::unitsReset();
  Serial.println("WiFi credentials, location, and units cleared");
}

void onConfigPortalApStarted(WiFiManager*) {
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  statusScreenPortal();
#ifdef WM_MDNS
  if (MDNS.begin(config::kPortalHostname)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("Setup portal: http://%s.local (or http://%s)\n",
                  config::kPortalHostname, config::kPortalIp);
  } else {
    Serial.printf("Setup portal: http://%s (mDNS unavailable)\n", config::kPortalIp);
  }
#else
  Serial.printf("Setup portal: http://%s\n", config::kPortalIp);
#endif
}

bool wifiLinkUp() {
  return WiFi.status() == WL_CONNECTED &&
         WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

void ensureWifiManager() {
  if (s_wm_configured) {
    return;
  }
  s_wm.setConfigPortalTimeout(config::kWifiPortalTimeoutSec);
  s_wm.setAPStaticIPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                           IPAddress(255, 255, 255, 0));
  s_wm.setHostname(config::kPortalHostname);
  s_wm.setAPCallback(onConfigPortalApStarted);
  attachPortalParams(s_wm);
  s_wm_configured = true;
}

void startLanWebPortal() {
  if (!wifiLinkUp() || s_wm.getWebPortalActive() ||
      s_wm.getConfigPortalActive()) {
    return;
  }
  refreshPortalParamDefaults();
  WiFi.mode(WIFI_STA);
  s_wm.setConfigPortalBlocking(false);
#ifdef WM_MDNS
  MDNS.end();
  if (MDNS.begin(config::kPortalHostname)) {
    MDNS.addService("http", "tcp", 80);
  }
#endif
  s_wm.startWebPortal();
  Serial.printf("LAN config: http://%s.local or http://%s\n",
                config::kPortalHostname, WiFi.localIP().toString().c_str());
}

void stopLanWebPortal() {
  if (!s_wm.getWebPortalActive()) {
    return;
  }
  s_wm.stopWebPortal();
#ifdef WM_MDNS
  MDNS.end();
#endif
}

void prepareSta() {
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.setAutoReconnect(true);
}

void startStaConnect(const String& ssid, const String& pass) {
  prepareSta();
  if (ssid.length() > 0) {
    WiFi.begin(ssid.c_str(), pass.c_str());
  } else {
    WiFi.begin();
  }
}

bool waitForLinkWithUi(const char* ssid_for_ui, unsigned long attempt_ms) {
  const unsigned long deadline = millis() + attempt_ms;
  while (millis() < deadline) {
    if (wifiLinkUp()) {
      return true;
    }
    bootButtonPollLongPress();
    statusScreenConnectingTick();
    delay(config::kWifiConnectingFrameMs);
  }
  return wifiLinkUp();
}

bool tryConnectWithUi(const String& ssid, const String& pass, bool show_ui) {
  if (wifiLinkUp()) {
    return true;
  }

  const char* ui_ssid = ssid.length() > 0 ? ssid.c_str() : "network";
  if (show_ui) {
    statusScreenConnectingBegin(ui_ssid);
  }

  for (uint8_t attempt = 1; attempt <= config::kWifiConnectAttempts; ++attempt) {
    if (attempt > 1) {
      Serial.printf("WiFi connect retry %u/%u\n", attempt,
                    config::kWifiConnectAttempts);
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      delay(400);
    }

    startStaConnect(ssid, pass);

    if (waitForLinkWithUi(ui_ssid, config::kWifiConnectAttemptMs)) {
      return true;
    }
  }

  return false;
}

bool connectSavedNetwork(bool show_ui) {
  if (!storedWifiCredentials()) {
    return false;
  }

  ensureWifiManager();
  const String ssid = s_wm.getWiFiSSID();
  if (ssid.length() == 0) {
    return false;
  }
  const String pass = s_wm.getWiFiPass();
  return tryConnectWithUi(ssid, pass, show_ui);
}

void sendChoiceAccepted(WebServer& server,
                        const char* heading,
                        const char* text) {
  String page;
  page.reserve(900);
  page += F("<!doctype html><html lang='de'><head><meta charset='utf-8'>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>Satellite Radar</title><style>");
  page += F("body{margin:0;min-height:100vh;display:grid;place-items:center;");
  page += F("padding:24px;background:#07111b;color:#eef7ff;font-family:system-ui,sans-serif}");
  page += F("main{max-width:460px;padding:28px;border:1px solid #29465d;");
  page += F("border-radius:20px;background:#0b1d2b;text-align:center}");
  page += F("h1{margin-top:0}p{color:#b7c9d8;line-height:1.5}</style></head><body><main><h1>");
  page += heading;
  page += F("</h1><p>");
  page += text;
  page += F("</p></main></body></html>");
  server.send(200, "text/html; charset=utf-8", page);
}

FirstSetupChoice waitForFirstSetupChoice() {
  stopLanWebPortal();

#ifdef WM_MDNS
  MDNS.end();
#endif

  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);

  const IPAddress portal_ip(192, 168, 4, 1);
  const IPAddress portal_mask(255, 255, 255, 0);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(portal_ip, portal_ip, portal_mask);

  if (!WiFi.softAP(config::kPortalApName)) {
    Serial.println("First setup chooser: could not start access point");
    WiFi.mode(WIFI_OFF);
    return FirstSetupChoice::kOnline;
  }

  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  DNSServer dns;
  WebServer server(80);
  FirstSetupChoice choice = FirstSetupChoice::kNone;

  server.on("/", HTTP_GET, [&server]() {
    server.send(200, "text/html; charset=utf-8", kFirstSetupHtml);
  });

  server.on("/online", HTTP_GET, [&server, &choice]() {
    choice = FirstSetupChoice::kOnline;
    sendChoiceAccepted(
        server,
        "Online-Einrichtung startet",
        "Der Einrichtungs-Hotspot wird gleich neu gestartet. "
        "Verbinde dich bei Bedarf erneut und wähle dein WLAN aus.");
  });

  server.on("/offline", HTTP_GET, [&server, &choice]() {
    choice = FirstSetupChoice::kOffline;
    sendChoiceAccepted(
        server,
        "Offline-Modus startet",
        "Der Offline-Hotspot wird gleich gestartet. "
        "Verbinde dich damit, damit die Uhrzeit übernommen werden kann.");
  });

  server.onNotFound([&server]() {
    server.send(200, "text/html; charset=utf-8", kFirstSetupHtml);
  });

  dns.start(53, "*", portal_ip);
  server.begin();

  statusScreenPortal();

  Serial.printf(
      "First setup chooser: connect to %s and open http://%s\n",
      config::kPortalApName,
      portal_ip.toString().c_str());

  while (choice == FirstSetupChoice::kNone) {
    dns.processNextRequest();
    server.handleClient();
    bootButtonPollLongPress();
    delay(10);
  }

  delay(350);
  server.stop();
  dns.stop();

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(150);

  return choice;
}

bool openConfigPortal() {
  stopLanWebPortal();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(50);
  statusScreenPortal();
  s_wm.setConfigPortalBlocking(false);
  s_wm.startConfigPortal(config::kPortalApName);
  while (s_wm.getConfigPortalActive()) {
    bootButtonPollLongPress();
    if (s_wm.process()) {
      return true;
    }
    delay(10);
  }
  return wifiLinkUp();
}

}  // namespace

bool wifiShowsSetupScreenOnBoot() {
  if (s_force_config_portal) {
    return true;
  }
  Preferences prefs;
  if (!prefs.begin(kWifiPrefsNamespace, true)) {
    return false;
  }
  const bool pending = prefs.getBool(kPrefsForcePortalKey, false);
  prefs.end();
  return pending;
}

bool wifiBootButtonPressed() {
  return digitalRead(config::kBootPin) == LOW;
}

void bootButtonInit() { initBootButton(); }

bool bootButtonConsumeTap() {
  portENTER_CRITICAL(&s_boot_mux);
  const bool tap = s_boot_tap_pending;
  if (tap) {
    s_boot_tap_pending = false;
  }
  portEXIT_CRITICAL(&s_boot_mux);
  return tap;
}

bool bootButtonConsumeInfoToggle() {
  portENTER_CRITICAL(&s_boot_mux);

  const bool pending = s_boot_info_toggle_pending;

  if (pending) {
    s_boot_info_toggle_pending = false;
  }

  portEXIT_CRITICAL(&s_boot_mux);

  return pending;
}

void bootButtonPollLongPress() {
  if (wifiBootButtonPressed()) {
    portENTER_CRITICAL(&s_boot_mux);
    if (!s_boot_is_down) {
      s_boot_is_down = true;
      s_boot_down_ms = millis();
    }
    const unsigned long down_ms = s_boot_down_ms;
    portEXIT_CRITICAL(&s_boot_mux);

    if (!s_long_press_handled &&
        millis() - down_ms >= config::kBootResetHoldMs) {
      s_long_press_handled = true;
      Serial.println("BOOT held — resetting WiFi");
      wifiResetCredentialsAndReboot();
    }
  } else {
    portENTER_CRITICAL(&s_boot_mux);
    s_boot_is_down = false;
    portEXIT_CRITICAL(&s_boot_mux);
    s_long_press_handled = false;
  }
}

void wifiResetCredentialsAndReboot() {
  resetWifiCredentials();
  statusScreenWifiReset();
  delay(800);
  esp_restart();
}

bool wifiReconnect() {
  initBootButton();
  Serial.println("WiFi reconnecting...");
  return connectSavedNetwork(true);
}

void wifiLoop() {
  ensureWifiManager();
  if (wifiLinkUp()) {
    if (!s_wm.getWebPortalActive() && !s_wm.getConfigPortalActive()) {
      startLanWebPortal();
    }
    if (s_wm.getWebPortalActive() || s_wm.getConfigPortalActive()) {
      bootButtonPollLongPress();
      s_wm.process();
    }
  } else {
    stopLanWebPortal();
  }
}

bool wifiSetupConnect() {
  initBootButton();
  ensureWifiManager();

  const bool force_portal = consumeForceConfigPortal();
  WiFi.setAutoReconnect(false);

  if (force_portal) {
    eraseWifiCredentials();
    WiFi.mode(WIFI_OFF);
    delay(100);
  }

  const bool has_saved_wifi = storedWifiCredentials();

  if (!has_saved_wifi) {
    Serial.println("No saved WiFi — opening online/offline chooser");

    const FirstSetupChoice choice =
        waitForFirstSetupChoice();

    if (choice == FirstSetupChoice::kOffline) {
      Serial.println("First setup choice: offline");
      return false;
    }

    Serial.println("First setup choice: online");

    if (openConfigPortal() && wifiLinkUp()) {
      WiFi.setAutoReconnect(true);
      Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                    WiFi.localIP().toString().c_str());
      return true;
    }

    Serial.println("WiFi connection failed");
    statusScreenConnectFailed();
    return false;
  }

  Serial.println("Connecting to saved WiFi...");

  if (wifiLinkUp()) {
    WiFi.setAutoReconnect(true);
    Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
  }

  if (connectSavedNetwork(true)) {
    WiFi.setAutoReconnect(true);
    Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
  }

  Serial.println("Saved WiFi could not connect — opening setup portal");

  if (openConfigPortal() && wifiLinkUp()) {
    WiFi.setAutoReconnect(true);
    Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
  }

  Serial.println("WiFi connection failed");
  statusScreenConnectFailed();
  return false;
}
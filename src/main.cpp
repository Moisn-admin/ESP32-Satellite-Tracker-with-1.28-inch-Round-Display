/**
 * Plane Radar — WiFi setup, then radar UI on the round GC9A01 display.
 */

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "hardware/display.h"
#include "network/tle_download.h"
#include "satellite/satellite_engine.h"
#include "satellite/satellite_manager.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "services/time_sync.h"
#include "services/offline_time_portal.h"
#include "services/wifi_setup.h"
#include "storage/filesystem.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"
#include "ui/satellite_overlay.h"

namespace {

bool g_radar_visible = false;
bool g_offline_mode = false;
bool g_tracking_ready = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_satellite_update_ms = 0;


void showRadar() {
  ui::radarDisplayDraw();
  g_radar_visible = true;
}

void showRadarIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    return;
  }

  showRadar();
}
bool updateSatellitesAndDraw() {
  if (!satellite::update()) {
    Serial.println("Satellite update failed");
    return false;
  }

  Serial.printf(
      "Visible satellites: %d\n",
      satellite::count());

  satellite::updateAutomaticSelection(millis());

  const Satellite* selected_satellite =
      satellite::selected();

  if (selected_satellite != nullptr) {
    Serial.printf(
        "Selected: %s  Az %.1f  El %.1f\n",
        selected_satellite->name,
        selected_satellite->azimuth,
        selected_satellite->elevation);
  }

  showRadar();
  return true;
}
void onRangeTap() {
  ui::radar::rangeNext();

  char range_label[12];
  ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));

  Serial.printf("Range: %s (outer ~%.0f km)\n",
                range_label,
                ui::radar::rangeCurrent().outer_km);

  if (g_radar_visible) {
    ui::radarDisplayDraw();
  }
}

void handleBootButton() {
  bootButtonPollLongPress();

  if (bootButtonConsumeInfoToggle()) {
    ui::toggleSatelliteInfoBox();
    ui::radarDisplayDraw();

    Serial.printf(
        "Info box: %s\n",
        ui::satelliteInfoBoxVisible() ? "on" : "off");

    return;
  }

  if (bootButtonConsumeTap()) {
    satellite::selectNextManual(millis());
    ui::radarDisplayDraw();
  }
}

  

void fetchAndDrawAircraft() {
  const float fetch_km = ui::radar::fetchRadiusKm();

  if (!services::adsb::fetchUpdate(
          services::location::lat(),
          services::location::lon(),
          fetch_km)) {
    handleBootButton();
    return;
  }

  ui::radarDisplayRefreshAircraft();
  handleBootButton();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("Plane Radar");

  bootButtonInit();
  displayInit();

  if (!storage::begin()) {
    while (true) {
      delay(1000);
    }
  }

  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }

  services::location::init();
  ui::radar::rangeInit();
  services::adsb::setPollFn(wifiLoop);

  const bool wifi_connected = wifiSetupConnect();

  if (wifi_connected) {
    Serial.println("Online mode");

    if (!services::time_sync::begin()) {
      Serial.println("Online time synchronization failed");
      return;
    }
  } else {
    Serial.println("WiFi unavailable — starting offline mode");

    g_offline_mode = true;

    if (!services::offline_time_portal::run()) {
      Serial.println("Offline time synchronization failed");
      return;
    }
  }

  if (!services::time_sync::ready()) {
    Serial.println("No valid system time");
    return;
  }

  g_tracking_ready = true;

  updateSatellitesAndDraw();

  Serial.println("Satellite module ready");
}
void loop() {
  handleBootButton();

  if (!g_tracking_ready) {
    delay(10);
    return;
  }

  // Offline-Betrieb: kein WLAN-Reconnect und kein wifiLoop().
  if (g_offline_mode) {
    if (millis() - g_last_satellite_update_ms >= 2000UL) {
      g_last_satellite_update_ms = millis();
      updateSatellitesAndDraw();
    }

    delay(10);
    return;
  }

  // Ab hier normaler Online-Betrieb.
  wifiLoop();

  if (WiFi.status() != WL_CONNECTED) {
    if (g_radar_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms =
        millis() - g_wifi_down_since;

    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >=
            config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();

      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        showRadarIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;

    if (!g_radar_visible) {
      showRadarIfConnected();
    } else if (
        millis() - g_last_satellite_update_ms >= 2000UL) {
      g_last_satellite_update_ms = millis();
      updateSatellitesAndDraw();
    }
  }

  delay(10);
}
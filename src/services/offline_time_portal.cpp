#include "services/offline_time_portal.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include <cstdlib>

#include "config.h"
#include "services/time_sync.h"
#include "services/wifi_setup.h"
#include "ui/status_screens.h"

namespace services {
namespace offline_time_portal {

namespace {

constexpr byte kDnsPort = 53;
constexpr unsigned long kPortalTimeoutMs =
    10UL * 60UL * 1000UL;

DNSServer s_dns;
WebServer s_server(80);

bool s_active = false;
bool s_time_received = false;
bool s_routes_configured = false;

const char kPortalHtml[] PROGMEM = R"HTML(
<!doctype html>
<html lang="de">
<head>
  <meta charset="utf-8">
  <meta
    name="viewport"
    content="width=device-width, initial-scale=1">

  <title>Satellite Radar – Offline-Zeit</title>

  <style>
    :root {
      color-scheme: dark;
    }

    body {
      font-family: system-ui, sans-serif;
      margin: 0;
      min-height: 100vh;
      display: grid;
      place-items: center;
      background: #111;
      color: #fff;
    }

    main {
      box-sizing: border-box;
      width: min(90vw, 420px);
      padding: 28px;
      text-align: center;
      border: 1px solid #444;
      border-radius: 18px;
      background: #1c1c1c;
    }

    h1 {
      margin-top: 0;
      font-size: 1.5rem;
    }

    #status {
      margin: 20px 0;
      line-height: 1.5;
    }

    button {
      width: 100%;
      padding: 14px;
      border: 0;
      border-radius: 12px;
      font-size: 1rem;
      font-weight: 700;
      cursor: pointer;
    }

    .detail {
      opacity: 0.7;
      font-size: 0.9rem;
    }
  </style>
</head>

<body>
  <main>
    <h1>Satellite Radar</h1>

    <p id="status">
      UTC-Zeit wird vom Handy übernommen …
    </p>

    <button id="retry" type="button" hidden>
      Erneut versuchen
    </button>

    <p class="detail">
      Dafür wird keine Internetverbindung benötigt.
    </p>
  </main>

  <script>
    const statusElement =
      document.getElementById("status");

    const retryButton =
      document.getElementById("retry");

    async function synchronizeTime() {
      retryButton.hidden = true;

      statusElement.textContent =
        "UTC-Zeit wird vom Handy übernommen …";

      /*
       * Date.now() liefert Millisekunden seit 01.01.1970 UTC.
       * Durch 1000 erhalten wir den Unix-Zeitstempel in Sekunden.
       */
      const unixTime =
        Math.floor(Date.now() / 1000);

      try {
        const response = await fetch("/api/time", {
          method: "POST",
          headers: {
            "Content-Type":
              "application/x-www-form-urlencoded"
          },
          body:
            "unix=" +
            encodeURIComponent(unixTime)
        });

        if (!response.ok) {
          throw new Error(
            "HTTP " + response.status
          );
        }

        statusElement.textContent =
          "Zeit erfolgreich synchronisiert. " +
          "Das Satellitenradar wird gestartet.";
      } catch (error) {
        statusElement.textContent =
          "Die Synchronisierung ist fehlgeschlagen. " +
          "Bitte erneut versuchen.";

        retryButton.hidden = false;
      }
    }

    retryButton.addEventListener(
      "click",
      synchronizeTime
    );

    window.addEventListener(
      "load",
      synchronizeTime
    );
  </script>
</body>
</html>
)HTML";

void handleRoot() {
  s_server.send_P(
      200,
      "text/html; charset=utf-8",
      kPortalHtml);
}

void redirectToPortal() {
  s_server.sendHeader(
      "Location",
      String("http://") +
          config::kOfflinePortalIp,
      true);

  s_server.send(
      302,
      "text/plain",
      "");
}

void handleTime() {
  if (!s_server.hasArg("unix")) {
    s_server.send(
        400,
        "text/plain",
        "Missing Unix timestamp");

    return;
  }

  const String timestamp_text =
      s_server.arg("unix");

  const long long parsed =
      strtoll(
          timestamp_text.c_str(),
          nullptr,
          10);

  const time_t unix_time =
      static_cast<time_t>(parsed);

  if (!services::time_sync::setUtc(unix_time)) {
    s_server.send(
        400,
        "text/plain",
        "Invalid Unix timestamp");

    return;
  }

  s_time_received = true;

  s_server.send(
      200,
      "text/plain",
      "OK");
}

void configureRoutes() {
  if (s_routes_configured) {
    return;
  }

  s_server.on(
      "/",
      HTTP_GET,
      handleRoot);

  s_server.on(
      "/api/time",
      HTTP_POST,
      handleTime);

  /*
   * Typische Adressen, mit denen Android, iOS und Windows
   * prüfen, ob ein Captive Portal vorhanden ist.
   */
  s_server.on(
      "/generate_204",
      HTTP_GET,
      redirectToPortal);

  s_server.on(
      "/gen_204",
      HTTP_GET,
      redirectToPortal);

  s_server.on(
      "/hotspot-detect.html",
      HTTP_GET,
      redirectToPortal);

  s_server.on(
      "/connecttest.txt",
      HTTP_GET,
      redirectToPortal);

  s_server.on(
      "/ncsi.txt",
      HTTP_GET,
      redirectToPortal);

  s_server.onNotFound(handleRoot);

  s_routes_configured = true;
}

void stopPortal() {
  s_server.stop();
  s_dns.stop();

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);

  delay(100);
}

}  // namespace

bool run() {
  s_active = false;
  s_time_received = false;

  Serial.println(
      "Offline portal: starting access point");

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  delay(100);

  WiFi.mode(WIFI_AP);

  const IPAddress ap_ip(
      192,
      168,
      4,
      1);

  const IPAddress gateway(
      192,
      168,
      4,
      1);

  const IPAddress netmask(
      255,
      255,
      255,
      0);

  WiFi.softAPConfig(
      ap_ip,
      gateway,
      netmask);

  if (!WiFi.softAP(
          config::kOfflinePortalApName)) {
    Serial.println(
        "Offline portal: access point failed");

    return false;
  }

  configureRoutes();

  s_dns.start(
      kDnsPort,
      "*",
      ap_ip);

  s_server.begin();

  Serial.printf(
      "Offline portal: connect to \"%s\"\n",
      config::kOfflinePortalApName);

  Serial.printf(
      "Offline portal: open http://%s\n",
      config::kOfflinePortalIp);

  statusScreenOfflinePortal();

  const unsigned long started =
      millis();

  while (!s_time_received &&
         millis() - started <
             kPortalTimeoutMs) {
    bootButtonPollLongPress();

    s_dns.processNextRequest();
    s_server.handleClient();

    delay(2);
  }

  if (!s_time_received) {
    Serial.println(
        "Offline portal: timeout");

    stopPortal();
    return false;
  }

  /*
   * Der Browser soll noch die erfolgreiche HTTP-Antwort
   * erhalten, bevor der Hotspot beendet wird.
   */
  const unsigned long response_started =
      millis();

  while (millis() - response_started <
         500UL) {
    s_dns.processNextRequest();
    s_server.handleClient();

    delay(2);
  }

  s_active = true;

  statusScreenOfflineReady(
      services::time_sync::now());

  delay(2200);

  stopPortal();

  Serial.println(
      "Offline portal: time synchronized");

  return true;
}

bool active() {
  return s_active;
}

}  // namespace offline_time_portal
}  // namespace services
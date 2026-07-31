#include "services/time_sync.h"

#include <Arduino.h>
#include <WiFi.h>

#include <sys/time.h>
#include <time.h>

namespace services {
namespace time_sync {

namespace {

constexpr unsigned long kTimeoutMs = 15000UL;

/**
 * Verhindert, dass offensichtlich falsche Werte wie 0 oder 1970
 * als gültige Uhrzeit übernommen werden.
 */
constexpr time_t kMinimumValidUnixTime = 1700000000;

bool s_ready = false;

void printUtcTime(const char* prefix, time_t value) {
  struct tm utc_time {};
  gmtime_r(&value, &utc_time);

  char buffer[32];
  strftime(
      buffer,
      sizeof(buffer),
      "%Y-%m-%d %H:%M:%S UTC",
      &utc_time);

  Serial.printf("%s%s\n", prefix, buffer);
}

}  // namespace

bool begin() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("NTP: WiFi not connected");
    return false;
  }

  Serial.println("NTP: synchronizing UTC time...");

  configTime(
      0,
      0,
      "pool.ntp.org",
      "time.cloudflare.com",
      "time.google.com");

  const unsigned long started = millis();

  while (millis() - started < kTimeoutMs) {
    const time_t current = time(nullptr);

    if (current > kMinimumValidUnixTime) {
      s_ready = true;

      printUtcTime("NTP: ", current);
      return true;
    }

    delay(250);
  }

  Serial.println("NTP: synchronization failed");
  return false;
}

bool setUtc(time_t unix_time) {
  if (unix_time <= kMinimumValidUnixTime) {
    Serial.printf(
        "Manual time: invalid Unix timestamp: %lld\n",
        static_cast<long long>(unix_time));

    return false;
  }

  timeval value {};
  value.tv_sec = unix_time;
  value.tv_usec = 0;

  if (settimeofday(&value, nullptr) != 0) {
    Serial.println("Manual time: settimeofday failed");
    return false;
  }

  s_ready = true;

  printUtcTime("Manual time: ", time(nullptr));
  return true;
}

bool ready() {
  return s_ready;
}

time_t now() {
  return time(nullptr);
}

}  // namespace time_sync
}  // namespace services
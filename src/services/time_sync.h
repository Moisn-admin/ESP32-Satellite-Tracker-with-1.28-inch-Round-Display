#pragma once

#include <time.h>

namespace services {
namespace time_sync {

/** Synchronisiert die Uhr über NTP. WLAN muss verbunden sein. */
bool begin();

/** Setzt die ESP-Systemzeit über einen Unix-Zeitstempel in UTC. */
bool setUtc(time_t unix_time);

/** True, sobald NTP oder Handy-Synchronisation erfolgreich war. */
bool ready();

/** Aktuelle ESP-Systemzeit als Unix-Zeitstempel. */
time_t now();

}  // namespace time_sync
}  // namespace services
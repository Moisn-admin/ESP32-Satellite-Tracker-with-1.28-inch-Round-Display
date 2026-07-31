#pragma once

namespace services {
namespace offline_time_portal {

/**
 * Startet einen temporären WLAN-Hotspot und wartet darauf,
 * dass ein Handy seine aktuelle UTC-Zeit übermittelt.
 */
bool run();

/** True, wenn die Zeit in diesem Startvorgang vom Handy kam. */
bool active();

}  // namespace offline_time_portal
}  // namespace services
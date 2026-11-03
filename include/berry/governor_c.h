#pragma once
// Performance-governor C ABI: lets a host that is not linked against BerryAPI's C++ ABI — a
// launcher's native preloader, which `dlopen`s `libBerryAPI.so` and talks to it through the small C
// surface — read the adaptive quality governor and set its ceilings.
//
// `PerformanceGovernor` (berry/perf.h) is C++ only, so a dlopen'ing host could not reach it. This
// header closes that gap the same way `berry/seam.h` and `berry/replay_c.h` do: a plain C struct of
// function pointers from `BERRY_GetGovernorApi()`.
//
// Fail-closed throughout:
//   * `BERRY_GetGovernorApi` refuses (returns 0) when BerryAPI is not attached or the client (which
//     owns the governor) is not composed, so a host cannot drive a governor that is not running.
//   * The struct carries `size`/`abi_version`; the host checks both before calling a pointer.
//   * Reads that have no data report the governor's own no-data default, never a fabricated reading.

#include "berry/export.h"
#include "berry/version.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The quality knobs, in the governor's enum order. The order is the ABI.
typedef enum BerryQualityKnob {
  BERRY_KNOB_PARTICLES = 0,
  BERRY_KNOB_ENTITY_DISTANCE = 1,
  BERRY_KNOB_ANIMATION_DISTANCE = 2,
  BERRY_KNOB_ENTITY_CULLING = 3,
  BERRY_KNOB_LIGHTING_UPDATES = 4,
  BERRY_KNOB_RENDER_DISTANCE = 5,
  BERRY_KNOB_COUNT = 6,
} BerryQualityKnob;

/// Platform thermal pressure, mirroring `ThermalState`.
typedef enum BerryThermalState {
  BERRY_THERMAL_NOMINAL = 0,
  BERRY_THERMAL_FAIR = 1,
  BERRY_THERMAL_SERIOUS = 2,
  BERRY_THERMAL_CRITICAL = 3,
  BERRY_THERMAL_SHUTDOWN = 4,
} BerryThermalState;

/// The device class the governor tunes for, mirroring `PlatformClass`.
typedef enum BerryPlatformClass {
  BERRY_PLATFORM_GENERIC = 0,
  BERRY_PLATFORM_MOBILE_LOW = 1,
  BERRY_PLATFORM_MOBILE_HIGH = 2,
  BERRY_PLATFORM_DESKTOP = 3,
} BerryPlatformClass;

/// A reading of the governor, flattened.
typedef struct BerryGovernorStatus {
  /// Quality per knob, in `BerryQualityKnob` order, each 0..1.
  float quality[BERRY_KNOB_COUNT];
  /// The thermal state the governor holds.
  int32_t thermal;
  /// 1 while the platform is actually throttling (Serious or worse).
  int32_t thermally_limited;
  /// The device class the governor tunes for.
  int32_t platform;
  /// The hard ceiling a policy set, 0..1.
  float ceiling;
  /// The concrete settings the current quality implies.
  int32_t render_distance_chunks;
  int32_t max_particles;
  float entity_distance;
  float animation_distance;
  int32_t cull_far_entities;
  int32_t lighting_updates_per_tick;
  int32_t target_fps;
} BerryGovernorStatus;

/// The governor C surface. Every pointer is optional; the host checks `size`/`abi_version` first.
typedef struct BerryGovernorApi {
  uint32_t size;
  uint32_t abi_version;

  /// Reads the governor's current state. Writes the no-data default when nothing has run yet.
  void (*status)(BerryGovernorStatus *out);

  /// Reports platform thermal pressure, so the governor clamps and holds. Returns the action taken.
  int32_t (*set_thermal)(int32_t state);

  /// Sets the device class the governor tunes for.
  void (*set_platform)(int32_t platform);

  /// Sets a hard ceiling the governor may not exceed, 0..1. A value outside that is clamped.
  void (*set_ceiling)(float ceiling);

  /// 1 when the governor is present (the client is composed), else 0.
  int32_t (*available)(void);
} BerryGovernorApi;

/// The `BERRY_GOVERNOR_ABI_VERSION` this build was compiled with.
BERRY_API uint32_t BERRY_GovernorAbiVersion(void);

/// The exact `sizeof(BerryGovernorApi)` this build expects.
BERRY_API uint32_t BERRY_GovernorStructSize(void);

/// The governor C surface, or null when BerryAPI is not attached or the client is not composed.
BERRY_API const BerryGovernorApi *BERRY_GetGovernorApi(void);

/// A human-readable name for a thermal state.
BERRY_API const char *BERRY_ThermalStateName(int32_t state);

/// A human-readable name for a quality knob.
BERRY_API const char *BERRY_QualityKnobName(int32_t knob);

#ifdef __cplusplus
}  // extern "C"
#endif

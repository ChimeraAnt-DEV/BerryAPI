// Host tests for the governor C ABI (berry/governor_c.h).
//
// Covers the contract a dlopen'ing host relies on:
//   * The ABI version and struct size are reported and self-consistent.
//   * `BERRY_GetGovernorApi` refuses before attach and before the client is composed.
//   * Every call fails closed before the surface exists (no crash).
//   * The name helpers answer for every enum value and "unknown" otherwise.
#include "berry/governor_c.h"

#include "berry/engine.h"

#include <cstdio>
#include <cstring>

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { \
  printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

int main() {
  // --- Before attach: no surface, and the name helpers still answer. ---
  CHECK(BERRY_GetGovernorApi() == nullptr);
  CHECK(BERRY_GovernorAbiVersion() == 1);
  CHECK(BERRY_GovernorStructSize() > 0);
  CHECK(std::strcmp(BERRY_ThermalStateName(BERRY_THERMAL_NOMINAL), "nominal") == 0);
  CHECK(std::strcmp(BERRY_ThermalStateName(BERRY_THERMAL_SHUTDOWN), "shutdown") == 0);
  CHECK(std::strcmp(BERRY_ThermalStateName(99), "unknown") == 0);
  CHECK(std::strcmp(BERRY_QualityKnobName(BERRY_KNOB_PARTICLES), "particles") == 0);
  CHECK(std::strcmp(BERRY_QualityKnobName(BERRY_KNOB_RENDER_DISTANCE), "render_distance") == 0);
  CHECK(std::strcmp(BERRY_QualityKnobName(99), "unknown") == 0);

  // --- Attach: the surface appears once the client (which owns the governor) is composed. ---
  Berry::Engine& eng = Berry::Engine::Instance();
  eng.Bootstrap();
  CHECK(eng.Attach());

  const BerryGovernorApi* api = BERRY_GetGovernorApi();
  CHECK(api != nullptr);
  if (api == nullptr) {
    printf("no governor api after attach\n");
    return 1;
  }

  // The guard a host checks before calling a single pointer.
  CHECK(api->size == BERRY_GovernorStructSize());
  CHECK(api->abi_version == BERRY_GovernorAbiVersion());
  CHECK(api->size == sizeof(BerryGovernorApi));
  CHECK(api->status && api->set_thermal && api->set_platform && api->set_ceiling && api->available);

  // --- A read reports real values, and quality is in range. ---
  CHECK(api->available() == 1);
  BerryGovernorStatus st;
  api->status(&st);
  for (int i = 0; i < BERRY_KNOB_COUNT; ++i) {
    CHECK(st.quality[i] >= 0.0f && st.quality[i] <= 1.0f);
  }
  CHECK(st.thermal >= BERRY_THERMAL_NOMINAL && st.thermal <= BERRY_THERMAL_SHUTDOWN);
  CHECK(st.target_fps > 0);
  CHECK(st.render_distance_chunks > 0);

  // A null out pointer is refused, not dereferenced.
  api->status(nullptr);

  // --- Thermal pressure clamps and reports. ---
  CHECK(api->set_thermal(BERRY_THERMAL_SERIOUS) >= 0);
  api->status(&st);
  CHECK(st.thermally_limited == 1);
  CHECK(st.quality[BERRY_KNOB_RENDER_DISTANCE] <= st.ceiling + 1e-4f);
  // An out-of-range state is refused.
  CHECK(api->set_thermal(99) == -1);
  CHECK(api->set_thermal(-1) == -1);
  // Back to nominal.
  api->set_thermal(BERRY_THERMAL_NOMINAL);
  api->status(&st);
  CHECK(st.thermally_limited == 0);

  // --- Platform and ceiling. ---
  api->set_platform(BERRY_PLATFORM_MOBILE_LOW);
  api->status(&st);
  CHECK(st.platform == BERRY_PLATFORM_MOBILE_LOW);
  // An out-of-range platform is ignored rather than set.
  api->set_platform(99);
  api->status(&st);
  CHECK(st.platform == BERRY_PLATFORM_MOBILE_LOW);

  api->set_ceiling(0.5f);
  api->status(&st);
  CHECK(st.ceiling >= 0.49f && st.ceiling <= 0.51f);
  // Out-of-range ceilings clamp rather than wrap.
  api->set_ceiling(5.0f);
  api->status(&st);
  CHECK(st.ceiling <= 1.0f);
  api->set_ceiling(-1.0f);
  api->status(&st);
  CHECK(st.ceiling >= 0.0f);

  if (g_fail == 0) printf("test_governor_c: all checks passed\n");
  return g_fail == 0 ? 0 : 1;
}

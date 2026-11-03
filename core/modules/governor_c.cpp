// The governor C ABI (berry/governor_c.h). A thin adapter over PerformanceGovernor, so a host that
// dlopen's libBerryAPI.so can read the governor and set its ceilings without linking the C++ surface.
//
// Fail-closed: the surface is refused before attach, and before the client (which owns the governor)
// is composed. A read with no governor writes the no-data default.

#include "berry/governor_c.h"

#include "berry/client.h"
#include "berry/engine.h"
#include "berry/perf.h"

#include <cstring>

#define BERRY_GOVERNOR_ABI_VERSION 1

namespace {

/// The governor, or null when the engine is not attached or the client is not composed.
Berry::PerformanceGovernor *Governor() {
  Berry::Engine &engine = Berry::Engine::Instance();
  if (!engine.attached()) return nullptr;
  Berry::BerryClient *client = engine.Client();
  if (client == nullptr) return nullptr;
  return &client->governor();
}

/// Writes the no-data default: full quality, nominal, the library's own defaults. Never a fabricated
/// low reading.
void WriteDefault(BerryGovernorStatus *out) {
  for (int i = 0; i < BERRY_KNOB_COUNT; ++i) out->quality[i] = 1.0f;
  out->thermal = BERRY_THERMAL_NOMINAL;
  out->thermally_limited = 0;
  out->platform = BERRY_PLATFORM_GENERIC;
  out->ceiling = 1.0f;
  out->render_distance_chunks = 12;
  out->max_particles = 4000;
  out->entity_distance = 64.0f;
  out->animation_distance = 32.0f;
  out->cull_far_entities = 0;
  out->lighting_updates_per_tick = 8;
  out->target_fps = 60;
}

void Status(BerryGovernorStatus *out) {
  if (out == nullptr) return;
  std::memset(out, 0, sizeof(*out));
  Berry::PerformanceGovernor *gov = Governor();
  if (gov == nullptr) {
    WriteDefault(out);
    return;
  }
  const Berry::QualityLevel &q = gov->quality();
  for (int i = 0; i < BERRY_KNOB_COUNT; ++i) {
    out->quality[i] = q.Get(static_cast<Berry::QualityKnob>(i));
  }
  out->thermal = static_cast<int32_t>(gov->thermalState());
  out->thermally_limited = gov->thermallyLimited() ? 1 : 0;
  out->platform = static_cast<int32_t>(gov->platform());
  out->ceiling = gov->ceiling();
  const Berry::RenderSettings s = gov->settings();
  out->render_distance_chunks = s.renderDistanceChunks;
  out->max_particles = s.maxParticles;
  out->entity_distance = s.entityDistance;
  out->animation_distance = s.animationDistance;
  out->cull_far_entities = s.cullFarEntities ? 1 : 0;
  out->lighting_updates_per_tick = s.lightingUpdatesPerTick;
  out->target_fps = s.targetFps;
}

int32_t SetThermal(int32_t state) {
  Berry::PerformanceGovernor *gov = Governor();
  if (gov == nullptr) return -1;
  if (state < 0 || state > BERRY_THERMAL_SHUTDOWN) return -1;
  return static_cast<int32_t>(gov->OnThermal(static_cast<Berry::ThermalState>(state)));
}

void SetPlatform(int32_t platform) {
  Berry::PerformanceGovernor *gov = Governor();
  if (gov == nullptr) return;
  if (platform < 0 || platform > BERRY_PLATFORM_DESKTOP) return;
  gov->SetPlatform(static_cast<Berry::PlatformClass>(platform));
}

void SetCeiling(float ceiling) {
  Berry::PerformanceGovernor *gov = Governor();
  if (gov == nullptr) return;
  if (!(ceiling >= 0.0f)) ceiling = 0.0f;  // also catches NaN
  if (ceiling > 1.0f) ceiling = 1.0f;
  gov->SetCeiling(ceiling);
}

int32_t Available() { return Governor() != nullptr ? 1 : 0; }

const BerryGovernorApi g_api = {
    sizeof(BerryGovernorApi),
    BERRY_GOVERNOR_ABI_VERSION,
    &Status,
    &SetThermal,
    &SetPlatform,
    &SetCeiling,
    &Available,
};

}  // namespace

extern "C" {

BERRY_API uint32_t BERRY_GovernorAbiVersion(void) { return BERRY_GOVERNOR_ABI_VERSION; }

BERRY_API uint32_t BERRY_GovernorStructSize(void) {
  return static_cast<uint32_t>(sizeof(BerryGovernorApi));
}

BERRY_API const BerryGovernorApi *BERRY_GetGovernorApi(void) {
  // Fail closed: no surface until the engine is attached and the client (which owns the governor)
  // is composed.
  if (Governor() == nullptr) return nullptr;
  return &g_api;
}

BERRY_API const char *BERRY_ThermalStateName(int32_t state) {
  switch (state) {
    case BERRY_THERMAL_NOMINAL: return "nominal";
    case BERRY_THERMAL_FAIR: return "fair";
    case BERRY_THERMAL_SERIOUS: return "serious";
    case BERRY_THERMAL_CRITICAL: return "critical";
    case BERRY_THERMAL_SHUTDOWN: return "shutdown";
    default: return "unknown";
  }
}

BERRY_API const char *BERRY_QualityKnobName(int32_t knob) {
  switch (knob) {
    case BERRY_KNOB_PARTICLES: return "particles";
    case BERRY_KNOB_ENTITY_DISTANCE: return "entity_distance";
    case BERRY_KNOB_ANIMATION_DISTANCE: return "animation_distance";
    case BERRY_KNOB_ENTITY_CULLING: return "entity_culling";
    case BERRY_KNOB_LIGHTING_UPDATES: return "lighting_updates";
    case BERRY_KNOB_RENDER_DISTANCE: return "render_distance";
    default: return "unknown";
  }
}

}  // extern "C"

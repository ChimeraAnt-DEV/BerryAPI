// The replay C ABI (berry/replay_c.h). A thin adapter over ReplayService, so a
// host that dlopen's libBerryAPI.so can drive a replay without linking the C++
// surface.
//
// Fail-closed: every entry point checks that the engine is attached and that the
// service exists, and a failing call writes nothing. The returned struct is a
// single process-lifetime instance whose function pointers never change, so a
// host may cache it after the size/abi check.

#include "berry/replay_c.h"

#include "berry/engine.h"
#include "berry/replay_format.h"
#include "berry/replay_service.h"

#include <cstring>
#include <string>

#define BERRY_REPLAY_ABI_VERSION 2

namespace {

// The last `save` buffer, kept alive until the next call or unload, so the host
// can read the bytes after the call returns.
std::string g_saveBuffer;
// The last `status` string, same lifetime rule.
std::string g_statusBuffer;

Berry::ReplayService* Service() {
  if (!Berry::Engine::Instance().attached()) return nullptr;
  return &Berry::Engine::Instance().Replay();
}

int32_t StatusOf(Berry::ReplayError err) {
  return err == Berry::ReplayError::Ok ? BERRY_REPLAY_OK : BERRY_REPLAY_BAD_REPLAY;
}

int32_t Load(const void* bytes, size_t len) {
  Berry::ReplayService* svc = Service();
  if (svc == nullptr) return BERRY_REPLAY_NOT_ATTACHED;
  if (bytes == nullptr || len == 0) return BERRY_REPLAY_BAD_ARGUMENT;
  std::string data(static_cast<const char*>(bytes), len);
  Berry::ReplayError err = Berry::ReplayError::Ok;
  return svc->Load(data, &err) ? BERRY_REPLAY_OK : StatusOf(err);
}

const void* Save(size_t* out_len) {
  Berry::ReplayService* svc = Service();
  if (svc == nullptr) return nullptr;
  // Something must be exportable: a loaded replay, or a capture that holds
  // sections. `ReplayService::Save` exports the loaded replay, or the capture
  // when none is loaded - so a host that only recorded the packet stream can
  // still save it - but a fresh, empty service still refuses, which is what the
  // "nothing to save" contract has always meant.
  if (!svc->loaded() && svc->capture().sectionCount() == 0) return nullptr;
  g_saveBuffer.clear();
  Berry::ReplayError err = svc->Save(&g_saveBuffer);
  if (err != Berry::ReplayError::Ok) return nullptr;
  if (out_len != nullptr) *out_len = g_saveBuffer.size();
  return g_saveBuffer.data();
}

void Unload() {
  Berry::ReplayService* svc = Service();
  if (svc != nullptr) svc->Unload();
}

int32_t Info(BerryReplayInfo* out) {
  if (out == nullptr) return BERRY_REPLAY_BAD_ARGUMENT;
  std::memset(out, 0, sizeof(*out));
  Berry::ReplayService* svc = Service();
  if (svc == nullptr) return BERRY_REPLAY_NOT_ATTACHED;
  const Berry::ReplayFile* file = svc->file();
  if (file == nullptr) return BERRY_REPLAY_NOT_LOADED;
  out->loaded = 1;
  out->first_tick = file->firstTick();
  out->last_tick = file->lastTick();
  out->section_count = static_cast<uint32_t>(file->sections.size());
  uint32_t records = 0;
  for (const Berry::ReplaySection& s : file->sections) {
    records += static_cast<uint32_t>(s.records.size());
  }
  out->record_count = records;
  out->payload_bytes = static_cast<uint64_t>(file->payloadBytes());
  out->build_id = file->fingerprint.buildId;
  out->protocol_version = file->fingerprint.protocolVersion;
  return BERRY_REPLAY_OK;
}

int32_t Play() {
  Berry::ReplayService* svc = Service();
  if (svc == nullptr) return BERRY_REPLAY_NOT_ATTACHED;
  Berry::ReplayWorld* world = svc->world();
  if (world == nullptr) return BERRY_REPLAY_NOT_LOADED;
  return svc->review().Play(world) ? BERRY_REPLAY_OK : BERRY_REPLAY_NOT_LOADED;
}

void Pause() {
  Berry::ReplayService* svc = Service();
  if (svc != nullptr) svc->review().Pause();
}

void Stop() {
  Berry::ReplayService* svc = Service();
  if (svc != nullptr) svc->review().Stop();
}

int32_t Seek(uint64_t tick) {
  Berry::ReplayService* svc = Service();
  if (svc == nullptr) return BERRY_REPLAY_NOT_ATTACHED;
  return svc->review().Seek(tick) ? BERRY_REPLAY_OK : BERRY_REPLAY_NOT_LOADED;
}

int32_t SetSpeed(float speed) {
  Berry::ReplayService* svc = Service();
  if (svc == nullptr) return BERRY_REPLAY_NOT_ATTACHED;
  return svc->review().SetSpeed(speed) ? BERRY_REPLAY_OK : BERRY_REPLAY_BAD_ARGUMENT;
}

void Playback(BerryReplayPlayback* out) {
  if (out == nullptr) return;
  std::memset(out, 0, sizeof(*out));
  Berry::ReplayService* svc = Service();
  if (svc == nullptr) return;
  out->active = svc->review().active() ? 1 : 0;
  out->playing = svc->review().playing() ? 1 : 0;
  out->active_world = svc->review().activeWorld() ? 1 : 0;
  out->tick = svc->review().tick();
  out->speed = svc->review().speed();
}

void SetLocalPlayer(uint64_t entity_id) {
  Berry::ReplayService* svc = Service();
  if (svc != nullptr) {
    Berry::EntityId id;
    id.raw = entity_id;
    svc->SetLocalPlayer(id);
    svc->review().SetLocalPlayer(id);
  }
}

int32_t Loaded() {
  Berry::ReplayService* svc = Service();
  return (svc != nullptr && svc->loaded()) ? 1 : 0;
}

const char* Status() {
  Berry::ReplayService* svc = Service();
  if (svc == nullptr) {
    g_statusBuffer.clear();
    return g_statusBuffer.c_str();
  }
  g_statusBuffer = svc->status();
  return g_statusBuffer.c_str();
}

//--- capture --------------------------------------------------------------

int32_t CaptureStart() {
  Berry::ReplayService* svc = Service();
  if (svc == nullptr) return BERRY_REPLAY_NOT_ATTACHED;
  // `Start` refuses only when already recording, which is not an error for a
  // host that just wants a session to be running, so both outcomes are OK.
  svc->capture().Start();
  return BERRY_REPLAY_OK;
}

int32_t CaptureStop() {
  Berry::ReplayService* svc = Service();
  if (svc == nullptr) return BERRY_REPLAY_NOT_ATTACHED;
  // Stopping when not recording is a no-op, not a failure.
  svc->capture().Stop();
  return BERRY_REPLAY_OK;
}

int32_t CaptureState() {
  Berry::ReplayService* svc = Service();
  if (svc == nullptr) return -1;
  return static_cast<int32_t>(svc->capture().state());
}

int32_t CapturePacket(int32_t dir, uint32_t id, const void *payload, size_t len,
                      uint64_t tick) {
  Berry::ReplayService* svc = Service();
  if (svc == nullptr) return BERRY_REPLAY_NOT_ATTACHED;
  if (payload == nullptr || len == 0) return BERRY_REPLAY_BAD_ARGUMENT;
  const Berry::PacketDir direction =
      dir == static_cast<int32_t>(Berry::PacketDir::Outbound)
          ? Berry::PacketDir::Outbound
          : Berry::PacketDir::Inbound;
  const std::string body(static_cast<const char*>(payload), len);
  const bool stored = svc->capture().CapturePacket(direction, id, body, tick);
  return stored ? BERRY_REPLAY_OK : BERRY_REPLAY_NOT_RECORDING;
}

const BerryReplayApi g_api = {
    sizeof(BerryReplayApi),
    BERRY_REPLAY_ABI_VERSION,
    &Load,
    &Save,
    &Unload,
    &Info,
    &Play,
    &Pause,
    &Stop,
    &Seek,
    &SetSpeed,
    &Playback,
    &SetLocalPlayer,
    &Loaded,
    &Status,
    &CaptureStart,
    &CaptureStop,
    &CaptureState,
    &CapturePacket,
};

}  // namespace

extern "C" {

BERRY_API uint32_t BERRY_ReplayAbiVersion(void) { return BERRY_REPLAY_ABI_VERSION; }

BERRY_API uint32_t BERRY_ReplayStructSize(void) {
  return static_cast<uint32_t>(sizeof(BerryReplayApi));
}

BERRY_API const BerryReplayApi* BERRY_GetReplayApi(void) {
  // Fail closed: a host must not drive a replay on an engine that is not running.
  if (!Berry::Engine::Instance().attached()) return nullptr;
  return &g_api;
}

BERRY_API const char* BERRY_ReplayStatusName(int32_t status) {
  switch (status) {
    case BERRY_REPLAY_OK: return "ok";
    case BERRY_REPLAY_NOT_ATTACHED: return "not attached";
    case BERRY_REPLAY_BAD_REPLAY: return "bad replay";
    case BERRY_REPLAY_BAD_ARGUMENT: return "bad argument";
    case BERRY_REPLAY_NOT_LOADED: return "no replay loaded";
    case BERRY_REPLAY_NOT_RECORDING: return "not recording";
    default: return "unknown";
  }
}

}  // extern "C"

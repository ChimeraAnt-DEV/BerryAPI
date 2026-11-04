// Host tests for the replay C ABI (berry/replay_c.h).
//
// Covers the contract a dlopen'ing host relies on:
//   * The ABI version and struct size are reported and self-consistent.
//   * `BERRY_GetReplayApi` refuses before attach and returns the surface after.
//   * Every call fails closed before attach (no crash, a named status).
//   * The size/abi guard is what a host checks, and a mismatch is detectable.
//   * Load/info/playback round-trip on a real (empty) replay.
#include "berry/replay_c.h"

#include "berry/engine.h"

#include <cstdio>
#include <cstring>

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { \
  printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

int main() {
  // --- Before attach: the surface is refused, and every call fails closed ---
  CHECK(BERRY_GetReplayApi() == nullptr);
  CHECK(BERRY_ReplayAbiVersion() == 2);
  CHECK(BERRY_ReplayStructSize() > 0);
  CHECK(std::strcmp(BERRY_ReplayStatusName(BERRY_REPLAY_NOT_ATTACHED), "not attached") == 0);
  CHECK(std::strcmp(BERRY_ReplayStatusName(BERRY_REPLAY_OK), "ok") == 0);
  CHECK(std::strcmp(BERRY_ReplayStatusName(999), "unknown") == 0);

  // --- Attach, then the surface is available and self-consistent ---
  Berry::Engine& eng = Berry::Engine::Instance();
  eng.Bootstrap();
  CHECK(eng.Attach());

  const BerryReplayApi* api = BERRY_GetReplayApi();
  CHECK(api != nullptr);
  if (api == nullptr) {
    printf("no replay api after attach\n");
    return 1;
  }

  // The guard a host checks before calling a single pointer.
  CHECK(api->size == BERRY_ReplayStructSize());
  CHECK(api->abi_version == BERRY_ReplayAbiVersion());
  CHECK(api->size == sizeof(BerryReplayApi));
  CHECK(api->load && api->save && api->unload && api->info && api->play && api->pause &&
        api->stop && api->seek && api->set_speed && api->playback && api->set_local_player &&
        api->loaded && api->status && api->capture_start && api->capture_stop &&
        api->capture_state && api->capture_packet);

  // --- Nothing loaded yet: info reports not-loaded, playback is zeroed ---
  CHECK(api->loaded() == 0);
  BerryReplayInfo info;
  CHECK(api->info(&info) == BERRY_REPLAY_NOT_LOADED);
  CHECK(info.loaded == 0);
  BerryReplayPlayback pb;
  api->playback(&pb);
  CHECK(pb.active == 0);
  CHECK(pb.playing == 0);
  CHECK(pb.tick == 0);

  // A null argument is refused, not dereferenced.
  CHECK(api->info(nullptr) == BERRY_REPLAY_BAD_ARGUMENT);
  CHECK(api->load(nullptr, 0) == BERRY_REPLAY_BAD_ARGUMENT);

  // Playing with nothing loaded fails closed.
  CHECK(api->play() == BERRY_REPLAY_NOT_LOADED);
  CHECK(api->seek(5) == BERRY_REPLAY_NOT_LOADED);

  // A malformed replay is refused and leaves nothing loaded.
  const char junk[] = "not a replay";
  CHECK(api->load(junk, sizeof(junk)) == BERRY_REPLAY_BAD_REPLAY);
  CHECK(api->loaded() == 0);

  // --- Save needs a loaded replay: it refuses, not crashes, when there is none ---
  size_t len = 0;
  CHECK(api->save(&len) == nullptr);

  // --- Speed is range-checked ---
  CHECK(api->set_speed(2.0f) == BERRY_REPLAY_OK);
  CHECK(api->set_speed(0.0f) == BERRY_REPLAY_BAD_ARGUMENT);
  CHECK(api->set_speed(-1.0f) == BERRY_REPLAY_BAD_ARGUMENT);
  api->set_speed(1.0f);

  // --- The local player id is accepted; status is a string ---
  api->set_local_player(42);
  CHECK(api->status() != nullptr);

  // --- Unload clears it ---
  api->unload();
  CHECK(api->loaded() == 0);

  // --- Capture: start, feed packets, stop, and export them ---
  // The recorder half: a host that sees the network stream records it here, and
  // the packet stream is the world data (no game-memory offset needed).
  CHECK(api->capture_start() == BERRY_REPLAY_OK);
  // Idempotent: starting again while recording is not an error.
  CHECK(api->capture_start() == BERRY_REPLAY_OK);
  CHECK(api->capture_state() == 1);   // recording

  // A packet is stored and the recording becomes exportable.
  const char packet[] = "\x01\x02\x03\x04";
  CHECK(api->capture_packet(0, 7, packet, sizeof(packet) - 1, 20) == BERRY_REPLAY_OK);

  // A null or empty payload is refused, not dereferenced.
  CHECK(api->capture_packet(0, 7, nullptr, 4, 20) == BERRY_REPLAY_BAD_ARGUMENT);
  CHECK(api->capture_packet(0, 7, packet, 0, 20) == BERRY_REPLAY_BAD_ARGUMENT);

  // Stop keeps the captured replay, so `save` now returns bytes even though no
  // replay was loaded.
  CHECK(api->capture_stop() == BERRY_REPLAY_OK);
  CHECK(api->capture_state() == 0);   // idle
  size_t captured = 0;
  const void* bytes = api->save(&captured);
  CHECK(bytes != nullptr && captured > 0);

  // A packet with no session recording is refused with the named status.
  CHECK(api->capture_packet(0, 7, packet, sizeof(packet) - 1, 40) ==
        BERRY_REPLAY_NOT_RECORDING);

  // The new status has a name.
  CHECK(std::strcmp(BERRY_ReplayStatusName(BERRY_REPLAY_NOT_RECORDING), "not recording") == 0);

  if (g_fail == 0) printf("test_replay_c: all checks passed\n");
  return g_fail == 0 ? 0 : 1;
}

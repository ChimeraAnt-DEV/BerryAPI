#pragma once
// Replay C ABI: lets a host that is not linked against BerryAPI's C++ ABI — a
// launcher's native preloader, which `dlopen`s `libBerryAPI.so` and talks to it
// through the small C surface — load a replay, drive playback and read the
// timeline.
//
// Until now `ReplayService` was C++ only, so a host could not reach it without
// linking the whole C++ surface. This header closes that gap the same way
// `berry/seam.h` closed it for the world seam: a plain C struct of function
// pointers, obtained by calling `BERRY_GetReplayApi()`.
//
// Fail-closed throughout:
//   * `BERRY_GetReplayApi` refuses (returns 0) when BerryAPI is not attached, so
//     a host cannot drive a replay on an engine that is not running.
//   * The struct carries `size`/`abi_version`; the host checks both against
//     `BERRY_ReplayAbiVersion()`/`BERRY_ReplayStructSize()` before it calls a
//     single pointer, so a layout drift is caught by a number.
//   * Every call that can fail returns a non-zero status and writes nothing on
//     failure, so a bad replay leaves the current one untouched.

#include "berry/export.h"
#include "berry/version.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Status codes. 0 is success; a caller tests `!= 0` for failure and may map the
/// value to a message with `BERRY_ReplayStatusName`.
typedef enum BerryReplayStatus {
  BERRY_REPLAY_OK = 0,
  /// BerryAPI is not attached, so there is no replay service to drive.
  BERRY_REPLAY_NOT_ATTACHED = 1,
  /// The bytes were not a replay this build understands.
  BERRY_REPLAY_BAD_REPLAY = 2,
  /// A required argument was null or out of range.
  BERRY_REPLAY_BAD_ARGUMENT = 3,
  /// No replay is loaded.
  BERRY_REPLAY_NOT_LOADED = 4,
} BerryReplayStatus;

/// The loaded replay's summary, flattened so no C++ type crosses the boundary.
typedef struct BerryReplayInfo {
  /// 1 when a replay is loaded.
  int32_t loaded;
  /// The first and last tick the replay covers, or 0 when empty.
  uint64_t first_tick;
  uint64_t last_tick;
  /// Sections (keyframes + deltas) and records across all of them.
  uint32_t section_count;
  uint32_t record_count;
  /// The total decoded payload bytes, for a size report.
  uint64_t payload_bytes;
  /// The build's numeric id and the network protocol version, from the file's
  /// fingerprint. 0 means unknown.
  uint32_t build_id;
  uint32_t protocol_version;
} BerryReplayInfo;

/// Playback state, flattened.
typedef struct BerryReplayPlayback {
  /// 1 while the overlay draws.
  int32_t active;
  /// 1 while it is advancing.
  int32_t playing;
  /// 1 when a world is bound (a replay is loaded and playable).
  int32_t active_world;
  /// The tick currently shown.
  uint64_t tick;
  /// The speed multiplier.
  float speed;
} BerryReplayPlayback;

/// The replay C surface. Every pointer is optional: a null one means this build
/// does not provide that call, and the host must not invoke it. The host checks
/// `size` and `abi_version` before using any pointer.
typedef struct BerryReplayApi {
  /// Must equal `sizeof(BerryReplayApi)`.
  uint32_t size;
  /// Must equal `BERRY_REPLAY_ABI_VERSION`.
  uint32_t abi_version;

  /// Decodes `bytes` into an owned replay and points the review at it. On a
  /// refusal the current replay is left untouched.
  int32_t (*load)(const void *bytes, size_t len);
  /// Encodes the loaded replay back to bytes. Writes the byte count to `*out_len`
  /// and returns the buffer (valid until the next call or `unload`), or null on
  /// failure.
  const void *(*save)(size_t *out_len);
  /// Drops the loaded replay and stops playback.
  void (*unload)(void);
  /// Reads the loaded replay's summary. Returns `BERRY_REPLAY_NOT_LOADED` when
  /// nothing is loaded (and still fills `out` with a zeroed summary).
  int32_t (*info)(BerryReplayInfo *out);

  /// Starts playback at the first keyframe.
  int32_t (*play)(void);
  /// Stops advancing but keeps the reconstructed state.
  void (*pause)(void);
  /// Stops and releases the world.
  void (*stop)(void);
  /// Seeks to an absolute tick. False when there is no world.
  int32_t (*seek)(uint64_t tick);
  /// Sets the playback speed; refused unless finite and in `(0, 32]`.
  int32_t (*set_speed)(float speed);
  /// Reads the playback state. Never fails.
  void (*playback)(BerryReplayPlayback *out);

  /// The local player's id, so the overlay can mark them. 0 means unknown.
  void (*set_local_player)(uint64_t entity_id);

  /// True (1) when a replay is loaded.
  int32_t (*loaded)(void);
  /// A one-line status for a status line, or "" when none. Valid until the next
  /// call.
  const char *(*status)(void);
} BerryReplayApi;

/// The `BERRY_REPLAY_ABI_VERSION` this build was compiled with. The host compares
/// its own expectation against this before calling any pointer.
BERRY_API uint32_t BERRY_ReplayAbiVersion(void);

/// The exact `sizeof(BerryReplayApi)` this build expects, so a host that mirrors
/// the struct reads it before binding and refuses on a mismatch.
BERRY_API uint32_t BERRY_ReplayStructSize(void);

/// Returns the replay C surface, or null when BerryAPI is not attached. The
/// returned pointer is stable for the process's lifetime; the host must check
/// `size`/`abi_version` before using it.
BERRY_API const BerryReplayApi *BERRY_GetReplayApi(void);

/// A human-readable name for a status code, for a host's error line.
BERRY_API const char *BERRY_ReplayStatusName(int32_t status);

#ifdef __cplusplus
}  // extern "C"
#endif

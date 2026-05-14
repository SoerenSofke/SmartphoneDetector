// PersistentArray.h
// ─────────────────────────────────────────────────────────────────────────
// Header-only ESP32-Arduino library: persists an array of N values of
// type T via Preferences/NVS, with global debouncing and lock-free
// cross-core reads.
//
// Supported types: bool, int8_t, uint8_t, int16_t, uint16_t, float
// Constraints:     1 <= N <= 99
// Requires:        C++17 (Arduino-ESP32-Core >= 2.0.7)
//
//
// THREADING MODEL
// ─────────────────────────────────────────────────────────────────────────
//
//   Producer (single thread/core): set(), update(), flush(), begin()
//   Reader   (any other thread):    get()
//
//   Slot values are stored bit-packed into std::atomic<uint32_t>. The
//   Xtensa LX6/LX7 ISA provides 4-byte aligned atomic load, store and
//   CAS (S32C1I) — the generated code is lock-free in practice, even
//   though libstdc++'s is_always_lock_free trait conservatively reports
//   false on the ESP32 Arduino Core 3.x toolchain. 8/16-bit
//   std::atomic<T> would fall back to libatomic locks, which is why we
//   always use 32-bit storage.
//
//   Constraints:
//     - Only ONE producer thread. Multiple concurrent set() callers are
//       not supported (the dirty flag and debounce timer are unprotected).
//     - begin() must complete BEFORE the reader task starts calling
//       get(); otherwise the reader may observe indeterminate values
//       during the initial load. Typical pattern: call begin() in setup(),
//       then xTaskCreatePinnedToCore() for the reader task.
//     - NVS access (begin(), update(), flush()) happens on the producer
//       only.
//
//
// REQUIRED USAGE CONTRACT
// ─────────────────────────────────────────────────────────────────────────
//
//   1) begin() MUST be called in setup() before any other method, and
//      before the reader task is spawned. Without begin(), the first
//      set()+update() will overwrite existing NVS values with constructor
//      defaults — silent data corruption.
//
//   2) nvsNamespace: <= 15 ASCII characters, non-null.
//      Longer namespaces are rejected by the NVS API and begin() will
//      return without loading any data.
//
//   3) One namespace per PersistentArray instance.
//      Two instances sharing the same namespace will overwrite each
//      other's keys (both use "00".."NN" within their namespace).
//
//
// KEY BEHAVIORAL CHARACTERISTICS
// ─────────────────────────────────────────────────────────────────────────
//
//   - Global debouncing: ALL dirty slots commit together, no earlier than
//     commitDelayMs after the last set(). Continuous sets without pause
//     postpone the commit indefinitely — call flush() periodically during
//     long active sessions (e.g. every 30 s) to limit power-loss exposure.
//
//   - No multi-key atomicity: a power loss mid-commit may persist some
//     slots and not others. Per-slot CRC is enforced by NVS; cross-slot
//     consistency is not. For atomic semantics across slots, pack the
//     values into a struct and store it as a blob via the raw NVS API.
//
//   - update() can block the producer for up to ~N * 5 ms during a full
//     commit (e.g. ~60 ms for N=12, ~500 ms for N=99). The reader is
//     unaffected; get() remains wait-free at all times.
//
//   - On NVS write failure: dirty stays true, implicit backoff of
//     commitDelayMs between retries.
//
//   - On NVS read error during begin(): no-op, previous RAM state retained.
//
//   - Equality check in set() is bit-based (memcmp via toBits()). For
//     integer types this is identical to value equality. For float:
//     +0.0 != -0.0 (different bits → write triggered); two NaNs with the
//     same bit pattern compare equal (no infinite re-write), but two
//     differently-encoded NaNs do not.
//
//
// KNOWN PITFALLS
// ─────────────────────────────────────────────────────────────────────────
//
//   - NaN values (float) with varying bit patterns can still cause
//     repeated re-writes. Do not store NaN as a slot value.
//
//   - Class is non-copyable (std::atomic members forbid copy/move).
//
//   - No constructor argument validation. A null nvsNamespace is caught
//     by the Preferences API (begin() returns false); the class then
//     behaves as a pure RAM cache (set/get work, update/flush are no-ops).
//
// ─────────────────────────────────────────────────────────────────────────

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include <cstring>
#include <type_traits>

template <typename T, uint8_t N>
class PersistentArray {
  // ── Type and size requirements (compile-time) ──────────────────────────
  static_assert(
    std::is_same_v<T, bool>     ||
    std::is_same_v<T, int8_t>   || std::is_same_v<T, uint8_t>  ||
    std::is_same_v<T, int16_t>  || std::is_same_v<T, uint16_t> ||
    std::is_same_v<T, float>,
    "Allowed value types: bool, int8_t, uint8_t, int16_t, uint16_t, float."
  );
  static_assert(N > 0,   "N must be > 0");
  static_assert(N < 100, "N must be <= 99 (two-digit key indices)");
  static_assert(sizeof(T) <= sizeof(uint32_t),
                "T must fit into a 32-bit word for atomic storage");
  // Note: we intentionally do NOT static_assert on
  // std::atomic<uint32_t>::is_always_lock_free. GCC's libstdc++ on the
  // ESP32 Arduino Core (3.x) conservatively reports this as false for
  // `unsigned long` (which is what uint32_t resolves to on this
  // toolchain), even though the Xtensa LX6/LX7 ISA provides 4-byte
  // aligned atomic load/store and CAS (S32C1I). The actual generated
  // code is lock-free in practice, so cross-core access via get()
  // remains wait-free.

  // ── Internal constants ─────────────────────────────────────────────────
  static constexpr size_t kKeyBufferSize = 4;

public:
  /// Construct the array without touching NVS. All slots are initialized
  /// to defaultValue in the RAM cache; nothing is loaded from flash until
  /// begin() is called.
  ///
  /// @param nvsNamespace   NVS namespace, <= 15 chars, non-null.
  ///                       Must be unique across PersistentArray instances.
  /// @param defaultValue   Returned by get() when no NVS entry exists.
  /// @param commitDelayMs  Quiet time required after the last set() before
  ///                       update() will persist changes to flash.
  PersistentArray(const char* nvsNamespace,
                  T           defaultValue  = T{},
                  uint32_t    commitDelayMs = 2000)
    : _ns(nvsNamespace)
    , _default(defaultValue)
    , _commitDelay(commitDelayMs)
    , _lastChangeMs(0)
  {
    const uint32_t defaultBits = toBits(_default);
    for (uint8_t i = 0; i < N; ++i) {
      _values[i].store(defaultBits, std::memory_order_relaxed);
      _dirty[i] = false;
    }
  }

  /// Load persisted values from NVS into the RAM cache. Producer-side.
  /// Must be called once in setup(), before spawning a reader task on
  /// the other core. Idempotent: each call performs a full reload,
  /// discarding any uncommitted in-flight changes.
  void begin() {
    Preferences prefs;
    if (!prefs.begin(_ns, /*readOnly=*/true)) return;

    char key[kKeyBufferSize];
    for (uint8_t i = 0; i < N; ++i) {
      makeKey(i, key);
      const T v = readFromPrefs(prefs, key, _default);
      _values[i].store(toBits(v), std::memory_order_relaxed);
      _dirty[i] = false;
    }
    prefs.end();
    _lastChangeMs = 0;
  }

  /// Update a slot. Producer-side; only one thread may call
  /// set()/update()/flush() concurrently. Out-of-range indices and no-op
  /// writes (same bits as currently stored) are silently ignored. The
  /// change is committed to flash later by update(), after commitDelayMs
  /// of inactivity, or immediately by flush().
  void set(uint8_t index, T value) {
    if (index >= N) return;
    const uint32_t bits = toBits(value);
    if (_values[index].load(std::memory_order_relaxed) == bits) return;

    _values[index].store(bits, std::memory_order_release);
    _dirty[index]  = true;
    _lastChangeMs  = millis();
  }

  /// Return the current value from the RAM cache. Thread-safe and
  /// core-safe — callable from any thread, including a different core
  /// than the producer. Wait-free: never blocks. For out-of-range
  /// indices, returns the constructor's defaultValue.
[[gnu::always_inline]]
T get(uint8_t index) const {
    if (__builtin_expect(index >= N, 0)) return _default;
    return fromBits(_values[index].load(std::memory_order_relaxed));
}

  /// Call regularly from the producer's loop. When the debounce window
  /// has elapsed since the last set(), commits all dirty slots to NVS
  /// in a single Preferences session.
  void update() {
    if (static_cast<uint32_t>(millis() - _lastChangeMs) < _commitDelay) return;
    commitDirtySlots();
    // Always advance the timer, even on commit failure — implicit
    // backoff against retry storms when the NVS partition is failing.
    _lastChangeMs = millis();
  }

  /// Immediately commit all dirty slots, regardless of the debounce
  /// timer. Producer-side. Useful before deepSleep(), reboot, or in
  /// response to an explicit save action.
  void flush() {
    commitDirtySlots();
  }

  /// Number of slots, known at compile time.
  static constexpr uint8_t size() noexcept { return N; }

private:
  // ── Bit-packing for atomic storage ─────────────────────────────────────

  static uint32_t toBits(T value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(T));
    return bits;
  }

  static T fromBits(uint32_t bits) {
    T value{};
    std::memcpy(&value, &bits, sizeof(T));
    return value;
  }

  // ── Key construction ───────────────────────────────────────────────────

  void makeKey(uint8_t index, char* out) const {
    snprintf(out, kKeyBufferSize, "%02u", static_cast<unsigned>(index));
  }

  // ── Commit logic ───────────────────────────────────────────────────────

  bool commitDirtySlots() {
    if (!hasAnyDirty()) return true;

    Preferences prefs;
    if (!prefs.begin(_ns, /*readOnly=*/false)) return false;

    char key[kKeyBufferSize];
    bool allOk = true;
    for (uint8_t i = 0; i < N; ++i) {
      if (!_dirty[i]) continue;
      makeKey(i, key);
      const T v = fromBits(_values[i].load(std::memory_order_relaxed));
      if (writeToPrefs(prefs, key, v)) {
        _dirty[i] = false;
      } else {
        allOk = false;
      }
    }
    prefs.end();
    return allOk;
  }

  bool hasAnyDirty() const {
    for (uint8_t i = 0; i < N; ++i) {
      if (_dirty[i]) return true;
    }
    return false;
  }

  // ── Type-dispatched NVS access (compile-time selection) ────────────────

  static T readFromPrefs(Preferences& prefs, const char* key, T defaultValue) {
    if      constexpr (std::is_same_v<T, bool>)     return prefs.getBool  (key, defaultValue);
    else if constexpr (std::is_same_v<T, int8_t>)   return prefs.getChar  (key, defaultValue);
    else if constexpr (std::is_same_v<T, uint8_t>)  return prefs.getUChar (key, defaultValue);
    else if constexpr (std::is_same_v<T, int16_t>)  return prefs.getShort (key, defaultValue);
    else if constexpr (std::is_same_v<T, uint16_t>) return prefs.getUShort(key, defaultValue);
    else if constexpr (std::is_same_v<T, float>)    return prefs.getFloat (key, defaultValue);
    else                                            return defaultValue;
  }

  static bool writeToPrefs(Preferences& prefs, const char* key, T value) {
    if      constexpr (std::is_same_v<T, bool>)     return prefs.putBool  (key, value) > 0;
    else if constexpr (std::is_same_v<T, int8_t>)   return prefs.putChar  (key, value) > 0;
    else if constexpr (std::is_same_v<T, uint8_t>)  return prefs.putUChar (key, value) > 0;
    else if constexpr (std::is_same_v<T, int16_t>)  return prefs.putShort (key, value) > 0;
    else if constexpr (std::is_same_v<T, uint16_t>) return prefs.putUShort(key, value) > 0;
    else if constexpr (std::is_same_v<T, float>)    return prefs.putFloat (key, value) > 0;
    return false;
  }

  // ── Configuration (set once at construction) ───────────────────────────
  const char* _ns;
  T           _default;
  uint32_t    _commitDelay;

  // ── Producer-only state ────────────────────────────────────────────────
  uint32_t _lastChangeMs;
  bool     _dirty[N];

  // ── Shared state, lock-free atomic ─────────────────────────────────────
  std::atomic<uint32_t> _values[N];
};
#pragma once

// ─────────────────────────────────────────────────────────────────
// AtomicQueue — header-only, lock-free single-producer/single-consumer
// ring buffer for ESP32-S3 (Xtensa LX7, dual-core, cache-coherent SRAM).
//
// Despite the generic name, this is strictly an SPSC queue.
//
// Design notes:
//   • Exactly ONE producer task/core calls push(); exactly ONE consumer
//     task/core calls pop(). This invariant is the user's responsibility —
//     it cannot be enforced at compile time. Violating it (e.g. calling
//     push() from both an ISR and a task) races on the shared index and
//     corrupts the buffer silently.
//   • Capacity must be a power of two so index wrap is a cheap bitmask.
//     Effective capacity is Capacity - 1 (one slot is sacrificed to
//     disambiguate the empty and full states).
//   • head_ is written only by the producer, tail_ only by the consumer.
//     Each side reads the opposite index with acquire, writes its own
//     with release — this is the standard Lamport SPSC pattern.
//   • head_ and tail_ are cache-line padded to eliminate false sharing
//     between the two cores.
//   • The element type must be trivially copyable (plain struct of PODs).
//
// Placement:
//   • The queue instance MUST live in internal SRAM. PSRAM on ESP32-S3
//     does not offer the same inter-core ordering guarantees that the
//     acquire/release annotations rely on, and its latency is unsuitable
//     for audio-rate polling. Do not tag the instance with EXT_RAM_BSS_ATTR
//     or allocate it via ps_malloc.
// ─────────────────────────────────────────────────────────────────

#include <atomic>
#include <cstddef>
#include <type_traits>

template <typename T, size_t Capacity>
class AtomicQueue
{
    static_assert(Capacity >= 2, "Capacity must be >= 2");
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");
    static_assert(std::is_trivially_copyable<T>::value,
                  "T must be trivially copyable");

    // Note on lock-freedom:
    //   We deliberately do NOT static_assert on is_always_lock_free.
    //   The Xtensa GCC port reports is_always_lock_free == false for
    //   32-bit atomics because the compile-time guarantee depends on
    //   alignment and build flags, but naturally-aligned std::atomic
    //   operations compile down to the lock-free S32C1I instruction on
    //   ESP32-S3 at runtime. Use is_lock_free() below for a runtime
    //   check in setup() if you want to be certain on an unfamiliar
    //   target.

    static constexpr size_t MASK = Capacity - 1;

    // 64-byte alignment is portable overkill for LX7 (32-byte lines),
    // but guarantees head_ and tail_ land in distinct cache lines
    // regardless of target. The alignas on buf_ is defensive — tail_
    // already fills its own line, but the explicit marker documents
    // intent and survives future field reordering.
    alignas(64) std::atomic<size_t> head_{0};  // producer writes
    alignas(64) std::atomic<size_t> tail_{0};  // consumer writes
    alignas(64) T buf_[Capacity]{};            // zero-initialized for safety

public:
    constexpr AtomicQueue() noexcept = default;

    // Non-copyable, non-movable — the queue is a fixed piece of shared state.
    AtomicQueue(const AtomicQueue &) = delete;
    AtomicQueue &operator=(const AtomicQueue &) = delete;

    /// Enqueue one element. Called only from the single producer.
    /// On false return, the value is dropped (not buffered elsewhere).
    /// @return true on success, false if the queue is full.
    [[nodiscard]] bool push(const T &value) noexcept
    {
        const size_t h    = head_.load(std::memory_order_relaxed);
        const size_t next = (h + 1) & MASK;

        // Full when the next slot would equal tail_.
        if (next == tail_.load(std::memory_order_acquire))
            return false;

        buf_[h] = value;
        head_.store(next, std::memory_order_release);
        return true;
    }

    /// Dequeue one element. Called only from the single consumer.
    /// On false return, `out` is left unmodified.
    /// @return true on success, false if the queue is empty.
    [[nodiscard]] bool pop(T &out) noexcept
    {
        const size_t t = tail_.load(std::memory_order_relaxed);

        // Empty when tail_ has caught up with head_.
        if (t == head_.load(std::memory_order_acquire))
            return false;

        out = buf_[t];
        tail_.store((t + 1) & MASK, std::memory_order_release);
        return true;
    }

    /// Quick emptiness check. Same stale-value caveat as size() —
    /// the answer may be outdated by the time the caller acts on it.
    [[nodiscard]] bool empty() const noexcept
    {
        return head_.load(std::memory_order_relaxed) ==
               tail_.load(std::memory_order_relaxed);
    }

    /// Approximate fill level. The two indices are read separately, so the
    /// value may reflect a state that never existed simultaneously.
    ///   • Called from the producer side: upper bound on occupancy
    ///     (useful for "do I have at least N free slots?").
    ///   • Called from the consumer side: lower bound on occupancy
    ///     (useful for "are there at least M items to read?").
    /// The result is always in [0, MASK].
    [[nodiscard]] size_t size() const noexcept
    {
        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t t = tail_.load(std::memory_order_relaxed);
        return (h - t) & MASK;
    }

    /// Maximum number of elements the queue can hold simultaneously.
    /// One slot of the underlying storage is reserved to disambiguate
    /// empty from full, so this is Capacity - 1.
    [[nodiscard]] static constexpr size_t capacity() noexcept
    {
        return Capacity - 1;
    }
};
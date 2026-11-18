// `SequenceLock<Model, concurrent_writers>`: a sequence lock that hands a
// versioned model between writers and readers.
//
// The lock stores the model together with a sequence number. A writer marks the
// sequence odd while it replaces the model and even again once the copy is
// complete; a reader retries until it observes the same even sequence before
// and after copying the model, so it never returns a torn snapshot.
//
// `read()` returns a `VersionedModel`: the model plus the sequence number that
// produced it. A writer modifies `versioned_model.model` and passes it back to
// `write()`. The write reports `WriteResult::kStored` only when
// `versioned_model.version` is still the current sequence; a writer working
// from an older snapshot gets `WriteResult::kRejected` instead of overwriting a
// newer model with stale data. After a successful write the sequence has
// advanced, so the caller must `read()` again before its next write.
//
// `concurrent_writers` selects the writer contract:
//   - `ConcurrentWriters::Disable` assumes a single writer. `write()` returns
//     void, does not take a mutex, and cannot block.
//   - `ConcurrentWriters::Enabled` serialises writers with a mutex.
//     `write()` returns a `WriteResult`, so a stale writer is rejected.
// Reads never take the mutex.
//
// `Model` must be trivially copyable so that the snapshot copy cannot invoke a
// user-defined operation, and default constructible because a default-
// constructed lock starts with a default `Model`.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <atomic>
#include <type_traits>
#include <utility>

#include <cassert>
#include <cstdint>
#include <mutex>

namespace thermo {

enum class ConcurrentWriters : std::uint8_t { Disable, Enabled };

namespace detail {

// Empty stand-ins for the writer mutex and its scope lock when there is only
// one writer. `NoLock` accepts the `NoMutex` so `do_write()` can construct the
// same `WriterLock` in both modes.
struct NoMutex {};

struct NoLock {
    explicit NoLock(NoMutex& /*mutex*/) noexcept {}
};

}  // namespace detail

template<typename Model, ConcurrentWriters concurrent_writers>
class SequenceLock {
    static_assert(std::is_default_constructible_v<Model>,
                  "SequenceLock<Model> requires a default-constructible Model");

  public:
    // The outcome of a write: only a current version is stored.
    enum class WriteResult : std::uint8_t {
        kStored,    // The version was current; the model was stored.
        kRejected,  // Another writer published first; nothing was stored.
    };

    // A consistent model snapshot together with the sequence number it was
    // read at. Pass it back to `write()` after modifying `model`.
    struct VersionedModel {
        std::uint32_t version = 0;
        Model model{};
    };

    explicit SequenceLock(Model const& initial = Model{}) : cell_{0, initial} {}

    // Return a consistent snapshot, retrying while a writer is active.
    [[nodiscard]] VersionedModel read() const noexcept {
        VersionedModel snapshot;
        for (;;) {
            const std::uint32_t before =
                cell_.version.load(std::memory_order_acquire);
            if (is_writing(before)) {
                continue;
            }

            snapshot.version = before;
            snapshot.model = cell_.model;

            const std::uint32_t after =
                cell_.version.load(std::memory_order_acquire);
            if (before == after) {
                return snapshot;
            }
        }
    }

    // Single-writer form. The caller guarantees that no other task writes, so
    // there is no mutex and nothing to reject: the write cannot fail or block.
    void write(VersionedModel const& versioned_model) noexcept
        requires(concurrent_writers == ConcurrentWriters::Disable)
    {
        const WriteResult result = do_write(versioned_model);
        assert(result == WriteResult::kStored &&
               "single-writer SequenceLock rejected a write");
        (void)result;
    }

    // Multi-writer form. A stale `versioned_model` (another writer got there
    // first) is rejected without changing the cell.
    [[nodiscard]] WriteResult write(
        VersionedModel const& versioned_model) noexcept
        requires(concurrent_writers == ConcurrentWriters::Enabled)
    {
        return do_write(versioned_model);
    }

  private:
    using WriterMutex =
        std::conditional_t<concurrent_writers == ConcurrentWriters::Enabled,
                           std::mutex, detail::NoMutex>;
    using WriterLock =
        std::conditional_t<concurrent_writers == ConcurrentWriters::Enabled,
                           std::lock_guard<std::mutex>, detail::NoLock>;

    // Store `versioned_model.model` if `versioned_model.version` is still
    // current. The lock is a no-op for a single writer.
    [[nodiscard]] WriteResult do_write(
        VersionedModel const& versioned_model) noexcept {
        WriterLock writer(writer_mutex_);

        const std::uint32_t current =
            cell_.version.load(std::memory_order_relaxed);
        if (versioned_model.version != current) {
            return WriteResult::kRejected;
        }

        // Enter the writing state in one atomic step. If another writer moved
        // the version since the load above, this model is stale.
        std::uint32_t expected = current;
        if (!cell_.version.compare_exchange_strong(expected, current + 1,
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed)) {
            return WriteResult::kRejected;
        }
        cell_.model = versioned_model.model;

        // Publish the completed model by returning the version to even. The
        // next successful write must use this new version.
        cell_.version.store(current + 2, std::memory_order_release);
        return WriteResult::kStored;
    }

    // The guarded cell: the model together with the sequence number that marks
    // whether it is stable (even) or being replaced (odd).
    struct Cell {
        std::atomic<std::uint32_t> version{0};
        Model model{};
    };

    static constexpr bool is_writing(std::uint32_t version) noexcept {
        return (version & 1U) != 0U;
    }

    Cell cell_;
    // Empty (and no storage) for a single writer.
    [[no_unique_address]] WriterMutex writer_mutex_;
};

}  // namespace thermo

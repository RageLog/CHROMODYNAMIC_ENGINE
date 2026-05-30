// =============================================================================
// CHROMODYNAMIC — cd/game/save/Save.hpp
// Phase 470 — cd::game::save::SaveSystem (G1.3 of the gameplay library family,
// see ADR-20260530-gameplay-library-family §2.2 G-08).
//
// Slot-based persistence with atomic on-disk writes:
//   * `SaveSystem` owns a per-process storage root (defaults to the OS user
//     data dir). Each slot is a small directory holding a `meta.json` header
//     and a `body.<json|bin>` blob — the body format is caller-chosen via
//     `SaveFormat`.
//   * `save()` is *atomic*: the body is first written to `body.<ext>.tmp`,
//     fsync'd (best effort), then renamed over the final path so a crash
//     mid-write leaves either the previous body intact or no body at all —
//     never a half-written corrupt file. This mirrors UE `SaveGame` and Unity
//     SerializationUtility behaviour.
//   * `load()` returns the bytes verbatim (no decompression / migration yet —
//     phase G5 will layer those on top per the ADR).
//   * Slot ids are restricted to a small ASCII subset so they round-trip
//     safely as folder names on every host filesystem. Invalid ids are
//     rejected up-front rather than producing platform-specific path errors.
//
// Dependencies (CLAUDE.md §7): cd::core only — the library is intentionally
// independent of cd::ecs / cd::scene so it can host both world-snapshots
// (the eventual Phase G5 use case) and lightweight per-feature persistence
// (settings backup, replay header, telemetry sidecar) without forcing a heavy
// transitive include set on every consumer.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cd::game::save
{

// -----------------------------------------------------------------------------
// Domain-specific error codes. Domain id 0x4753 ("GS" — Game Save) keeps us
// well clear of the core/mem/concurrency 0x000x reservations documented in
// cd/core/ErrorCode.hpp.
// -----------------------------------------------------------------------------
namespace save_errors
{
inline constexpr std::uint32_t kDomain = 0x4753;

enum class Code : std::uint32_t
{
    kOk             = 0,
    kInvalidSlotId  = 1,  ///< Slot id failed `is_valid_slot_id()`.
    kSlotNotFound   = 2,  ///< No on-disk record under storage_root/<slot_id>.
    kIoFailed       = 3,  ///< Underlying filesystem operation reported an error.
    kCorruptHeader  = 4,  ///< meta.json is missing or unparseable.
    kFormatMismatch = 5,  ///< Asked for kJson but body was written as kBinary (or vice versa).
};

constexpr cd::core::ErrorCode make(Code c, std::string_view message = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), message };
}
}  // namespace save_errors

// -----------------------------------------------------------------------------
// SaveFormat — body encoding hint stored alongside each slot.
//
// The library does NOT transcode for the caller; the value drives only the
// file extension and a sanity check on `load()` ("the file on disk says it
// was written as kJson — you asked for kBinary, do you really want it?").
// -----------------------------------------------------------------------------
enum class SaveFormat : std::uint8_t
{
    kJson   = 0,  ///< Body written as `body.json`. Caller serializes to JSON bytes.
    kBinary = 1,  ///< Body written as `body.bin`. Caller serializes to opaque bytes.
};

[[nodiscard]] constexpr std::string_view to_extension(SaveFormat f) noexcept
{
    return (f == SaveFormat::kJson) ? std::string_view{"json"} : std::string_view{"bin"};
}

[[nodiscard]] constexpr std::string_view to_string(SaveFormat f) noexcept
{
    return (f == SaveFormat::kJson) ? std::string_view{"json"} : std::string_view{"binary"};
}

// -----------------------------------------------------------------------------
// SaveSlot — handle-style summary of one slot on disk.
//
// Returned by `list_slots()`. `timestamp` is seconds-since-epoch of the last
// successful `save()` and is the sort key used by `list_slots()` (most-recent
// first — matches every shipping engine's save UI ordering).
// -----------------------------------------------------------------------------
struct SaveSlot
{
    std::string   id;                ///< Caller-chosen identifier (slot folder name).
    std::string   label;             ///< Free-form human label ("Autosave 1", "Before boss").
    std::int64_t  timestamp { 0 };   ///< Unix epoch seconds at last write.
    SaveFormat    format    { SaveFormat::kBinary };
    std::uint64_t size_bytes { 0 };  ///< Size of the body on disk (informational).
};

// -----------------------------------------------------------------------------
// Slot-id validation
//
// The slot id is used verbatim as a folder name. To keep behaviour identical
// across Windows / macOS / Linux we restrict it to a small ASCII alphabet:
// letters, digits, `-`, `_`. Length capped at 64 to keep total path length
// well within MAX_PATH considerations. Empty ids and any reserved Windows
// device names (CON, PRN, AUX, NUL, COM1..9, LPT1..9 — case-insensitive)
// are rejected.
//
// Public so callers (UI rebind dialog, REST endpoint) can pre-validate.
// -----------------------------------------------------------------------------
[[nodiscard]] bool is_valid_slot_id(std::string_view id) noexcept;

// -----------------------------------------------------------------------------
// Default storage root — `<user-data-dir>/CHROMODYNAMIC/saves`.
//
// Resolution order:
//   * Windows:  %LOCALAPPDATA%/CHROMODYNAMIC/saves
//               (falls back to %APPDATA% then %USERPROFILE%/AppData/Local).
//   * macOS:    $HOME/Library/Application Support/CHROMODYNAMIC/saves
//   * Linux:    $XDG_DATA_HOME/CHROMODYNAMIC/saves
//               (falls back to $HOME/.local/share/CHROMODYNAMIC/saves).
//
// The path is *computed* (not necessarily created); `SaveSystem::save()`
// creates the directory on first use.
// -----------------------------------------------------------------------------
[[nodiscard]] std::filesystem::path default_storage_root();

// -----------------------------------------------------------------------------
// SaveSystem — slot manager bound to one storage root.
//
// Construction is cheap (no I/O). Operations are not thread-safe; serialize
// access from the caller (the save UI normally runs on the main thread and
// kicks worker save jobs through a queue that owns its own SaveSystem).
// -----------------------------------------------------------------------------
class SaveSystem
{
public:
    SaveSystem();
    explicit SaveSystem(std::filesystem::path storage_root);

    /// Replace the storage root. Does NOT migrate existing slots; the caller
    /// is responsible for moving / copying files if a relocation is needed.
    /// Returns the previous root for restore purposes.
    std::filesystem::path set_storage_root(std::filesystem::path storage_root);

    /// Current storage root.
    [[nodiscard]] const std::filesystem::path& storage_root() const noexcept { return root_; }

    /// Atomically write `blob` into slot `slot_id` with the given format.
    /// The slot folder is created on first use. If a previous body exists
    /// under the *same or different* format extension it is removed after
    /// the new body is in place (so flipping a slot from kJson to kBinary
    /// does not leak the old file).
    ///
    /// `label` is stored in meta.json; pass an empty view to reuse the
    /// previous label (if any) or fall back to the slot id.
    [[nodiscard]] cd::core::Result<void> save(std::string_view              slot_id,
                                              std::span<const std::byte>    blob,
                                              SaveFormat                    format,
                                              std::string_view              label = {});

    /// Load the bytes previously written via `save()`. Returns
    /// `kSlotNotFound` if no slot exists, `kCorruptHeader` if meta.json is
    /// unparseable, `kIoFailed` if the body cannot be read.
    [[nodiscard]] cd::core::Result<std::vector<std::byte>> load(std::string_view slot_id) const;

    /// Same as `load()` but additionally verifies the on-disk format matches
    /// `expected`. Returns `kFormatMismatch` otherwise.
    [[nodiscard]] cd::core::Result<std::vector<std::byte>>
    load(std::string_view slot_id, SaveFormat expected) const;

    /// Enumerate every well-formed slot under the storage root. Sorted by
    /// `timestamp` *descending* (most recently saved first) to match the
    /// ordering every modern save UI expects. Slots whose meta.json is
    /// missing or unparseable are silently skipped — they will not crash a
    /// menu render. Use `load()` to surface per-slot errors explicitly.
    [[nodiscard]] std::vector<SaveSlot> list_slots() const;

    /// Remove the slot folder + every file inside. Returns:
    ///   * `kOk` if the slot was present and successfully removed,
    ///   * `kSlotNotFound` if nothing existed under that id,
    ///   * `kIoFailed` if filesystem removal failed mid-walk,
    ///   * `kInvalidSlotId` if `slot_id` fails validation.
    [[nodiscard]] cd::core::Result<void> delete_slot(std::string_view slot_id);

    /// True if a slot record exists under the current storage root.
    [[nodiscard]] bool slot_exists(std::string_view slot_id) const;

    /// Slot folder under the current root. Public so tests and the
    /// recovery tooling can poke at the on-disk layout deterministically.
    [[nodiscard]] std::filesystem::path slot_directory(std::string_view slot_id) const;

private:
    // Header file lives at `<root>/<slot>/meta.json`. The body file lives at
    // `<root>/<slot>/body.<ext>`. We keep the body in a separate file so the
    // header is cheap to enumerate without paging in the whole snapshot.
    [[nodiscard]] static std::filesystem::path meta_path(const std::filesystem::path& slot_dir);
    [[nodiscard]] static std::filesystem::path body_path(const std::filesystem::path& slot_dir,
                                                         SaveFormat                   format);
    [[nodiscard]] static std::filesystem::path tmp_body_path(const std::filesystem::path& slot_dir,
                                                             SaveFormat                   format);

    [[nodiscard]] cd::core::Result<SaveSlot> read_meta(const std::filesystem::path& slot_dir) const;

    [[nodiscard]] cd::core::Result<void> write_meta_atomic(const std::filesystem::path& slot_dir,
                                                           const SaveSlot&              header) const;

    std::filesystem::path root_;
};

}  // namespace cd::game::save

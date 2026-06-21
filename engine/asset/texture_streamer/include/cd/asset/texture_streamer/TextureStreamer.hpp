// =============================================================================
// CHROMODYNAMIC — cd/asset/texture_streamer/TextureStreamer.hpp
// Phase 599 — cd::asset::texture_streamer (Sprint-1: synchronous)
// Phase 714 — cd::asset::texture_streamer (Sprint-2: opt-in async path)
// Band 6   — real cdtex / image decode wired behind the async orchestration.
// Phase 1268 — CPU charter complete: mip residency policy, LRU/budget eviction,
//              screen-coverage priority weighting, mip-upgrade re-enqueue,
//              path bookkeeping (PathGuard move), request dedup extension.
//
// GPU-side texture streaming coordinator. Accepts enqueue/cancel requests
// keyed by asset path and drives upload via cd::rhi::IDevice on each tick().
//
// Sync mode  — highest-priority pending request decoded + uploaded inline.
// Async mode — opt-in via AsyncTexturePool + TextureStreamerConfig.
//   * Set use_async = true (and optionally worker_count > 0) to offload the
//     CPU decode to background threads while tick() only drains completions.
//   * Priority: higher uint8 value = higher priority (255 = highest, 0 = lowest).
//   * screen_coverage: 0..1 float added to effective priority for scheduling
//     (larger on-screen footprint = higher urgency).
//
// DECODE: workers run the real cd::asset::cdtex BC7 CPU decoder so each
//   completion carries REAL block bytes + dimensions (a DecodedTexture). The
//   GPU upload (create_texture) stays the owner-thread / RHI-consumer's job —
//   the streamer never calls IDevice from a worker thread. The RGBA8 image
//   (PNG/JPG via cd::asset_image) path is SEALED for now — see the Band-6 ADR.
//
// LRU/Budget eviction:
//   set_texture_budget(N) caps resident GPU textures at N. When a new load
//   would push the count past the budget, the least-recently-used resident
//   texture is evicted (GPU handle destroyed, record removed). Callers can
//   also call evict_lru() manually or touch(path) to promote a texture to
//   the MRU position without re-loading it.
//
// Mip residency policy:
//   is_mip_loaded(path, mip_level) reports whether a given mip level is
//   resident. resident_mip_target(path) returns the mip_target stored in the
//   loaded record. enqueue_mip_upgrade(request) re-queues an already-loaded
//   texture with a higher mip_target, replacing the existing record on the
//   next successful tick.
//
// SEALED (multi-week backend work, see PROJECT_COMPLETION_STATUS.md):
//   - Real async GPU upload (staging buffer + transfer queue).
//   - Sparse-residency / virtual textures (VkSparseImageMemoryBind).
//   - PNG/JPG via cd::asset_image (stb symbol collision — ADR-20260616-band6).
//
// Namespace: cd::asset::texture_streamer
// =============================================================================
#pragma once

#include <cd/rhi/Handles.hpp>
#include <cd/rhi/IDevice.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::asset::texture_streamer
{

// ---- Public types -----------------------------------------------------------

/// Caller-facing load request.
///   `mip_target`      — target mip level count to stream (0 = base mip only, same as 1).
///   `priority`        — 255 = highest, 0 = lowest (discrete bucket).
///   `screen_coverage` — normalized [0,1] fraction of screen covered by this texture.
///                       Blended into the effective scheduling priority so larger
///                       on-screen textures are loaded before tiny background ones.
///                       Default 0.0 = no coverage weighting (backward-compatible).
struct StreamRequest
{
    std::string  asset_path;
    std::uint8_t mip_target      { 0U };
    std::uint8_t priority        { 128U };
    float        screen_coverage { 0.0F };
};

/// Decoded CPU texture — the REAL payload produced by a worker (or the sync
/// path) before any GPU upload. Carries the actual pixel data + dimensions
/// read from the file so consumers + tests can verify real decode happened
/// (not a placeholder). `is_block_compressed` distinguishes a .cdtex BC7
/// block payload (`blocks`) from an RGBA8 texel payload (`rgba`).
struct DecodedTexture
{
    std::uint32_t             width  { 0U };
    std::uint32_t             height { 0U };
    bool                      is_block_compressed { false };
    /// Tightly-packed RGBA8 texels (image path). Empty when block-compressed.
    std::vector<std::uint8_t> rgba;
    /// BC7 block bytes for mip 0 (cdtex path). Empty when not block-compressed.
    std::vector<std::uint8_t> blocks;

    [[nodiscard]] bool has_pixels() const noexcept
    {
        return !rgba.empty() || !blocks.empty();
    }
};

/// Worker → owner completion record: the asset path plus its decoded payload.
struct CompletedTexture
{
    std::string    path;
    DecodedTexture decoded;
};

/// Decode a texture file from disk into a DecodedTexture. Dispatches on the
/// extension: ".cdtex" → cd::asset::cdtex BC7 reader; everything else →
/// cd::asset::image stb decoder (PNG/JPG/TGA/BMP/HDR-as-8bit). CPU-only — no
/// GPU upload. Returns nullopt on any decode/IO failure (caller drops it).
[[nodiscard]] std::optional<DecodedTexture> decode_texture_file(std::string_view path);

// ---- AsyncTexturePool -------------------------------------------------------

/// Thread-pool that executes StreamRequests on background worker threads.
///
/// Usage (standalone or via TextureStreamer when use_async == true):
///   AsyncTexturePool pool;
///   pool.configure(2);                          // start 2 workers
///   pool.submit_async({ "tex.cdtex", 0, 200 }); // enqueue a request
///   auto done = pool.poll_completed();           // drain finished paths
///   pool.join_all();                             // wait + shutdown
///
/// Thread safety: submit_async / poll_completed / join_all are each
///   individually thread-safe via internal mutex + condition_variable.
///   Do NOT call join_all while another thread holds a lock on the pool.
///
/// Each worker runs the REAL CPU decoder (decode_texture_file) outside the
/// lock, so a completion carries the decoded texels + dimensions. A path that
/// fails to decode is NOT reported as completed (matches the sync path's
/// silent-drop semantics).
class AsyncTexturePool
{
public:
    AsyncTexturePool()  = default;
    ~AsyncTexturePool() noexcept;

    // Non-copyable, non-movable (owns threads + mutex).
    AsyncTexturePool(const AsyncTexturePool&)            = delete;
    AsyncTexturePool& operator=(const AsyncTexturePool&) = delete;
    AsyncTexturePool(AsyncTexturePool&&)                 = delete;
    AsyncTexturePool& operator=(AsyncTexturePool&&)      = delete;

    /// Start `worker_count` background worker threads (idempotent if called
    /// once; must NOT be called after join_all()).
    void configure(std::uint32_t worker_count = 2U);

    /// Enqueue a request for async processing. Thread-safe.
    void submit_async(StreamRequest request);

    /// Drain and return decoded textures that have been fully processed since
    /// the last poll_completed() call. Thread-safe. Returns an empty vector if
    /// nothing has finished yet. Does NOT block.
    [[nodiscard]] std::vector<CompletedTexture> poll_completed();

    /// Signal all workers to finish outstanding work, then join them.
    /// Blocks until all submitted requests are processed. Safe to call from
    /// the owner thread. Must not be called concurrently with submit_async().
    void join_all();

    /// Number of requests successfully completed (cumulative, thread-safe).
    [[nodiscard]] std::size_t completed_count() const noexcept;

private:
    // ---- Worker loop ---------------------------------------------------
    void worker_loop();

    // ---- State ---------------------------------------------------------
    struct PendingEntry
    {
        std::string  path;
        std::uint8_t mip_target { 0U };
        std::uint8_t priority   { 0U };
    };

    mutable std::mutex      mutex_;
    std::condition_variable work_cv_;    // workers wait for work or stop
    std::condition_variable idle_cv_;    // join_all waits for quiescence

    std::vector<PendingEntry>     pending_queue_;   // protected by mutex_
    std::vector<CompletedTexture> completed_queue_; // protected by mutex_

    std::atomic<std::size_t>   completed_count_ { 0U };
    std::atomic<std::uint32_t> inflight_        { 0U }; // jobs in-progress

    bool                       stop_ { false };         // protected by mutex_

    std::vector<std::thread>   workers_;
};

// ---- TextureStreamerConfig --------------------------------------------------

/// Optional configuration for TextureStreamer Sprint-2.
///   `use_async`    — false (default) = Sprint-1 sync path; true = async pool.
///   `worker_count` — number of worker threads when use_async == true.
struct TextureStreamerConfig
{
    bool         use_async    { false };
    std::uint32_t worker_count { 2U };
};

// ---- TextureStreamer ---------------------------------------------------------

/// Texture streamer — Sprint-1 (sync) or Sprint-2 (opt-in async).
///
/// Lifecycle (sync, Sprint-1 default):
///   cd::rhi::NullDevice device;
///   TextureStreamer ts;
///   ts.enqueue({ "textures/albedo.cdtex", 0, 200 });
///   while (!ts.is_loaded("textures/albedo.cdtex")) { ts.tick(dt, device); }
///   auto h = ts.get_loaded("textures/albedo.cdtex"); // has value
///
/// Lifecycle (async, Sprint-2):
///   TextureStreamer ts({ .use_async = true, .worker_count = 4 });
///   ts.enqueue({ "textures/albedo.cdtex", 0, 200 });
///   ts.tick(dt, device);   // delegates to AsyncTexturePool
///   ts.join_pending();      // optional — wait for all async loads to finish
///
/// Thread safety: not thread-safe for concurrent calls — all public methods
///   must come from the same (owner) thread.
class TextureStreamer
{
public:
    /// Construct with optional Sprint-2 config. Default = Sprint-1 sync path.
    explicit TextureStreamer(TextureStreamerConfig cfg = {});
    ~TextureStreamer();

    // Non-copyable; non-movable (owns AsyncTexturePool which is non-movable).
    TextureStreamer(const TextureStreamer&)            = delete;
    TextureStreamer& operator=(const TextureStreamer&) = delete;
    TextureStreamer(TextureStreamer&&)                 = delete;
    TextureStreamer& operator=(TextureStreamer&&)      = delete;

    // ---- Mutation -----------------------------------------------------------

    /// Enqueue a load request. If the path is already pending or loaded,
    /// this is a no-op (idempotent). Priority on duplicate is NOT updated.
    /// screen_coverage (0..1) is folded into effective scheduling priority.
    void enqueue(StreamRequest request);

    /// Re-enqueue an already-loaded texture to upgrade its mip_target.
    /// If the new mip_target is higher than the resident one, the old record
    /// is evicted and the path is queued for a fresh decode at the higher level.
    /// If the path is not loaded or the new mip_target is not higher, no-op.
    void enqueue_mip_upgrade(StreamRequest request);

    /// Cancel a pending request. If the path is already loaded or unknown,
    /// this is a no-op.  NOTE: in async mode cancel() only removes the path
    /// from the local pending_map_ dedup set; a request already dispatched to
    /// the AsyncTexturePool will still complete (its result is accepted on the
    /// next poll inside tick()).
    void cancel(std::string_view asset_path);

    /// Drive loading.
    ///   Sync mode  (use_async == false): decode the highest-priority pending
    ///              request + create its GPU texture — at most one per tick().
    ///              After creating the new record, evicts LRU if over budget.
    ///   Async mode (use_async == true):  submit ALL pending requests to the
    ///              AsyncTexturePool (which CPU-decodes them), then drain the
    ///              pool's completed textures and create the GPU texture for
    ///              each on the owner thread.
    /// `dt` is reserved for future budget / time-slicing.
    /// `device` is used to create the GPU texture in BOTH modes (only ever on
    /// the owner thread — workers do CPU decode only).
    void tick(float dt, cd::rhi::IDevice& device);

    /// Block until the AsyncTexturePool has processed all outstanding requests.
    /// No-op in sync mode. Safe to call from the owner thread.
    void join_pending();

    /// Set the maximum number of GPU textures resident at one time.
    /// 0 = unlimited (default). When a new load would exceed the budget the
    /// LRU texture is evicted before the new record is committed.
    void set_texture_budget(std::size_t max_loaded) noexcept;

    /// Evict the least-recently-used resident texture from the streamer's
    /// completed table (destroying the GPU handle and removing the record).
    /// No-op if nothing is loaded. Returns the evicted path, or empty string
    /// if nothing was evicted.
    std::string evict_lru(cd::rhi::IDevice& device);

    /// Mark a loaded texture as most-recently-used without re-loading it.
    /// This promotes it to the front of the LRU order, protecting it from
    /// eviction until less-recently-used textures are consumed. No-op for
    /// paths that are not currently loaded.
    void touch(std::string_view asset_path);

    // ---- Queries ------------------------------------------------------------

    /// True if the asset has been successfully loaded and its GPU handle is valid.
    [[nodiscard]] bool is_loaded(std::string_view asset_path) const;

    /// Returns the TextureHandle if loaded, std::nullopt otherwise
    /// (pending / failed / unknown).
    [[nodiscard]] std::optional<cd::rhi::TextureHandle>
    get_loaded(std::string_view asset_path) const;

    /// Returns the REAL decoded dimensions (width, height) of a loaded texture,
    /// or std::nullopt if the path is not loaded. Lets consumers + tests verify
    /// the decode produced the file's actual size, not a placeholder.
    [[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>>
    get_dimensions(std::string_view asset_path) const;

    /// Returns true if a specific mip level is resident for the given path.
    /// Mip level 0 is always the base mip. Level K is resident when the
    /// texture was loaded with mip_target == 0 (base only) and K == 0, or
    /// mip_target > K (i.e. K < mip_target levels were allocated).
    [[nodiscard]] bool is_mip_loaded(std::string_view asset_path,
                                     std::uint8_t      mip_level) const;

    /// Returns the mip_target stored in the loaded record for a path,
    /// or std::nullopt if the path is not loaded.
    [[nodiscard]] std::optional<std::uint8_t>
    resident_mip_target(std::string_view asset_path) const;

    /// Current texture budget (0 = unlimited).
    [[nodiscard]] std::size_t texture_budget() const noexcept;

    /// Number of enqueued requests not yet loaded or failed.
    [[nodiscard]] std::size_t pending_count() const noexcept;

    /// Number of successfully completed uploads since construction.
    /// NOTE: eviction does NOT decrement this counter; it is cumulative.
    [[nodiscard]] std::size_t completed_count() const noexcept;

    /// Number of currently resident (non-evicted) loaded textures.
    [[nodiscard]] std::size_t loaded_count() const noexcept;

private:
    // ---- Internal state -----------------------------------------------------

    // Pending queue entry: path + mip_target + priority + screen_coverage.
    struct PendingEntry
    {
        std::string  path;
        std::uint8_t mip_target      { 0U };
        std::uint8_t priority        { 0U };
        float        screen_coverage { 0.0F };
    };

    // Loaded texture record stored in completion table.
    struct LoadedRecord
    {
        cd::rhi::TextureHandle handle     {};
        std::uint8_t           mip_target { 0U };
        std::uint32_t          width      { 0U };  // REAL decoded width
        std::uint32_t          height     { 0U };  // REAL decoded height
    };

    // Configuration (set at construction, immutable thereafter).
    TextureStreamerConfig config_;

    // Sprint-2 async pool — only constructed when config_.use_async == true.
    // Heap-allocated so we can forward-declare and avoid pulling thread headers
    // into every translation unit that includes TextureStreamer.hpp.
    // (AsyncTexturePool is defined above in the same header so the unique_ptr
    // destructor is reachable.)
    std::unique_ptr<AsyncTexturePool> async_pool_;

    // Pending set: path → entry (fast cancel + dedup).
    std::unordered_map<std::string, PendingEntry> pending_map_;

    // ---- Resident texture state ---------------------------------------------

    // path → LoadedRecord for every currently-resident texture.
    std::unordered_map<std::string, LoadedRecord> loaded_map_;

    // LRU order: front = MRU (most-recently used), back = LRU candidate.
    // Each element is an iterator into loaded_map_ (stable after insert/erase).
    // lru_order_ is maintained by touch_impl() and evict_lru_impl().
    std::list<std::string> lru_order_;

    // Fast O(1) iterator lookup: path → iterator in lru_order_.
    std::unordered_map<std::string, std::list<std::string>::iterator> lru_iter_;

    // Cumulative completed uploads (never decremented by eviction).
    std::size_t completed_count_ { 0U };

    // GPU texture budget: 0 = unlimited.
    std::size_t texture_budget_ { 0U };

    // Last device seen by tick() — reused by join_pending() (which has no
    // device param) to create GPU textures for any async completions drained
    // after the final tick(). Non-owning observer; never called off-thread.
    cd::rhi::IDevice* last_device_ { nullptr };

    // ---- Helpers ------------------------------------------------------------

    /// Accept a batch of decoded textures from the async pool. Creates the GPU
    /// texture (on the owner thread) sized to the REAL decoded dimensions and
    /// registers the record in loaded_map_ / lru_order_.
    void accept_async_completions(std::vector<CompletedTexture> done, cd::rhi::IDevice& device);

    /// Owner-thread GPU texture creation from a decoded payload. Sizes the
    /// resource to the REAL decoded width/height and records the result.
    /// Runs budget eviction BEFORE inserting if over-budget.
    void create_gpu_record(const std::string&    path,
                           std::uint8_t          mip_target,
                           const DecodedTexture& decoded,
                           cd::rhi::IDevice&     device);

    /// Promote path to MRU position in the LRU list. No-op if not resident.
    void touch_impl(const std::string& path);

    /// Evict the LRU entry, destroying its GPU handle. No-op if empty.
    /// Returns the evicted path (empty if nothing evicted).
    std::string evict_lru_impl(cd::rhi::IDevice& device);

    /// Enforce budget: evict LRU entries until loaded_map_.size() <= budget.
    /// No-op if budget == 0 (unlimited).
    void enforce_budget(cd::rhi::IDevice& device);

    /// Sprint-1 sync tick: process the single highest-priority pending entry.
    void tick_sync_impl(cd::rhi::IDevice& device);

    /// Sprint-2 async tick: drain pending_map_ into the AsyncTexturePool.
    void tick_async_impl();
};

}  // namespace cd::asset::texture_streamer

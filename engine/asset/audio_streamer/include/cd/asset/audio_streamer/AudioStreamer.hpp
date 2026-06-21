// =============================================================================
// CHROMODYNAMIC — cd/asset/audio_streamer/AudioStreamer.hpp
// Phase 619 — cd::asset::audio_streamer (Sprint-1: synchronous)
// Phase 756 — cd::asset::audio_streamer (Sprint-2: opt-in async path)
// Band 6   — real WAV PCM decode wired behind the orchestration. OGG SEALED
//            (no decoder at the asset layer yet — see ADR-20260616-band6).
// Sprint-3 — AudioRingBuffer: CPU ring-buffer streaming layer (chunk
//            producer/consumer, read cursor, loop points, underrun detection,
//            sample-frame bookkeeping, AudioFormatDescriptor).
//
// Audio clip streaming coordinator. Accepts enqueue/cancel requests keyed by
// asset path and drives a decode on each tick() call. Resolved clips are
// identified by cd::asset::AssetId derived from the asset path.
//
// Sync mode  — highest-priority pending request decoded inline.
// Async mode — opt-in via AsyncAudioPool + AudioStreamerConfig.
//   * Set use_async = true (and optionally worker_count > 0) to offload the
//     decode to background threads while tick() only drains completions.
//   * Priority: higher uint8 value = higher priority (255 = highest, 0 = lowest).
//   * channel_target: target audio bus index; reserved for routing; carried
//     through but not acted on yet.
//
// DECODE: BOTH paths run the real cd::asset::wav::load() RIFF/WAVE decoder, so
//   a completion carries real PCM bytes + (channels, sample_rate, frame_count)
//   metadata. ".ogg" inputs are SEALED — there is no Vorbis decoder at the
//   asset layer; an .ogg request fails to decode (silently dropped) until a
//   cd::asset::ogg loader lands (trigger documented in the Band-6 ADR).
//
// RING BUFFER: AudioRingBuffer is a single-producer / single-consumer
//   lock-free-friendly circular buffer operating on PCM byte frames. The
//   producer writes decoded chunk bytes; the consumer reads them for device
//   playback. Loop points and underrun detection are part of the read path.
//   The audio-device callback backend is SEALED (see class docstring below).
//
// Sibling to:
//   cd::asset::scene_streamer   (Phase 586)
//   cd::asset::texture_streamer (Phase 599 + 714)
//
// Namespace: cd::asset::audio_streamer
// =============================================================================
#pragma once

#include <cd/asset/AssetId.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace cd::asset::audio_streamer
{

// ---- Public types -----------------------------------------------------------

/// Caller-facing load request.
///   `asset_path`     — VFS path to the audio clip (e.g. "audio/footstep.wav").
///   `channel_target` — target audio bus index; reserved for Sprint-2 routing.
///   `priority`       — 255 = highest, 0 = lowest.
struct StreamRequest
{
    std::string  asset_path;
    std::uint8_t channel_target { 0U };
    std::uint8_t priority       { 128U };
};

/// Decoded CPU audio — the REAL payload produced by a worker (or the sync
/// path). Carries the raw interleaved PCM bytes + the format metadata read
/// from the file so consumers + tests can verify a genuine decode happened
/// (not a path-hash placeholder).
struct DecodedAudio
{
    std::uint16_t             channels        { 0U };
    std::uint32_t             sample_rate     { 0U };
    std::uint16_t             bits_per_sample { 0U };
    std::uint64_t             frame_count     { 0U };  ///< frames = channels-grouped samples
    std::vector<std::uint8_t> pcm;                     ///< interleaved PCM bytes

    [[nodiscard]] bool has_samples() const noexcept
    {
        return !pcm.empty();
    }
};

/// Worker → owner completion record: the asset path plus its decoded PCM.
struct CompletedAudio
{
    std::string  path;
    DecodedAudio decoded;
};

// ---- AudioFormatDescriptor --------------------------------------------------

/// Fully describes the PCM layout of an audio stream — channels, sample rate,
/// bit depth and derived byte-level bookkeeping. Matches the fields carried by
/// DecodedAudio so callers can size ring-buffer capacity from a clip's format.
///
/// bytes_per_frame = channels × (bits_per_sample / 8)
/// Total byte count of N frames = N × bytes_per_frame
struct AudioFormatDescriptor
{
    std::uint16_t channels        { 0U };   ///< 1 = mono, 2 = stereo, etc.
    std::uint32_t sample_rate     { 0U };   ///< Hz — e.g. 44100, 48000
    std::uint16_t bits_per_sample { 0U };   ///< Bit depth per single sample (e.g. 16)

    /// Bytes consumed by one frame (channels × bytes-per-sample).
    /// Returns 0 when channels or bits_per_sample are not initialised.
    [[nodiscard]] std::uint32_t bytes_per_frame() const noexcept
    {
        return static_cast<std::uint32_t>(channels) * (static_cast<std::uint32_t>(bits_per_sample) / 8U);
    }

    /// True when all fields are non-zero (format is usable).
    [[nodiscard]] bool is_valid() const noexcept
    {
        return channels > 0U && sample_rate > 0U && bits_per_sample > 0U;
    }

    /// Build from a DecodedAudio's metadata (convenience).
    [[nodiscard]] static AudioFormatDescriptor from_decoded(const DecodedAudio& d) noexcept
    {
        return AudioFormatDescriptor{ d.channels, d.sample_rate, d.bits_per_sample };
    }
};

// ---- AudioRingBuffer --------------------------------------------------------

/// Single-producer / single-consumer circular byte buffer for PCM streaming.
///
/// Design intent (Sprint-3 CPU charter):
///   * The producer thread (decode worker or file-read thread) writes decoded
///     chunk bytes via write_chunk().
///   * The consumer thread (audio-device callback) reads frames via
///     read_frames().
///   * Loop points: when loop_end_frame is set, the read cursor wraps to
///     loop_start_frame instead of past the last written sample, enabling
///     seamless clip loops without re-filling the buffer.
///   * Underrun detection: if the consumer asks for more frames than are
///     available, the returned span is short (possibly empty), and
///     underrun_count() increments. The consumer is responsible for filling
///     with silence.
///   * Sample-frame bookkeeping: all externally visible positions are in
///     FRAMES, not bytes, so callers never need to multiply by bytes_per_frame.
///
/// AUDIO-DEVICE CALLBACK BACKEND IS SEALED:
///   Wiring AudioRingBuffer to a real audio-device callback (WASAPI, CoreAudio,
///   ALSA, XAudio2) requires platform-specific callback APIs that are outside
///   the scope of the cpu-only asset layer. This is formally sealed; see
///   docs/PROJECT_COMPLETION_STATUS.md (audio_streamer row) for the trigger.
///   The ring buffer itself is device-agnostic PCM plumbing; plugging it into
///   a device is two lines in the device callback:
///       auto span = ring.read_frames(frames_requested);
///       copy_or_silence(out_buffer, span, frames_requested);
///
/// Thread safety:
///   write_chunk() and read_frames() may be called concurrently from exactly
///   ONE producer and ONE consumer thread respectively. All other methods
///   (reset, set_loop_points, capacity, etc.) must be called from a single
///   thread when no concurrent producer/consumer is active.
///
/// Usage:
///   AudioRingBuffer ring { fmt, /*capacity_frames=*/4096U };
///   ring.set_loop_points(0U, total_frames);   // optional: loop whole clip
///
///   // Producer side:
///   ring.write_chunk(pcm_bytes_span);         // returns bytes actually written
///
///   // Consumer side:
///   auto pcm = ring.read_frames(256U);         // span into internal buffer
///   // pcm.size() == 0 → underrun; fill out with silence
class AudioRingBuffer
{
public:
    /// Construct a ring buffer for the given PCM format and capacity.
    ///   `format`          — PCM format descriptor (must be valid).
    ///   `capacity_frames` — maximum number of PCM frames the buffer can hold
    ///                       at any one time. Must be >= 2.
    AudioRingBuffer(AudioFormatDescriptor format, std::uint64_t capacity_frames);

    // Non-copyable, non-movable (owns internal storage + atomic cursors).
    AudioRingBuffer(const AudioRingBuffer&)            = delete;
    AudioRingBuffer& operator=(const AudioRingBuffer&) = delete;
    AudioRingBuffer(AudioRingBuffer&&)                 = delete;
    AudioRingBuffer& operator=(AudioRingBuffer&&)      = delete;

    ~AudioRingBuffer() = default;

    // ---- Configuration (call before producer/consumer threads start) --------

    /// Set loop playback points. When looping is enabled the read cursor
    /// wraps from loop_end_frame back to loop_start_frame.
    ///   `loop_start_frame` — first frame of the loop region (0 = clip start).
    ///   `loop_end_frame`   — one-past-last frame of the loop region; 0 means
    ///                        "not set" (looping disabled). Must be >
    ///                        loop_start_frame when non-zero.
    ///
    /// Loop coordinates are in terms of the clip's absolute frame timeline,
    /// NOT ring-buffer positions. The ring buffer translates them to byte
    /// offsets internally.
    void set_loop_points(std::uint64_t loop_start_frame, std::uint64_t loop_end_frame) noexcept;

    /// Clear loop points (disable looping). Equivalent to
    /// set_loop_points(0, 0).
    void clear_loop_points() noexcept;

    /// Reset the buffer to empty (zero write/read cursors, clear storage).
    /// Loop points are preserved. Call from the owner thread only.
    void reset() noexcept;

    // ---- Producer API -------------------------------------------------------

    /// Write up to `data.size()` bytes of raw PCM into the ring buffer.
    /// Bytes are silently truncated if the buffer does not have room for all
    /// of `data`. The truncation is aligned on a frame boundary so a partial
    /// write never leaves a half-frame.
    ///
    /// Returns the number of bytes actually written (0..data.size()).
    [[nodiscard]] std::size_t write_chunk(std::span<const std::uint8_t> data) noexcept;

    // ---- Consumer API -------------------------------------------------------

    /// Read up to `frame_count` frames from the buffer and advance the read
    /// cursor. Returns a span (view into the internal storage) covering the
    /// bytes that were read. The span is valid until the next call to
    /// write_chunk() or reset().
    ///
    /// If fewer than `frame_count` frames are available:
    ///   * The returned span covers only the available frames.
    ///   * underrun_count() is incremented by 1.
    ///   * The caller should treat the gap as silence.
    ///
    /// When loop_end_frame is set and the read cursor reaches loop_end_frame,
    /// the cursor wraps to loop_start_frame. If the wrap point falls inside
    /// the current read request the returned span ends at the wrap boundary;
    /// the caller must issue another read_frames() to continue from the start
    /// of the loop.
    [[nodiscard]] std::span<const std::uint8_t> read_frames(std::uint64_t frame_count) noexcept;

    // ---- Queries ------------------------------------------------------------

    /// PCM format the buffer was constructed with.
    [[nodiscard]] const AudioFormatDescriptor& format() const noexcept;

    /// Maximum number of PCM frames the buffer can hold at one time.
    [[nodiscard]] std::uint64_t capacity_frames() const noexcept;

    /// Number of frames currently available to the consumer.
    [[nodiscard]] std::uint64_t frames_available() const noexcept;

    /// Number of free frames the producer can write into.
    [[nodiscard]] std::uint64_t frames_free() const noexcept;

    /// True when no frames are available (frames_available() == 0).
    [[nodiscard]] bool is_empty() const noexcept;

    /// True when no free space remains (frames_free() == 0).
    [[nodiscard]] bool is_full() const noexcept;

    /// Absolute read-cursor position, in frames from the start of the stream.
    /// Incremented by every read_frames() call; wraps at loop_end_frame when
    /// looping is enabled.
    [[nodiscard]] std::uint64_t read_cursor_frames() const noexcept;

    /// Absolute write-cursor position, in frames from the start of the stream.
    [[nodiscard]] std::uint64_t write_cursor_frames() const noexcept;

    /// Number of underrun events since construction or last reset().
    [[nodiscard]] std::uint64_t underrun_count() const noexcept;

    /// Active loop start frame (0 if looping is disabled).
    [[nodiscard]] std::uint64_t loop_start_frame() const noexcept;

    /// Active loop end frame (0 if looping is disabled).
    [[nodiscard]] std::uint64_t loop_end_frame() const noexcept;

    /// True when loop points are active (loop_end_frame > 0).
    [[nodiscard]] bool is_looping() const noexcept;

private:
    AudioFormatDescriptor format_;
    std::uint64_t         capacity_frames_ { 0U };

    // Internal ring storage: capacity_frames_ * bytes_per_frame bytes.
    std::vector<std::uint8_t> storage_;

    // Write cursor (in frames, monotonically increasing).
    std::uint64_t write_cursor_ { 0U };

    // Read cursor (in frames, monotonically increasing — wraps on loop).
    std::uint64_t read_cursor_ { 0U };

    // Underrun event count.
    std::uint64_t underrun_count_ { 0U };

    // Loop point configuration (0/0 = disabled).
    std::uint64_t loop_start_frame_ { 0U };
    std::uint64_t loop_end_frame_   { 0U };

    // Scratch buffer reused by read_frames() for contiguous output when a
    // read wraps the ring boundary.
    std::vector<std::uint8_t> read_scratch_;
};

// ---- decode_audio_file ------------------------------------------------------

/// Decode an audio file from disk into a DecodedAudio. ".wav" → real
/// cd::asset::wav RIFF/WAVE decoder. ".ogg" (and any other extension) is NOT
/// supported at the asset layer yet → std::nullopt (SEALED, see Band-6 ADR).
/// CPU-only — no audio-device upload. Returns nullopt on any decode/IO failure.
[[nodiscard]] std::optional<DecodedAudio> decode_audio_file(std::string_view path);

// ---- AsyncAudioPool ---------------------------------------------------------

/// Thread-pool that executes StreamRequests on background worker threads.
///
/// Usage (standalone or via AudioStreamer when use_async == true):
///   AsyncAudioPool pool;
///   pool.configure(2);                            // start 2 workers
///   pool.submit_async({ "audio/music.wav", 0, 200 }); // enqueue a request
///   auto done = pool.poll_completed();             // drain finished paths
///   pool.join_all();                               // wait + shutdown
///
/// Thread safety: submit_async / poll_completed / join_all are each
///   individually thread-safe via internal mutex + condition_variable.
///   Do NOT call join_all while another thread holds a lock on the pool.
///
/// Each worker runs the REAL cd::asset::wav decoder (decode_audio_file) outside
/// the lock, so a completion carries the decoded PCM + format. A path that
/// fails to decode (incl. any .ogg input) is NOT reported as completed.
class AsyncAudioPool
{
public:
    AsyncAudioPool()  = default;
    ~AsyncAudioPool() noexcept;

    // Non-copyable, non-movable (owns threads + mutex).
    AsyncAudioPool(const AsyncAudioPool&)            = delete;
    AsyncAudioPool& operator=(const AsyncAudioPool&) = delete;
    AsyncAudioPool(AsyncAudioPool&&)                 = delete;
    AsyncAudioPool& operator=(AsyncAudioPool&&)      = delete;

    /// Start `worker_count` background worker threads (idempotent if called
    /// once; must NOT be called after join_all()).
    void configure(std::uint32_t worker_count = 2U);

    /// Enqueue a request for async processing. Thread-safe.
    void submit_async(StreamRequest request);

    /// Drain and return decoded clips that have been fully processed since the
    /// last poll_completed() call. Thread-safe. Returns an empty vector if
    /// nothing has finished yet. Does NOT block.
    [[nodiscard]] std::vector<CompletedAudio> poll_completed();

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
        std::uint8_t channel_target { 0U };
        std::uint8_t priority       { 0U };
    };

    mutable std::mutex      mutex_;
    std::condition_variable work_cv_;    // workers wait for work or stop
    std::condition_variable idle_cv_;    // join_all waits for quiescence

    std::vector<PendingEntry>   pending_queue_;   // protected by mutex_
    std::vector<CompletedAudio> completed_queue_; // protected by mutex_

    std::atomic<std::size_t>   completed_count_ { 0U };
    std::atomic<std::uint32_t> inflight_        { 0U }; // jobs in-progress

    bool                       stop_ { false };         // protected by mutex_

    std::vector<std::thread>   workers_;
};

// ---- AudioStreamerConfig ----------------------------------------------------

/// Optional configuration for AudioStreamer Sprint-2.
///   `use_async`    — false (default) = Sprint-1 sync path; true = async pool.
///   `worker_count` — number of worker threads when use_async == true.
struct AudioStreamerConfig
{
    bool          use_async    { false };
    std::uint32_t worker_count { 2U };
};

// ---- AudioStreamer -----------------------------------------------------------

/// Audio clip streamer — Sprint-1 (sync) or Sprint-2 (opt-in async).
///
/// Lifecycle (sync, Sprint-1 default):
///   AudioStreamer as;
///   as.enqueue({ "audio/music.wav", 0, 200 });
///   while (!as.is_loaded("audio/music.wav")) { as.tick(dt); }
///   auto id = as.get_loaded("audio/music.wav"); // has value
///
/// Lifecycle (async, Sprint-2):
///   AudioStreamer as({ .use_async = true, .worker_count = 2 });
///   as.enqueue({ "audio/music.wav", 0, 200 });
///   as.tick(dt);        // delegates to AsyncAudioPool
///   as.join_pending();  // optional — wait for all async loads to finish
///
/// Sprint-1 load strategy: the asset path is hashed to an AssetId via
/// cd::asset::AssetId::from_path(). This is always deterministic and never
/// fails, modelling "resident in the registry". Actual WAV decode,
/// sample-rate conversion and device upload are deferred to a future Sprint.
///
/// Thread safety: not thread-safe for concurrent calls — all public methods
///   must come from the same (owner) thread.
class AudioStreamer
{
public:
    /// Construct with optional Sprint-2 config. Default = Sprint-1 sync path.
    explicit AudioStreamer(AudioStreamerConfig cfg = {});
    ~AudioStreamer();

    // Non-copyable; non-movable (owns AsyncAudioPool which is non-movable).
    AudioStreamer(const AudioStreamer&)            = delete;
    AudioStreamer& operator=(const AudioStreamer&) = delete;
    AudioStreamer(AudioStreamer&&)                 = delete;
    AudioStreamer& operator=(AudioStreamer&&)      = delete;

    // ---- Mutation -----------------------------------------------------------

    /// Enqueue a load request. If the path is already pending or loaded,
    /// this is a no-op (idempotent). Priority on duplicate is NOT updated.
    void enqueue(StreamRequest request);

    /// Cancel a pending request. If the path is already loaded or unknown,
    /// this is a no-op.  NOTE: in async mode cancel() only removes the path
    /// from the local pending_map_ dedup set; a request already dispatched to
    /// the AsyncAudioPool will still complete (its result is accepted on the
    /// next poll inside tick()).
    void cancel(std::string_view asset_path);

    /// Drive loading.
    ///   Sync mode  (use_async == false): process the highest-priority pending
    ///              request synchronously — at most one load per tick() call.
    ///   Async mode (use_async == true):  submit ALL pending requests to the
    ///              AsyncAudioPool, then drain the pool's completion queue
    ///              into completed_paths_.
    /// `dt` is reserved for future budget / time-slicing.
    void tick(float dt);

    /// Block until the AsyncAudioPool has processed all outstanding requests.
    /// No-op in sync mode. Safe to call from the owner thread.
    void join_pending();

    // ---- Queries ------------------------------------------------------------

    /// True if the asset has been successfully loaded.
    [[nodiscard]] bool is_loaded(std::string_view asset_path) const;

    /// Returns the AssetId if loaded, std::nullopt otherwise
    /// (pending / failed / unknown).
    [[nodiscard]] std::optional<cd::asset::AssetId>
    get_loaded(std::string_view asset_path) const;

    /// Returns the REAL decoded format (channels, sample_rate, frame_count) of
    /// a loaded clip, or std::nullopt if the path is not loaded. Lets consumers
    /// + tests verify a genuine PCM decode happened, not a path-hash placeholder.
    struct ClipFormat
    {
        std::uint16_t channels    { 0U };
        std::uint32_t sample_rate { 0U };
        std::uint64_t frame_count { 0U };
    };
    [[nodiscard]] std::optional<ClipFormat>
    get_format(std::string_view asset_path) const;

    /// Number of enqueued requests not yet loaded or failed.
    [[nodiscard]] std::size_t pending_count() const noexcept;

    /// Number of successfully completed loads since construction.
    [[nodiscard]] std::size_t completed_count() const noexcept;

private:
    // ---- Internal state -----------------------------------------------------

    // Pending queue entry: path + channel_target + priority.
    struct PendingEntry
    {
        std::string  path;
        std::uint8_t channel_target { 0U };
        std::uint8_t priority       { 0U };
    };

    // Loaded clip record stored in completion table.
    struct LoadedRecord
    {
        cd::asset::AssetId id {};
        std::uint8_t       channel_target { 0U };
        std::uint16_t      channels       { 0U };  // REAL decoded channels
        std::uint32_t      sample_rate    { 0U };  // REAL decoded sample rate
        std::uint64_t      frame_count    { 0U };  // REAL decoded frame count
    };

    // Configuration (set at construction, immutable thereafter).
    AudioStreamerConfig config_;

    // Sprint-2 async pool — only constructed when config_.use_async == true.
    std::unique_ptr<AsyncAudioPool> async_pool_;

    // Pending set: path → entry (fast cancel + dedup).
    std::unordered_map<std::string, PendingEntry> pending_map_;

    // Completion table indexed by insertion order.
    std::vector<LoadedRecord> completed_;

    // Reverse lookup: path → index into completed_ for completed assets.
    std::unordered_map<std::string, std::size_t> completed_index_;

    // ---- Helpers ------------------------------------------------------------

    /// Accept a batch of decoded clips from the async pool and register the
    /// real PCM metadata + AssetId in completed_ / completed_index_.
    void accept_async_completions(std::vector<CompletedAudio> done);

    /// Owner-thread record registration from a decoded clip. Stores the AssetId
    /// + REAL decoded format in completed_ / completed_index_.
    void register_clip(const std::string& path,
                       std::uint8_t        channel,
                       const DecodedAudio& decoded);

    /// Sprint-1 sync tick: process the single highest-priority pending entry.
    void tick_sync_impl();

    /// Sprint-2 async tick: drain pending_map_ into the AsyncAudioPool.
    void tick_async_impl();
};

}  // namespace cd::asset::audio_streamer

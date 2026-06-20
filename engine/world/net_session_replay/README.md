# cd::net::session_replay

**Record + replay of network packets** for debugging multiplayer
issues. Capture a session into a `.srpk` file, replay it offline at
real-time, fast, or step-by-step to reproduce a flaky desync.

## Public types

| Type           | Role                                            |
|----------------|-------------------------------------------------|
| `PacketRecord` | Timestamped packet snapshot (POD-friendly).     |
| `Recorder`     | Captures live packets + serialises to file.     |
| `Replayer`     | Loads a `.srpk` file + drives time-based replay.|

## Public surface

```cpp
namespace cd::net::session_replay {

struct PacketRecord
{
    double                    timestamp_ms;   // ms since recording start
    std::vector<uint8_t>      payload;
    uint32_t                  channel_id;
    bool                      incoming;       // true = received, false = sent
};

class Recorder
{
public:
    void        start_recording();
    void        record_packet(std::span<const uint8_t> payload,
                              uint32_t channel_id, bool incoming);
    void        stop_recording() noexcept;
    [[nodiscard]] bool        save_to_file(const std::filesystem::path&) const;
    [[nodiscard]] std::size_t packet_count() const noexcept;
    [[nodiscard]] bool        is_recording() const noexcept;
};

class Replayer
{
public:
    [[nodiscard]] bool load_from_file(const std::filesystem::path&);
    [[nodiscard]] std::span<const PacketRecord> all() const noexcept;
    [[nodiscard]] std::optional<PacketRecord>   next_packet(double current_ms);
    [[nodiscard]] bool                          finished() const noexcept;
    [[nodiscard]] std::size_t                   packet_count() const noexcept;
    void reset() noexcept;
    void seek_to(double target_ms) noexcept;  // jump cursor to first pkt >= target_ms
};

}
```

## Binary format (SRPK v1, little-endian throughout)

```
[4 B  magic        0x53 0x52 0x50 0x4B  ("SRPK")]
[4 B  version      uint32_t = 1                 ]
[4 B  packet_count uint32_t                     ]
for each packet:
    [8 B  timestamp_ms  double (ms since record start)]
    [4 B  channel_id    uint32_t                      ]
    [1 B  incoming      uint8_t  (0 or 1)             ]
    [4 B  payload_size  uint32_t                      ]
    [N B  payload       uint8_t[payload_size]         ]
```

Total header: 12 bytes. Per-record overhead: 17 bytes + payload.

Intentionally simple — no compression, no checksum. A future SRPK v2
break bumps `version`; the loader rejects any `version != 1`.

## Monotonicity contract

`load_from_file` validates that timestamps are **non-decreasing**.
A file with out-of-order packets is rejected. This guarantees that
`next_packet(current_ms)` never needs to look ahead; it can rely on
the linear cursor model.

`Recorder::record_packet` captures timestamps automatically from
`std::chrono::steady_clock` relative to `start_recording()`, so
monotonicity is inherent for the live-capture path.

## Seek / scrub

`seek_to(target_ms)` advances the cursor to the first packet whose
`timestamp_ms >= target_ms` using `std::ranges::lower_bound`, giving
O(log n) seek time. Combine with `reset()` for a full rewind.

## Use cases

* **Desync regression repro.** Customer reports a desync at minute
  4:32 of a 1v1 match. With session replay enabled they ship the
  `.srpk` to a support inbox; engineering replays it locally with
  the same client build and steps through the packet sequence.
* **Network-condition fuzz.** Replay the same trace 50× while
  injecting drop / reorder / latency to find a race window.
* **Bot training.** Replay captured human play traces to bootstrap
  the offline RL pipeline (not yet wired; queued under `L-rl-train`).

## Dependencies

* `cd::core` — `Defines.hpp`.

No allocator, no math, no network backend dep. The library is a
state machine + serialiser; the caller routes the recorded packets
to / from `cd::world::net_session` themselves.

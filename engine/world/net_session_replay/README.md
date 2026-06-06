# cd::net::session_replay

**Record + replay of network packets** for debugging multiplayer
issues. Capture a session into a `.srpk` file, replay it offline at
real-time, fast, or step-by-step to reproduce a flaky desync.

## Public types

| Type           | Role                                            |
|----------------|-------------------------------------------------|
| `PacketRecord` | Timestamped packet snapshot (POD).              |
| `Recorder`     | Captures live packets + serialises to file.     |
| `Replayer`     | Loads a `.srpk` file + drives time-based replay.|

## Public surface

```cpp
namespace cd::net::session_replay {

struct PacketRecord
{
    uint64_t          t_ns;
    uint32_t          remote_peer;
    uint32_t          channel;
    std::vector<std::byte> payload;
};

class Recorder
{
public:
    void                          start(std::filesystem::path);
    void                          push(PacketRecord);
    cd::expected<void, Error>     finish();
};

class Replayer
{
public:
    cd::expected<void, Error>     open(std::filesystem::path);
    [[nodiscard]] std::optional<PacketRecord>
                                  next();          // monotonic in t_ns
    [[nodiscard]] bool            finished() const noexcept;
};

}
```

## Binary format

```
[8 B  magic       "SRPK\x00\x01\x00\x00"]
[4 B  packet_count]
for each packet:
    [8 B  t_ns       ]
    [4 B  remote_peer]
    [4 B  channel    ]
    [4 B  payload_len]
    [N B  payload    ]
```

Little-endian throughout. Intentionally simple — no compression, no
schema versioning beyond the magic. A future SRPK v2 break bumps
the magic.

## Determinism contract

Same as `cd::game::input_recorder`:

* `Recorder::push` must be **monotonic** in `t_ns`. Caller drives
  the clock through whatever monotonic source it normally uses
  (`cd::frame_timing`).
* `Replayer::next` returns packets in the order they were written.
  It does NOT re-clock to wall time; the caller walks a frame, polls
  `next()` repeatedly, stops when the returned `t_ns` exceeds the
  current simulation tick.

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

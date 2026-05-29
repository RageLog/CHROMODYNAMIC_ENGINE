# cd::net

**Purpose**: Low-latency multiplayer networking layer. Provides UDP connections, packet sequencing, jitter/loss handling, snapshot buffering, and delta compression for state replication. Designed for action games with <100ms round-trip latency targets.

**Namespace**: `cd::net`.

**Public Headers**:
- `cd/net/IConnection.hpp` — abstract connection interface (send/recv).
- `cd/net/UdpConnection.hpp` — concrete UDP backend (Winsock on Windows, BSD sockets on Unix).
- `cd/net/LoopbackConnection.hpp` — in-process testing connection (100% reliability).
- `cd/net/PacketHeader.hpp` — packet framing + seq/ack tracking.
- `cd/net/SequenceId.hpp` — sequence number with wrap-around safety.
- `cd/net/SequenceWindow.hpp` — sliding window for ack tracking (32-bit ack field covers 32 prior packets).
- `cd/net/Retransmit.hpp` — timeout-based retransmission of lost packets.
- `cd/net/QoSTier.hpp` — QoS levels (unreliable, sequenced, reliable-ordered).
- `cd/net/RleCodec.hpp` — run-length encoding for delta compression.
- `cd/net/DeltaWriter.hpp` — delta writer for state snapshots (only changed fields).
- `cd/net/SnapshotBuffer.hpp` — server-side snapshot ring buffer (interpolation points).
- `cd/net/PredictionBuffer.hpp` — client-side prediction buffer (extrapolation).
- `cd/net/LatencyStats.hpp` — RTT, jitter, loss tracking.
- `cd/net/Throttle.hpp` — bandwidth limiter (bits per second).
- `cd/net/ChannelMux.hpp` — multiplex logical channels over one UDP socket.

**Build**:
```bash
cmake --build --preset ninja-debug --target cd_net
ctest --preset ninja-debug -R net --output-on-failure
```

**Platform Support**:
- **Windows**: Winsock2 (ws2_32.lib via #pragma comment).
- **Unix/Linux/macOS**: BSD sockets (standard libc).

**Dependencies**: cd::core.

**Notes**:
- UDP-only; TCP not supported (too slow for games).
- Snapshot compression uses delta encoding (send only changed fields) + RLE coding.
- Loopback connection used in single-player netplay tests and local debugging.
- RTT estimation via ping/pong packets; jitter computed as RTT variance.

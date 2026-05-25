# ADR-011 — Networking Architecture

- **Status**: Accepted (Phase 1 Design)
- **Date**: 2026-05-17
- **Related**: ADR-004 (ECS), ADR-005 (Foundation), ADR-015 (Concurrency), ADR-016 (Vendor Matrix), ADR-017 (DtForHil Salvage)

## Bağlam

`cd::net` library-oriented + ECS-friendly + opt-in determinism (T3.Q2=D rollback netcode altyapısı). DtForHil pattern miras (T21.Q1 talimat: "DtForHil yapısından ilham + esneklik"). Real-time multiplayer (FPS/RTS/fighting), gelecek voice chat. T21.Q5=C first-class baştan.

## Karar

### A. Layered Architecture

`cd::net` 3 alt-namespace:

```
cd::net::transport — ITransport + backend implementations
cd::net::protocol  — PacketHeader + BitStream + framing (DtForHil port ADR-017 P3)
cd::net::replication — IReplicationStrategy + 3 plugin (snapshot+delta, lockstep, rollback)
```

ECS adapter ayrı target: `cd::net::ecs_adapter` (`cd::net` ECS'den bağımsız; adapter ECS view sağlar).

### B. Transport (T21.Q2) — Plugin Matrix

| Backend | Lisans | Kategori | Use case |
|---|---|---|---|
| **GameNetworkingSockets (Valve)** | BSD-3 | K3, primary | Real-time game, Steam Datagram Relay opsiyonel |
| **ENet** | MIT | K3, fallback | Lightweight, no Steam SDK dep |
| **msquic (Microsoft)** | MIT | K3, opt | Voice/asset/HTTP3 stream |
| **libdatachannel** | MPL | K3, opt | WebRTC voice + browser bridge |

Tek `cd::net::ITransport` adapter; build flag `CD_NET_TRANSPORT=gns|enet|both` + `CD_NET_QUIC=ON` + `CD_NET_WEBRTC=ON`.

**Reddedilen**: yojimbo (2019 stalled), Photon (closed cloud), KCP (mostly niche custom RUDP).

### C. Replication (T21.Q1 — DtForHil ilham + esneklik)

**Strategy plugin** — `cd::net::IReplicationStrategy` arkasında 3 mode:

1. **Snapshot Interpolation + Delta** (default) — FPS/MMO, Quake III + Overwatch pattern.
2. **Deterministic Lockstep** — RTS/fighting (legacy).
3. **Rollback Netcode** — fighting/P2P RTS, GGPO-style.

**Aşma noktası**: Tek motor build'de 3 mode **runtime-switchable** (Mirror/Iris'te yok). Tri-clock (ADR-005) + opt-in determinism (T3.Q2) + fixed-tick scheduler hepsinin temeli — rollback netcode "free" gelir (UE/production engine'de retrofit zor).

### D. DtForHil Pattern Salvage (ADR-017 P3)

DtForHil 11 dosya 1:1 port + adapt:

| DtForHil | CHROMODYNAMIC | Refactor |
|---|---|---|
| `ISocket` + `SocketTrait` | `cd::net::ITransport` + `Reliability{Unreliable,Reliable,ReliableOrdered}` | Multi-channel + connection genişletme |
| `NetworkFactory` singleton | `cd::net::TransportRegistry` | Context-passing (CLAUDE.md §7) |
| `IFrameStrategy` + `LengthHeaderStrategy` | `cd::net::IFraming` | TCP/IPC fallback için; UDP path bypass |
| `StreamAssembler` (mutex+callback) | `cd::net::PacketAssembler` | Lock-free SPSC (S5b upgrade) |
| `BinaryStream::readBits` | `cd::serialization::BitStream` | Float quantization (Glenn Fiedler tabloları) hazır gelir |
| `ControlEnvelopeHeader` (kind+size) | `cd::net::PacketHeader{version, channel, flags, seq, ack, ackBits}` | Ack-bitfield + seq alanı eklendi |
| `CorrelationId u64` | `cd::net::RpcId` | RPC pattern aynen |
| `MessageEngine` schema-driven | `cd::net::SchemaCodec` (opsiyonel) | Editor/replay tooling — altın değerinde |
| `infra_contracts/ControlEnvelopeHeader` env-var endpoint | `CHROMA_NET_ENDPOINT` headless dedicated server | Pattern korunur |

### E. Topology (T21.Q3)

- **Default**: Authoritative client-server (cheat-resistant, modern FPS std — Source 2 / Overwatch / Valorant).
- **P2P + relay**: Rollback netcode için zorunlu; symmetric NAT → relay fallback (GNS Steam Datagram Relay-vari).
- **Lobby + dedicated matchmaking**: `cd::net::lobby` ayrı kütüphane (Steamworks adapter + opsiyonel custom).
- **Dedicated server**: `cd::net::server` headless target (engine renderer-less link).

### F. Voice Chat (T21.Q4) — Gelecek

- **Codec**: Opus (libopus BSD, 6-510 kbps, 2.5-60 ms frame, WebRTC std).
- **Transport**: WebRTC DataChannel (libdatachannel) **veya** GNS unreliable + SRTP-lite.
- **ECS integration**: `cd::ecs::VoiceSourceComponent{playerId, codec, spatialBlend}` → audio mixer 3D spatial routing (ADR-007).
- **Privacy**: opt-in push-to-talk, E2E DTLS-SRTP.
- **Sprint 14+**: codec API stub bu sprint; full integration sonra.

### G. Library-Oriented (T21.Q5 = C)

- `cd::net` engine'siz derlenir; headless server binary olarak da derlenebilir.
- ECS bağımlılığı **opsiyonel adapter** ayrı target.
- Bot framework, headless test, dedicated server, network simülatörü kendi başına ürün.

### H. Aşma Noktaları

1. **Strategy-pluggable replication runtime-switchable** — Mirror/Iris tek modele kilitli.
2. **Transport polymorphism** — GNS+ENet+QUIC+WebRTC tek `ITransport`.
3. **Determinism alt katmanı default-ready** — rollback/lockstep "free".
4. **Library-oriented first-class** — `cd::net` engine'siz.
5. **Schema-driven debug/replay** — DtForHil `MessageEngine` portu → editor inspector + packet replay bedava.
6. **Bit-packed delta + quantization zorunlu** — DtForHil `BinaryStream::readBits` temel; biz float quantization hazır.
7. **Lock-free pipeline** — DtForHil mutex'li StreamAssembler → SPSC + actor model (S5b).

## Reddedilen

- **Tek transport (GNS sadece)**: WebGL/browser dışlanır.
- **Tek replication (snapshot sadece)**: rollback genre desteklenmez.
- **Photon/Wwise/commercial network**: library-oriented + lisans çelişir.
- **yojimbo direkt adopsiyon**: 2019 stalled, pattern referans yeterli.
- **DtForHil ControlEnvelopeHeader direkt port**: TCP-flavored — UDP PacketHeader genişletmesi zorunlu.
- **Singleton factory**: CLAUDE.md §7.

## Sonuçlar

**Pozitif**:
- 3 oyun türü (FPS, RTS, fighting) tek engine.
- Headless dedicated server + bot framework first-class.
- DtForHil olgun pattern miras → risk düşük.
- Determinism alt katmanı rollback-ready by construction.

**Negatif**:
- 3 transport × 3 replication test matrix şişer; CI matrix planlı.
- Voice integration Sprint 14+; bu sprint sadece stub.
- ECS adapter inversion ek indirection — hot-path inline-friendly template wrapper.

**Replace-Ready (D1)**: GNS K3 Phase 5+ değerlendir; transport adapter pattern sayesinde tüm vendor swap edilebilir.

## Açık Sorular

| ID | Soru | Çözüm |
|---|---|---|
| Q1 | Cheat mitigation: Bernier rewind-style lag compensation Sprint? | Ayrı ADR Sprint 13+ |
| Q2 | Async I/O: io_uring/IOCP/kqueue direct mı asio mı? | ADR-015 custom `cd::io::context` |
| Q3 | Serialization format: bit-packed vs FlatBuffers vs MessagePack? | Custom bit-packed default, schema-driven editor için MessageEngine |
| Q4 | Replication batching: per-tick vs priority+relevance hybrid? | Iris-vari priority queue |
| Q5 | Encryption default: TLS 1.3 mandatory mı? | Opt-in v1, mandatory v2 |
| Q6 | Steam Relay dep: GNS Steam SDK olmadan? | Open variant doğrula |
| Q7 | Replay format: MessageEngine JSON vs binary delta log? | Binary delta default, JSON debug |
| Q8 | Rollback determinism: Jolt physics + Box2D garanti? | T3.Q2 fixed-point Tier-2 cross |

## Cross-Cutting

- **ADR-004 (ECS)**: `ReplicationComponent` + `NetworkAuthorityComponent` (server-owned / client-predicted / cosmetic). Iris-vari dirty tracking sparse-set. No ECS↔net cycle (adapter inverted).
- **ADR-005 (Foundation)**: tri-clock (`SimClock` + `RealClock` + `NetClock`) + fixed-tick scheduler. Opt-in determinism foundation. `NetClock` offset estimation + drift correction (NTP-lite) foundation.
- **ADR-015 (Concurrency)**: Network actor (dedicated thread) + SPSC ring buffer (transport → game). DtForHil mutex'li StreamAssembler lock-free port. Coroutine `task<>` send/receive. Job system: snapshot encode/decode per-entity parallel.
- **ADR-007 (Audio)**: voice spatial routing.

## Kanıt

- Valve GameNetworkingSockets: github.com/ValveSoftware/GameNetworkingSockets
- ENet: enet.bespin.org
- msquic: github.com/microsoft/msquic
- libdatachannel: github.com/paullouisageneau/libdatachannel
- Bernier (2001) — Latency Compensating Methods Valve GDC — **STUB**
- Cronin et al. (2003) — Distributed Multiplayer Game Server UMich — **STUB**
- Mauve et al. (2004) — Local-lag and Timewarp IEEE Trans Multimedia — **STUB**
- GGPO: github.com/pond3r/ggpo
- Glenn Fiedler — Game Networking: gafferongames.com/categories/networking
- DtForHil dosyaları (ADR-017 referans): `libraries/communication/network/*`, `libraries/protocol/parser/*`, `libraries/protocol/message/*`, `libraries/infra_client/automationclient.hpp`, `libraries/infra_contracts/controlplane.hpp`

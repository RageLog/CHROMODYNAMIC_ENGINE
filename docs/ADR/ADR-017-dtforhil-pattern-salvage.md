# ADR-017 — DtForHil Pattern Salvage

- **Status**: Accepted (Phase 1 Design)
- **Date**: 2026-05-17
- **Scope**: All patterns salvaged from DtForHil project into CHROMODYNAMIC
- **Related**: ADR-005 (Foundation), ADR-015 (Concurrency), ADR-011 (Networking), ADR-016 (Vendor Matrix)

## Bağlam

DtForHil HIL/SCADA projesinde **üretim-kalite**, **CLAUDE.md-uyumlu** modern C++ kod tabanı mevcut. Engine domain'ine **gerçekten uyan** generic patternler vardır — yeniden yazmak verimsiz. Ancak HIL-specific kod (radar, scenario, twin, GPIO, CAN/serial) **engine'i yozlaştırır** ve alınmaz.

Bu ADR salvage politikasını ve taşınacak pattern'leri normatif olarak dondurur. Detaylı tarama: `research/reports/dtforhil-deep-salvage-scan-2026-05-17.md` (architect/analyst ajan çıktısı).

## Karar

### A. Salvage Prensipleri

1. **Generic > Specific**: HIL/SCADA-specific kod taşınmaz. Generic pattern (event bus, allocator, ring buffer) taşınır.
2. **Refactor zorunlu**: 1:1 port yerine her pattern (a) namespace remap, (b) Singleton sökme (CLAUDE.md §7), (c) explicit context parametresi, (d) HIL-specific dependency'sini interface ile soyutlama.
3. **DAG koruma**: Salvage edilen pattern'ler CHROMODYNAMIC library DAG'ı içinde doğru katmana yerleşir. Yukarı bağımlılık yasak.
4. **Test izolasyonu**: her library kendi gtest binary'siyle, DtForHil test'lerinden bağımsız.

### B. Namespace Remap

| DtForHil | CHROMODYNAMIC | Library |
|---|---|---|
| `dfh::common::event` | `cd::events` | `cd_events` |
| `dfh::memory::allocator` | `cd::mem` | `cd_mem` |
| `dfh::common::timing` | `cd::time` | `cd_time` |
| `dfh::common::plugin` | `cd::plugin` | `cd_plugin` |
| `dfh::store` | `cd::core::handle_store` | `cd_core` |
| `dfh::logging` | `cd::log` | `cd_log` |
| `dfh::common::diagnostics` | `cd::diag` | `cd_diag` |
| `dfh::common::schema` | `cd::asset::schema` | `cd_asset_schema` |
| `dfh::common::error` | `cd::core::result` | `cd_core` |
| `dfh::common::threading` | `cd::concurrency` | `cd_concurrency` |
| `dfh::common::coroutine` | `cd::concurrency::task` | `cd_concurrency` |
| `dfh::common::utility` (ringbuffer/signalmanager) | `cd::concurrency::ring_buffer`, `cd::platform::signal_manager` | mixed |
| `dfh::common::data` (datachannel) | `cd::concurrency::data_channel` | `cd_concurrency` |
| `dfh::common::platform` | `cd::platform` | `cd_platform` |
| `dfh::common::base` (defines/compat) | `cd::core::defines` | `cd_core` |
| `dfh::common::binary` / `codec` | `cd::serialization` | `cd_serialization` |
| `dfh::common::parameter` | `cd::core::cvar` | `cd_core` |
| `dfh::common::contracts::system_events` | `cd::core::events` (canonical IDs) | `cd_core` |
| `dfh::safety_monitor::watchdog` | `cd::diag::deadline_monitor` | `cd_diag` |
| `dfh::safety_monitor::auditlogger` | `cd::log::audit_trail` | `cd_log` |
| `dfh::communication::network` | `cd::net` | `cd_net` |
| `dfh::protocol::parser` / `message` | `cd::serialization::*` | `cd_serialization` |
| `dfh::configuration` | `cd::config` | `cd_config` |

### C. Taşınacak Pattern Listesi (Öncelik Sırasıyla)

#### P0 — Foundation (Phase 2 implementation prework, ilk sprint)

| Pattern | Kaynak | Hedef | Effort |
|---|---|---|---|
| `Result<T>` (`std::expected<T, ErrorCode>`) | `common/error/result.hpp` | `cd::core::Result<T>` | 1:1 port |
| `IAllocator` + Pool/Arena/Page | `memory/allocator/*` | `cd::mem::*` | 1:1 port + alignment fix |
| `RingBuffer<T,N>` SPSC | `common/utility/ringbuffer.hpp` | `cd::concurrency::RingBuffer<T,N>` | 1:1 port |
| `Handle` + `Store` + `Table` | `store/{handle,store,table}.hpp` | `cd::core::HandleStore` | Refactor (RHI/ECS uyarla) |
| `IClock` + SteadyClock | `common/timing/{iclock,steadyclock}.hpp` | `cd::time::IClock` | 1:1 port |
| `defines.hpp` / `compat.hpp` | `common/base/*` | `cd::core::defines` | Refactor — `CD_<MODULE>_API` macro convention |
| `SignalManager` (`<csignal>`) | `common/utility/signalmanager.hpp` | `cd::platform::SignalManager` | 1:1 port |
| `CrashReporter` | `common/diagnostics/crashreporter.hpp` | `cd::diag::CrashReporter` | 1:1 port |

#### P1 — Core Services (Phase 2 sprint 2-3)

| Pattern | Kaynak | Hedef | Effort |
|---|---|---|---|
| `AsyncEventBus` (typed-only mode) | `common/event/asynceventbus.hpp` (652 satır) | `cd::events::EventBus` | Refactor — ConfigValue dep sök, type-only `publish<T>`, Singleton sök, threadpool entegrasyonu kalır |
| `Subscription` RAII | `common/event/asynceventbus.hpp:119` | `cd::events::ScopedConnection` | 1:1 port |
| `EventRecorder` (JSONL + rotation) | `common/event/eventrecorder.hpp` | `cd::events::EventRecorder` | 1:1 port |
| `TimerQueue` (priority-queue + stop_token) | `common/timing/timerqueue.hpp` | `cd::time::TimerQueue` | 1:1 port |
| `ThreadPool` (priority tasks) | `common/threading/threadpool.hpp` | `cd::concurrency::ThreadPool` (Job system altında) | Refactor — mutex deque → Chase-Lev (ADR-015 upgrade) |
| Coroutine task + scheduler + awaiters | `common/coroutine/*` | `cd::concurrency::task<T>` | 1:1 port — stop_token propagation pattern aynen |
| `ILogger` + format helpers | `logging/ilogger.hpp` | `cd::log::ILogger` | 1:1 port (`source_location` C++23 hazır) |
| `DataChannel<T>` (named SPSC) | `common/data/datachannel.hpp` | `cd::concurrency::DataChannel<T>` | 1:1 port |

#### P2 — Engine Patterns (Phase 2 sprint 4-6)

| Pattern | Kaynak | Hedef | Effort |
|---|---|---|---|
| `SimClock` (pause/step/speed-multiplier, fixed-timestep) | `common/timing/simclock.hpp` (396 satır) | `cd::time::SimClock` | Refactor — EventBus dep, Singleton sök; **birinci sınıf engine pattern** |
| `ParameterRegistry` (runtime CVar) | `common/parameter/parameterregistry.hpp` | `cd::core::CVarRegistry` | Refactor — EventBus dep, Singleton sök; **birinci sınıf engine pattern** |
| `DeadlineMonitor` (watchdog → render thread heartbeat) | `safety_monitor/watchdog.hpp` | `cd::diag::DeadlineMonitor` | Refactor — "Module" → "Subsystem", GPIO emergency callback yerine fail-fast log |
| `SchemaRegistry` (Value/Object/Array + Validator) | `common/schema/schema.hpp` | `cd::asset::SchemaRegistry` | Refactor — material/shader reflection layout check |
| `PluginLoader` (DLL load + ABI gate + manifest + RAII) | `common/plugin/pluginloader.hpp` (315 satır) | `cd::plugin::Loader` | Refactor — IResourceFactory coupling sök; **SOTA üstü** Iglberger 2022 referansı |
| `AuditTrail` (ring history + crash dump) | `safety_monitor/auditlogger.hpp` | `cd::log::AuditTrail` | Refactor — Singleton sök, RHI son N komut snapshot |

#### P3 — Serialization / Networking

| Pattern | Kaynak | Hedef | Effort |
|---|---|---|---|
| `BinaryStream` (read/write + bit-level) | `protocol/parser/binarystream.hpp` | `cd::serialization::BinaryStream` | 1:1 port |
| `BitReader` (compact asset format) | `protocol/message/bitreader.hpp` | `cd::serialization::BitReader` | 1:1 port |
| `Framing` strategies (length-header, delimiter) | `protocol/parser/framestrategies.hpp` | `cd::net::Framing` + `cd::serialization::Frame` | 1:1 port |
| `ISocket` + `SocketTrait` | `communication/network/isocket.hpp` | `cd::net::ITransport` (ADR-011) | Refactor — multi-channel, reliability concept |
| TCP/UDP socket impl | `communication/network/{tcp,udp}socket.*` | `cd::net::vendor::posix::*` | Refactor — Boost.Asio alternative considered |
| `MessageEngine` (schema-driven, multi-format) | `protocol/message/messageengine.hpp` | `cd::asset::SchemaCodec` (opsiyonel target) | Refactor — editor/replay tooling |
| `JsonlBackend` (logging) | `logging/jsonllogger/jsonllogger.hpp` | `cd::log::JsonlBackend` | 1:1 port |
| `HexCodec` / `BinaryCodec` | `common/binary/*`, `common/codec/*` | `cd::serialization::*` | 1:1 port |

#### P4 — Optional / Utility

| Pattern | Kaynak | Hedef | Effort |
|---|---|---|---|
| `ConfigValue` (variant tree) | `configuration/configvalue.hpp` | `cd::config::Value` | Refactor — `throw` → `std::expected`, ostream visitor |
| `IConfiguration` + providers | `configuration/iconfiguration.hpp`, `json_config/`, `xml_config/` | `cd::config::IProvider` | Refactor — IPlugin coupling sök |
| `Platform::Timer/Path/Env` | `common/platform/platform.hpp` | `cd::platform::*` | 1:1 port |
| `corelogger` (spdlog backend) | `logging/corelogger/corelogger.hpp` | `cd::log::SpdLogBackend` (opsiyonel) | Refactor — spdlog dependency opsiyonel |

### D. Reddedilen DtForHil Kod (yozlaştırıcı)

| Klasör/Dosya | Sebep |
|---|---|
| `infra_client/` (automationclient, catalogclient) | HIL automation/twin RPC |
| `infra_contracts/` (automation, capabilitygrant, controlplane) | HIL command contracts |
| `safety_monitor/safety_fsm.hpp` + `safetyplugin.hpp` | Emergency GPIO sequencing reactor-specific |
| `twin/` | Digital twin recording/replay (engine kendi replay sistemini kuracak) |
| `hardware/gpio/` | Hardware GPIO |
| `communication/can/` | CAN bus endüstriyel |
| `communication/serial/` | Serial port |
| `scripting/` (Lua scenario) | Engine kendi script binding'ini tasarlar (ADR'da gelecek) |
| `integration/remote_config/` | Uzaktan HIL config push |
| `system_sdk/` | HIL system SDK |
| `testing/` | DtForHil-specific test helpers |
| `ui/` | Qt-tabanlı HIL operatör paneli (engine editor'üne uygun değil) |
| `common/utility/singleton.hpp` | CLAUDE.md §7 explicit context istiyor; refactor değil reddet |

### E. Library DAG (Salvage edilenler)

```
cd::core (Result, defines, compat, Handle, CVarRegistry)
   │
   ├─→ cd::mem (IAllocator, Pool/Arena/Page/Freelist/TLSF/Tracking)
   │     │
   │     └─→ cd::concurrency (RingBuffer, ThreadPool, task<>, DataChannel, atomic helpers)
   │           │
   │           └─→ cd::time (IClock, SteadyClock, SimClock, TimerQueue, FramePacer)
   │                 │
   │                 ├─→ cd::events (EventBus, ScopedConnection, EventRecorder)
   │                 ├─→ cd::diag (DeadlineMonitor, CrashReporter, AuditTrail)
   │                 ├─→ cd::log (ILogger, JsonlBackend, SpdLogBackend opt)
   │                 └─→ cd::platform (SignalManager, Timer, Path, Env)
   │
   └─→ cd::serialization (BinaryStream, BitReader, HexCodec, BinaryCodec, Framing)
        │
        ├─→ cd::config (Value, IProvider)
        └─→ cd::asset::schema (SchemaRegistry, Validator)

cd::plugin (Loader, IPlugin) — uses cd::core, cd::log
cd::net (ITransport, sockets, Framing) — uses cd::serialization, cd::concurrency
```

### F. Sprint Plan (Salvage Roll-out)

| Sprint | Hedef |
|---|---|
| **Phase 2 — Sprint S2.1** | P0 hepsi (foundation derlenebilir) |
| **Phase 2 — Sprint S2.2** | P1: EventBus + ScopedConnection + TimerQueue + ThreadPool entegre |
| **Phase 2 — Sprint S2.3** | P1: ILogger + DataChannel + coroutine task |
| **Phase 2 — Sprint S2.4** | P2: SimClock + ParameterRegistry/CVar |
| **Phase 2 — Sprint S2.5** | P2: DeadlineMonitor + SchemaRegistry + PluginLoader |
| **Phase 2 — Sprint S2.6** | P3: BinaryStream + BitReader + Framing + ISocket |
| **Phase 2 — Sprint S2.7** | P4: ConfigValue + Platform + opsiyonel |

Toplam tahmini effort: **6-8 sprint** (Phase 2 başlangıcı). Yeniden yazımdan **3-6 ay kazanç**.

## Reddedilen Alternatifler

| Alternatif | Sebep |
|---|---|
| **Tam yeniden yazım, salvage yok** | 3-6 ay kayıp; üretim-test edilmiş kod atılır; CLAUDE.md research-first ihlali |
| **DtForHil'i submodule olarak çek** | HIL/SCADA domain coupling engine'i yozlaştırır; namespace çakışması |
| **Sadece P0 salvage** | P1-P2 patternleri (EventBus, SimClock, PluginLoader, CVar) yeniden yazımı 3-4 ay kayıp |
| **Singleton'ları korumak (kolay yol)** | CLAUDE.md §7 explicit context kuralı ihlali; library-oriented hedef çelişir |

## Sonuçlar

**Pozitif**:
- Foundation library 2-3 hafta içinde derlenebilir.
- `EventBus` + `ScopedConnection` ile cross-system messaging günler içinde kullanılabilir.
- `SimClock` engine'in fixed-timestep + bullet-time desteğini bedavaya getirir.
- `PluginLoader` editor extension/renderer backend swap için hazır (SOTA üstü).
- DtForHil kod tabanı üretim-test edilmiş — kalite garantisi var.

**Negatif**:
- Refactor sırasında ABI/macro kirliliği — mitigation: her library kendi gtest binary'siyle izole test.
- Singleton sökme bazı yerlerde public API surface'i değiştirir — explicit context paramları öğrenme eğrisi yarat.

**Replace-Ready (D1)**:
- Bu salvage **vendor değildir**; bizim ekosistemimiz içinde. Replace policy uygulanmaz.
- Ancak refactor sonrası elimizdeki kod **bizim** kodumuz; engine kimliği korunur.

## Cross-Cutting

- **ADR-005 (Foundation)**: Salvage P0+P1+P2 patterns Foundation katmanının omurgası.
- **ADR-015 (Concurrency)**: 7 pattern miras + 6 upgrade (mutex deque → Chase-Lev, condition_variable → atomic::wait, vs).
- **ADR-011 (Networking)**: P3 patterns (BinaryStream, BitReader, Framing, ISocket) `cd::net` temeli.
- **ADR-006 (Asset)**: SchemaRegistry, BinaryStream asset pipeline'da kullanılır.

## Kanıt

Tüm pattern'ler `architect`/`analyst` ajan tarafından gerçek dosya satır numarasıyla doğrulandı. Detay rapor: `research/reports/dtforhil-deep-salvage-scan-2026-05-17.md` (40 pattern, 24 taşınabilir, 10 reddedildi).

Anahtar kanıt dosyaları:
- `C:/UserFiles/Project/DtForHil/01_Code/libraries/common/event/asynceventbus.hpp` (652 satır)
- `C:/UserFiles/Project/DtForHil/01_Code/libraries/common/plugin/pluginloader.hpp` (315 satır)
- `C:/UserFiles/Project/DtForHil/01_Code/libraries/common/timing/simclock.hpp` (396 satır)
- `C:/UserFiles/Project/DtForHil/01_Code/libraries/store/{handle,table,store}.hpp`
- `C:/UserFiles/Project/DtForHil/01_Code/libraries/common/error/result.hpp`
- `C:/UserFiles/Project/DtForHil/01_Code/libraries/common/parameter/parameterregistry.hpp`
- `C:/UserFiles/Project/DtForHil/01_Code/libraries/common/utility/ringbuffer.hpp`
- `C:/UserFiles/Project/DtForHil/01_Code/libraries/common/coroutine/*`
- `C:/UserFiles/Project/DtForHil/01_Code/libraries/common/data/datachannel.hpp`
- `C:/UserFiles/Project/DtForHil/01_Code/libraries/safety_monitor/{watchdog,auditlogger}.hpp`
- `C:/UserFiles/Project/DtForHil/01_Code/libraries/protocol/parser/*`
- `C:/UserFiles/Project/DtForHil/01_Code/libraries/memory/allocator/*`

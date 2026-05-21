# ADR-008 — Physics Architecture

- **Status**: Accepted (Phase 1 Design)
- **Date**: 2026-05-17
- **Related**: ADR-004 (ECS), ADR-005 (Foundation), ADR-015 (Concurrency), ADR-016 (Vendor Matrix)

## Bağlam

Library-oriented + esnek geniş amaçlı kullanılabilirlik (T17). 3D rigid body custom yazma 18-36 ay; 2D physics (Box2D) custom yazma 12+ ay. Vendor matrix E2 kararı reverse edildi (K1 onayında): Box2D v3 vendor.

T3.Q2 = D opt-in determinism → rollback netcode altyapısı (ADR-011 cross).

## Karar

### A. 3D Backend (T17.Q1) — Adapter Pattern + Jolt Default

- **Default**: **Jolt Physics 5.x** (MIT, AAA-proven Horizon Forbidden West / Death Stranding 2, parallel-by-design, modern C++). Vendor: `cd::physics::vendor::jolt`.
- **Opt-in**: **PhysX 5.6** (BSD-3, GPU rigid+soft+fluid Apr 2025 açık kaynak). `cd::physics::vendor::physx`, `CD_PHYSICS_PHYSX=ON`. GPU soft body / fluid isteyen tüketici için.
- **Reddedilen**: Bullet (legacy, modern multi-core'a uymaz), Havok (kapalı), ReactPhysics3D (production-grade değil), Newton/ODE (stagnant).

```cpp
namespace cd::physics {
    class IPhysicsWorld { ... };  // adapter interface
}
namespace cd::physics::vendor::jolt  { class JoltWorld : public IPhysicsWorld {...}; }
namespace cd::physics::vendor::physx { class PxWorld   : public IPhysicsWorld {...}; }
namespace cd::physics::native::cdyn  { /* Phase 5+ değerlendir, opsiyonel */ }
```

### B. 2D Backend (T17.Q2) — Box2D v3 Vendor (E2 reverse)

- **Default**: **Box2D v3** (Erin Catto, C99, soft-step solver, MIT). Vendor: `cd::physics2d::vendor::box2d`.
- **Reddedilen kendi yazma**: stable stacking + CCD 12+ ay tuning; ROI negatif. (E2 "Box2D yazarız" kararı reverse — kullanıcı onayı).
- Adapter pattern: chipmunk2D opsiyonel adapter (mobile/MIT alternatif).

### C. Character Controller (T17.Q3) — Hybrid

`cd::physics::character::CapsuleController` ayrı bağımsız modül (backend-agnostic): kinematic capsule + sweep-test + collide-and-slide (Jolt `CharacterVirtual` pattern, Guerrilla GDC 2022).

Rigid-body character FPS jitter problemi yaygın; kinematic+sweep pattern endüstri standart.

### D. Soft Body / Cloth / Fluid (T17.Q4)

**Sprint 8 scope**:
- **XPBD cloth + rope** (kendi yazım, ~1500 LoC C++23, Macklin et al. 2016 + XPBD 2024 cluster paper).

**Out-of-scope Sprint 8** (Phase 12+):
- FEM soft body
- PIC/FLIP fluid
- Smoke

İhtiyaç olursa PhysX 5 GPU adapter aktive edilir.

### E. Determinism (T17.Q5) — 3-tier opt-in

| Tier | İçerik | Use case |
|---|---|---|
| **Tier-0 (default)** | Non-deterministic, max-perf | Standard gameplay |
| **Tier-1 opt-in** | Same-platform deterministic (fixed timestep + Jolt FP-strict + disable SIMD reorder) | Replay, test |
| **Tier-2 future** | Cross-platform fixed-point (`cd::physics::deterministic::fixed64`, Sprint 14+ rollback netcode) | Lockstep, rollback |

Tier-2 şimdilik **scaffolding API only**; implementation Sprint 14+.

### F. Trade-off Justification (Custom vs Adapter)

| Boyut | Adapter + Vendor | Full Custom |
|---|---|---|
| 3D rigid CCD-stable solver | Jolt: hazır | 18-36 ay 1 senior |
| Multi-thread broadphase | Jolt: production | 6-12 ay + lock-free uzmanlık |
| 2D stable stacking | Box2D v3: 17 yıl tuning | 12+ ay niş |
| Cross-platform determinism | Tier-1 feasible | Custom'da daha kolay ama ROI? |
| Tooling (debug viz, recording) | Jolt DebugRendering hazır | Sıfırdan |
| Innovation surface | Düşük (vendor kilit) | Yüksek (riskli) |
| Library-oriented | ✅ Adapter sayesinde | ✅ Saf ama maliyet 10× |

**Sonuç**: Full custom Phase 5+ değerlendirme, Phase 1-4'te adapter+vendor.

### G. Aşma Noktaları (Bizim katma değer)

1. **C++23 API quality**: Jolt C++17; biz wrapper'ı `std::expected`, `std::span`, ranges, `[[nodiscard]]` ile modern. PIMPL → ABI stable.
2. **ECS-native integration**: `cd::ecs::RigidBodyComponent` → Jolt body handle; broadphase update ECS system stage entegrasyonu.
3. **Job system unification**: Jolt internal job system → `cd::concurrency::JobGraph` ADR-015 bind; physics step engine job graph'ında bir node.
4. **Deterministic opt-in**: Jolt FP-strict mode `cd::physics::deterministic::Tier1` profile.
5. **Adapter swap demo**: Tek test app aynı sahneyi Jolt ve PhysX adapter ile koşar — library-oriented kanıtı.

## Reddedilen

- **Tek backend (Jolt only) + no adapter**: library-oriented felsefe + vendor kilit riski.
- **Full custom 3D physics**: maliyet 18-36 ay, ROI yok (Phase 5+ değerlendir).
- **Box2D yazma (E2 orijinal kararı)**: stable stacking + CCD tuning 12+ ay; Box2D v3 MIT yeterli.
- **Bullet vendor**: legacy.
- **Havok**: kapalı kaynak, MIT ruhuyla çatışır.
- **Cross-platform fixed-point default**: ROI negatif single-player; Tier-2 opt-in.

## Sonuçlar

**Pozitif**:
- AAA-proven backend; 3 ay içinde çalışan rigid body.
- Tüketici adapter swap; ECS+JobSystem entegrasyon temiz.
- Lisans risk yok (Jolt MIT, PhysX BSD-3, Box2D MIT).

**Negatif**:
- İki vendor maintenance (Jolt + PhysX opt).
- Upstream bump policy (quarterly tag pin).
- Cross-platform determinism deferred → Tier-2 Sprint 14+.
- XPBD kendi yazım test maliyeti (~3 hafta).

**Replace-Ready (D1)**: Phase 5+ Jolt → `cd::physics::native::cdyn` değerlendir; Box2D → kendi 2D değerlendir.

## Açık Sorular

| ID | Soru | Çözüm |
|---|---|---|
| Q1 | PhysX 5 adapter Sprint 8 vs 10? | Scaffolding S8, impl S10 |
| Q2 | Box2D v3 vs v2.4? | v3 (geleceğe yatırım) |
| Q3 | Jolt internal JobSystem mı CHROMODYNAMIC JobGraph? | Wrap+bind to `cd::concurrency::JobGraph` |
| Q4 | XPBD GPU compute vs CPU SIMD? | Sprint 8 CPU SIMD; GPU Sprint 12+ |
| Q5 | Tier-2 fixed-point Q-format: Q32.32 vs Q42.22? | Sprint 14 ADR-deferred |
| Q6 | Determinism testing infra: replay+checksum harness ne zaman? | Sprint 9 (S6 cross-cut) |
| Q7 | Character controller depenetration: stack vs slope FSM? | Sprint 8 prototyp |
| Q8 | Vendor upgrade policy | Tag-pin + quarterly bump |

## Cross-Cutting

- **ADR-004 (ECS)**: `cd::physics::BodyComponent` (handle-only opaque) + `TransformComponent` sync. Broadphase update ECS `PhysicsStage` (pre-render). Spatial integration: physics scene query → ECS spatial-hash adapter, NO duplicate spatial structure.
- **ADR-005 (Foundation)**: Determinism opt-in `cd::foundation::DeterminismProfile` global align — Tier-1 → FP-strict CMake guard paylaşımı. Fixed-point `cd::foundation::fixed<N,M>` Tier-2 temeli. Math SIMD wrap Jolt SIMD ABI çakışmamalı (PIMPL boundary).
- **ADR-015 (Concurrency)**: Jolt `JobSystem` → `cd::concurrency::IJobSystem` adapter bind. Physics step `cd::concurrency::JobGraph` içinde tek node; ayrı thread pool yasak. XPBD parallel-for-block. TSan preset physics test zorunlu.

## Kanıt

- Jolt Physics: https://github.com/jrouwe/JoltPhysics ; https://jrouwe.nl/jolt/JoltPhysicsMulticoreScaling.pdf
- Rouwe Guerrilla GDC 2022: https://www.guerrilla-games.com/read/architecting-jolt-physics-for-horizon-forbidden-west
- PhysX 5: https://developer.nvidia.com/blog/open-source-simulation-expands-with-nvidia-physx-5-release/
- Box2D v3: https://github.com/erincatto/box2d/releases
- Macklin XPBD: https://matthias-research.github.io/pages/publications/XPBD.pdf — **STUB**
- Fiedler — Deterministic Lockstep: https://gafferongames.com/post/deterministic_lockstep/
- Bender et al. (2014) — Survey on Position-Based Simulation, CGF — **STUB**

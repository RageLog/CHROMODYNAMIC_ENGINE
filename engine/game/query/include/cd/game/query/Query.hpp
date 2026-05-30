// =============================================================================
// CHROMODYNAMIC — cd/game/query/Query.hpp
// Phase 475 — cd::game::query (G3.1: spatial + ECS query helpers)
//
// Gameplay-tier query facade that lets game code ask "what is near this
// point / inside this volume / under this ray?" without having to know
// whether the answer is served by a uniform-grid spatial hash, an octree,
// or a brute-force ECS walk. The library is intentionally narrow: a
// `QueryWorld` owns a `cd::scene::SpatialHash<cd::ecs::Entity>` rebuilt
// from a user-supplied position component and exposes four canonical
// queries — raycast, sphere, AABB, frustum.
//
// Design rationale (SOTA references):
//   * Unreal `UPrimitiveComponent::LineTraceSingle` / `OverlapMulti`,
//     Unity `Physics.OverlapSphereNonAlloc` / `Physics.Raycast`, Godot
//     `PhysicsDirectSpaceState3D` — all expose a single
//     "shape-against-world" query surface with a thin result struct.
//   * Akenine-Möller et al. *Real-Time Rendering 4e* §22 — sphere /
//     AABB / ray broad-phase queries served by uniform-grid hashing for
//     well-conditioned scenes; degrades to brute force for unbounded /
//     long rays which we handle with a fallback walk.
//   * Frustum cull primitive lives in cd::scene::Frustum (n/p-vertex
//     trick, Akenine-Möller §16.10) — we just adapt it to ECS payloads.
//
// Threading model:
//   * `rebuild()` mutates internal state; one writer at a time.
//   * `raycast / sphere_query / box_query / frustum_query` are const
//     and re-entrant after `rebuild()` returns.
//
// Dependencies (CLAUDE.md §7): cd::core, cd::math, cd::ecs, cd::physics,
// cd::scene. All sit strictly below cd::game::* per the gameplay-tier ADR.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ecs/Entity.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/Aabb.hpp>
#include <cd/scene/Frustum.hpp>
#include <cd/scene/SpatialHash.hpp>

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

namespace cd::game::query
{

// -----------------------------------------------------------------------------
// Result structs.
//
// Mirrors the canonical "hit" packets every mainstream engine exposes
// (Unreal `FHitResult`, Unity `RaycastHit`, Godot `PhysicsRayResult`):
// just the fields gameplay actually needs at the call site — entity id,
// world-space hit point, surface normal, parametric distance.
// -----------------------------------------------------------------------------

/// Result of a successful raycast against the spatial index.
///
/// `point` and `normal` are populated from the AABB hit — the entry-face
/// of the box the ray pierces (slab-method normal). `distance` is the
/// parametric `t` along the unit-direction ray, i.e. `origin + dir*t`.
struct RayHit
{
    cd::ecs::Entity entity {};
    float distance { 0.0F };
    cd::math::Vec3f point {};
    cd::math::Vec3f normal {};
};

/// Result of a sphere-overlap query: the entity that overlaps the
/// sphere plus the centre-to-centre Euclidean distance (useful for
/// sorting "nearest first").
struct SphereOverlap
{
    cd::ecs::Entity entity {};
    float distance { 0.0F };
};

// -----------------------------------------------------------------------------
// QueryWorld — spatial + ECS query facade.
//
// `QueryWorld` is parameterised on an ECS world and a per-entity payload
// extractor that yields both a world position and an AABB. The hash is
// built from the per-entity position; AABBs feed the precise ray /
// box / sphere overlap tests after the broad phase.
//
// Insertion model:
//   * Call `add_entity(e, pos, aabb)` for every gameplay-relevant
//     entity, then `rebuild()` to commit them to the spatial hash.
//   * Alternatively, call `rebuild_from(world, fn)` and pass a callable
//     that visits entities and returns `(pos, aabb)` per entry — the
//     facade wipes its internal state and rebuilds in one pass.
//
// All four queries operate on the snapshot the last `rebuild` produced.
// -----------------------------------------------------------------------------
class QueryWorld
{
public:
    /// Payload remembered per inserted entity. Public so callers can
    /// inspect what is in the index — used in tests + tooling.
    struct Record
    {
        cd::ecs::Entity entity {};
        cd::math::Vec3f position {};
        cd::physics::Aabb aabb {};
    };

    /// Construct a query world backed by a `cd::scene::SpatialHash` with
    /// the given uniform cell size (world units). Pick `cell_size`
    /// roughly equal to the average AABB diameter for the entities you
    /// expect — too small bloats per-cell vectors, too large degrades
    /// queries toward O(N).
    explicit QueryWorld(float cell_size = 4.0F) noexcept
        : hash_ { cell_size }
        , cell_size_ { cell_size > 0.0F ? cell_size : 1.0F }
    {
    }

    /// Drop everything and start over. Cell size is preserved.
    void clear() noexcept;

    /// Register an entity + its position + its AABB. Re-inserting an
    /// existing entity replaces the record. Call `rebuild()` after the
    /// batch is staged.
    void add_entity(cd::ecs::Entity e, const cd::math::Vec3f& pos,
                    const cd::physics::Aabb& aabb);

    /// Remove an entity from the staging table. Cheap O(N) scan; for
    /// tournament-size churn rebuild from scratch instead.
    void remove_entity(cd::ecs::Entity e) noexcept;

    /// Rebuild the spatial hash from the current staging table. Cheap
    /// (cell map is cleared and re-populated; records are reused).
    void rebuild();

    /// Convenience: clear, run `enumerate` which is expected to call
    /// `add_entity` for each gameplay-relevant entity, then `rebuild()`.
    /// Decouples the query world from any specific ECS-walk strategy —
    /// the caller picks `world.for_each<Position>(...)`,
    /// `world.each<Position, AabbComp>(...)`, or even a hand-rolled
    /// iterator over a registry external to the ECS.
    using EnumerateFn = std::function<void(QueryWorld&)>;
    void rebuild_from(const EnumerateFn& enumerate);

    // ---- Introspection ----------------------------------------------------

    [[nodiscard]] std::size_t entity_count() const noexcept
    {
        return records_.size();
    }

    [[nodiscard]] float cell_size() const noexcept { return cell_size_; }

    [[nodiscard]] const std::vector<Record>& records() const noexcept
    {
        return records_;
    }

    // ---- Queries ----------------------------------------------------------

    /// Cast a ray against the indexed AABBs. Returns the nearest hit in
    /// `[0, max_dist]` along the ray, or `std::nullopt` if nothing was
    /// hit. The ray's direction does NOT need to be unit-length; the
    /// reported `distance` is measured along `dir` (i.e. the slab `t`
    /// values are scaled by `length(dir)`).
    ///
    /// Implementation: walks the spatial hash cells the ray pierces
    /// (DDA-style cell stepping); falls back to a brute-force scan if
    /// `dir` is zero-length.
    [[nodiscard]] std::optional<RayHit>
    raycast(const cd::math::Vec3f& origin, const cd::math::Vec3f& dir,
            float max_dist) const;

    /// Enumerate every entity whose AABB-centre-to-`center` Euclidean
    /// distance is `<= radius`. Results are sorted nearest-first. The
    /// returned vector is owned by the caller — small allocations are
    /// fine here; gameplay typically queries a handful of overlaps per
    /// frame and a hot-loop variant can drop in later.
    [[nodiscard]] std::vector<SphereOverlap>
    sphere_query(const cd::math::Vec3f& center, float radius) const;

    /// Enumerate every entity whose AABB overlaps `aabb`.
    [[nodiscard]] std::vector<cd::ecs::Entity>
    box_query(const cd::physics::Aabb& aabb) const;

    /// Enumerate every entity whose AABB-centre is inside the six-plane
    /// convex frustum (`contains_sphere` with the AABB's bounding
    /// sphere — conservative but correct n/p-vertex cull).
    [[nodiscard]] std::vector<cd::ecs::Entity>
    frustum_query(const std::array<cd::scene::Plane, 6>& planes) const;

    /// Overload taking the engine's structured `cd::scene::Frustum`.
    [[nodiscard]] std::vector<cd::ecs::Entity>
    frustum_query(const cd::scene::Frustum& frustum) const;

private:
    cd::scene::SpatialHash<cd::ecs::Entity> hash_;
    float cell_size_ { 1.0F };
    std::vector<Record> records_ {};
};

// -----------------------------------------------------------------------------
// Free-function utilities (header-inline, no link dependency).
//
// Exposed for unit tests + callers that want to do their own scratch
// queries without going through the QueryWorld facade.
// -----------------------------------------------------------------------------

/// Slab-method ray vs AABB intersection. Returns the entry `t` along
/// `dir` (NOT necessarily unit) and the surface normal of the entered
/// face, or `std::nullopt` when the ray misses or only touches behind
/// the origin / beyond `max_dist`.
///
/// `dir` must be non-zero; pass `dir` in its natural (possibly non-unit)
/// form — the `t` is consistent with the caller's parametric space.
struct AabbHit
{
    float t { 0.0F };
    cd::math::Vec3f normal {};
};
[[nodiscard]] std::optional<AabbHit>
intersect_ray_aabb(const cd::math::Vec3f& origin, const cd::math::Vec3f& dir,
                   const cd::physics::Aabb& aabb, float max_dist) noexcept;

}  // namespace cd::game::query

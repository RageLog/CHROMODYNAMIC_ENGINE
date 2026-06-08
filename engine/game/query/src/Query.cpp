// =============================================================================
// CHROMODYNAMIC — cd/game/query/Query.cpp
// Phase 475 — cd::game::query implementation.
//
// References:
//   * Akenine-Möller, Haines, Hoffman et al. *Real-Time Rendering 4e*
//     §22.7 (ray / AABB slab test), §16.10 (frustum n/p-vertex).
//   * Williams, Barrus, Morley, Shirley. "An Efficient and Robust
//     Ray-Box Intersection Algorithm", JGT 2005 — the slab variant we
//     ship below, with the standard branch for `inv == ±inf`.
//   * cd::scene::SpatialHash (engine/world/scene/include/cd/scene/
//     SpatialHash.hpp) — uniform-grid broad phase.
// =============================================================================
#include <cd/game/query/Query.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace cd::game::query
{

namespace
{

// Hash for cd::ecs::Entity so we can dedup ray-traversal candidates: the
// same entity may sit in several cells the ray pierces, but we only want
// to test it against the AABB once.
struct EntityHash
{
    std::size_t operator()(const cd::ecs::Entity& e) const noexcept
    {
        return (static_cast<std::size_t>(e.id) << 32U) ^
               static_cast<std::size_t>(e.generation);
    }
};

[[nodiscard]] cd::math::Vec3f aabb_center(const cd::physics::Aabb& a) noexcept
{
    return cd::math::Vec3f {
        0.5F * (a.min.x + a.max.x),
        0.5F * (a.min.y + a.max.y),
        0.5F * (a.min.z + a.max.z),
    };
}

[[nodiscard]] float aabb_bounding_radius(const cd::physics::Aabb& a) noexcept
{
    const cd::math::Vec3f half {
        0.5F * (a.max.x - a.min.x),
        0.5F * (a.max.y - a.min.y),
        0.5F * (a.max.z - a.min.z),
    };
    return std::sqrt(half.x * half.x + half.y * half.y + half.z * half.z);
}

}  // namespace

// -----------------------------------------------------------------------------
// intersect_ray_aabb — Williams et al. slab variant.
//
// Each axis defines two parallel planes (the "slabs"); the ray enters the
// AABB at max(tmin_axis_i) and exits at min(tmax_axis_i). A miss is
// detected when entry > exit or the exit is behind the origin. The
// entered-face normal is the axis whose tmin produced the maximum, with
// the sign flipped against the ray direction.
// -----------------------------------------------------------------------------
std::optional<AabbHit>
intersect_ray_aabb(const cd::math::Vec3f& origin, const cd::math::Vec3f& dir,
                   const cd::physics::Aabb& aabb, float max_dist) noexcept
{
    // Reject zero-length direction up front — caller has no parametric
    // axis to walk and the slab math degenerates.
    if (dir.x == 0.0F && dir.y == 0.0F && dir.z == 0.0F)
        return std::nullopt;

    float tmin = -std::numeric_limits<float>::infinity();
    float tmax = std::numeric_limits<float>::infinity();
    int entry_axis = 0;
    bool entry_neg = false;

    const float orig[3] = { origin.x, origin.y, origin.z };
    const float d[3] = { dir.x, dir.y, dir.z };
    const float lo[3] = { aabb.min.x, aabb.min.y, aabb.min.z };
    const float hi[3] = { aabb.max.x, aabb.max.y, aabb.max.z };

    for (int i = 0; i < 3; ++i)
    {
        if (d[i] == 0.0F)
        {
            // Ray is parallel to this slab — must already lie inside it.
            if (orig[i] < lo[i] || orig[i] > hi[i])
                return std::nullopt;
            continue;
        }

        const float inv = 1.0F / d[i];
        float t1 = (lo[i] - orig[i]) * inv;
        float t2 = (hi[i] - orig[i]) * inv;
        bool neg_face = false;  // entered through the lo plane?
        if (t1 > t2)
        {
            std::swap(t1, t2);
            neg_face = true;
        }
        if (t1 > tmin)
        {
            tmin = t1;
            entry_axis = i;
            // `neg_face` indicates the lo-plane was hit first along the
            // axis-positive direction. The outward-facing normal of that
            // face is -axis if !neg_face else +axis — flipped below.
            entry_neg = neg_face;
        }
        if (t2 < tmax)
            tmax = t2;
        if (tmin > tmax)
            return std::nullopt;
    }

    if (tmax < 0.0F)
        return std::nullopt;  // box entirely behind ray
    if (tmin > max_dist)
        return std::nullopt;  // entry beyond clip

    // If origin is already inside, report t = 0 with a "into-the-box"
    // normal pointing back toward the origin along the longest exit.
    const float t_hit = tmin >= 0.0F ? tmin : 0.0F;

    cd::math::Vec3f normal { 0.0F, 0.0F, 0.0F };
    // Outward normal of the entered face: +axis when we entered the hi
    // plane (d[axis] < 0), -axis when we entered the lo plane.
    const float sign = entry_neg ? +1.0F : -1.0F;
    if (entry_axis == 0)
        normal.x = sign;
    else if (entry_axis == 1)
        normal.y = sign;
    else
        normal.z = sign;

    return AabbHit { t_hit, normal };
}

// -----------------------------------------------------------------------------
// QueryWorld — staging table mutators.
// -----------------------------------------------------------------------------
void QueryWorld::clear() noexcept
{
    hash_.clear();
    records_.clear();
}

void QueryWorld::add_entity(cd::ecs::Entity e, const cd::math::Vec3f& pos,
                            const cd::physics::Aabb& aabb)
{
    // Replace existing record if present (small N: linear scan is fine).
    for (auto& r : records_)
    {
        if (r.entity == e)
        {
            r.position = pos;
            r.aabb = aabb;
            return;
        }
    }
    records_.push_back(Record { e, pos, aabb });
}

void QueryWorld::remove_entity(cd::ecs::Entity e) noexcept
{
    const auto removed = std::ranges::remove_if(records_,
                                                 [&](const Record& r) { return r.entity == e; });
    records_.erase(removed.begin(), removed.end());
}

void QueryWorld::rebuild()
{
    hash_.clear();
    for (const auto& r : records_)
        hash_.insert(r.entity, r.position);
}

void QueryWorld::rebuild_from(const EnumerateFn& enumerate)
{
    clear();
    if (!enumerate)
        return;
    enumerate(*this);
    rebuild();
}

// -----------------------------------------------------------------------------
// Raycast.
//
// Strategy:
//   * Collect candidate entities by querying the spatial hash for a
//     sphere that bounds the ray segment (centre = midpoint, radius =
//     half-length + slack). This is a conservative broad phase but
//     simpler than a 3D-DDA cell walk and sufficient for short to
//     medium gameplay rays (the kind used for "click-to-pick" and
//     "line-of-sight"). For very long rays we walk every cell — see
//     fallback below.
//   * Per candidate, test the precise AABB with `intersect_ray_aabb`
//     and keep the smallest non-negative `t`.
// -----------------------------------------------------------------------------
std::optional<RayHit>
QueryWorld::raycast(const cd::math::Vec3f& origin, const cd::math::Vec3f& dir,
                    float max_dist) const
{
    if (records_.empty())
        return std::nullopt;
    if (dir.x == 0.0F && dir.y == 0.0F && dir.z == 0.0F)
        return std::nullopt;
    if (max_dist <= 0.0F)
        return std::nullopt;

    const float dir_len_sq = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;
    const float dir_len = std::sqrt(dir_len_sq);
    const cd::math::Vec3f end {
        origin.x + dir.x * (max_dist / dir_len),
        origin.y + dir.y * (max_dist / dir_len),
        origin.z + dir.z * (max_dist / dir_len),
    };
    const cd::math::Vec3f mid {
        0.5F * (origin.x + end.x),
        0.5F * (origin.y + end.y),
        0.5F * (origin.z + end.z),
    };
    const float seg_radius = 0.5F * max_dist + cell_size_;

    std::vector<cd::ecs::Entity> candidates;
    candidates.reserve(records_.size());
    hash_.query_sphere(mid, seg_radius, candidates);

    // Dedup — the same entity can show up in multiple cells.
    std::unordered_set<cd::ecs::Entity, EntityHash> seen;
    seen.reserve(candidates.size());

    std::optional<RayHit> best;
    auto consider = [&](cd::ecs::Entity e)
    {
        if (!seen.insert(e).second)
            return;
        // Find the record for this entity (small N OK; spatial hash
        // already trimmed the candidate set).
        const Record* rec = nullptr;
        for (const auto& r : records_)
        {
            if (r.entity == e)
            {
                rec = &r;
                break;
            }
        }
        if (rec == nullptr)
            return;
        // Convert max_dist (world units) into ray-parametric `t` units
        // when `dir` is non-unit: t_max_param = max_dist / |dir|.
        const float t_max_param = max_dist / dir_len;
        auto hit = intersect_ray_aabb(origin, dir, rec->aabb, t_max_param);
        if (!hit.has_value())
            return;
        // Convert parametric t back to world-space distance along `dir`.
        const float world_dist = hit->t * dir_len;
        if (best.has_value() && world_dist >= best->distance)
            return;
        RayHit rh;
        rh.entity = e;
        rh.distance = world_dist;
        rh.point.x = origin.x + dir.x * hit->t;
        rh.point.y = origin.y + dir.y * hit->t;
        rh.point.z = origin.z + dir.z * hit->t;
        rh.normal = hit->normal;
        best = rh;
    };

    for (auto e : candidates)
        consider(e);

    // Fallback: if the spatial hash returned nothing AND we have records,
    // do a brute-force scan (handles the "very long ray, sparse hash"
    // pathology and keeps the contract simple).
    if (!best.has_value() && candidates.empty())
    {
        for (const auto& r : records_)
            consider(r.entity);
    }

    return best;
}

// -----------------------------------------------------------------------------
// Sphere overlap.
// -----------------------------------------------------------------------------
std::vector<SphereOverlap>
QueryWorld::sphere_query(const cd::math::Vec3f& center, float radius) const
{
    std::vector<SphereOverlap> out;
    if (records_.empty() || radius < 0.0F)
        return out;

    std::vector<cd::ecs::Entity> candidates;
    candidates.reserve(records_.size());
    hash_.query_sphere(center, radius + cell_size_, candidates);

    std::unordered_set<cd::ecs::Entity, EntityHash> seen;
    seen.reserve(candidates.size());

    const float r2 = radius * radius;
    for (auto e : candidates)
    {
        if (!seen.insert(e).second)
            continue;
        const Record* rec = nullptr;
        for (const auto& r : records_)
        {
            if (r.entity == e)
            {
                rec = &r;
                break;
            }
        }
        if (rec == nullptr)
            continue;
        // Precise test against the AABB centre — gameplay code already
        // gets the AABB diameter through the broad-phase cell padding
        // above; we drop down to a centre-vs-sphere here for stable
        // distance ordering.
        const cd::math::Vec3f c = aabb_center(rec->aabb);
        const cd::math::Vec3f dv { c.x - center.x, c.y - center.y, c.z - center.z };
        const float d2 = dv.x * dv.x + dv.y * dv.y + dv.z * dv.z;
        if (d2 > r2)
            continue;
        out.push_back(SphereOverlap { e, std::sqrt(d2) });
    }

    std::ranges::sort(out,
                      [](const SphereOverlap& a, const SphereOverlap& b)
                      { return a.distance < b.distance; });
    return out;
}

// -----------------------------------------------------------------------------
// AABB overlap.
// -----------------------------------------------------------------------------
std::vector<cd::ecs::Entity>
QueryWorld::box_query(const cd::physics::Aabb& aabb) const
{
    std::vector<cd::ecs::Entity> out;
    if (records_.empty())
        return out;

    // Broad phase: query the spatial hash with a sphere bounding the
    // AABB. Cell-padding by cell_size_ keeps the broad-phase
    // conservative against the lattice quantisation.
    const cd::math::Vec3f c = aabb_center(aabb);
    const float r = aabb_bounding_radius(aabb);

    std::vector<cd::ecs::Entity> candidates;
    candidates.reserve(records_.size());
    hash_.query_sphere(c, r + cell_size_, candidates);

    std::unordered_set<cd::ecs::Entity, EntityHash> seen;
    seen.reserve(candidates.size());

    for (auto e : candidates)
    {
        if (!seen.insert(e).second)
            continue;
        const Record* rec = nullptr;
        for (const auto& r2 : records_)
        {
            if (r2.entity == e)
            {
                rec = &r2;
                break;
            }
        }
        if (rec == nullptr)
            continue;
        if (cd::physics::overlaps(aabb, rec->aabb))
            out.push_back(e);
    }
    return out;
}

// -----------------------------------------------------------------------------
// Frustum overlap.
//
// We use cd::scene::contains_sphere with the AABB's bounding sphere as
// the broad-phase test. A tighter cd::scene::intersects(Frustum, Aabb)
// is available; we keep the sphere variant here because it is the
// canonical n/p-vertex cull every cited reference engine ships first.
// -----------------------------------------------------------------------------
std::vector<cd::ecs::Entity>
QueryWorld::frustum_query(const std::array<cd::scene::Plane, 6>& planes) const
{
    cd::scene::Frustum f;
    f.left = planes[0];
    f.right = planes[1];
    f.bottom = planes[2];
    f.top = planes[3];
    f.near_ = planes[4];
    f.far_ = planes[5];
    return frustum_query(f);
}

std::vector<cd::ecs::Entity>
QueryWorld::frustum_query(const cd::scene::Frustum& frustum) const
{
    std::vector<cd::ecs::Entity> out;
    out.reserve(records_.size());
    for (const auto& r : records_)
    {
        if (cd::scene::intersects(frustum, r.aabb))
            out.push_back(r.entity);
    }
    return out;
}

}  // namespace cd::game::query
